/*  Moment-Gst - GStreamer support module for Moment Video Server
    Copyright (C) 2011 Dmitry Shatrov

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/


#include <moment-gst/gst_stream_ctl.h>


using namespace M;
using namespace Moment;

namespace MomentGst {

static LogGroup libMary_logGroup_ctl ("mod_gst/GstStreamCtl", LogLevel::D);

void
GstStreamCtl::setStreamParameters (VideoStream * const mt_nonnull video_stream)
{
    Ref<StreamParameters> const stream_params = grab (new StreamParameters);
    if (no_audio)
        stream_params->setParam ("no_audio", "true");
    if (no_video)
        stream_params->setParam ("no_video", "true");

    video_stream->setStreamParameters (stream_params);
}

VideoStream::EventHandler const GstStreamCtl::stream_event_handler = {
    NULL /* audioMessage */,
    NULL /* videoMessage */,
    NULL /* rtmpCommandMessage */,
    NULL /* closed */,
    numWatchersChanged
};

void
GstStreamCtl::numWatchersChanged (Count   const num_watchers,
                                  void  * const _stream_data)
{
    logD_ (_func, num_watchers);

    StreamData * const stream_data = static_cast <StreamData*> (_stream_data);
    GstStreamCtl * const self = stream_data->gst_stream_ctl;

    self->mutex.lock ();
    if (stream_data != self->cur_stream_data /* ||
	stream_data->stream_closed */)
    {
	self->mutex.unlock ();
	return;
    }

    stream_data->num_watchers = num_watchers;

    if (num_watchers == 0) {
        if (!self->connect_on_demand_timer) {
            logD_ (_func, "starting timer, timeout: ", self->connect_on_demand_timeout);
            self->connect_on_demand_timer = self->timers->addTimer (
                    CbDesc<Timers::TimerCallback> (connectOnDemandTimerTick,
                                                   stream_data /* cb_data */,
                                                   self        /* coderef_container */,
                                                   stream_data /* ref_data */),
                    self->connect_on_demand_timeout,
                    false /* periodical */);
        }
    } else {
        if (self->connect_on_demand_timer) {
            self->timers->deleteTimer (self->connect_on_demand_timer);
            self->connect_on_demand_timer = NULL;
        }

        if (!self->gst_stream
            && !self->stream_stopped)
        {
            logD_ (_func, "connecting on demand");
            mt_unlocks (mutex) self->doRestartStream (true /* from_ondemand_reconnect */);
            return;
        }
    }

    self->mutex.unlock ();
}

void
GstStreamCtl::connectOnDemandTimerTick (void * const _stream_data)
{
    logD_ (_func_);

    StreamData * const stream_data = static_cast <StreamData*> (_stream_data);
    GstStreamCtl * const self = stream_data->gst_stream_ctl;

    self->mutex.lock ();
    if (stream_data != self->cur_stream_data /* ||
	stream_data->stream_closed */)
    {
	self->mutex.unlock ();
	return;
    }

    if (stream_data->num_watchers == 0) {
        logD_ (_func, "disconnecting on timeout");
        self->closeStream (true /* replace_video_stream */);
    }

    self->mutex.unlock ();
}

mt_mutex (mutex) void
GstStreamCtl::beginConnectOnDemand (bool const start_timer)
{
    assert (video_stream);

    if (!connect_on_demand || stream_stopped)
        return;

    video_stream->lock ();

    if (start_timer
        && video_stream->getNumWatchers_unlocked() == 0)
    {
        logD_ (_func, "starting timer, timeout: ", connect_on_demand_timeout);
        connect_on_demand_timer = timers->addTimer (
                CbDesc<Timers::TimerCallback> (connectOnDemandTimerTick,
                                               cur_stream_data /* cb_data */,
                                               this        /* coderef_container */,
                                               cur_stream_data /* ref_data */),
                connect_on_demand_timeout,
                false /* periodical */);
    }

    video_stream_events_sbn = video_stream->getEventInformer()->subscribe_unlocked (
            CbDesc<VideoStream::EventHandler> (
                    &stream_event_handler,
                    cur_stream_data /* cb_data */,
                    this        /* coderef_container */,
                    cur_stream_data /* ref_data */));

    video_stream->unlock ();
}

