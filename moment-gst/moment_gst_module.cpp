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


// TODO These header macros are the same as in rtmpt_server.cpp
#define MOMENT_GST__HEADERS_DATE \
	Byte date_buf [timeToString_BufSize]; \
	Size const date_len = timeToString (Memory::forObject (date_buf), getUnixtime());

#define MOMENT_GST__COMMON_HEADERS \
	"Server: Moment/1.0\r\n" \
	"Date: ", ConstMemory (date_buf, date_len), "\r\n" \
	"Connection: Keep-Alive\r\n" \
	"Cache-Control: no-cache\r\n"

#define MOMENT_GST__OK_HEADERS(mime_type, content_length) \
	"HTTP/1.1 200 OK\r\n" \
	MOMENT_GST__COMMON_HEADERS \
	"Content-Type: ", (mime_type), "\r\n" \
	"Content-Length: ", (content_length), "\r\n"

#define MOMENT_GST__404_HEADERS(content_length) \
	"HTTP/1.1 404 Not found\r\n" \
	MOMENT_GST__COMMON_HEADERS \
	"Content-Type: text/plain\r\n" \
	"Content-Length: ", (content_length), "\r\n"

#define MOMENT_GST__400_HEADERS(content_length) \
	"HTTP/1.1 400 Bad Request\r\n" \
	MOMENT_GST__COMMON_HEADERS \
	"Content-Type: text/plain\r\n" \
	"Content-Length: ", (content_length), "\r\n"

#define MOMENT_GST__500_HEADERS(content_length) \
	"HTTP/1.1 500 Internal Server Error\r\n" \
	MOMENT_GST__COMMON_HEADERS \
	"Content-Type: text/plain\r\n" \
	"Content-Length: ", (content_length), "\r\n"


using namespace M;
using namespace Moment;

