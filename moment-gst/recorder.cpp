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


#include <moment-gst/recorder.h>


using namespace M;
using namespace Moment;

namespace MomentGst {

Playback::Frontend Recorder::playback_frontend = {
    startPlaybackItem,
    stopPlaybackItem
};

void
Recorder::startPlaybackItem (Playlist::Item           * const item,
			     Time                       const /* seek */,
			     Playback::AdvanceTicket  * const /* advance_ticket */,
			     void                     * const _self)
{
    Recorder * const self = static_cast <Recorder*> (_self);

    self->mutex.lock ();

    self->cur_recording = NULL;
    self->cur_channel_name = NULL;
    self->weak_cur_channel = NULL;
    self->weak_cur_video_stream = NULL;

    {
	Ref<Channel> const channel = self->weak_cur_channel.getRef ();
	if (channel && self->channel_sbn) {
	    channel->getEventInformer()->unsubscribe (self->channel_sbn);
	}
	self->channel_sbn = GenericInformer::SubscriptionKey ();
    }

    self->doStopItem ();
    self->recording_now = false;

    if (!item->id) {
	self->mutex.unlock ();
	logW_ (_func, "Channel name is not specified for a recorder item");
	return;
    }

    Ref<Channel> const channel = self->channel_set->lookupChannel (item->id->mem());
    if (!channel) {
	self->mutex.unlock ();
	logD_ (_func, "Channel \"", item->id->mem(), "\" not found");
	return;
    }

    self->recording_now = true;
    self->cur_channel_name = grab (new String (item->id->mem()));
    self->weak_cur_channel = channel;

    self->cur_recording = grab (new Recording);
    self->cur_recording->recorder = self;

    self->channel_sbn = channel->getEventInformer()->subscribe (CbDesc<Channel::ChannelEvents> (
	    &channel_events,
	    self->cur_recording /* cb_data */,
	    self /* coderef_container */,
	    self->cur_recording /* ref_data */));

    self->doStartItem ();

    self->mutex.unlock ();
}

void
Recorder::stopPlaybackItem (void * const _self)
{
    Recorder * const self = static_cast <Recorder*> (_self);

    self->mutex.lock ();

    {
	Ref<Channel> const channel = self->weak_cur_channel.getRef ();
	if (channel && self->channel_sbn) {
	    channel->getEventInformer()->unsubscribe (self->channel_sbn);
	}
	self->channel_sbn = GenericInformer::SubscriptionKey ();
    }

    self->doStopItem ();
    self->recording_now = false;

    self->mutex.unlock ();
}

Channel::ChannelEvents Recorder::channel_events = {
    startChannelItem,
    stopChannelItem,
    newVideoStream
};

void
Recorder::startChannelItem (void * const _recording)
{
    Recording * const recording = static_cast <Recording*> (_recording);
    Recorder * const self = recording->recorder;

    logD_ (_func_);

    self->mutex.lock ();
    if (self->cur_recording != recording) {
	self->mutex.unlock ();
	logD_ (_func, "recording mismatch");
	return;
    }

    self->doStopItem ();

    Ref<Channel> const channel = self->weak_cur_channel.getRef();
    if (!channel) {
	self->mutex.unlock ();
	logD_ (_func, "channel gone");
	return;
    }

    self->doStartItem ();

    self->mutex.unlock ();
}

void
Recorder::stopChannelItem (void * const _recording)
{
    Recording * const recording = static_cast <Recording*> (_recording);
    Recorder * const self = recording->recorder;

    self->mutex.lock ();
    if (self->cur_recording != recording) {
	self->mutex.unlock ();
	return;
    }

    self->doStopItem ();

    self->mutex.unlock ();
}

void
Recorder::newVideoStream (void * const _recording)
{
    Recording * const recording = static_cast <Recording*> (_recording);
    Recorder * const self = recording->recorder;

    self->mutex.lock ();
    if (self->cur_recording != recording) {
	self->mutex.unlock ();
	return;
    }

    // Verifying that video stream has actually changed.
    if (self->cur_channel_name) {
	Ref<VideoStream> const video_stream = self->moment->getVideoStream (self->cur_channel_name->mem());
	if (video_stream) {
	    Ref<VideoStream> const cur_video_stream = self->weak_cur_video_stream.getRef ();
	    if (cur_video_stream && cur_video_stream == video_stream.ptr()) {
	      // Video stream has not changed.
		self->mutex.unlock ();
		return;
	    }
	}
    }

    self->doStopItem ();

    Ref<Channel> const channel = self->weak_cur_channel.getRef();
    if (!channel) {
	self->mutex.unlock ();
	return;
    }

    self->doStartItem ();

    self->mutex.unlock ();
}

mt_mutex (mutex) void
Recorder::doStartItem ()
{
    logD_ (_func_);

    if (!recording_now) {
	logD_ (_func, "!recording_now");
	return;
    }

    assert (cur_channel_name);
    Ref<VideoStream> const video_stream = moment->getVideoStream (cur_channel_name->mem());
    if (!video_stream) {
	logW_ (_func, "Video stream \"", cur_channel_name->mem(), "\" not found");
	return;
    }

    weak_cur_video_stream = video_stream;
    recorder.setVideoStream (video_stream);

    {
	Format fmt;
	fmt.num_base = 10;
	fmt.min_digits = 2;

	LibMary_ThreadLocal * const tlocal = libMary_getThreadLocal();
	Ref<String> const filename = makeString (
		fmt,
		filename_prefix->mem(),
		filename_prefix->mem().len() > 0 ? "_" : "",
		tlocal->localtime.tm_year + 1900, "-",
		tlocal->localtime.tm_mon + 1, "-",
		tlocal->localtime.tm_mday, "_",
		tlocal->localtime.tm_hour, "-",
		tlocal->localtime.tm_min, "-",
		tlocal->localtime.tm_sec,
		".flv");

	logD_ (_func, "Calling recorder.start(), filename: ", filename->mem());
	recorder.start (filename->mem());
    }
}

mt_mutex (mutex) void
Recorder::doStopItem ()
{
    logD_ (_func_);

    if (!recording_now)
	return;

    recorder.stop ();
}

mt_const void
Recorder::init (MomentServer * const moment,
		PagePool     * const page_pool,
		ChannelSet   * const channel_set,
		ConstMemory    const filename_prefix)
{
    this->moment = moment;
    this->channel_set = channel_set;
    this->filename_prefix = grab (new String (filename_prefix));

    playback.init (moment->getServerApp()->getTimers());
    playback.setFrontend (CbDesc<Playback::Frontend> (
	    &playback_frontend,
	    this /* cb_data */,
	    this /* coderef_container */));

    {
	ServerThreadContext *thread_ctx =
		moment->getRecorderThreadPool()->grabThreadContext (filename_prefix);
	if (thread_ctx) {
	    recorder_thread_ctx = thread_ctx;
	} else {
	    logE_ (_func, "Couldn't get recorder thread context: ", exc->toString());
	    thread_ctx = moment->getServerApp()->getMainThreadContext();
	}

	recorder.init (thread_ctx, moment->getStorage());
    }

    flv_muxer.setPagePool (page_pool);

    recorder.setMuxer (&flv_muxer);

// TODO recorder frontend + error reporting
//    recorder.setFrontend (CbDesc<AvRecorder::Frontend> (
//	    recorder_frontend, this /* cb_data */, this /* coderef_container */));
}

Recorder::Recorder ()
    : moment (NULL),
      channel_set (NULL),
      recorder_thread_ctx (NULL),
      playback (this /* coderef_container */),
      recorder (this /* coderef_container */),
      recording_now (false)
{
}

Recorder::~Recorder ()
{
    mutex.lock ();

    {
	Ref<Channel> const channel = weak_cur_channel.getRef ();
	if (channel && channel_sbn) {
	    channel->getEventInformer()->unsubscribe (channel_sbn);
	}
	channel_sbn = GenericInformer::SubscriptionKey ();
    }

    doStopItem ();

    if (recorder_thread_ctx) {
	moment->getRecorderThreadPool()->releaseThreadContext (recorder_thread_ctx);
	recorder_thread_ctx = NULL;
    }

    mutex.unlock ();
}

}

