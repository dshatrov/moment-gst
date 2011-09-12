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

#include <moment-gst/moment_gst_module.h>


namespace Moment {

namespace {
LogGroup libMary_logGroup_bus ("moment-gst_bus", LogLevel::E);
}

// If @is_chain is 'true', then @stream_spec is a chain spec with gst-launch
// syntax. Otherwise, @stream_spec is an uri for uridecodebin2.
void
MomentGstModule::createStream (ConstMemory const &stream_name,
			       ConstMemory const &stream_spec,
			       VideoStream::VideoCodecId const video_codec,
			       bool        const  is_chain)
{
    Ref<Stream> const stream = grab (new Stream);
    stream->weak_gst_module = this;
    stream->unsafe_gst_module = this;

    stream->stream_name  = grab (new String (stream_name));
    stream->stream_spec = grab (new String (stream_spec));
    stream->encoder_video_codec = video_codec;
    stream->is_chain = is_chain;

    createVideoStream (stream);

    stream->last_frame_time = 0;

    mutex.lock ();
    stream_list.append (stream);
    mutex.unlock ();

    stream->ref ();
}

// Must be called with stream->stream_mutex held.
// TODO Spawn a separate thread to actually restart the stream.
void
MomentGstModule::restartStream (Stream * const stream)
{
    closeVideoStream (stream);
    createVideoStream (stream);
}

void
MomentGstModule::noVideoTimerTick (void * const _stream)
{
//    logD_ (_func_);

    Stream * const stream = static_cast <Stream*> (_stream);

    CodeRef self_ref;
    if (stream->weak_gst_module.isValid()) {
	self_ref = stream->weak_gst_module;
	if (!self_ref)
	    return;
    }
    MomentGstModule * const self = stream->unsafe_gst_module;

    // TODO Update time efficiently.
    updateTime ();
    Time const time = getTime ();

    stream->stream_mutex.lock ();
    if (time > stream->last_frame_time &&
	time - stream->last_frame_time >= 15 /* TODO Config param for the timeout */)
    {
	logD_ (_func, "restarting stream");
	self->restartStream (stream);
    }
    stream->stream_mutex.unlock ();
}

class MomentGstModule::CreateVideoStream_Data
{
public:
    MomentGstModule *gst_module;
    Ref<Stream> stream;
    Ref<String> stream_spec;
    bool is_chain;
};

// Mutex be called with stream->stream_mutex held.
void
MomentGstModule::createVideoStream (Stream * const stream)
{
    stream->video_stream = grab (new VideoStream);
    stream->video_stream_key = moment->addVideoStream (stream->video_stream, stream->stream_name->mem());

    if (stream->no_video_timer) {
	timers->restartTimer (stream->no_video_timer);
    } else {
	// TODO Update time efficiently.
	updateTime ();
	// TODO Release stream->no_video_timer with timers->deleteTimer() at some point.
	stream->no_video_timer = timers->addTimer (noVideoTimerTick, stream, stream, 15 /* TODO config param for the timeout */, true /* periodical */);
    }

    {
	CreateVideoStream_Data * const data = new CreateVideoStream_Data;
	assert (data);

	data->gst_module = this;
	data->stream = stream;
	data->stream_spec = stream->stream_spec;
	data->is_chain = stream->is_chain;

	this->ref ();
	GThread * const thread = g_thread_create (streamThreadFunc,
						  data,
						  FALSE /* joinable */,
						  NULL  /* error */);
	if (thread == NULL) {
	    logE_ (_func, "g_thread_create() failed");
	    this->unref ();
	    moment->removeVideoStream (stream->video_stream_key);
	    stream->video_stream = NULL;
	    delete data;
	}
    }
}

gpointer
MomentGstModule::streamThreadFunc (gpointer const _data)
{
    CreateVideoStream_Data * const data = static_cast <CreateVideoStream_Data*> (_data);
    MomentGstModule * const self = data->gst_module;

    data->stream->stream_mutex.lock ();
    if (!self->createPipeline (data->stream))
	self->closeVideoStream (data->stream);
    data->stream->stream_mutex.unlock ();

    // TODO What to do with this?
    // In GStreamer, bus messages are delivered via glib main loop. Therefore,
    // we won't get any bus messages unless special action is taken.
#if 0
    GMainLoop * const loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (loop);
    assert (0);
    g_main_loop_unref (loop);
#endif

    self->unref ();
    delete data;
    return (gpointer) 0;
}

mt_mutex (stream->stream_mutex) void
MomentGstModule::closeVideoStream (Stream * const stream)
{
    stream->last_frame_time = 0;

    if (stream->playbin) {
	logD_ (_func, "Destroying playbin");

#if 0
	do {
	    GstPad * const pad = gst_element_get_static_pad (stream->encoder, "src");
	    if (!pad) {
		logE_ (_func, "gst_element_get_static_pad() failed");
		break;
	    }

	    gst_pad_remove_buffer_probe (pad, stream->buffer_probe_handler_id);
	    gst_object_unref (pad);
	} while (0);
#endif

	gst_element_set_state (stream->playbin, GST_STATE_NULL);
	gst_object_unref (GST_OBJECT (stream->playbin));
	stream->playbin = NULL;
    }

    if (stream->video_stream) {
	stream->video_stream->close ();
	moment->removeVideoStream (stream->video_stream_key);
	stream->video_stream = NULL;
    }
}

// TODO Always succeeds currently.
Result
MomentGstModule::init (MomentServer * const moment)
{
    this->moment = moment;
    this->timers = moment->getServerApp()->getTimers();
    this->page_pool = moment->getPagePool();

  // Opening video streams.

    MConfig::Config * const config = moment->getConfig();

    {
	ConstMemory const opt_name = "mod_gst/send_metadata";
	MConfig::Config::BooleanValue const val = config->getBoolean (opt_name);
	if (val == MConfig::Config::Boolean_Invalid) {
	    logE_ (_func, "Invalid value for ", opt_name, ": ", config->getString (opt_name));
	    return Result::Failure;
	}

	if (val == MConfig::Config::Boolean_False) {
	    send_metadata = false;
	    logI_ (_func, "onMetaData messages will not be generated by mod_gst. "
		   "Set \"", opt_name, "\" option to \"yes\" to enable sending of onMetaData messages.");
	}
    }

    {
	ConstMemory const opt_name = "mod_gst/width";
	MConfig::GetResult const res = config->getUint64_default (opt_name, &default_width, default_width);
	if (!res) {
	    logE_ (_func, "bad value for ", opt_name);
	    return Result::Failure;
	}
    }

    {
	ConstMemory const opt_name = "mod_gst/height";
	MConfig::GetResult const res = config->getUint64_default (opt_name, &default_height,  default_height);
	if (!res) {
	    logE_ (_func, "bad value for ", opt_name);
	    return Result::Failure;
	}
    }

    {
	ConstMemory const opt_name = "mod_gst/bitrate";
	MConfig::GetResult const res = config->getUint64_default (opt_name, &default_bitrate,  default_bitrate);
	if (!res) {
	    logE_ (_func, "bad value for ", opt_name);
	    return Result::Failure;
	}
    }

    {
	MConfig::Section * const src_section = config->getSection ("mod_gst/sources");
	if (src_section) {
	    MConfig::Section::iter iter (*src_section);
	    while (!src_section->iter_done (iter)) {
		MConfig::SectionEntry * const section_entry = src_section->iter_next (iter);
		if (section_entry->getType() == MConfig::SectionEntry::Type_Option) {
		    MConfig::Option * const src_option = static_cast <MConfig::Option*> (section_entry);
		    if (!src_option->getValue())
			continue;

		    ConstMemory const stream_name = src_option->getName();
		    ConstMemory const stream_uri = src_option->getValue()->mem();

		    logD_ (_func, "Stream name: ", stream_name, "; stream uri: ", stream_uri);

		    createStream (stream_name, stream_uri, VideoStream::VideoCodecId::SorensonH263, false /* chain */);
		}
	    }
	} else {
	    logI_ ("No video stream sources specified "
		   "(\"mod_gst/sources\" config section is missing).");
	}
    }

    {
#if 0
// Debugging
	do {
	    logD_ (_func, "Iterating through \"mod_gst\" section");
	    MConfig::Section * const root_section = config->getSection ("mod_gst");
	    MConfig::Section::iter iter (*root_section);
	    while (!root_section->iter_done (iter)) {
		MConfig::SectionEntry * const section_entry = root_section->iter_next (iter);
		switch (section_entry->getType()) {
		    case MConfig::SectionEntry::Type_Section: {
			MConfig::Section * const subsection = static_cast <MConfig::Section*> (section_entry);
			logD_ (_func, section_entry->getName(), ", section");
		    } break;
		    default:
			logD_ (_func, section_entry->getName(), ", not a section");
		}
	    }
	} while (0);
#endif

	MConfig::Section * const chains_section = config->getSection ("mod_gst/chains");
	if (chains_section) {
	    MConfig::Section::iter iter (*chains_section);
	    while (!chains_section->iter_done (iter)) {
		MConfig::SectionEntry * const section_entry = chains_section->iter_next (iter);
		if (section_entry->getType() == MConfig::SectionEntry::Type_Option) {
		    MConfig::Option * const chain_option = static_cast <MConfig::Option*> (section_entry);
		    if (!chain_option->getValue())
			continue;

		    VideoStream::VideoCodecId video_codec = VideoStream::VideoCodecId::SorensonH263;
		    {
			MConfig::Option::iter iter (*chain_option);
			assert (!chain_option->iter_done (iter));
			chain_option->iter_next (iter);
			if (!chain_option->iter_done (iter)) {
			    ConstMemory const codec_str = chain_option->iter_next (iter)->mem();
			    if (equal (codec_str, "flv")) {
				logD_ (_func, "codec: ", codec_str);
				video_codec = VideoStream::VideoCodecId::SorensonH263;
			    } else
			    if (equal (codec_str, "flashsv")) {
				logD_ (_func, "codec: ", codec_str);
				video_codec = VideoStream::VideoCodecId::ScreenVideo;
			    } else {
				logW_ (_func, "unknown codec \"", codec_str, "\"specified, ignoring chain");
				continue;
			    }
			}
		    }

		    ConstMemory const stream_name = chain_option->getName();
		    ConstMemory const chain_spec = chain_option->getValue()->mem();

		    createStream (stream_name, chain_spec, video_codec, true /* chain */);
		}
	    }
	} else {
	    logI_ ("No custom chains specified "
		   "(\"mod_gst/chains\" config section is missing).");
	}
    }

    return Result::Success;
}

Result
MomentGstModule::createPipelineForChainSpec (Stream * const stream)
{
    assert (stream->is_chain);

    GstElement *chain_el = NULL;
    GstElement *video_el = NULL;
    GstElement *audio_el = NULL;

  {
    GError *error = NULL;
    chain_el = gst_parse_launch (stream->stream_spec->cstr (), &error);
    if (!chain_el) {
	if (error) {
	    logE_ (_func, "gst_parse_launch() failed: ", error->code,
		   " ", error->message);
	} else {
	    logE_ (_func, "gst_parse_launch() failed");
	}

	goto _failure;
    }

    {
	audio_el = gst_bin_get_by_name (GST_BIN (chain_el), "audio");
	if (audio_el) {
	    GstPad * const pad = gst_element_get_static_pad (audio_el, "sink");
	    if (!pad) {
		logE_ (_func, "element called \"audio\" doesn't have a \"sink\" "
		       "pad. Chain spec: ", stream->stream_spec);
		goto _failure;
	    }

	    stream->got_audio = true;

#if 0
// At this moment, the caps are not negotiated yet.
	    {
	      // TEST
		GstCaps * const caps = gst_pad_get_negotiated_caps (pad);
		{
		    gchar * const str = gst_caps_to_string (caps);
		    logD_ (_func, "audio caps: ", str);
		    g_free (str);
		}
		gst_caps_unref (caps);
	    }
#endif

	    stream->audio_probe_id = gst_pad_add_buffer_probe (
		    pad, G_CALLBACK (audioDataCb), stream);

	    gst_object_unref (pad);

	    gst_object_unref (audio_el);
	    audio_el = NULL;
	} else {
	    logW_ (_func, "chain \"", stream->stream_name, "\" does not contain "
		   "an element named \"audio\". There'll be no audio "
		   "for the stream. Chain spec: ", stream->stream_spec);
	}
    }

    {
	video_el = gst_bin_get_by_name (GST_BIN (chain_el), "video");
	if (video_el) {
	    GstPad * const pad = gst_element_get_static_pad (video_el, "sink");
	    if (!pad) {
		logE_ (_func, "element called \"video\" doesn't have a \"sink\" "
		       "pad. Chain spec: ", stream->stream_spec);
		goto _failure;
	    }

	    stream->got_video = true;

	    stream->video_probe_id = gst_pad_add_buffer_probe (
		    pad, G_CALLBACK (videoDataCb), stream);

	    gst_object_unref (pad);

	    gst_object_unref (video_el);
	    video_el = NULL;
	} else {
	    logW_ (_func, "chain \"", stream->stream_name, "\" does not contain "
		   "an element named \"video\". There'll be no video "
		   "for the stream. Chain spec: ", stream->stream_spec);
	}
    }

    logD_ (_func, "chain \"", stream->stream_name, "\" created");

    stream->playbin = chain_el;

    gst_element_set_state (chain_el, GST_STATE_PLAYING);

    return Result::Success;
  }

_failure:
    if (chain_el)
	gst_object_unref (chain_el);

    if (video_el)
	gst_object_unref (video_el);

    if (audio_el)
	gst_object_unref (audio_el);

    return Result::Failure;
}

Result
MomentGstModule::createPipelineForUri (Stream * const stream)
{
    assert (!stream->is_chain);

    GstElement *playbin           = NULL,
	       *audio_encoder_bin = NULL,
	       *video_encoder_bin = NULL,
	       *audio_encoder     = NULL,
	       *video_encoder     = NULL,
	       *fakeaudiosink     = NULL,
	       *fakevideosink     = NULL,
	       *videoscale        = NULL,
	       *audio_capsfilter  = NULL,
	       *video_capsfilter  = NULL;

  {
    playbin = gst_element_factory_make ("playbin2", NULL);
    if (!playbin) {
	logE_ (_func, "gst_element_factory_make() failed (playbin2)");
	goto _failure;
    }

    {
	GstBus * const bus = gst_element_get_bus (playbin);
	assert (bus);
	gst_bus_add_watch (bus, busCallCb, stream);
	gst_object_unref (bus);
    }

    fakeaudiosink = gst_element_factory_make ("fakesink", NULL);
    if (!fakeaudiosink) {
	logE_ (_func, "gst_element_factory_make() failed (fakeaudiosink)");
	goto _failure;
    }
    g_object_set (G_OBJECT (fakeaudiosink),
		  "sync", TRUE,
		  "signal-handoffs", TRUE, NULL);

    fakevideosink = gst_element_factory_make ("fakesink", NULL);
    if (!fakevideosink) {
	logE_ (_func, "gst_element_factory_make() failed (fakevideosink)");
	goto _failure;
    }
    g_object_set (G_OBJECT (fakevideosink),
		  "sync", TRUE,
		  "signal-handoffs", TRUE, NULL);

#if 0
// Deprecated in favor of "handoff" signal.
    {
	GstPad * const pad = gst_element_get_static_pad (fakeaudiosink, "sink");
	stream->audio_probe_id = gst_pad_add_buffer_probe (
		pad, G_CALLBACK (audioDataCb), stream);
	gst_object_unref (pad);
    }

    {
	GstPad * const pad = gst_element_get_static_pad (fakevideosink, "sink");

	stream->video_probe_id = gst_pad_add_buffer_probe (
		pad, G_CALLBACK (videoDataCb), stream);

#if 0
	GstCaps * const caps = gst_caps_new_simple ("video/x-raw-yuv", "width", G_TYPE_INT, 64, "height", G_TYPE_INT, 48, NULL);
	gst_pad_set_caps (pad, caps);
	gst_caps_unref (caps);
#endif

	gst_object_unref (pad);
    }
#endif

    g_signal_connect (fakeaudiosink, "handoff", G_CALLBACK (handoffAudioDataCb), stream);
    g_signal_connect (fakevideosink, "handoff", G_CALLBACK (handoffVideoDataCb), stream);

    {
      // Audio transcoder.

	audio_encoder_bin = gst_bin_new (NULL);
	if (!audio_encoder_bin) {
	    logE_ (_func, "gst_bin_new() failed (audio_encoder_bin)");
	    goto _failure;
	}

	audio_capsfilter = gst_element_factory_make ("capsfilter", NULL);
	if (!audio_capsfilter) {
	    logE_ (_func, "gst_element_factory_make() failed (audio capsfilter)");
	    goto _failure;
	}
	g_object_set (GST_OBJECT (audio_capsfilter), "caps",
		      gst_caps_new_simple ("audio/x-raw-int",
					   "rate", G_TYPE_INT, 16000,
					   "channels", G_TYPE_INT, 1,
					   NULL), NULL);

//	audio_encoder = gst_element_factory_make ("ffenc_adpcm_swf", NULL);
	audio_encoder = gst_element_factory_make ("speexenc", NULL);
	if (!audio_encoder) {
	    logE_ (_func, "gst_element_factory_make() failed (speexenc)");
	    goto _failure;
	}
//	g_object_set (audio_encoder, "quality", 10, NULL);

	gst_bin_add_many (GST_BIN (audio_encoder_bin), audio_capsfilter, audio_encoder, fakeaudiosink, NULL);
	gst_element_link_many (audio_capsfilter, audio_encoder, fakeaudiosink, NULL);

	{
	    GstPad * const pad = gst_element_get_static_pad (audio_capsfilter, "sink");
	    gst_element_add_pad (audio_encoder_bin, gst_ghost_pad_new ("sink", pad));
	    gst_object_unref (pad);
	}

	audio_encoder = NULL;
	fakeaudiosink = NULL;
    }

    {
      // Transcoder to Sorenson h.263.

	video_encoder_bin = gst_bin_new (NULL);
	if (!video_encoder_bin) {
	    logE_ (_func, "gst_bin_new() failed (video_encoder_bin)");
	    goto _failure;
	}

	videoscale = gst_element_factory_make ("videoscale", NULL);
	if (!videoscale) {
	    logE_ (_func, "gst_element_factory_make() failed (videoscale)");
	    goto _failure;
	}
	g_object_set (G_OBJECT (videoscale), "add-borders", TRUE, NULL);

	video_capsfilter = gst_element_factory_make ("capsfilter", NULL);
	if (!video_capsfilter) {
	    logE_ (_func, "gst_element_factory_make() failed (video capsfilter)");
	    goto _failure;
	}

	if (default_width && default_height) {
	    g_object_set (G_OBJECT (video_capsfilter), "caps",
			  gst_caps_new_simple ("video/x-raw-yuv",
					       "width",  G_TYPE_INT, (int) default_width,
					       "height", G_TYPE_INT, (int) default_height,
					       "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
					       NULL), NULL);
	} else
	if (default_width) {
	    g_object_set (G_OBJECT (video_capsfilter), "caps",
			  gst_caps_new_simple ("video/x-raw-yuv",
					       "width",  G_TYPE_INT, (int) default_width,
					       "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
					       NULL), NULL);
	} else
	if (default_height) {
	    g_object_set (G_OBJECT (video_capsfilter), "caps",
			  gst_caps_new_simple ("video/x-raw-yuv",
					       "height", G_TYPE_INT, (int) default_height,
					       "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
					       NULL), NULL);
	}

	video_encoder = gst_element_factory_make ("ffenc_flv", NULL);
	if (!video_encoder) {
	    logE_ (_func, "gst_element_factory_make() failed (ffenc_flv)");
	    goto _failure;
	}
	stream->encoder = video_encoder;
	// TODO Config parameter for bitrate.
//	g_object_set (G_OBJECT (video_encoder), "bitrate", 100000, NULL);
	g_object_set (G_OBJECT (video_encoder), "bitrate", (gulong) default_bitrate, NULL);

#if 0
	{
//	    GstPad * const pad = gst_element_get_static_pad (video_encoder, "sink");
	    GstPad * const pad = gst_element_get_static_pad (videoscale, "src");
	    GstCaps * const caps = gst_caps_new_simple ("video/x-raw-yuv", "width", G_TYPE_INT, 64, "height", G_TYPE_INT, 48, NULL);
	    gst_pad_set_caps (pad, caps);
	    gst_caps_unref (caps);
	    gst_object_unref (pad);
	}
#endif

#if 0
	{
	    GstPad * const pad = gst_element_get_static_pad (video_encoder, "sink");
//	    GstCaps * const caps = gst_caps_new_simple ("video/x-raw-yuv", "width", G_TYPE_INT, 32, NULL);
	    GstCaps * const caps = gst_caps_new_simple ("video/x-raw-yuv", "width", G_TYPE_INT, 64, "height", G_TYPE_INT, 48, NULL);
	    gst_pad_set_caps (pad, caps);
	    gst_caps_unref (caps);
	    gst_object_unref (pad);
	}
#endif

	gst_bin_add_many (GST_BIN (video_encoder_bin), videoscale, video_capsfilter, video_encoder, fakevideosink, NULL);
	gst_element_link_many (videoscale, video_capsfilter, video_encoder, fakevideosink, NULL);

	{
	    GstPad * const pad = gst_element_get_static_pad (videoscale, "sink");
	    gst_element_add_pad (video_encoder_bin, gst_ghost_pad_new ("sink", pad));
	    gst_object_unref (pad);
	}

	// 'videoscale', 'video_encoder' and 'fakevideosink' belong to
	// 'video_encoder_bin' now.
	videoscale    = NULL;
	video_encoder = NULL;
	fakevideosink = NULL;
    }

    g_object_set (G_OBJECT (playbin), "audio-sink", audio_encoder_bin, NULL);
    audio_encoder_bin = NULL;

    g_object_set (G_OBJECT (playbin), "video-sink", video_encoder_bin, NULL);
    video_encoder_bin = NULL;

    g_object_set (G_OBJECT (playbin), "uri", stream->stream_spec->cstr(), NULL);
    gst_element_set_state (playbin, GST_STATE_PLAYING);
  }

    stream->playbin = playbin;
    return Result::Success;

_failure:
    if (playbin)
	gst_object_unref (GST_OBJECT (playbin));
    if (audio_encoder_bin)
	gst_object_unref (GST_OBJECT (audio_encoder_bin));
    if (video_encoder_bin)
	gst_object_unref (GST_OBJECT (video_encoder_bin));
    if (audio_encoder)
	gst_object_unref (GST_OBJECT (audio_encoder));
    if (video_encoder)
	gst_object_unref (GST_OBJECT (video_encoder));
    if (fakeaudiosink)
	gst_object_unref (GST_OBJECT (fakeaudiosink));
    if (fakevideosink)
	gst_object_unref (GST_OBJECT (fakevideosink));
    if (videoscale)
	gst_object_unref (GST_OBJECT (videoscale));
    if (audio_capsfilter)
	gst_object_unref (GST_OBJECT (audio_capsfilter));
    if (video_capsfilter)
	gst_object_unref (GST_OBJECT (video_capsfilter));

    return Result::Failure;
}

mt_mutex (stream->stream_mutex) Result
MomentGstModule::createPipeline (Stream * const stream)
{
    stream->audio_codec_id = VideoStream::AudioCodecId::Unknown;
    stream->first_audio_frame = true;
    // The first two buffers for Speex are headers. They do appear to contain
    // audio data and their timestamps look random (very large).
    stream->audio_skip_counter = 2;

    stream->video_codec_id = VideoStream::VideoCodecId::Unknown;
    stream->first_video_frame = true;

    if (stream->is_chain)
	return createPipelineForChainSpec (stream);

    return createPipelineForUri (stream);
}

mt_mutex (stream->stream_mutex) void
MomentGstModule::reportMetaData (Stream * const stream)
{
    logD_ (_func_);

    if (stream->metadata_reported)
	return;
    stream->metadata_reported = true;

    // TODO Keep page_pool pointer in the Stream object. This will make aquiring
    // a reference to MomentGstModule unnecessary.
    CodeRef self_ref;
    if (stream->weak_gst_module.isValid()) {
	self_ref = stream->weak_gst_module;
	if (!self_ref)
	    return;
    }
    MomentGstModule * const self = stream->unsafe_gst_module;

    if (!self->send_metadata)
	return;

    VideoStream::VideoMessage msg;
    if (!RtmpServer::encodeMetaData (&stream->metadata,
				     self->page_pool,
				     &msg))
    {
	logE_ (_func, "encodeMetaData() failed");
	return;
    }

    logD_ (_func, "Firing video message");
    stream->video_stream->fireVideoMessage (&msg);

    self->page_pool->msgUnref (msg.page_list.first);
}

void
MomentGstModule::doAudioData (GstBuffer * const buffer,
			      Stream    * const stream)
{
//    logD_ (_func, "stream 0x", fmt_hex, (UintPtr) stream, ", "
//	   "timestamp 0x", fmt_hex, GST_BUFFER_TIMESTAMP (buffer));

    VideoStream::AudioCodecId audio_codec_id = VideoStream::AudioCodecId::Unknown;

    stream->stream_mutex.lock ();
    stream->last_frame_time = getTime ();

    if (stream->prv_audio_timestamp >= GST_BUFFER_TIMESTAMP (buffer)) {
	logD_ (_func, "backwards timestamp: prv 0x", fmt_hex, stream->prv_audio_timestamp,
	       ", cur 0x", fmt_hex, GST_BUFFER_TIMESTAMP (buffer)); 
    }
    stream->prv_audio_timestamp = GST_BUFFER_TIMESTAMP (buffer);

    GstBuffer *aac_codec_data_buffer = NULL;
    if (stream->first_audio_frame) {
	GstCaps * const caps = gst_buffer_get_caps (buffer);
	{
	    gchar * const str = gst_caps_to_string (caps);
	    logD_ (_func, "caps: ", str);
	    g_free (str);
	}

	GstStructure * const structure = gst_caps_get_structure (caps, 0);
	gchar const * structure_name = gst_structure_get_name (structure);
	logD_ (_func, "structure name: ", gst_structure_get_name (structure));

	ConstMemory const structure_name_mem (structure_name, strlen (structure_name));

	gint channels;
	gint rate;
	if (!gst_structure_get_int (structure, "channels", &channels))
	    channels = 1;
	if (!gst_structure_get_int (structure, "rate", &rate))
	    rate = 44100;

	if (equal (structure_name_mem, "audio/mpeg")) {
	    gint mpegversion;
	    gint layer;

	    if (!gst_structure_get_int (structure, "mpegversion", &mpegversion))
		mpegversion = 1;
	    if (!gst_structure_get_int (structure, "layer", &layer))
		layer = 3;

	    if (mpegversion == 1 && layer == 3) {
	      // MP3
		stream->audio_codec_id = VideoStream::AudioCodecId::MP3;
		stream->audio_hdr = 0x22; // MP3, _ kHz, 16-bit samples, mono

		switch (rate) {
		    case 8000:
			stream->audio_hdr &= 0x0f;
			stream->audio_hdr |= 0xe4; // MP3 8 kHz, 11 kHz
			break;
		    case 11025:
			stream->audio_hdr |= 0x4; // 11 kHz
			break;
		    case 22050:
			stream->audio_hdr |= 0x8; // 22 kHz
			break;
		    case 44100:
			stream->audio_hdr |= 0xc; // 44 kHz
			break;
		    default:
			logW_ (_func, "Unsupported bitrate: ", rate);
			stream->audio_hdr |= 0xc; // 44 kHz
		}
	    } else {
	      // AAC
		stream->audio_codec_id = VideoStream::AudioCodecId::AAC;
		// TODO Correct header based on caps.
	        stream->audio_hdr = 0xae; // AAC, 44 kHz, 16-bit samples, mono

		do {
		  // Processing AacSequenceHeader.

		    GValue const * const val = gst_structure_get_value (structure, "codec_data");
		    if (!val) {
			logW_ (_func, "Codec data not found");
			break;
		    }

		    if (!GST_VALUE_HOLDS_BUFFER (val)) {
			logW_ (_func, "codec_data doesn't hold a buffer");
			break;
		    }

		    aac_codec_data_buffer = gst_value_get_buffer (val);
		    logD_ (_func, "aac_codec_data_buffer: 0x", fmt_hex, (UintPtr) aac_codec_data_buffer);
		} while (0);
	    }
	} else
	if (equal (structure_name_mem, "audio/x-speex")) {
	  // Speex
	    stream->audio_codec_id = VideoStream::AudioCodecId::Speex;
	    stream->audio_hdr = 0xb6; // Speex, 11 kHz, 16-bit samples, mono
	} else
	if (equal (structure_name_mem, "audio/x-nellymoser")) {
	  // Nellymoser
	    stream->audio_codec_id = VideoStream::AudioCodecId::Nellymoser;
	    stream->audio_hdr = 0x6e; // Nellymoser, 44 kHz, 16-bit samples, mono
	} else
	if (equal (structure_name_mem, "audio/x-adpcm")) {
	  // ADPCM
	    stream->audio_codec_id = VideoStream::AudioCodecId::ADPCM;
	    stream->audio_hdr = 0x1e; // ADPCM, 44 kHz, 16-bit samples, mono
	} else
	if (equal (structure_name_mem, "audio/x-raw-int")) {
	  // Linear PCM, little endian
	    stream->audio_codec_id = VideoStream::AudioCodecId::LinearPcmLittleEndian;
	    stream->audio_hdr = 0x3e; // Linear PCM (little endian), 44 kHz, 16-bit samples, mono
	} else
	if (equal (structure_name_mem, "audio/x-alaw")) {
	  // G.711 A-law logarithmic PCM
	    stream->audio_codec_id = VideoStream::AudioCodecId::G711ALaw;
	    stream->audio_hdr = 0x7e; // G.711 A-law, 44 kHz, 16-bit samples, mono
	} else
	if (equal (structure_name_mem, "audio/x-mulaw")) {
	  // G.711 mu-law logarithmic PCM
	    stream->audio_codec_id = VideoStream::AudioCodecId::G711MuLaw;
	    stream->audio_hdr = 0x8e; // G.711 mu-law, 44 kHz, 16-bit samples, mono
	}

	if (channels > 1) {
	    logD_ (_func, "stereo");
	    stream->audio_hdr |= 1; // stereo
	}

	stream->metadata.audio_sample_rate = (Uint32) rate;
	stream->metadata.got_flags |= RtmpServer::MetaData::AudioSampleRate;

	stream->metadata.audio_sample_size = 16;
	stream->metadata.got_flags |= RtmpServer::MetaData::AudioSampleSize;

	stream->metadata.num_channels = (Uint32) channels;
	stream->metadata.got_flags |= RtmpServer::MetaData::NumChannels;

	gst_caps_unref (caps);
    }

    audio_codec_id = stream->audio_codec_id;

    if (stream->first_audio_frame) {
	stream->first_audio_frame = false;

	if (!stream->got_video || !stream->first_video_frame) {
	  // There's no video or we've got the first video frame already.
	    reportMetaData (stream);
	    stream->metadata_reported_cond.signal ();
	} else {
	  // Waiting for the first video frame.
	    while (stream->got_video && stream->first_video_frame)
		stream->metadata_reported_cond.wait (stream->stream_mutex);
	}
    }

    if (audio_codec_id == VideoStream::AudioCodecId::Speex) {
	// The first two buffers for Speex are headers. They do appear to contain
	// audio data and their timestamps look random (very large).
	if (stream->audio_skip_counter > 0) {
	    --stream->audio_skip_counter;
//	    logD_ (_func, "skipping initial audio frame, ", stream->audio_skip_counter, " left");
	    stream->stream_mutex.unlock ();
	    return;
	}
    }
    Ref<VideoStream> const video_stream = stream->video_stream;

    Byte const tmp_audio_hdr = stream->audio_hdr;
//    logD_ (_func, "audio_hdr: ", fmt_hex, tmp_audio_hdr);

    stream->stream_mutex.unlock ();

    if (!video_stream)
	return;

    if (audio_codec_id == VideoStream::AudioCodecId::Unknown) {
	logD_ (_func, "unknown codec id, dropping audio frame");
	return;
    }

    // TODO Keep page_pool pointer in the Stream object. This will make aquiring
    // a reference to MomentGstModule unnecessary.
    CodeRef self_ref;
    if (stream->weak_gst_module.isValid()) {
	self_ref = stream->weak_gst_module;
	if (!self_ref)
	    return;
    }
    MomentGstModule * const self = stream->unsafe_gst_module;

    if (aac_codec_data_buffer) {
      // Reporting AAC codec data if needed.

	Size msg_len = 0;

	PagePool::PageListHead page_list;
	RtmpConnection::PrechunkContext prechunk_ctx;

	Uint64 const timestamp = (Uint64) (GST_BUFFER_TIMESTAMP (aac_codec_data_buffer) / 1000000);
	Byte aac_audio_hdr [2] = { tmp_audio_hdr, 0 };

	RtmpConnection::fillPrechunkedPages (&prechunk_ctx,
					     ConstMemory::forObject (aac_audio_hdr),
					     self->page_pool,
					     &page_list,
					     RtmpConnection::DefaultAudioChunkStreamId,
					     timestamp,
					     true /* first_chunk */);
	msg_len += sizeof (aac_audio_hdr);

	logD_ (_func, "AAC SEQUENCE HEADER");
	hexdump (logs, ConstMemory (GST_BUFFER_DATA (aac_codec_data_buffer), GST_BUFFER_SIZE (aac_codec_data_buffer)));

	RtmpConnection::fillPrechunkedPages (&prechunk_ctx,
					     ConstMemory (GST_BUFFER_DATA (aac_codec_data_buffer),
							  GST_BUFFER_SIZE (aac_codec_data_buffer)),
					     self->page_pool,
					     &page_list,
					     RtmpConnection::DefaultAudioChunkStreamId,
					     timestamp,
					     false /* first_chunk */);
	msg_len += GST_BUFFER_SIZE (aac_codec_data_buffer);

	VideoStream::AudioMessage msg;
	msg.timestamp = timestamp;
	msg.prechunk_size = RtmpConnection::PrechunkSize;
	msg.frame_type = VideoStream::AudioFrameType::AacSequenceHeader;
	msg.codec_id = audio_codec_id;

	msg.page_pool = self->page_pool;
	msg.page_list = page_list;
	msg.msg_len = msg_len;
	msg.msg_offset = 0;

	video_stream->fireAudioMessage (&msg);

	self->page_pool->msgUnref (page_list.first);
    }

    Size msg_len = 0;

    Uint64 const timestamp = (Uint64) (GST_BUFFER_TIMESTAMP (buffer) / 1000000);
//    logD_ (_func, "timestamp: 0x", fmt_hex, timestamp, ", size: ", fmt_def, GST_BUFFER_SIZE (buffer));
//    logD_ (_func, "audio_codec_id: ", audio_codec_id);

    Byte gen_audio_hdr [2];
    Size gen_audio_hdr_len = 1;
    gen_audio_hdr [0] = tmp_audio_hdr;
    if (audio_codec_id == VideoStream::AudioCodecId::AAC) {
	gen_audio_hdr [1] = 1;
	gen_audio_hdr_len = 2;
    }

    PagePool::PageListHead page_list;
    RtmpConnection::PrechunkContext prechunk_ctx;

    // Non-prechunked variant
    // self->page_pool->getFillPages (&page_list, ConstMemory (gen_audio_hdr, gen_audio_hdr_len));
    RtmpConnection::fillPrechunkedPages (&prechunk_ctx,
					 ConstMemory (gen_audio_hdr, gen_audio_hdr_len),
					 self->page_pool,
					 &page_list,
					 RtmpConnection::DefaultAudioChunkStreamId,
					 timestamp,
					 true /* first_chunk */);
    msg_len += gen_audio_hdr_len;

    // Non-prechunked variant
    // self->page_pool->getFillPages (&page_list, ConstMemory (GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer)));
    RtmpConnection::fillPrechunkedPages (&prechunk_ctx,
					 ConstMemory (GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer)),
					 self->page_pool,
					 &page_list,
					 RtmpConnection::DefaultAudioChunkStreamId,
					 timestamp,
					 false /* first_chunk */);
    msg_len += GST_BUFFER_SIZE (buffer);

//    hexdump (errs, ConstMemory (GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer)));