namespace MomentGst {

Result
MomentGstModule::updatePlaylist (ConstMemory   const channel_name,
				 bool          const keep_cur_item,
				 Ref<String> * const mt_nonnull ret_err_msg)
{
    logD_ (_func, "channel_name: ", channel_name);

    mutex.lock ();

    ChannelEntry * const channel_entry = channel_entry_hash.lookup (channel_name);
    if (!channel_entry) {
	mutex.unlock ();
	Ref<String> const err_msg = makeString ("Channel not found: ", channel_name);
	logE_ (_func, err_msg);
	*ret_err_msg = err_msg;
	return Result::Failure;
    }

    if (!channel_entry->playlist_filename) {
	mutex.unlock ();
	Ref<String> const err_msg = makeString ("No playlist for channel \"", channel_name, "\"");
	logE_ (_func, err_msg);
	*ret_err_msg = err_msg;
	return Result::Failure;
    }

    Ref<String> err_msg;
    if (!channel_entry->channel->loadPlaylistFile (
		channel_entry->playlist_filename->mem(), keep_cur_item, &err_msg))
    {
	mutex.unlock ();
	logE_ (_func, "channel->loadPlaylistFile() failed: ", err_msg);
	*ret_err_msg = makeString ("Playlist parsing error: ", err_msg->mem());
	return Result::Failure;
    }

    mutex.unlock ();

    return Result::Success;
}

Result
MomentGstModule::setPosition (ConstMemory const channel_name,
			      ConstMemory const item_name,
			      bool        const item_name_is_id,
			      ConstMemory const seek_str)
{
    Time seek;
    if (!parseDuration (seek_str, &seek)) {
	logE_ (_func, "Couldn't parse seek time: ", seek_str);
	return Result::Failure;
    }

    mutex.lock ();

    ChannelEntry * const channel_entry = channel_entry_hash.lookup (channel_name);
    if (!channel_entry) {
	mutex.unlock ();
	logE_ (_func, "Channel not found: ", channel_name);
	return Result::Failure;
    }

    Result res;
    if (item_name_is_id) {
	res = channel_entry->channel->setPosition_Id (item_name, seek);
    } else {
	Count item_idx;
	if (!strToUint32_safe (item_name, &item_idx)) {
	    mutex.unlock ();
	    logE_ (_func, "Failed to parse item index");
	    return Result::Failure;
	}

	res = channel_entry->channel->setPosition_Index (item_idx, seek);
    }

    if (!res) {
	mutex.unlock ();
	logE_ (_func, "Item not found: ", item_name, item_name_is_id ? " (id)" : " (idx)", ", channel: ", channel_name);
	return Result::Failure;
    }

    mutex.unlock ();

    return Result::Success;
}

void
MomentGstModule::createPlaylistChannel (ConstMemory const channel_name,
					ConstMemory const playlist_filename,
					bool        const recording,
					ConstMemory const record_filename)
{
    ChannelEntry * const channel_entry = new ChannelEntry;

    channel_entry->channel_name = grab (new String (channel_name));
    channel_entry->playlist_filename = grab (new String (playlist_filename));

    Ref<Channel> const channel = grab (new Channel);
    channel_entry->channel = channel;

    channel->init (moment,
		   channel_name,
		   send_metadata,
		   default_width,
		   default_height,
		   default_bitrate);

    mutex.lock ();
    channel_entry_hash.add (channel_entry);
    mutex.unlock ();

    {
	Ref<String> err_msg;
	if (!channel->loadPlaylistFile (playlist_filename, false /* keep_cur_item */, &err_msg))
	    logE_ (_func, "Could not parse playlist file \"", playlist_filename, "\":\n", err_msg);
    }
}

void
MomentGstModule::createStreamChannel (ConstMemory const channel_name,
				      ConstMemory const stream_spec,
				      bool        const is_chain,
				      bool        const recording,
				      ConstMemory const record_filename)
{
    ChannelEntry * const channel_entry = new ChannelEntry;

    channel_entry->channel_name = grab (new String (channel_name));
    channel_entry->playlist_filename = NULL;

    Ref<Channel> const channel = grab (new Channel);
    channel_entry->channel = channel;

    channel->init (moment,
		   channel_name,
		   send_metadata,
		   default_width,
		   default_height,
		   default_bitrate);

    mutex.lock ();
    channel_entry_hash.add (channel_entry);
    mutex.unlock ();

    channel->setSingleItem (stream_spec, is_chain);
}

HttpService::HttpHandler MomentGstModule::admin_http_handler = {
    adminHttpRequest,
    NULL /* httpMessageBody */
};

Result
MomentGstModule::adminHttpRequest (HttpRequest  * const mt_nonnull req,
				   Sender       * const mt_nonnull conn_sender,
				   Memory const &msg_body,
				   void        ** const mt_nonnull ret_msg_data,
				   void         * const _self)
{
    MomentGstModule * const self = static_cast <MomentGstModule*> (_self);

    MOMENT_GST__HEADERS_DATE;

    if (req->getNumPathElems() == 3
	&& (equal (req->getPath (1), "update_playlist") ||
	    equal (req->getPath (1), "update_playlist_now")))
    {
	ConstMemory const playlist_name = req->getPath (2);

	bool const keep_cur_item = equal (req->getPath (1), "update_playlist");
	Ref<String> err_msg;
	if (!self->updatePlaylist (playlist_name, keep_cur_item, &err_msg)) {
	    conn_sender->send (self->page_pool,
			       true /* do_flush */,
			       MOMENT_GST__500_HEADERS (err_msg->mem().len()),
			       "\r\n",
			       err_msg->mem());
	    goto _return;
	}

	ConstMemory const reply_body = "OK";
	conn_sender->send (self->page_pool,
			   true /* do_flush */,
			   MOMENT_GST__OK_HEADERS ("text/plain", reply_body.len()),
			   "\r\n",
			   reply_body);
    } else
    if (req->getNumPathElems() == 5
	&& (equal (req->getPath (1), "set_position") ||
	    equal (req->getPath (1), "set_position_id")))
    {
	ConstMemory const channel_name = req->getPath (2);
	ConstMemory const item_name = req->getPath (3);
	ConstMemory const seek_str = req->getPath (4);
	bool const item_name_is_id = equal (req->getPath (1), "set_position_id");

	if (!self->setPosition (channel_name, item_name, item_name_is_id, seek_str)) {
	    ConstMemory const reply_body = "Error";
	    conn_sender->send (self->page_pool,
			       true /* do_flush */,
			       MOMENT_GST__500_HEADERS (reply_body.len()),
			       "\r\n",
			       reply_body);
	    goto _return;
	}

	ConstMemory const reply_body = "OK";
	conn_sender->send (self->page_pool,
			   true /* do_flush */,
			   MOMENT_GST__OK_HEADERS ("text/plain", reply_body.len()),
			   "\r\n",
			   reply_body);
    } else {
	logE_ (_func, "Unknown admin HTTP request: ", req->getFullPath());

	ConstMemory const reply_body = "Unknown command";
	conn_sender->send (self->page_pool,
			   true /* do_flush */,
			   MOMENT_GST__404_HEADERS (reply_body.len()),
			   "\r\n",
			   reply_body);
    }

_return:
    return Result::Success;
}

void
MomentGstModule::parseSourcesConfigSection ()
{
    logD_ (_func_);

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

	    createStreamChannel (stream_name,
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
    logD_ (_func_);

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

	    createStreamChannel (stream_name,
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

	    createStreamChannel (stream_name,
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
    logD_ (_func_);

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

		if (num_set_opts > 1) {
		    logW_ (_func, "Only one of uri/chain/playlist "
			   "should be specified for stream \"", stream_name, "\"");
		}
	    }

	    Ref<String> record_path;
	    {
		MConfig::Option * const opt = item_section->getOption ("record_path");
		if (opt && opt->getValue())
		    record_path = opt->getValue()->getAsString();
	    }

	    if (chain && !chain->isNull()) {
		createStreamChannel (stream_name,
				     chain->mem(),
				     true /* chain */,
				     record_path ? true : false /* recording */,
				     record_path ? record_path->mem() : ConstMemory());
	    } else
	    if (uri && !uri->isNull()) {
		createStreamChannel (stream_name,
				     uri->mem(),
				     false /* chain */,
				     record_path ? true : false /* recording */,
				     record_path ? record_path->mem() : ConstMemory());
	    } else
	    if (playlist && !playlist->isNull()) {
		logD_ (_func, "playlist: ", playlist);
		createPlaylistChannel (stream_name,
				       playlist->mem(),
				       record_path ? true : false /* recording */,
				       record_path ? record_path->mem() : ConstMemory());
	    } else {
		logW_ (_func, "None of chain/uri/playlist specified for stream \"", stream_name, "\"");
	    }
	}
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
	MConfig::GetResult const res = config->getUint64_default (
		opt_name, &default_width, default_width);
	if (!res) {
	    logE_ (_func, "bad value for ", opt_name);
	    return Result::Failure;
	}
    }

    {
	ConstMemory const opt_name = "mod_gst/height";
	MConfig::GetResult const res = config->getUint64_default (
		opt_name, &default_height,  default_height);
	if (!res) {
	    logE_ (_func, "bad value for ", opt_name);
	    return Result::Failure;
	}
    }

    {
	ConstMemory const opt_name = "mod_gst/bitrate";
	MConfig::GetResult const res = config->getUint64_default (
		opt_name, &default_bitrate,  default_bitrate);
	if (!res) {
	    logE_ (_func, "bad value for ", opt_name);
	    return Result::Failure;
	}
    }

    moment->getAdminHttpService()->addHttpHandler (
	    Cb<HttpService::HttpHandler> (
		    &admin_http_handler, this /* cb_data */, NULL /* coderef_container */),
	    "moment_admin");

    parseSourcesConfigSection ();
    parseChainsConfigSection ();
    parseStreamsConfigSection ();

    return Result::Success;
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

    {
	ChannelEntryHash::iter iter (channel_entry_hash);
	while (!channel_entry_hash.iter_done (iter)) {
	    ChannelEntry * const channel_entry = channel_entry_hash.iter_next (iter);
	    delete channel_entry;
	}
    }
}

} // namespace Moment