mt_mutex (mutex) void
GstStreamCtl::createStream (Time const initial_seek)
{
    logD (ctl, _this_func_);

/* closeStream() is always called before createStream(), so this is unnecessary.
 *
    if (gst_stream) {
	gst_stream->releasePipeline ();
	gst_stream = NULL;
    }
 */

    stream_stopped = false;

    got_video = false;

    if (!cur_stream_data) {
        Ref<StreamData> const stream_data = grab (new StreamData (
                this, stream_ticket, stream_ticket_ref.ptr()));
        cur_stream_data = stream_data;
    }

    if (video_stream && video_stream_events_sbn) {
        video_stream->getEventInformer()->unsubscribe (video_stream_events_sbn);
        video_stream_events_sbn = NULL;
    }

    if (!video_stream) {
	video_stream = grab (new VideoStream);
        setStreamParameters (video_stream);

	logD_ (_func, "Calling moment->addVideoStream, stream_name: ", stream_name->mem());
	video_stream_key = moment->addVideoStream (video_stream, stream_name->mem());
    }

    beginConnectOnDemand (true /* start_timer */);

    if (stream_start_time == 0)
	stream_start_time = getTime();

    gst_stream = grab (new GstStream);
    gst_stream->init (stream_name->mem(),
		      stream_spec->mem(),
		      is_chain,
		      timers,
		      page_pool,
		      video_stream,
		      moment->getMixVideoStream(),
		      initial_seek,
		      send_metadata,
                      enable_prechunking,
		      default_width,
		      default_height,
		      default_bitrate,
		      no_video_timeout);

    gst_stream->setFrontend (CbDesc<GstStream::Frontend> (
	    &gst_stream_frontend,
	    cur_stream_data /* cb_data */,
	    this            /* coderef_container */,
	    cur_stream_data /* ref_data */));

    {
	gst_stream->ref ();
	GThread * const thread = g_thread_create (
		streamThreadFunc, gst_stream, FALSE /* joinable */, NULL /* error */);
	if (thread == NULL) {
	    logE_ (_func, "g_thread_create() failed");
	    gst_stream->unref ();
	}
    }
}

gpointer
GstStreamCtl::streamThreadFunc (gpointer const _gst_stream)
{
    GstStream * const gst_stream = static_cast <GstStream*> (_gst_stream);

    logD (ctl, _func_);

    updateTime ();
    gst_stream->createPipeline ();

    gst_stream->unref ();
    return (gpointer) 0;
}

mt_mutex (mutex) void
GstStreamCtl::closeStream (bool const replace_video_stream)
{
    logD (ctl, _this_func_);

    got_video = false;

    if (connect_on_demand_timer) {
        timers->deleteTimer (connect_on_demand_timer);
        connect_on_demand_timer = NULL;
    }

    if (gst_stream) {
	{
	    GstStream::TrafficStats traffic_stats;
	    gst_stream->getTrafficStats (&traffic_stats);

	    rx_bytes_accum += traffic_stats.rx_bytes;
	    rx_audio_bytes_accum += traffic_stats.rx_audio_bytes;
	    rx_video_bytes_accum += traffic_stats.rx_video_bytes;
	}

	gst_stream->releasePipeline ();
	gst_stream = NULL;
    }
    cur_stream_data = NULL;

    if (video_stream
        && replace_video_stream
	&& !keep_video_stream)
    {
	// TODO moment->replaceVideoStream() to swap video streams atomically
	moment->removeVideoStream (video_stream_key);
	video_stream->close ();

        if (video_stream_events_sbn) {
            video_stream->getEventInformer()->unsubscribe (video_stream_events_sbn);
            video_stream_events_sbn = NULL;
        }

	video_stream = NULL;

	if (replace_video_stream) {
            {
                assert (!cur_stream_data);

                Ref<StreamData> const stream_data = grab (new StreamData (
                        this, stream_ticket, stream_ticket_ref.ptr()));
                cur_stream_data = stream_data;
            }

	    video_stream = grab (new VideoStream);
            setStreamParameters (video_stream);

	    logD_ (_func, "Calling moment->addVideoStream, stream_name: ", stream_name->mem());
	    video_stream_key = moment->addVideoStream (video_stream, stream_name->mem());

            beginConnectOnDemand (false /* start_timer */);
	}
    }

    logD_ (_func, "done");
}

mt_unlocks (mutex) void
GstStreamCtl::doRestartStream (bool const from_ondemand_reconnect)
{
    logD (ctl, _this_func_);

    bool new_video_stream = false;
    if (gst_stream
        && !from_ondemand_reconnect)
    {
        closeStream (true /* replace_video_stream */);
        new_video_stream = true;
    }

    // TODO FIXME Set correct initial seek
    createStream (0 /* initial_seek */);

    VirtRef const tmp_stream_ticket_ref = stream_ticket_ref;
    void * const tmp_stream_ticket = stream_ticket;

    mutex.unlock ();

    if (new_video_stream) {
        if (frontend)
            frontend.call (frontend->newVideoStream, tmp_stream_ticket);
    }
}

