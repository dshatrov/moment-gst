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
#include <gst/app/gstappsrc.h>

#include <moment/libmoment.h>


namespace MomentGst {

using namespace M;
using namespace Moment;

class GstStream : public Object
{
private:
    StateMutex mutex;

public:
    struct Frontend {
	void (*error) (void *cb_data);

	// End of stream.
	void (*eos) (void *cb_data);

	void (*noVideo) (void *cb_data);

	void (*gotVideo) (void *cb_data);

	// Called with gstreamer locks held.
	void (*statusEvent) (void *cb_data);
    };

private:
    class WorkqueueItem : public Referenced
    {
    public:
        enum ItemType {
            ItemType_CreatePipeline,
            ItemType_ReleasePipeline
        };

        mt_const ItemType item_type;
    };

    // For log lines.
    mt_const Ref<String> stream_name;

    mt_const DataDepRef<Timers> timers;
    mt_const DataDepRef<PagePool> page_pool;

    mt_const Ref<VideoStream> video_stream;
    mt_const Ref<VideoStream> mix_video_stream;

    mt_const GstCaps *mix_audio_caps;
    mt_const GstCaps *mix_video_caps;

    mt_const Ref<String> stream_spec;
    mt_const bool is_chain;

    mt_const bool send_metadata;
    mt_const bool enable_prechunking;

    mt_const Uint64 default_width;
    mt_const Uint64 default_height;
    mt_const Uint64 default_bitrate;

    mt_const Time no_video_timeout;

    mt_const Ref<Thread> workqueue_thread;

    mt_mutex (mutex)
    mt_begin

      List< Ref<WorkqueueItem> > workqueue_list;
      Cond workqueue_cond;

      Timers::TimerKey no_video_timer;

      GstElement *playbin;
      gulong audio_probe_id;
      gulong video_probe_id;

      GstAppSrc *mix_audio_src;
      GstAppSrc *mix_video_src;

      Time initial_seek;
      bool initial_seek_pending;
      bool initial_seek_complete;
      bool initial_play_pending;

      RtmpServer::MetaData metadata;
      Cond metadata_reported_cond;
      bool metadata_reported;

      Time last_frame_time;

      VideoStream::AudioCodecId audio_codec_id;
      unsigned audio_rate;
      unsigned audio_channels;

      VideoStream::VideoCodecId video_codec_id;

      bool got_in_stats;
      bool got_video;
      bool got_audio;

      bool first_audio_frame;
      Count audio_skip_counter;
      Count video_skip_counter;

      bool first_video_frame;

      Uint64 prv_audio_timestamp;

      // This flag helps to prevent concurrent pipeline state transition
      // requests (to NULL and to PLAYING states).
      // TODO FIXME chaning_state_to_playing is not used in all cases where it should be.
      bool changing_state_to_playing;

      bool reporting_status_events;

      // If 'true', then a seek to 'initial_seek' position should be initiated
      // in reportStatusEvents().
      bool seek_pending;
      // If 'true', then the pipeline should be set to PLAYING state
      // in reportStatusEvents().
      bool play_pending;

      // No "no_video" notifications should be made after error or eos
      // notification for the same GstStream instance.
      bool no_video_pending;
      bool got_video_pending;
      bool error_pending;
      bool eos_pending;
      // If 'true', then no further notifications for this GstStream
      // instance should be made.
      // 'close_notified' is set to true after error or eos notification.
      bool close_notified;

      // If 'true', then the stream associated with this GstStream
      // instance has been closed, which means that all associated gstreamer
      // objects should be released.
      bool stream_closed;

      // Bytes received (in_stats_el's sink pad).
      Uint64 rx_bytes;
      // Bytes generated (audio fakesink's sink pad).
      Uint64 rx_audio_bytes;
      // Bytes generated (video fakesink's sink pad).
      Uint64 rx_video_bytes;

      LibMary_ThreadLocal *tlocal;

    mt_end

    mt_const Cb<Frontend> frontend;

    static void workqueueThreadFunc (void *_self);

  // Pipeline manipulation

    void createPipelineForChainSpec ();
    void createPipelineForUri ();

    void doCreatePipeline ();
    void doReleasePipeline ();

    mt_unlocks (mutex) Result setPipelinePlaying ();

    mt_unlocks (mutex) void pipelineCreationFailed ();

  // Audio/video data handling

    mt_mutex (mutex) void reportMetaData ();

    static gboolean inStatsDataCb (GstPad    *pad,
				   GstBuffer *buffer,
				   gpointer   _self);

  // Audio data handling

    mt_mutex (mutex) void doAudioData (GstBuffer *buffer);

    static gboolean audioDataCb (GstPad    *pad,
				 GstBuffer *buffer,
				 gpointer   _self);

    static void handoffAudioDataCb (GstElement *element,
				    GstBuffer  *buffer,
				    GstPad     *pad,
				    gpointer    _self);

  // Video data handling

    mt_mutex (mutex) void doVideoData (GstBuffer *buffer);

    static gboolean videoDataCb (GstPad    *pad,
				 GstBuffer *buffer,
				 gpointer   _self);

    static void handoffVideoDataCb (GstElement *element,
				    GstBuffer  *buffer,
				    GstPad     *pad,
				    gpointer    _self);

  // State management

    static GstBusSyncReply busSyncHandler (GstBus     *bus,
					   GstMessage *msg,
					   gpointer    _self);

    static void noVideoTimerTick (void *_self);

  mt_iface (VideoStream::EventHandler)

    static VideoStream::EventHandler mix_stream_handler;

    static void mixStreamAudioMessage (VideoStream::AudioMessage * mt_nonnull audio_msg,
				       void *_self);

    static void mixStreamVideoMessage (VideoStream::VideoMessage * mt_nonnull video_msg,
				       void *_self);

  mt_iface_end

public:
    void createPipeline ();

    void releasePipeline ();

    void reportStatusEvents ();

    class TrafficStats
    {
    public:
	Uint64 rx_bytes;
	Uint64 rx_audio_bytes;
	Uint64 rx_video_bytes;

	void reset ()
	{
	    rx_bytes = 0;
	    rx_audio_bytes = 0;
	    rx_video_bytes = 0;
	}

#if 0
	TrafficStats (Uint64 const rx_audio_bytes,
		      Uint64 const rx_video_bytes)
	    : rx_audio_bytes (rx_audio_bytes),
	      rx_video_bytes (rx_video_bytes)
	{
	}
#endif
    };

    void getTrafficStats (TrafficStats *ret_traffic_stats);

    void resetTrafficStats ();

    mt_const void setFrontend (CbDesc<Frontend> const &frontend)
    {
	this->frontend = frontend;
    }

    mt_const void init (ConstMemory     stream_name,
			ConstMemory     stream_spec,
			bool            is_chain,
			Timers         *timers,
			PagePool       *page_pool,
			VideoStream    *video_stream,
			VideoStream    *mix_video_stream,
			Time            initial_seek,
			bool            send_metadata,
                        bool            enable_prechunking,
			Uint64          default_width,
			Uint64          default_height,
			Uint64          default_bitrate,
			Time            no_video_timeout);

    GstStream ();

    ~GstStream ();
};

}


#endif /* __MOMENT__GST_STREAM__H__ */

