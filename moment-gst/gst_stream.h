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
private:
    mt_const MomentServer *moment;
    mt_const Timers *timers;
    mt_const PagePool *page_pool;

    mt_const Ref<String> stream_name;

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

//#error Synchronize access to 'playbin' and other fields
    mt_mutex (mutex) GstElement *playbin;
    mt_mutex (mutex) GstElement *encoder;
    mt_mutex (mutex) gulong audio_probe_id;
    mt_mutex (mutex) gulong video_probe_id;

  // Video stream state

    mt_mutex (mutex) Ref<VideoStream> video_stream;
    mt_mutex (mutex) MomentServer::VideoStreamKey video_stream_key;

    mt_mutex (mutex) Timers::TimerKey no_video_timer;
    mt_mutex (mutex) Time last_frame_time;

    // TODO Unify with 'video_codec'.
    mt_mutex (mutex) VideoStream::AudioCodecId audio_codec_id;
    mt_mutex (mutex) Byte audio_hdr;

    mt_mutex (mutex) VideoStream::VideoCodecId video_codec_id;
    mt_mutex (mutex) Byte video_hdr;

    mt_mutex (mutex) RtmpServer::MetaData metadata;

    mt_mutex (mutex) Cond metadata_reported_cond;
    mt_mutex (mutex) bool metadata_reported;

    mt_mutex (mutex) bool got_video;
    mt_mutex (mutex) bool got_audio;

    mt_mutex (mutex) bool first_audio_frame;
    mt_mutex (mutex) Count audio_skip_counter;

    mt_mutex (mutex) bool first_video_frame;

    mt_mutex (mutex) Uint64 prv_audio_timestamp;

    // Separate state mutex to decouple from synchronization of deletion
    // subscriptions.
    StateMutex mutex;

#if 0
    Ref<Stream> createStream (ConstMemory const &stream_name,
			      ConstMemory const &stream_spec,
			      bool               is_chain,
			      bool               recording,
			      ConstMemory        record_filename);
#endif

    class CreateVideoStream_Data;

    mt_mutex (mutex) void createVideoStream ();

    static gpointer streamThreadFunc (gpointer _self);

    mt_mutex (mutex) void doCloseVideoStream ();

    mt_mutex (mutex) void restartStream ();

    static void noVideoTimerTick (void *_self);

    Result createPipelineForChainSpec ();

    Result createPipelineForUri ();

    mt_mutex (mutex) Result createPipeline ();

    mt_mutex (mutex) void reportMetaData ();

    void doAudioData (GstBuffer *buffer);

    static gboolean audioDataCb (GstPad    * /* pad */,
				 GstBuffer *buffer,
				 gpointer   _self);

    static void handoffAudioDataCb (GstElement * /* element */,
				    GstBuffer  *buffer,
				    GstPad     * /* pad */,
				    gpointer    _self);

    void doVideoData (GstBuffer *buffer);

    static gboolean videoDataCb (GstPad    * /* pad */,
				 GstBuffer *buffer,
				 gpointer   _self);

    static void handoffVideoDataCb (GstElement * /* element */,
				    GstBuffer  *buffer,
				    GstPad     * /* pad */,
				    gpointer    _self);

    static gboolean busCallCb (GstBus     *bus,
			       GstMessage *msg,
			       gpointer    _self);

public:
    void beginVideoStream (ConstMemory stream_spec,
			   bool        is_chain);

    void endVideoStream ();

    mt_const void init (MomentServer *moment,
			ConstMemory   stream_name,
			bool          recording,
			ConstMemory   record_filename,
			bool          send_metadata,
			Uint64        default_width,
			Uint64        default_height,
			Uint64        default_bitrate);

    void release ();

    GstStream ();

    ~GstStream ();
};

}


#endif /* __MOMENT__GST_STREAM__H__ */