bool
GstStreamCtl::deferredTask (void * const _self)
{
    GstStreamCtl * const self = static_cast <GstStreamCtl*> (_self);

    logD (ctl, _self_func_);

  {
    self->mutex.lock ();
    if (!self->gst_stream) {
	self->mutex.unlock ();
	goto _return;
    }

    Ref<GstStream> const tmp_gst_stream = self->gst_stream;
    self->mutex.unlock ();

    tmp_gst_stream->reportStatusEvents ();
  }

_return:
    return false /* Do not reschedule */;
}

GstStream::Frontend GstStreamCtl::gst_stream_frontend = {
    streamError,
    streamEos,
    noVideo,
    gotVideo,
    streamStatusEvent
};

void
GstStreamCtl::streamError (void * const _stream_data)
{
    StreamData * const stream_data = static_cast <StreamData*> (_stream_data);
    GstStreamCtl * const self = stream_data->gst_stream_ctl;

    logD (ctl, _self_func_);

    self->mutex.lock ();

    stream_data->stream_closed = true;
    if (stream_data != self->cur_stream_data) {
	self->mutex.unlock ();
	return;
    }

    VirtRef const tmp_stream_ticket_ref = self->stream_ticket_ref;
    void * const tmp_stream_ticket = self->stream_ticket;

    self->mutex.unlock ();

    if (self->frontend)
	self->frontend.call (self->frontend->error, tmp_stream_ticket);
}

void
GstStreamCtl::streamEos (void * const _stream_data)
{
    StreamData * const stream_data = static_cast <StreamData*> (_stream_data);
    GstStreamCtl * const self = stream_data->gst_stream_ctl;

    logD (ctl, _self_func_);

    self->mutex.lock ();

    stream_data->stream_closed = true;
    if (stream_data != self->cur_stream_data) {
	self->mutex.unlock ();
	return;
    }

    VirtRef const tmp_stream_ticket_ref = self->stream_ticket_ref;
    void * const tmp_stream_ticket = self->stream_ticket;

    self->mutex.unlock ();

    if (self->frontend)
	self->frontend.call (self->frontend->eos, tmp_stream_ticket);
}

void
GstStreamCtl::noVideo (void * const _stream_data)
{
//    logD_ (_func_);
    StreamData * const stream_data = static_cast <StreamData*> (_stream_data);
    GstStreamCtl * const self = stream_data->gst_stream_ctl;

    self->mutex.lock ();
    if (stream_data != self->cur_stream_data ||
	stream_data->stream_closed)
    {
	self->mutex.unlock ();
	return;
    }

    mt_unlocks (mutex) self->doRestartStream ();
}

void
GstStreamCtl::gotVideo (void * const _stream_data)
{
    StreamData * const stream_data = static_cast <StreamData*> (_stream_data);
    GstStreamCtl * const self = stream_data->gst_stream_ctl;

    logD (ctl, _self_func_);

    self->mutex.lock ();
    if (stream_data != self->cur_stream_data ||
	stream_data->stream_closed)
    {
	self->mutex.unlock ();
	return;
    }

    self->got_video = true;
    self->mutex.unlock ();
}

void
GstStreamCtl::streamStatusEvent (void * const _stream_data)
{
    StreamData * const stream_data = static_cast <StreamData*> (_stream_data);
    GstStreamCtl * const self = stream_data->gst_stream_ctl;

    logD (ctl, _self_func_);

    self->deferred_reg.scheduleTask (&self->deferred_task, false /* permanent */);
}

// If @is_chain is 'true', then @stream_spec is a chain spec with gst-launch
// syntax. Otherwise, @stream_spec is an uri for uridecodebin2.
void
GstStreamCtl::beginVideoStream (ConstMemory      const stream_spec,
				bool             const is_chain,
				void           * const stream_ticket,
				VirtReferenced * const stream_ticket_ref,
				Time             const seek)
{
    logD (ctl, _this_func_);

    mutex.lock ();

    if (gst_stream)
	closeStream (true /* replace_video_stream */);

    this->stream_spec = grab (new String (stream_spec));
    this->is_chain = is_chain;

    this->stream_ticket = stream_ticket;
    this->stream_ticket_ref = stream_ticket_ref;

    createStream (seek);

    mutex.unlock ();
}

