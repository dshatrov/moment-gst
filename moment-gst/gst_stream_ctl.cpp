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

mt_mutex (mutex) void
GstStreamCtl::createStream (Time const initial_seek)
{
    if (!video_stream) {
	video_stream = grab (new VideoStream);
	logD_ (_func, "Calling moment->addVideoStream, stream_name: ", stream_name->mem());
	video_stream_key = moment->addVideoStream (video_stream, stream_name->mem());
    }

    gst_stream = grab (new GstStream);
    gst_stream->init (stream_name->mem(),
		      stream_spec->mem(),
		      is_chain,
		      timers,
		      page_pool,
		      video_stream,
		      initial_seek,
		      send_metadata,
		      default_width,
		      default_height,
		      default_bitrate);

    Ref<StreamData> const stream_data = grab (new StreamData (
	    this, stream_ticket, stream_ticket_ref.ptr()));
    cur_stream_data = stream_data;

    gst_stream->setFrontend (CbDesc<GstStream::Frontend> (
	    &gst_stream_frontend,
	    stream_data /* cb_data */,
	    this /* coderef_container */,
	    stream_data /* ref_data */));

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

    updateTime ();
    gst_stream->createPipeline ();

    gst_stream->unref ();
    return (gpointer) 0;
}

mt_mutex (mutex) void
GstStreamCtl::closeStream (bool const replace_video_stream)
{
    logD_ (_func_);

    if (gst_stream) {
	gst_stream->releasePipeline ();
	gst_stream = NULL;
    }
    cur_stream_data = NULL;

    if (video_stream) {
	video_stream->close ();
	// TODO moment->replaceVideoStream() to swap video streams atomically
	moment->removeVideoStream (video_stream_key);
	video_stream = NULL;

	if (replace_video_stream) {
	    video_stream = grab (new VideoStream);
	    logD_ (_func, "Calling moment->addVideoStream, stream_name: ", stream_name->mem());
	    video_stream_key = moment->addVideoStream (video_stream, stream_name->mem());
	}
    }

    logD_ (_func, "done");
}

mt_unlocks (mutex) void
GstStreamCtl::restartStream ()
{
    closeStream (true /* replace_video_stream */);

    // TODO FIXME Set correct initial seek
    createStream (0 /* initial_seek */);

    VirtRef const tmp_stream_ticket_ref = stream_ticket_ref;
    void * const tmp_stream_ticket = stream_ticket;

    mutex.unlock ();

    if (frontend)
	frontend.call (frontend->newVideoStream, tmp_stream_ticket);
}

bool
GstStreamCtl::deferredTask (void * const _self)
{
    GstStreamCtl * const self = static_cast <GstStreamCtl*> (_self);

    logD_ (_func, "this: 0x", fmt_hex, (UintPtr) self);

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
    streamStatusEvent
};

void
GstStreamCtl::streamError (void * const _stream_data)
{
    StreamData * const stream_data = static_cast <StreamData*> (_stream_data);
    GstStreamCtl * const self = stream_data->gst_stream_ctl;

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

    mt_unlocks (mutex) self->restartStream ();
}

void
GstStreamCtl::streamStatusEvent (void * const _stream_data)
{
    StreamData * const stream_data = static_cast <StreamData*> (_stream_data);
    GstStreamCtl * const self = stream_data->gst_stream_ctl;

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
    mutex.lock ();
    if (gst_stream) {
	closeStream (true /* replace_video_stream */);
    }
    mutex.unlock ();
}

void
GstStreamCtl::init (MomentServer      * const moment,
		    DeferredProcessor * const deferred_processor,
		    ConstMemory         const stream_name,
		    bool                const send_metadata,
		    Uint64              const default_width,
		    Uint64              const default_height,
		    Uint64              const default_bitrate)
{
    this->moment = moment;
    this->timers = moment->getServerApp()->getTimers();
    this->page_pool = moment->getPagePool();

    this->stream_name = grab (new String (stream_name));

    this->send_metadata = send_metadata;

    this->default_width = default_width;
    this->default_height = default_height;
    this->default_bitrate = default_bitrate;

    deferred_reg.setDeferredProcessor (deferred_processor);
}

void
GstStreamCtl::release ()
{
    mutex.lock ();
    closeStream (false /* replace_video_stream */);
    mutex.unlock ();
}

GstStreamCtl::GstStreamCtl ()
    : moment (NULL),
      timers (NULL),
      page_pool (NULL),

      send_metadata (true),

      default_width (0),
      default_height (0),
      default_bitrate (0),

      is_chain (false),

      stream_ticket (NULL)
{
    logD_ (_func, "0x", fmt_hex, (UintPtr) this);

    deferred_task.cb = CbDesc<DeferredProcessor::TaskCallback> (
	    deferredTask, this /* cb_data */, this /* coderef_container */);
}

GstStreamCtl::~GstStreamCtl ()
{
    logD_ (_func, "0x", fmt_hex, (UintPtr) this);

    deferred_reg.release ();
}

}

