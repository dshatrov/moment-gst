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


#include <libmary/types.h>
#include <gst/gst.h>

#include <libmary/module_init.h>

#include <moment/libmoment.h>

#include <moment-gst/moment_gst.h>


namespace Moment {

namespace {

MomentGstModule moment_gst_module;

Result
MomentGstModule::init (MomentServer * const moment)
{
    this->moment = moment;
}

MomentServer *moment = NULL;

class MomentGstStream : public IntrusiveListElement<>
{
public:
    Ref<VideoStream> video_stream;
    Time last_frame_time;

    Ref<String> stream_name;
    Ref<Srting> stream_chain;

    MomentGstStream ()
	: last_frame_time (0)
    {
    }
};

IntrusiveList<MomentGstStream> stream_list;

void reconnect (MomentGstStream * const stream)
{
    stream->last_frame_time = 0;

    stream->video_stream->close ();

    createStream (stream->stream_name->mem(), stream->stream_chain->mem());

    stream_list.remove (stream);
    stream->unref ();
}

gboolean busCall (GstBus     * const bus,
		  GstMessage * const msg,
		  gpointer     const _stream)
{
    MomentGstStream * const stream = static_cast <MomentGstStream*> (_stream);

    switch (GST_MESSAGE_TYPE (msg)) {
	case GST_MESSAGE_EOS: {
	    logE_ (_func, "GST_MESSAGE_EOS");
	    reconnect (stream);
	} break;
	case GST_MESSAGE_ERROR: {
	    logE_ (_func, "GST_MESSAGE_ERROR");
	    reconnect (stream);
	} break;
	default:
	  // No-op
	    ;
    }

    return TRUE;
}

void rtspsrcNewPadCb (GstElement *rtspsrc,
		      GstPad     *pad,
		      gpointer    _decodebin);

void decodebinNewPadCb (GstElement *decodebin,
			GstPad     *pad,
			gboolean    is_last,
			gpointer    _encoder);

gboolean videoDataCb (GstPad    *pad,
		      GstBuffer *buffer,
		      gpointer   _stream);

GstElement* initStream (ConstMemory const &stream_chain)
{
    GstElement *pipeline  = NULL,
	       *rtspsrc   = NULL,
	       *decodebin = NULL,
	       *encoder   = NULL,
	       *fakesink  = NULL;

  {
    pipeline = gst_pipeline_new (NULL);
    if (!pipeline) {
	logE_ (_func, "gst_pipeline_new() failed");
	return NULL;
    }

    rtspsrc = gst_element_make_from_uri (GST_URI_SRC, grab (new String (stream_chain))->cstr(), NULL);
    if (!rtspsrc) {
	logE_ (_func, "gst_element_make_from_uri() failed");
	goto _release_pipeline;
    }

    g_object_set (G_OBJECT (rtspsrc), "protocols", 0x4, NULL);

    decodebin = gst_element_factory_make ("decodebin2", NULL);
    if (decodebin == NULL)  {
	logE_ (_func, "gst_element_factory_make() failed (doecodebin2)");
	goto _release_pipeline;
    }

    encoder = gst_element_factory_make ("ffenc_flv", NULL);
    if (encoder == NULL) {
	logE_ (_func, "gst_element_factory_make() failed (ffenc_flv)");
	goto _release_decodebin;
    }

    g_object_set (G_OBJECT (encoder), "bitrate", 100000, NULL);

    fakesink = gst_element_factory_make ("fakesink", NULL);
    if (fakesink == NULL) {
	logE_ (_func, "gst_element_factory_make() failed (fakesink)");
	goto _release_encoder;
    }

    {
	GstBus * const bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
	assert (bus);
	gst_bus_add_watch (bus, busCall, /* camera */);
	gst_object_unref (bus);
    }

    gst_bin_add_many (GST_BIN (pipeline), rtspsrc, decodebin, encoder, fakesink, NULL);

    if (!gst_element_link_many (encoder, fakesink, NULL)) {
	logE_ (_func, "gst_element_link_many() failed");
	return _release_pipeline;
    }

    g_signal_connect (rtspsrc, "pad-added", G_CALLBACK (rtspsrc_new_pad_cb), decodebin);
    g_signal_connect (decodebin, "new-decoded-pad", G_CALLBACK (decodebin_new_pad_cb), encoder);

    do {
	GstPad * const pad = gst_element_get_static_pad (encoder, "src");
	if (!pad) {
	    logE_ (_func, "gst_element_get_static_pad() faied");
	    break;
	}

	/* camera->buffer_probe_handler_id = */
		gst_pad_add_buffer_probe (pad, G_CALLBACK (video_data_cb), /* camera */);

	gst_object_unref (pad);
    } while (0);

    gst_element_set_state (pipeline, GST_STATE_PLAYING);
  }

    return pipeline;

//_release_fakesink:
//    gst_object_unref (GST_OBJECT (fakesink));

_release_encoder:
    gst_object_unref (GST_OBJECT (encoder));

_release_decodebin:
    gst_object_unref (GST_OBJECT (decodebin));

_release_rtspsrc:
    gst_object_unref (GST_OBJECT (rtspsrc));

_release_pipline:
    gst_object_unref (GST_OBJECT (pipeline));

    return NULL;
}

void rtspsrcNewPadCb (GstElement * const rtspsrc,
		      GstPad     * const pad,
		      gpointer     const _decodebin)
{
    GstElement * const decodebin = GST_ELEMENT (_decodebin);

    GstPad * const sinkpad = gst_element_get_static_cast (decodebin, "sink");
    if (sinkpad == NULL) {
	logE_ (_func, "gst_element_get_static_pad() failed");
	return;
    }

    if (gst_pad_link (pad, sinkpad) != GST_PAD_LINK_OK)
	logE_ (_func, "gst_pad_link() failed");

    gst_object_unref (sinkpad);
}

void decodebinNewPadCb (GstElement * const decodebin,
			GstPad     * const pad,
			gboolean     const is_last,
			gpointer     const _encoder)
{
    GstElement * const encoder = GST_ELEMENT (_encoder);

    GstPad * const sinkpad = gst_element_get_static_pad (encoder, "sink");
    if (!sinkpad) {
	logE_ (_func, "gst_element_get_static_pad() failed");
	return;
    }

    if (gst_pad_link (pad, sinkpad) != GST_PAD_LINK_OK)
	logE_ (_func, "gst_pad_link() failed");

    gst_object_unref (sinkpad);
}

gboolean videoDataCb (GstPad    * const pad,
		      GstBuffer * const buffer,
		      gpointer    const _stream)
{
    MomentGstStream * const stream = static_cast <MomentGstStream*> (_stream);

    // TODO Retreive current time efficiently.
//    stream->last_videoframe_time = ;

    PagePool::PageList...

    Size msg_len = 0;

    {
      // Sorenson H.263 codec.
	Byte const video_hdr = 0x12;
	getFillPages ();
	msg_len += 1;
    }

    getFillPages (GST_BUFFER_DATA (buffer), GST_BUFFER_DATA (buffer));
    msg_len += GST_BUFFER_SIZE (buffer);

    // TODO is_keyframe

    Uint32 const timestamp = (Uint32) (GST_BUFFER_TIMESTAMP (buffer) / 1000000);

#if 0
    if (is_keyframe) {
    }
#endif

    stream->video_stream->fireVideoMessage (...);

    page_pool->putPages ();

    return TRUE;
}

class CreateStream_Data
{
public:
    Ref<VideoStream> video_stream;
    Ref<String> stream_chain;
};

void doCreateStream (CreateStream_Data * const data)
{
    Ref<MomentGstStream> stream = grab (new MomentGstStream);

    GstElement * const pipeline = initStream ();
    if (!pipeline) {
	// TODO delete video_stream
	return;
    }

    // TODO no_video_timer
}

gpointer streamThreadFunc (gpointer const _data)
{
    CreateStream_Data * const data = static_cast <CreateStream_Data*> (_data);

    doCreateStream ();

    delete data;
    return (gpointer) 0;
}

void createStream (ConstMemory const &stream_name,
		   ConstMemory const &stream_chain)
{
    Ref<VideoStream> video_stream = grab (new VideoStream);
    moment->addVideoStream (stream_name, video_stream);

    CreateStreamData * const data = new CreateStream_Data;
    assert (data);

    data->video_stream = video_stream;
    data->stream_chain = grab (new String (stream_chain));

    GThread * const thread = g_thread_create (streamThreadFunc,
					      data,
					      FALSE /* joinable */,
					      NULL  /* error */);
    if (thread == NULL) {
	logE_ (_func, "g_thread_create() failed");
	// TODO delete video_stream
	delete data;
    }
}

void momentGstInit ()
{
    logD_ ("GST MODULE INIT");

    moment = MomentServer::getInstance();

    ServerApp * const server_app = moment->getServerApp();
    MConfig::Config * const config = moment->getConfig();

    MConfig::Section * const src_section = config->getSection ("mod_gst/sources");
    if (!src_section) {
	logI_ ("No video stream sources specified "
	       "(\"mod_gst/sources\" config section is missing).");
	return;
    }

    MConfig::Section::iter iter (*src_section);
    while (!src_section->iter_done (iter)) {
	MConfig::SectionEntry * const section_entry = src_section->iter_next (iter);
	if (section_entry->getType() == MConfig::SectionEntry::Type_Option) {
	    MConfig::Option * const src_option = static_cast <MConfig::Option*> (section_entry);
	    if (!src_option->getValue())
		continue;

	    ConstMemory const stream_name = src_option->getName();
	    ConstMemory const stream_chain = src_option->getValue()->mem();

	    logD_ (_func, "Stream name: ", stream_name, "; srteam chain: ", stream_chain);

	    createStream (stream_name, stream_chain);
	}
    }
}

void momentGstUnload ()
{
    logD_ ("GST MODULE UNLOAD");
}

}

}


namespace M {

void libMary_moduleInit ()
{
    Moment::momentGstInit ();
}

void libMary_moduleUnload ()
{
    Moment::momentGstUnload ();
}

}

