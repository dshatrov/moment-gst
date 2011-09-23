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


#ifndef __MOMENT__GST_STREAM__H__
#define __MOMENT__GST_STREAM__H__


#include <libmary/types.h>
#include <gst/gst.h>

#include <moment/libmoment.h>


namespace MomentGst {

using namespace M;
using namespace Moment;

class GstStream : public IntrusiveListElement<>,
		  public Object
{
public:
    struct Frontend
    {
	void (*error) (void *stream_ticket,
		       void *cb_data);

	// End of stream.
	void (*eos) (void *stream_ticket,
		     void *cb_data);
    };

private:
    // StreamControl object helps to resolve deadlocks caused by mixing
    // GstStream::mutex and internal gstreamer locks.
    //
    // A pseudo-code example ('mnt' - GstStream::mutex, 'gst' - gstreamer locks,
    // 'ctl' - StreamControl::mutex) follows.
    //
    // Without StreamControl, synchronization looked like this:
    //
    //     A method of GstStream makes a gstreamer call:
    //         mnt { gst{} }
    //
    //     A callback called by gstreamer accesses GstStream data:
    //         gst { mnt{} }
    //
    // The above example involves lock order inversion, which means deadlocks.
    // With StreamControl, callbacks called by gstreamer access StreamControl
    // data, and not GstStream data, which resolves the deadlock:
    //
    //     A method of GstStream makes a gstreamer call:
    //         mnt { ctl{} gst{} }
    //
    //     A callback called by gstreamer accesses GstStream data:
    //         gst { ctl{} }
    //
    class StreamControl : public Referenced
    {
    public:
	mt_const GstStream *gst_stream;
	mt_const PagePool *page_pool;
	mt_const VideoStream *video_stream;

	mt_const VirtRef stream_ticket_ref;
	mt_const void *stream_ticket;

	mt_const bool send_metadata;

	mt_mutex (ctl_mutex)
	mt_begin

	  GstElement *playbin;

	  Time initial_seek;
	  bool initial_seek_pending;

	  RtmpServer::MetaData metadata;

	  Cond metadata_reported_cond;
	  bool metadata_reported;

	  Time last_frame_time;

	  // TODO Unify with 'video_codec'.
	  VideoStream::AudioCodecId audio_codec_id;
	  Byte audio_hdr;

	  VideoStream::VideoCodecId video_codec_id;
	  Byte video_hdr;

	  bool got_video;
	  bool got_audio;

	  bool first_audio_frame;
	  Count audio_skip_counter;

	  bool first_video_frame;

	  Uint64 prv_audio_timestamp;

	  bool error_pending;
	  bool eos_pending;

	mt_end

	Mutex ctl_mutex;

	mt_mutex (ctl_mutex) void reportMetaData ();

	mt_mutex (ctl_mutex) void doAudioData (GstBuffer *buffer);

	static gboolean audioDataCb (GstPad    * /* pad */,
				     GstBuffer *buffer,
				     gpointer   _ctl);

	static void handoffAudioDataCb (GstElement * /* element */,
					GstBuffer  *buffer,
					GstPad     * /* pad */,
					gpointer    _ctl);

	mt_mutex (ctl_mutex) void doVideoData (GstBuffer *buffer);

	static gboolean videoDataCb (GstPad    * /* pad */,
				     GstBuffer *buffer,
				     gpointer   _ctl);

	static void handoffVideoDataCb (GstElement * /* element */,
					GstBuffer  *buffer,
					GstPad     * /* pad */,
					gpointer    _ctl);

#if 0
    // Unused
	static gboolean busCallCb (GstBus     *bus,
				   GstMessage *msg,
				   gpointer    _ctl);
#endif

	static GstBusSyncReply busSyncHandler (GstBus     *bus,
					       GstMessage *msg,
					       gpointer    _ctl);

	void init (GstStream   *gst_stream,
		   PagePool    *page_pool,
		   VideoStream *video_stream,
		   bool         send_metadata);

	StreamControl ();
    };

    mt_const Cb<Frontend> frontend;

    mt_const MomentServer *moment;
    mt_const Timers *timers;
    mt_const PagePool *page_pool;

    mt_const Ref<String> stream_name;

    DeferredProcessor::Registration deferred_reg;
    DeferredProcessor::Task deferred_task;

    mt_mutex (mutex) bool valid;

  // Defaults

    mt_const bool send_metadata;

    mt_const Uint64 default_width;
    mt_const Uint64 default_height;
    mt_const Uint64 default_bitrate;

  // Stream source description

    mt_mutex (mutex) Ref<String> stream_spec;
    mt_mutex (mutex) bool is_chain;

    // TODO Remember stream position
    mt_mutex (mutex) Time stream_position;

  // Recording state

    mt_const ServerThreadContext *recorder_thread_ctx;

    mt_async AvRecorder recorder;
    mt_async FlvMuxer flv_muxer;

    mt_mutex (mutex) bool recording;
    mt_mutex (mutex) ConstMemory record_filename;

  // Gstreamer state

    mt_mutex (mutex) GstElement *playbin;
    mt_mutex (mutex) GstElement *encoder;
    mt_mutex (mutex) gulong audio_probe_id;
    mt_mutex (mutex) gulong video_probe_id;

  // Video stream state

    mt_mutex (mutex) Ref<VideoStream> video_stream;
    mt_mutex (mutex) MomentServer::VideoStreamKey video_stream_key;

    mt_mutex (mutex) Ref<StreamControl> stream_ctl;

    mt_mutex (mutex) Timers::TimerKey no_video_timer;

    // Separate state mutex to decouple from synchronization of deletion
    // subscriptions.
    StateMutex mutex;

    static bool deferredTask (void *_self);

    class CreateVideoStream_Data;

    mt_mutex (mutex) void createVideoStream (Time            initial_seek,
					     void           *stream_ticket,
					     VirtReferenced *stream_ticket_ref);

    static gpointer streamThreadFunc (gpointer _self);

    mt_mutex (mutex) void doCloseVideoStream ();
    mt_mutex (mutex) mt_unlocks (stream_ctl->ctl_mutex) void restartStream ();

    static void noVideoTimerTick (void *_self);

    mt_mutex (mutex) Result createPipelineForChainSpec ();
    mt_mutex (mutex) Result createPipelineForUri ();
    mt_mutex (mutex) Result createPipeline ();

public:
    void beginVideoStream (ConstMemory     stream_spec,
			   bool            is_chain,
			   void           *stream_ticket,
			   VirtReferenced *stream_ticket_ref,
			   Time            seek = 0);

    void endVideoStream ();

    mt_const void init (MomentServer      *moment,
			DeferredProcessor *deferred_processor,
			ConstMemory        stream_name,
			bool               recording,
			ConstMemory        record_filename,
			bool               send_metadata,
			Uint64             default_width,
			Uint64             default_height,
			Uint64             default_bitrate);

    void release ();

    mt_const void setFrontend (CbDesc<Frontend> const &frontend)
    {
	this->frontend = frontend;
    }

    GstStream ();

    ~GstStream ();
};

}


#endif /* __MOMENT__GST_STREAM__H__ */

