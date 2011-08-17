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


#ifndef __MOMENT_GST__MOMENT_GST_MODULE__H__
#define __MOMENT_GST__MOMENT_GST_MODULE__H__


#include <libmary/types.h>
#include <gst/gst.h>

#include <moment/libmoment.h>


namespace Moment {

using namespace M;

class MomentGstModule : public Object
{
private:
  // TODO MT-safety

    class Stream : public IntrusiveListElement<>,
		   public Object
    {
    public:
	WeakCodeRef weak_gst_module;
	MomentGstModule *unsafe_gst_module;

	mt_const Ref<String> stream_name;
	mt_const Ref<String> stream_spec;
	mt_const VideoStream::VideoCodecId video_codec;
	mt_const bool is_chain;

	Timers::TimerKey no_video_timer;

	Ref<VideoStream> video_stream;
	MomentServer::VideoStreamKey video_stream_key;

	GstElement *playbin;
	GstElement *encoder;
	gulong audio_probe_id;
	gulong video_probe_id;

	RtmpServer::MetaData metadata;

	mt_mutex (stream_mutex) Time last_frame_time;

	// TODO Unify with 'video_codec'.
	mt_mutex (stream_mutex) VideoStream::AudioCodecId audio_codec_id;
	mt_mutex (stream_mutex) Byte audio_hdr;

	mt_mutex (stream_mutex) Cond metadata_reported_cond;
	mt_mutex (stream_mutex) bool metadata_reported;

	mt_mutex (stream_mutex) bool got_video;
	mt_mutex (stream_mutex) bool got_audio;

	mt_mutex (stream_mutex) bool first_audio_frame;
	mt_mutex (stream_mutex) Count audio_skip_counter;

	mt_mutex (stream_mutex) bool first_video_frame;

	mt_mutex (stream_mutex) Uint64 prv_audio_timestamp;

	Mutex stream_mutex;

	Stream ()
	    : video_codec (VideoStream::VideoCodecId::SorensonH263),
	      is_chain (false),
	      no_video_timer (NULL) /* TODO This nullification should be unnecessary */,
	      playbin (NULL),
	      encoder (NULL),
	      audio_probe_id (0),
	      video_probe_id (0),
	      last_frame_time (0),
	      audio_codec_id (VideoStream::AudioCodecId::Unknown),
	      audio_hdr (0xbe /* Speex */),
	      metadata_reported (false),
	      got_video (false),
	      got_audio (false),
	      first_audio_frame (true),
	      audio_skip_counter (0),
	      first_video_frame (true),
	      prv_audio_timestamp (0)
	{
	}
    };

    mt_const MomentServer *moment;
    mt_const Timers *timers;
    mt_const PagePool *page_pool;

    mt_const bool send_metadata;

    mt_const Uint64 default_width;
    mt_const Uint64 default_height;
    mt_const Uint64 default_bitrate;

    typedef IntrusiveList<Stream> StreamList;
    StreamList stream_list;

    void createStream (ConstMemory const &stream_name,
		       ConstMemory const &stream_spec,
		       VideoStream::VideoCodecId video_codec,
		       bool               is_chain);

    void restartStream (Stream *stream);

    static void noVideoTimerTick (void *_stream);

    class CreateVideoStream_Data;

    void createVideoStream (Stream *stream);

    static gpointer streamThreadFunc (gpointer _data);

    mt_mutex (stream->stream_mutex) void closeVideoStream (Stream *stream);

    Result createPipelineForChainSpec (Stream *stream);

    Result createPipelineForUri (Stream *stream);

    mt_mutex (stream->stream_mutex) Result createPipeline (Stream *stream);

    mt_mutex (stream->stream_mutex) static void reportMetaData (Stream *stream);

    static void doAudioData (GstBuffer *buffer,
			     Stream    *stream);

    static gboolean audioDataCb (GstPad    * /* pad */,
				 GstBuffer *buffer,
				 gpointer   _stream);

    static void handoffAudioDataCb (GstElement * /* element */,
				    GstBuffer  *buffer,
				    GstPad     * /* pad */,
				    gpointer    _stream);

    static void doVideoData (GstBuffer *buffer,
			     Stream    *stream);

    static gboolean videoDataCb (GstPad    * /* pad */,
				 GstBuffer *buffer,
				 gpointer   _stream);

    static void handoffVideoDataCb (GstElement * /* element */,
				    GstBuffer  *buffer,
				    GstPad     * /* pad */,
				    gpointer    _stream);

    static gboolean busCallCb (GstBus     *bus,
			       GstMessage *msg,
			       gpointer    _stream);

public:
    Result init (MomentServer *moment);

    MomentGstModule ();

    ~MomentGstModule ();
};

}


#endif /* __MOMENT_GST__MOMENT_GST_MODULE__H__ */

