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
	mt_const VideoCodec video_codec;
	mt_const bool is_chain;

	Timers::TimerKey no_video_timer;

	Ref<VideoStream> video_stream;
	MomentServer::VideoStreamKey video_stream_key;

	GstElement *playbin;
	GstElement *encoder;
	gulong audio_probe_id;
	gulong video_probe_id;

	Time last_frame_time;

	Mutex stream_mutex;

	Stream ()
	    : video_codec (VideoCodec::SorensonH263),
	      is_chain (false),
	      no_video_timer (NULL) /* TODO This nullification should be unnecessary */,
	      playbin (NULL),
	      encoder (NULL),
	      audio_probe_id (0),
	      video_probe_id (0),
	      last_frame_time (0)
	{
	}
    };

    MomentServer *moment;
    Timers *timers;
    PagePool *page_pool;

    Uint64 default_width;
    Uint64 default_height;
    Uint64 default_bitrate;

    typedef IntrusiveList<Stream> StreamList;
    StreamList stream_list;

    void createStream (ConstMemory const &stream_name,
		       ConstMemory const &stream_spec,
		       VideoCodec         video_codec,
		       bool               is_chain);

    void restartStream (Stream *stream);

    static void noVideoTimerTick (void *_stream);

    class CreateVideoStream_Data;

    void createVideoStream (Stream * const stream);

    static gpointer streamThreadFunc (gpointer _data);

    void closeVideoStream (Stream * const stream);

    Result createPipelineForChainSpec (Stream * const stream);

    Result createPipelineForUri (Stream * const stream);

    Result createPipeline (Stream * const stream);

    static gboolean audioDataCb (GstPad    * const pad,
				 GstBuffer * const buffer,
				 gpointer    const _stream);

    static gboolean videoDataCb (GstPad    * const pad,
				 GstBuffer * const buffer,
				 gpointer    const _stream);

    static gboolean busCallCb (GstBus     * const bus,
			       GstMessage * const msg,
			       gpointer     const _stream);

public:
    Result init (MomentServer *moment);

    MomentGstModule ();

    ~MomentGstModule ();
};

}


#endif /* __MOMENT_GST__MOMENT_GST_MODULE__H__ */

