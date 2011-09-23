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


using namespace M;
using namespace Moment;

namespace MomentGst {

void
MomentGstModule::createPlayback (ConstMemory const stream_name,
				 ConstMemory const playlist_filename,
				 bool        const recording,
				 ConstMemory const record_filename)
{
    Playback * const playback = new Playback;
    playback->weak_module = this;

    playback->stream_name = grab (new String (stream_name));
    playback->cur_item = NULL;
    playback->cur_stream_ticket = NULL;
    playback->cur_item_start_time = 0;

    {
	Ref<String> err_msg;
	if (!playback->playlist.parsePlaylistFile (playlist_filename, &err_msg)) {
	    logE_ (_func, "Could not parse playlist file \"", playlist_filename, "\":\n", err_msg);
	    delete playback;
	    return;
	}
    }

//    playItem (&playback->root_block);

    playback->timers = timers;

    {
	Ref<GstStream> const stream = grab (new GstStream);

	mutex.lock ();
	stream_list.append (stream);
	mutex.unlock ();
	stream->ref();

	stream->init (moment,
		      moment->getServerApp()->getMainThreadContext()->getDeferredProcessor(),
		      stream_name,
		      recording,
		      record_filename,
		      send_metadata,
		      default_width,
		      default_height,
		      default_bitrate);

	stream->setFrontend (CbDesc<GstStream::Frontend> (
		&gst_stream_frontend, playback /* cb_data */, playback /* coderef_container */));

//	stream->beginVideoStream (stream_spec, is_chain);

	playback->stream = stream;
    }

    playback->mutex.lock ();
    playback->playback_timer = timers->addTimer (playbackTimerTick,
						 playback /* cb_data */,
						 playback /* coderef_container */,
						 0 /* time_seconds */,
						 false /* periodical */);
    playback->mutex.unlock ();
}

mt_mutex (playback->mutex) void
MomentGstModule::advancePlayback (Playback * const playback)
{
    if (playback->playback_timer) {
	playback->timers->deleteTimer (playback->playback_timer);
	playback->playback_timer = NULL;
    }

    playback->stream->endVideoStream ();

#if 0
    // TEST
    if (playback->cur_item) {
	playback->mutex.unlock ();
	return;
    }
#endif

#if 0
    Ref<MomentGstModule> const module = playback->weak_module.getRef();
    if (!module) {
	playback->mutex.unlock ();
	return;
    }
#endif

    Time start_rel;
    Time seek;
    Time duration;
    bool duration_full;
    Playlist::Item * const item = playback->playlist.getNextItem (playback->cur_item,
								  getUnixtime(),
								  0 /* time_offset */,
								  &start_rel,
								  &seek,
								  &duration,
								  &duration_full);
    if (!item) {
	if (!playback->cur_item) {
	  // Empty playlist
	    logD_ (_func, "Empty playlist");
	    return;
	}

	playback->cur_item = NULL;
	playback->cur_stream_ticket = NULL;

	logD_ (_func, "Playlist end");

	// FIXME 0 timeout causes busy-looping
	playback->playback_timer = playback->timers->addTimer (playbackTimerTick,
							       playback /* cb_data */,
							       playback /* coderef_container */,
							       0 /* time_seconds */,
							       false /* periodical */);
	return;
    }

    playback->cur_item = item;
    playback->cur_stream_ticket = grab (new StreamTicket);

    logD_ (_func, "chain_spec: ", item->chain_spec);
    logD_ (_func, "start_rel: ", start_rel, ", seek: ", seek, ", "
	   "duration: ", duration, ", duration_full: ", (duration_full ? "true" : "false"));

    if (!duration_full) {
	logD_ (_func, "Setting playback timer to ", duration);
	playback->playback_timer = playback->timers->addTimer (playbackTimerTick,
							       playback /* cb_data */,
							       playback /* coderef_container */,
							       duration);
    }

    // TODO Minimum duration limit
    if (item->chain_spec && !item->chain_spec.isNull()) {
	playback->stream->beginVideoStream (item->chain_spec->mem(),
					    true /* is_chain */,
					    playback->cur_stream_ticket /* stream_ticket */,
					    playback->cur_stream_ticket /* stream_ticket_ref */,
					    seek);
    } else
    if (item->uri && !item->uri.isNull()) {
	playback->stream->beginVideoStream (item->uri->mem(),
					    false /* is_chain */,
					    playback->cur_stream_ticket /* stream_ticket */,
					    playback->cur_stream_ticket /* stream_ticket_ref */,
					    seek);
    } else {
	logW_ (_func, "No chain spec and no uri for playlist item");
    }
}

void
MomentGstModule::playbackTimerTick (void * const _playback)
{
    Playback * const playback = static_cast <Playback*> (_playback);

    logD_ (_func, "playback 0x", fmt_hex, (UintPtr) _playback, ", cur_item 0x", (UintPtr) playback->cur_item);

    playback->mutex.lock ();
// TODO FIXME Protect against concurrent Eos and playbackTimerTick
    advancePlayback (playback);
    playback->mutex.unlock ();
}

void
MomentGstModule::createStream (ConstMemory const stream_name,
			       ConstMemory const stream_spec,
			       bool        const is_chain,
			       bool        const recording,
			       ConstMemory const record_filename)
{
    Ref<GstStream> const stream = grab (new GstStream);

    mutex.lock ();
    stream_list.append (stream);
    mutex.unlock ();

    stream->init (moment,
		  moment->getServerApp()->getMainThreadContext()->getDeferredProcessor(),
		  stream_name,
		  recording,
		  record_filename,
		  send_metadata,
		  default_width,
		  default_height,
		  default_bitrate);

    stream->beginVideoStream (stream_spec, is_chain, NULL /* stream_ticket */, NULL /* stream_ticket_ref */);

    stream->ref();
}

GstStream::Frontend
MomentGstModule::gst_stream_frontend = {
    streamError,
    streamEos
};

void
MomentGstModule::streamError (void * const stream_ticket,
			      void * const _playback)
{
    logD_ (_func, "stream_ticket: 0x", fmt_hex, (UintPtr) stream_ticket);
}

void
MomentGstModule::streamEos (void * const stream_ticket,
			    void * const _playback)
{
    logD_ (_func, "stream_ticket: 0x", fmt_hex, (UintPtr) stream_ticket);

    Playback * const playback = static_cast <Playback*> (_playback);

    playback->mutex.lock ();
    if ((void*) playback->cur_stream_ticket == stream_ticket) {
	advancePlayback (playback);
    }
    playback->mutex.unlock ();
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

    parseSourcesConfigSection ();
    parseChainsConfigSection ();
    parseStreamsConfigSection ();

    return Result::Success;
}

void
MomentGstModule::parseSourcesConfigSection ()
{
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
	    ConstMemory const stream_uri = src_option->getValue()->mem();

	    logD_ (_func, "Stream name: ", stream_name, "; stream uri: ", stream_uri);

	    createStream (stream_name,
			  stream_uri,
			  false /* chain */,
			  false /* recording */,
			  ConstMemory() /* record_filename */);
	}
    }
}