    VideoStream::AudioMessage msg;
    msg.timestamp = timestamp;
    msg.prechunk_size = RtmpConnection::PrechunkSize;
    msg.frame_type = VideoStream::AudioFrameType::RawData;
    msg.codec_id = audio_codec_id;

    msg.page_pool = self->page_pool;
    msg.page_list = page_list;
    msg.msg_len = msg_len;
    msg.msg_offset = 0;

//    logD_ (_func, fmt_hex, msg.timestamp);

    video_stream->fireAudioMessage (&msg);

    self->page_pool->msgUnref (page_list.first);

    // TEST
    MomentServer::getInstance()->getServerApp()->getActivePollGroup()->trigger();
}

gboolean
MomentGstModule::audioDataCb (GstPad    * const /* pad */,
			      GstBuffer * const buffer,
			      gpointer    const _stream)
{
    Stream * const stream = static_cast <Stream*> (_stream);
    doAudioData (buffer, stream);
    return TRUE;
}

void
MomentGstModule::handoffAudioDataCb (GstElement * const /* element */,
				     GstBuffer  * const buffer,
				     GstPad     * const /* pad */,
				     gpointer     const _stream)
{
    Stream * const stream = static_cast <Stream*> (_stream);
    doAudioData (buffer, stream);
}

void
MomentGstModule::doVideoData (GstBuffer * const buffer,
			      Stream    * const stream)
{
//    logD_ (_func, "stream 0x", fmt_hex, (UintPtr) stream, ", "
//	   "timestamp 0x", fmt_hex, GST_BUFFER_TIMESTAMP (buffer));

    VideoStream::VideoCodecId video_codec_id = VideoStream::VideoCodecId::Unknown;

    // TODO Update current time efficiently.
    updateTime ();

    stream->stream_mutex.lock ();

    stream->last_frame_time = getTime ();

    GstBuffer *avc_codec_data_buffer = NULL;
    if (stream->first_video_frame) {
	stream->first_video_frame = false;

	GstCaps * const caps = gst_buffer_get_caps (buffer);
	{
	    gchar * const str = gst_caps_to_string (caps);
	    logD_ (_func, "caps: ", str);
	    g_free (str);
	}

	GstStructure * const st = gst_caps_get_structure (caps, 0);
	gchar const * st_name = gst_structure_get_name (st);
	logD_ (_func, "st_name: ", gst_structure_get_name (st));
	ConstMemory const st_name_mem (st_name, strlen (st_name));

	if (equal (st_name_mem, "video/x-flash-video")) {
	   stream->video_codec_id = VideoStream::VideoCodecId::SorensonH263;
	   stream->video_hdr = 0x02; // Sorenson H.263
	} else
	if (equal (st_name_mem, "video/x-h264")) {
	   stream->video_codec_id = VideoStream::VideoCodecId::AVC;
	   stream->video_hdr = 0x07; // AVC

	   do {
	     // Processing AvcSeqienceHeader

	       GValue const * const val = gst_structure_get_value (st, "codec_data");
	       if (!val) {
		   logW_ (_func, "Codec data not found");
		   break;
	       }

	       if (!GST_VALUE_HOLDS_BUFFER (val)) {
		   logW_ (_func, "codec_data doesn't hold a buffer");
		   break;
	       }

	       avc_codec_data_buffer = gst_value_get_buffer (val);
	       logD_ (_func, "avc_codec_data_buffer: 0x", fmt_hex, (UintPtr) avc_codec_data_buffer);
	   } while (0);
	} else
	if (equal (st_name_mem, "video/x-vp6")) {
	   stream->video_codec_id = VideoStream::VideoCodecId::VP6;
	   stream->video_hdr = 0x04; // On2 VP6
	} else
	if (equal (st_name_mem, "video/x-flash-screen")) {
	   stream->video_codec_id = VideoStream::VideoCodecId::ScreenVideo;
	   stream->video_hdr = 0x03; // Screen video
	}

	if (!stream->got_audio || !stream->first_audio_frame) {
	  // There's no video or we've got the first video frame already.
	    reportMetaData (stream);
	    stream->metadata_reported_cond.signal ();
	} else {
	  // Waiting for the first audio frame.
	    while (stream->got_audio && stream->first_audio_frame)
		stream->metadata_reported_cond.wait (stream->stream_mutex);
	}
    }

    video_codec_id = stream->video_codec_id;
    Ref<VideoStream> const video_stream = stream->video_stream;
    Byte const tmp_video_hdr = stream->video_hdr;

    stream->stream_mutex.unlock ();

    if (!video_stream)
	return;

    CodeRef self_ref;
    if (stream->weak_gst_module.isValid()) {
	self_ref = stream->weak_gst_module;
	if (!self_ref)
	    return;
    }
    MomentGstModule * const self = stream->unsafe_gst_module;

    if (avc_codec_data_buffer) {
      // Reporting AVC codec data if needed.

	Size msg_len = 0;

	PagePool::PageListHead page_list;
	RtmpConnection::PrechunkContext prechunk_ctx;

	Uint64 const timestamp = (Uint64) (GST_BUFFER_TIMESTAMP (avc_codec_data_buffer) / 1000000);
	Byte avc_video_hdr [5] = { 0x17, 0, 0, 0, 0 }; // AVC, seekable frame;
						       // AVC sequence header;
						       // Composition time offset = 0.

	RtmpConnection::fillPrechunkedPages (&prechunk_ctx,
					     ConstMemory::forObject (avc_video_hdr),
					     self->page_pool,
					     &page_list,
					     RtmpConnection::DefaultVideoChunkStreamId,
					     timestamp,
					     true /* first_chunk */);
	msg_len += sizeof (avc_video_hdr);

	logD_ (_func, "AVC SEQUENCE HEADER");
	hexdump (logs, ConstMemory (GST_BUFFER_DATA (avc_codec_data_buffer), GST_BUFFER_SIZE (avc_codec_data_buffer)));

	RtmpConnection::fillPrechunkedPages (&prechunk_ctx,
					     ConstMemory (GST_BUFFER_DATA (avc_codec_data_buffer),
							  GST_BUFFER_SIZE (avc_codec_data_buffer)),
					     self->page_pool,
					     &page_list,
					     RtmpConnection::DefaultVideoChunkStreamId,
					     timestamp,
					     false /* first_chunk */);
	msg_len += GST_BUFFER_SIZE (avc_codec_data_buffer);

	VideoStream::VideoMessage msg;
	msg.timestamp = timestamp;
	msg.prechunk_size = RtmpConnection::PrechunkSize;
	msg.frame_type = VideoStream::VideoFrameType::AvcSequenceHeader;
	msg.codec_id = video_codec_id;

	msg.page_pool = self->page_pool;
	msg.page_list = page_list;
	msg.msg_len = msg_len;
	msg.msg_offset = 0;

	video_stream->fireVideoMessage (&msg);

	self->page_pool->msgUnref (page_list.first);
    }

    Size msg_len = 0;

    Uint64 const timestamp = (Uint64) (GST_BUFFER_TIMESTAMP (buffer) / 1000000);
//    logD_ (_func, "timestamp: 0x", fmt_hex, timestamp);

    VideoStream::VideoMessage msg;
    msg.frame_type = VideoStream::VideoFrameType::InterFrame;
    msg.codec_id = video_codec_id;

    Byte gen_video_hdr [5];
    Size gen_video_hdr_len = 1;
    gen_video_hdr [0] = tmp_video_hdr;
    if (video_codec_id == VideoStream::VideoCodecId::AVC) {
	gen_video_hdr [1] = 1; // AVC NALU

	// Composition time offset
	gen_video_hdr [2] = 0;
	gen_video_hdr [3] = 0;
	gen_video_hdr [4] = 0;

	gen_video_hdr_len = 5;
    }

    bool is_keyframe = false;
#if 0
    // Keyframe detection by parsing message body for Sorenson H.263
    // See ffmpeg:h263.c for reference.
    if (video_codec_id == VideoStream::VideoCodecId::SorensonH263) {
	if (GST_BUFFER_SIZE (buffer) >= 5) {
	    Byte const format = ((GST_BUFFER_DATA (buffer) [3] & 0x03) << 1) |
				((GST_BUFFER_DATA (buffer) [4] & 0x80) >> 7);
	    size_t offset = 4;
	    switch (format) {
		case 0:
		    offset += 2;
		    break;
		case 1:
		    offset += 4;
		    break;
		default:
		    break;
	    }

	    if (GST_BUFFER_SIZE (buffer) > offset) {
		if (((GST_BUFFER_DATA (buffer) [offset] & 0x60) >> 4) == 0)
		    is_keyframe = true;
	    }
	}
    } else
#endif
    {
	is_keyframe = !GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
    }

    if (is_keyframe) {
	msg.frame_type = VideoStream::VideoFrameType::KeyFrame;
	gen_video_hdr [0] |= 0x10;
    } else {
      // TODO We do not make difference between inter frames and
      // disposable inter frames for Sorenson h.263 here.
	gen_video_hdr [0] |= 0x20;
    }

    PagePool::PageListHead page_list;
    RtmpConnection::PrechunkContext prechunk_ctx;
    // Non-prechunked variant
    // self->page_pool->getFillPages (&page_list, ConstMemory::forObject (video_hdr));
    RtmpConnection::fillPrechunkedPages (&prechunk_ctx,
					 ConstMemory (gen_video_hdr, gen_video_hdr_len),
					 self->page_pool,
					 &page_list,
					 RtmpConnection::DefaultVideoChunkStreamId,
					 timestamp,
					 true /* first_chunk */);
    msg_len += gen_video_hdr_len;

    // Non-prechunked variant
    // self->page_pool->getFillPages (&page_list, ConstMemory (GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer)));
    RtmpConnection::fillPrechunkedPages (&prechunk_ctx,
					 ConstMemory (GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer)),
					 self->page_pool,
					 &page_list,
					 RtmpConnection::DefaultVideoChunkStreamId,
					 timestamp,
					 false /* first_chunk */);
    msg_len += GST_BUFFER_SIZE (buffer);