void
GstStreamCtl::endVideoStream ()
{
    logD (ctl, _this_func_);

    mutex.lock ();

    stream_stopped = true;

    if (gst_stream)
	closeStream (true /* replace_video_stream */);

    mutex.unlock ();
}

void
GstStreamCtl::restartStream ()
{
    logD (ctl, _this_func_);

    mutex.lock ();
    mt_unlocks (mutex) doRestartStream ();
}

bool
GstStreamCtl::isSourceOnline ()
{
    mutex.lock ();
    bool const res = got_video;
    mutex.unlock ();
    return res;
}

void
GstStreamCtl::getTrafficStats (TrafficStats * const ret_traffic_stats)
{
  StateMutexLock l (mutex);

    GstStream::TrafficStats stream_tstat;
    if (gst_stream)
	gst_stream->getTrafficStats (&stream_tstat);
    else
	stream_tstat.reset ();

    ret_traffic_stats->rx_bytes = rx_bytes_accum + stream_tstat.rx_bytes;
    ret_traffic_stats->rx_audio_bytes = rx_audio_bytes_accum + stream_tstat.rx_audio_bytes;
    ret_traffic_stats->rx_video_bytes = rx_video_bytes_accum + stream_tstat.rx_video_bytes;
    {
	Time const cur_time = getTime();
	if (cur_time > stream_start_time)
	    ret_traffic_stats->time_elapsed = cur_time - stream_start_time;
	else
	    ret_traffic_stats->time_elapsed = 0;
    }
}

void
GstStreamCtl::resetTrafficStats ()
{
  StateMutexLock l (mutex);

    if (gst_stream)
	gst_stream->resetTrafficStats ();

    rx_bytes_accum = 0;
    rx_audio_bytes_accum = 0;
    rx_video_bytes_accum = 0;

    stream_start_time = getTime();
}

mt_const void
GstStreamCtl::init (MomentServer      * const moment,
		    DeferredProcessor * const deferred_processor,
		    ConstMemory         const stream_name,
                    bool                const no_audio,
                    bool                const no_video,
		    bool                const send_metadata,
                    bool                const enable_prechunking,
		    bool                const keep_video_stream,
                    bool                const connect_on_demand,
                    Time                const connect_on_demand_timeout,
		    Uint64              const default_width,
		    Uint64              const default_height,
		    Uint64              const default_bitrate,
		    Time                const no_video_timeout)
{
    logD (ctl, _this_func_);

    this->moment = moment;
    this->timers = moment->getServerApp()->getServerContext()->getTimers();
    this->page_pool = moment->getPagePool();

    this->stream_name = grab (new String (stream_name));

    this->no_audio = no_audio;
    this->no_video = no_video;
    this->send_metadata = send_metadata;
    this->enable_prechunking = enable_prechunking;
    this->keep_video_stream = keep_video_stream;

    this->connect_on_demand = connect_on_demand;
    this->connect_on_demand_timeout = connect_on_demand_timeout;

    this->default_width = default_width;
    this->default_height = default_height;
    this->default_bitrate = default_bitrate;

    this->no_video_timeout = no_video_timeout;

    deferred_reg.setDeferredProcessor (deferred_processor);
}

// TODO Unused?
#if 0
void
GstStreamCtl::release ()
{
    mutex.lock ();
    closeStream (false /* replace_video_stream */);
    mutex.unlock ();
}
#endif

GstStreamCtl::GstStreamCtl ()
    : moment (NULL),
      timers (NULL),
      page_pool (NULL),

      send_metadata (true),
      enable_prechunking (true),

      default_width (0),
      default_height (0),
      default_bitrate (0),

      no_video_timeout (0),

      is_chain (false),

      stream_ticket (NULL),

      stream_stopped (false),
      got_video (false),

      connect_on_demand_timer (NULL),

      stream_start_time (0),

      rx_bytes_accum (0),
      rx_audio_bytes_accum (0),
      rx_video_bytes_accum (0)
{
    logD (ctl, _this_func_);

    deferred_task.cb = CbDesc<DeferredProcessor::TaskCallback> (
	    deferredTask, this /* cb_data */, this /* coderef_container */);
}

GstStreamCtl::~GstStreamCtl ()
{
    logD (ctl, _this_func_);

    mutex.lock ();
    if (gst_stream) {
        gst_stream->releasePipeline ();
        gst_stream = NULL;
    }
    mutex.unlock ();

    deferred_reg.release ();
}

}