void
MomentGstModule::parseChainsConfigSection ()
{
    MConfig::Config * const config = moment->getConfig();

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
    if (!chains_section) {
	logI_ ("No custom chains specified "
	       "(\"mod_gst/chains\" config section is missing).");
	return;
    }

    MConfig::Section::iter iter (*chains_section);
    while (!chains_section->iter_done (iter)) {
	MConfig::SectionEntry * const section_entry = chains_section->iter_next (iter);
	if (section_entry->getType() == MConfig::SectionEntry::Type_Option) {
	    MConfig::Option * const chain_option = static_cast <MConfig::Option*> (section_entry);
	    if (!chain_option->getValue())
		continue;

#if 0
// Unused
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
#endif

	    ConstMemory const stream_name = chain_option->getName();
	    ConstMemory const chain_spec = chain_option->getValue()->mem();

	    createStream (stream_name,
			  chain_spec,
			  true /* chain */,
			  false /* recording */,
			  ConstMemory() /* record_filename */);
	} else
	if (section_entry->getType() == MConfig::SectionEntry::Type_Section) {
	    MConfig::Section * const section = static_cast <MConfig::Section*> (section_entry);

	    ConstMemory stream_name;
	    bool got_stream_name = false;

	    ConstMemory chain_spec;
	    bool got_chain_spec = false;

	    ConstMemory record_path;
	    bool got_record_path = false;

	    {
		MConfig::Section::iter iter (*section);
		while (!section->iter_done (iter)) {
		    MConfig::SectionEntry * const section_entry = section->iter_next (iter);
		    if (section_entry->getType() != MConfig::SectionEntry::Type_Option)
			continue;

		    MConfig::Option * const option = static_cast <MConfig::Option*> (section_entry);
		    ConstMemory const opt_name = option->getName ();
		    MConfig::Value * const opt_val = option->getValue ();
		    if (equal (opt_name, "name")) {
			if (opt_val) {
			    stream_name = opt_val->mem();
			    got_stream_name = true;
			}
		    } else
		    if (equal (opt_name, "chain")) {
			if (opt_val) {
			    chain_spec = opt_val->mem();
			    got_chain_spec = true;
			}
		    } else
		    if (equal (opt_name, "record_path")) {
			if (opt_val) {
			    record_path = opt_val->mem();
			    got_record_path = true;
			}
		    } else {
			logW_ (_func, "Unknown chain option (\"chains\" section): ", opt_name);
		    }
		}
	    }

	    if (!got_stream_name) {
		logE_ (_func, "Stream name not specified (\"chains\" section)");
		continue;
	    }

	    if (!got_chain_spec) {
		logE_ (_func, "No chain specification for stream "
		       "\"", stream_name, "\" (\"chains\" section)");
		continue;
	    }

	    createStream (stream_name,
			  chain_spec,
			  true /* chain */,
			  got_record_path /* recording */,
			  record_path);
	}
    }
}