//    hexdump (errs, ConstMemory (GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer)));

    msg.timestamp = timestamp;
    msg.prechunk_size = RtmpConnection::PrechunkSize;

    msg.page_pool = self->page_pool;
    msg.page_list = page_list;
    msg.msg_len = msg_len;
    msg.msg_offset = 0;

    video_stream->fireVideoMessage (&msg);

    self->page_pool->msgUnref (page_list.first);

    // TEST
    MomentServer::getInstance()->getServerApp()->getActivePollGroup()->trigger();
}

gboolean
MomentGstModule::videoDataCb (GstPad    * const /* pad */,
			      GstBuffer * const buffer,
			      gpointer    const _stream)
{
    Stream * const stream = static_cast <Stream*> (_stream);
    doVideoData (buffer, stream);
    return TRUE;
}

void
MomentGstModule::handoffVideoDataCb (GstElement * const /* element */,
				     GstBuffer  * const buffer,
				     GstPad     * const /* pad */,
				     gpointer     const _stream)
{
    Stream * const stream = static_cast <Stream*> (_stream);
    doVideoData (buffer, stream);
}

gboolean
MomentGstModule::busCallCb (GstBus     * const /* bus */,
			    GstMessage * const msg,
			    gpointer     const _stream)
{
    logD (bus, _func, gst_message_type_get_name (GST_MESSAGE_TYPE (msg)));

    Stream * const stream = static_cast <Stream*> (_stream);

    CodeRef self_ref;
    if (stream->weak_gst_module.isValid()) {
	self_ref = stream->weak_gst_module;
	if (!self_ref)
	    return TRUE;
    }
//    MomentGstModule * const self = stream->unsafe_gst_module;

    if (GST_MESSAGE_SRC (msg) == GST_OBJECT (stream->playbin)) {
	logD (bus, _func, "PIPELINE MESSAGE");
	logD (bus, _func, gst_message_type_get_name (GST_MESSAGE_TYPE (msg)));

	if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_STATE_CHANGED) {
	    GstState new_state,
		     pending_state;
	    gst_message_parse_state_changed (msg, NULL, &new_state, &pending_state);
	    if (pending_state == GST_STATE_VOID_PENDING
		&& new_state != GST_STATE_PLAYING)
	    {
		logD (bus, _func, "PIPELINE READY");
		gst_element_set_state (stream->playbin, GST_STATE_PLAYING);
	    }
	}
    }

    switch (GST_MESSAGE_TYPE (msg)) {
	case GST_MESSAGE_BUFFERING: {
	    gint percent = 0;
	    gst_message_parse_buffering (msg, &percent);
	    logD (bus, _func, "GST_MESSAGE_BUFFERING ", percent, "%");
	} break;
	case GST_MESSAGE_EOS: {
	    logE (bus, _func, "GST_MESSAGE_EOS");

	  // The stream will be restarted gracefully by noVideoTimerTick().
	  // We do not restart the stream here immediately to avoid triggering
	  // a fork bomb (e.g. when network is down).

#if 0
	    stream->stream_mutex.lock ();
	    self->restartStream (stream);
	    stream->stream_mutex.unlock ();
#endif
	} break;
	case GST_MESSAGE_ERROR: {
	    logE (bus, _func, "GST_MESSAGE_ERROR");

#if 0
	    stream->stream_mutex.lock ();
	    self->restartStream (stream);
	    stream->stream_mutex.unlock ();
#endif
	} break;
	default:
	  // No-op
	    ;
    }

    return TRUE;
}

MomentGstModule::MomentGstModule()
    : moment (NULL),
      timers (NULL),
      page_pool (NULL),
      send_metadata (true),
      default_width (0),
      default_height (0),
      default_bitrate (500000)
{
}

MomentGstModule::~MomentGstModule ()
{
  StateMutexLock l (&mutex);

    StreamList::iter iter (stream_list);
    while (!stream_list.iter_done (iter)) {
	Stream * const stream = stream_list.iter_next (iter);
	stream->stream_mutex.lock ();
	closeVideoStream (stream);
	stream->stream_mutex.unlock ();
	stream->unref ();
    }
}

} // namespace Moment

