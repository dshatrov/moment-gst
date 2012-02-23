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


#ifndef __MOMENT_GST__GST_STREAM_CTL__H__
#define __MOMENT_GST__GST_STREAM_CTL__H__


#include <libmary/types.h>
#include <gst/gst.h>

#include <moment/libmoment.h>

#include <moment-gst/gst_stream.h>


namespace MomentGst {

using namespace M;
using namespace Moment;

class GstStreamCtl : public Object
{
public:
    struct Frontend
    {
	// Called when a new VideoStream object is created for the stream.
	//
	// In a perfect implementation, this would never be called. Currently,
	// newVideoStream() is called every time a new VideoStream is created
	// by GstStreamCtl's initiative. E.g., when it reconnects to the video
	// source after no_video_timeout expires). Note that beginVideoStream()
	// implies creation of a new VideoStream as well, but newVideoStream()
	// callback is not called in this cases because the initiative comes
	// from the outside.
	//
	// Called with GstStream::mutex held.
	void (*newVideoStream) (void *stream_ticket,
				void *cb_data);

	// Called every time playback interrupts because of some error.
	void (*error) (void *stream_ticket,
		       void *cb_data);

	// End of stream.
	// Called when end of stream is reached for the current video source.
	void (*eos) (void *stream_ticket,
		     void *cb_data);
    };

private:
    mt_const MomentServer *moment;
    mt_const Timers *timers;
    mt_const PagePool *page_pool;

    mt_const Ref<String> stream_name;

    DeferredProcessor::Registration deferred_reg;
    DeferredProcessor::Task deferred_task;

  // Defaults

    mt_const bool send_metadata;
    mt_const bool keep_video_stream;

    mt_const Uint64 default_width;
    mt_const Uint64 default_height;
    mt_const Uint64 default_bitrate;

    mt_const Time no_video_timeout;

  // Stream source description

    mt_mutex (mutex) Ref<String> stream_spec;
    mt_mutex (mutex) bool is_chain;

  // Video stream state

    class StreamData : public Referenced
    {
    public:
	GstStreamCtl * const gst_stream_ctl;

	void * const stream_ticket;
	VirtRef const stream_ticket_ref;

	mt_mutex (GstStreamCtl::mutex) bool stream_closed;

	StreamData (GstStreamCtl   * const gst_stream_ctl,
		    void           * const stream_ticket,
		    VirtReferenced * const stream_ticket_ref)
	    : gst_stream_ctl (gst_stream_ctl),
	      stream_ticket (stream_ticket),
	      stream_ticket_ref (stream_ticket_ref),
	      stream_closed (false)
	{
	}
    };

    mt_mutex (mutex) Ref<GstStream> gst_stream;
    // Serves as internal ticket.
    mt_mutex (mutex) Ref<StreamData> cur_stream_data;

    mt_mutex (mutex) Ref<VideoStream> video_stream;
    mt_mutex (mutex) MomentServer::VideoStreamKey video_stream_key;

    mt_mutex (mutex) void *stream_ticket;
    mt_mutex (mutex) VirtRef stream_ticket_ref;

    mt_mutex (mutex) bool got_video;

    mt_mutex (mutex) Time stream_start_time;

    mt_mutex (mutex) Uint64 rx_bytes_accum;
    mt_mutex (mutex) Uint64 rx_audio_bytes_accum;
    mt_mutex (mutex) Uint64 rx_video_bytes_accum;

    // Separate state mutex to decouple from synchronization of deletion
    // subscriptions.
    StateMutex mutex;

    mt_const Cb<Frontend> frontend;

    class CreateVideoStream_Data;

    mt_mutex (mutex) void createStream (Time initial_seek);

    static gpointer streamThreadFunc (gpointer _gst_stream);

    mt_mutex (mutex) void closeStream (bool replace_video_stream);

    mt_unlocks (mutex) void doRestartStream ();

    static bool deferredTask (void *_self);

    mt_iface (GstStream::Frontend)
    mt_begin

      static GstStream::Frontend gst_stream_frontend;

      static void streamError (void *_stream_data);

      static void streamEos (void *_stream_data);

      static void noVideo (void *_stream_data);

      static void gotVideo (void *_stream_data);

      static void streamStatusEvent (void *_stream_data);

    mt_end

public:
    void beginVideoStream (ConstMemory     stream_spec,
			   bool            is_chain,
			   void           *stream_ticket,
			   VirtReferenced *stream_ticket_ref,
			   Time            seek = 0);

    void endVideoStream ();

    void restartStream ();

    bool isSourceOnline ();

    class TrafficStats
    {
    public:
	Uint64 rx_bytes;
	Uint64 rx_audio_bytes;
	Uint64 rx_video_bytes;
	Time time_elapsed;
    };

    void getTrafficStats (TrafficStats *ret_traffic_stats);

    void resetTrafficStats ();

    mt_const void init (MomentServer      *moment,
			DeferredProcessor *deferred_processor,
			ConstMemory        stream_name,
			bool               send_metadata,
			bool               keep_video_stream,
			Uint64             default_width,
			Uint64             default_height,
			Uint64             default_bitrate,
			Time               no_video_timeout);

    void release ();

    mt_const void setFrontend (CbDesc<Frontend> const &frontend)
    {
	this->frontend = frontend;
    }

    GstStreamCtl ();

    ~GstStreamCtl ();
};

}


#endif /* __MOMENT_GST__GST_STREAM_CTL__H__ */