void
MomentGstModule::parseStreamsConfigSection ()
{
    MConfig::Config * const config = moment->getConfig();

    MConfig::Section * const streams_section = config->getSection ("mod_gst/streams");
    if (!streams_section)
	return;

    MConfig::Section::iter streams_iter (*streams_section);
    while (!streams_section->iter_done (streams_iter)) {
	MConfig::SectionEntry * const item_entry = streams_section->iter_next (streams_iter);
	if (item_entry->getType() == MConfig::SectionEntry::Type_Section) {
	    MConfig::Section* const item_section = static_cast <MConfig::Section*> (item_entry);

	    ConstMemory const stream_name = item_section->getName();
	    if (!stream_name.len())
		logW_ (_func, "Unnamed stream in section mod_gst/streams");

	    Ref<String> chain;
	    Ref<String> uri;
	    Ref<String> playlist;
	    {
		int num_set_opts = 0;

		{
		    MConfig::Option * const opt = item_section->getOption ("chain");
		    if (opt && opt->getValue()) {
			chain = opt->getValue()->getAsString();
			++num_set_opts;
		    }
		}

		{
		    MConfig::Option * const opt = item_section->getOption ("uri");
		    if (opt && opt->getValue()) {
			uri = opt->getValue()->getAsString();
			++num_set_opts;
		    }
		}

		{
		    MConfig::Option * const opt = item_section->getOption ("playlist");
		    if (opt && opt->getValue()) {
			playlist = opt->getValue()->getAsString();
			++num_set_opts;
		    }
		}

		if (num_set_opts > 1)
		    logW_ (_func, "Only one of uri/chain/playlist should be specified for stream \"", stream_name, "\"");
	    }

	    Ref<String> record_path;
	    {
		MConfig::Option * const opt = item_section->getOption ("record_path");
		if (opt && opt->getValue())
		    record_path = opt->getValue()->getAsString();
	    }

	    if (chain && !chain->isNull()) {
		createStream (stream_name,
			      chain->mem(),
			      true /* chain */,
			      record_path ? true : false /* recording */,
			      record_path ? record_path->mem() : ConstMemory());
	    } else
	    if (uri && !uri->isNull()) {
		createStream (stream_name,
			      uri->mem(),
			      false /* chain */,
			      record_path ? true : false /* recording */,
			      record_path ? record_path->mem() : ConstMemory());
	    } else
	    if (playlist && !playlist->isNull()) {
		logD_ (_func, "playlist: ", playlist);
		createPlayback (stream_name,
				playlist->mem(),
				record_path ? true : false /* recording */,
				record_path ? record_path->mem() : ConstMemory());
	    } else {
		logW_ (_func, "None of chain/uri/playlist specified for stream \"", stream_name, "\"");
	    }
	}
    }
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
	GstStream * const stream = stream_list.iter_next (iter);
	stream->release ();
	stream->unref ();
    }
}

} // namespace Moment

