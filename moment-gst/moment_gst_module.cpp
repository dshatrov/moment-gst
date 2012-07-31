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
	"HTTP/1.1 404 Not Found\r\n" \
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

static Ref<String> this_rtmpt_server_addr;

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
	Uint32 item_idx;
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
MomentGstModule::createPlaylistChannel (ConstMemory   const channel_name,
					ConstMemory   const channel_desc,
					ConstMemory   const playlist_filename,
					bool          const recording,
					ConstMemory   const record_filename,
                                        bool          const connect_on_demand,
                                        Time          const connect_on_demand_timeout,
                                        PushAgent   * const push_agent)
{
    ChannelEntry * const channel_entry = new ChannelEntry;

    channel_entry->channel_name = grab (new String (channel_name));
    channel_entry->channel_desc = grab (new String (channel_desc));
    channel_entry->playlist_filename = grab (new String (playlist_filename));

    channel_entry->push_agent = push_agent;

    Ref<Channel> const channel = grab (new Channel);
    channel_entry->channel = channel;

    channel->init (moment,
		   channel_name,
		   send_metadata,
                   enable_prechunking,
		   keep_video_streams,
                   connect_on_demand,
                   connect_on_demand_timeout,
		   default_width,
		   default_height,
		   default_bitrate,
		   no_video_timeout);

    mutex.lock ();
    channel_entry_hash.add (channel_entry);
    mutex.unlock ();
    channel_set.addChannel (channel, channel_name);

    {
	Ref<String> err_msg;
	if (!channel->loadPlaylistFile (playlist_filename, false /* keep_cur_item */, &err_msg))
	    logE_ (_func, "Could not parse playlist file \"", playlist_filename, "\":\n", err_msg);
    }

    if (recording)
	createChannelRecorder (channel_name, channel_name, record_filename);
}

void
MomentGstModule::createStreamChannel (ConstMemory   const channel_name,
				      ConstMemory   const channel_desc,
				      ConstMemory   const stream_spec,
				      bool          const is_chain,
				      bool          const recording,
				      ConstMemory   const record_filename,
                                      bool          const connect_on_demand,
                                      Time          const connect_on_demand_timeout,
                                      PushAgent   * const push_agent)
{
    logD_ (_func, "channel: ", channel_name, ",", is_chain ? "chain" : "uri", ": ", stream_spec);

    ChannelEntry * const channel_entry = new ChannelEntry;

    channel_entry->channel_name = grab (new String (channel_name));
    channel_entry->channel_desc = grab (new String (channel_desc));
    channel_entry->playlist_filename = NULL;

    channel_entry->push_agent = push_agent;

    Ref<Channel> const channel = grab (new Channel);
    channel_entry->channel = channel;

    channel->init (moment,
		   channel_name,
		   send_metadata,
                   enable_prechunking,
		   keep_video_streams,
                   connect_on_demand,
                   connect_on_demand_timeout,
		   default_width,
		   default_height,
		   default_bitrate,
		   no_video_timeout);

    mutex.lock ();
    channel_entry_hash.add (channel_entry);
    mutex.unlock ();
    channel_set.addChannel (channel, channel_name);

    channel->setSingleItem (stream_spec, is_chain);

    if (recording)
	createChannelRecorder (channel_name, channel_name, record_filename);
}

void
MomentGstModule::createDummyChannel (ConstMemory   const channel_name,
				     ConstMemory   const channel_desc,
                                     PushAgent   * const push_agent)
{
    ChannelEntry * const channel_entry = new ChannelEntry;

    channel_entry->channel_name = grab (new String (channel_name));
    channel_entry->channel_desc = grab (new String (channel_desc));
    channel_entry->playlist_filename = NULL;

    channel_entry->push_agent = push_agent;

    Ref<Channel> const channel = grab (new Channel);
    channel_entry->channel = channel;

    channel->init (moment,
		   channel_name,
		   send_metadata,
                   enable_prechunking,
		   keep_video_streams,
                   false /* connect_on_demand */,
                   60    /* connect_on_demand_timeout */,
		   default_width,
		   default_height,
		   default_bitrate,
		   no_video_timeout);

    mutex.lock ();
    channel_entry_hash.add (channel_entry);
    mutex.unlock ();
    channel_set.addChannel (channel, channel_name);

    channel->setSingleItem (ConstMemory(), true /* is_chain */);
}

void
MomentGstModule::createPlaylistRecorder (ConstMemory const recorder_name,
					 ConstMemory const playlist_filename,
					 ConstMemory const filename_prefix)
{
    RecorderEntry * const recorder_entry = new RecorderEntry;

    recorder_entry->recorder_name = grab (new String (recorder_name));
    recorder_entry->playlist_filename  = grab (new String (playlist_filename));

    Ref<Recorder> const recorder = grab (new Recorder);
    recorder_entry->recorder = recorder;

    recorder->init (moment,
		    page_pool,
		    &channel_set,
		    filename_prefix);

    mutex.lock ();
    recorder_entry_hash.add (recorder_entry);
    mutex.unlock ();

    {
	Ref<String> err_msg;
	if (!recorder->loadPlaylistFile (playlist_filename, false /* keep_cur_item */, &err_msg))
	    logE_ (_func, "Could not parse recorder playlist file \"", playlist_filename, "\":\n", err_msg);
    }
}

void
MomentGstModule::createChannelRecorder (ConstMemory const recorder_name,
					ConstMemory const channel_name,
					ConstMemory const filename_prefix)
{
    RecorderEntry * const recorder_entry = new RecorderEntry;

    recorder_entry->recorder_name = grab (new String (recorder_name));
    recorder_entry->playlist_filename = NULL;

    Ref<Recorder> const recorder = grab (new Recorder);
    recorder_entry->recorder = recorder;

    recorder->init (moment,
		    page_pool,
		    &channel_set,
		    filename_prefix);

    mutex.lock ();
    recorder_entry_hash.add (recorder_entry);
    mutex.unlock ();

    recorder->setSingleChannel (channel_name);
}

HttpService::HttpHandler MomentGstModule::admin_http_handler = {
    adminHttpRequest,
    NULL /* httpMessageBody */
};

void
MomentGstModule::printChannelInfoJson (PagePool::PageListHead * const page_list,
				       ChannelEntry           * const channel_entry)
{
    static char const open_str []  = "[ \"";
    page_pool->getFillPages (page_list, open_str);

    page_pool->getFillPages (page_list, channel_entry->channel_name->mem());

    static char const sep_a [] = "\", \"";
    page_pool->getFillPages (page_list, sep_a);

    page_pool->getFillPages (page_list, channel_entry->channel_desc->mem());

    page_pool->getFillPages (page_list, sep_a);

    {
	if (channel_entry->channel
	    && channel_entry->channel->isSourceOnline())
	{
	    page_pool->getFillPages (page_list, "ONLINE");
	} else {
	    page_pool->getFillPages (page_list, "OFFLINE");
	}
    }

    static char const close_str [] = "\" ]";
    page_pool->getFillPages (page_list, close_str);
}

Result
MomentGstModule::httpGetChannelsStat (HttpRequest  * const mt_nonnull req,
				      Sender       * const mt_nonnull conn_sender,
				      void         * const _self)
{
    MomentGstModule * const self = static_cast <MomentGstModule*> (_self);

    MOMENT_GST__HEADERS_DATE

    PagePool::PageListHead page_list;

    static char const prefix [] =
	    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	    "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n"
	    "<html style=\"height: 100%\" xmlns=\"http://www.w3.org/1999/xhtml\">\n"
	    "<head>\n"
	    "  <meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\"/>\n"
	    "  <title>Traffic stats</title>\n"
	    "  <style type=\"text/css\">\n"
	    "    .top_table      { border-right: 1px solid #777777; border-bottom: 1px solid #777777;\n"
            "                      margin-top: 5px; }\n"
	    "    .top_table td   { border-left:  1px solid #777777; border-top:    1px solid #777777; padding: 3px; }\n"
	    "    .inner_table    { border: 0; }\n"
	    "    .inner_table td { border: 0; padding: 0; }\n"
	    "    .total td       { border-top:   2px solid #444444; }\n"
	    "  </style>\n"
	    "</head>\n"
	    "<body style=\"font-family: sans-serif\">\n"
	    "<form name=\"reset\" action=\"channels_stat_reset\" method=\"post\">\n"
	    "  <input type=\"submit\" value=\"Сброс статистики\">\n"
	    "</form>\n"
	    "<table class=\"top_table\" border=\"0\" cellpadding=\"0\" cellspacing=\"0\">\n"
	    "<tr>\n"
	    "<td>Канал</td><td>Описание</td><td>Время<sup>1</sup> ЧЧ:ММ</td><td>Время<sup>1</sup>, сек</td>"
	    "<td>Ширина канала<sup>2</sup>, Мбит/сек</td>"
	    "<td>Получено<sup>3</sup>, ГБайт</td><td>Получено<sup>3</sup>, байт</td><td>Ген.<sup>4</sup>, Мбит/сек</td>"
	    "<td>Видео<sup>5</sup>, байт</td><td>Аудио<sup>6</sup>, байт</td>\n"
	    "</tr>\n";

    static char const suffix [] =
	    "</table>\n"
	    "<p>1 &mdash; Время, прошедшее с момента создания канала, включая время, когда камера была недоступна;<br/>\n"
	    "2 &mdash; Усреднённая скорость поступления видеоданных от камеры. Ширина = Получено / Время;<br/>\n"
	    "3 &mdash; Объём полученных с камеры видео/аудиоданных. Накладные расходы транспортных протоколов (HTTP, RTSP) не включены;<br/>\n"
	    "4 &mdash; Усреднённый битрейт генерируемого видеопотока, т.е. потока, который будет отдан смотрящим клиентам;<br/>\n"
	    "5 &mdash; Общий объём перекодированного видео, подготовленного для отдачи клиентам (только видео, без аудио);<br/>\n"
	    "6 &mdash; Общий объём аудио, подготовленного для отдачи клиентам.</p>\n"
	    "</body>\n"
	    "</html>\n";

    self->page_pool->getFillPages (&page_list, ConstMemory (prefix, sizeof (prefix) - 1));

    self->mutex.lock ();

    double width_total = 0.0;
    double rx_total = 0.0;
    {
	ChannelEntryHash::iter iter (self->channel_entry_hash);
	while (!self->channel_entry_hash.iter_done (iter)) {
	    ChannelEntry * const channel_entry = self->channel_entry_hash.iter_next (iter);

	    if (!channel_entry->channel)
		continue;

	    GstStreamCtl::TrafficStats traffic_stats;
	    channel_entry->channel->getTrafficStats (&traffic_stats);
	    Uint64 const rx_bytes = traffic_stats.rx_bytes;

	    double const width =
		    traffic_stats.time_elapsed != 0 ?
			    (double) rx_bytes / (double) traffic_stats.time_elapsed * 8.0 / (1024.0 * 1024.0) : 0.0;

	    width_total += width;
	    rx_total += (double) rx_bytes;

	    Format fmt_time;
	    fmt_time.min_digits = 2;
	    Format fmt;
	    fmt.precision = 3;
	    Ref<String> const line_str = makeString (
		    "<tr>"
		    "<td>"
		    "  <table class=\"inner_table\" border=\"0\" cellpadding=\"0\" cellspacing=\"0\"><tr>\n"
		    "    <td style=\"padding-right: 3px\">\n"
		    "      <form name=\"reset\" action=\"channel_reconnect?name=", channel_entry->channel_name->mem(), "\" method=\"post\">\n"
		    "        <input type=\"submit\" value=\"Переподкл.\">\n"
		    "      </form>\n"
		    "    </td>\n"
		    "    <td>", channel_entry->channel_name->mem(), "</td>\n"
		    "  </tr></table>\n"
		    "</td>\n"
		    "<td>", channel_entry->channel_desc->mem(), "</td>"
		    "<td>", fmt_time, traffic_stats.time_elapsed / 3600, ":", (traffic_stats.time_elapsed % 3600) / 60, "</td>"
		    "<td>", fmt_def, traffic_stats.time_elapsed, "</td>"
		    "<td>", fmt, width, "</td>"
		    "<td>", (double) rx_bytes / (1024.0 * 1024.0 * 1024.0), "</td>"
		    "<td>", rx_bytes, "</td>"
		    "<td>", (traffic_stats.time_elapsed != 0 ?
				    (double) (traffic_stats.rx_video_bytes + traffic_stats.rx_audio_bytes) /
					    (double) traffic_stats.time_elapsed * 8.0 / (1024.0 * 1024.0) : 0), "</td>"
		    "<td>", traffic_stats.rx_video_bytes, "</td>"
		    "<td>", traffic_stats.rx_audio_bytes, "</td>"
		    "</tr>");
	    self->page_pool->getFillPages (&page_list, line_str->mem());
	}
    }
    {
	Format fmt;
	fmt.precision = 3;
	Ref<String> const line_str = makeString (
		"<tr class=\"total\">"
		"<td colspan=\"2\"><b>Всего:</b></td>"
		"<td/>"
		"<td/>"
		"<td>", fmt, width_total, "</td>"
		"<td>", rx_total / (1024.0 * 1024.0 * 1024.0), "</td>"
		"<td></td>"
		"<td></td>"
		"<td></td>"
		"<td></td>"
		"</tr>");
	self->page_pool->getFillPages (&page_list, line_str->mem());
    }

    self->mutex.unlock ();

    self->page_pool->getFillPages (&page_list, ConstMemory (suffix, sizeof (suffix) - 1));

    Size content_len = 0;
    {
	PagePool::Page *page = page_list.first;
	while (page) {
	    content_len += page->data_len;
	    page = page->getNextMsgPage();
	}
    }

    conn_sender->send (self->page_pool,
		       false /* do_flush */,
		       MOMENT_GST__OK_HEADERS ("text/html", content_len),
		       "\r\n");
    conn_sender->sendPages (self->page_pool, &page_list, true /* do_flush */);

    logA_ ("mod_gst 200 ", req->getClientAddress(), " ", req->getRequestLine());
    return Result::Success;
}

Result
MomentGstModule::adminHttpRequest (HttpRequest  * const mt_nonnull req,
				   Sender       * const mt_nonnull conn_sender,
				   Memory const & /* msg_body */,
				   void        ** const mt_nonnull /* ret_msg_data */,
				   void         * const _self)
{
    MomentGstModule * const self = static_cast <MomentGstModule*> (_self);

    logD_ (_func_);

    MOMENT_GST__HEADERS_DATE

    if (req->getNumPathElems() >= 2
	&& equal (req->getPath (1), "set_channel"))
    {
	ConstMemory const channel_name = req->getParameter ("name");
	if (channel_name.mem() == NULL) {
	    logE_ (_func, "set_channel: no channel name (\"name\" parameter)\n");
	    goto _bad_request;
	}

	// "src_" prefix indicates that only one of these parameters can be
	// specified at a time.
	ConstMemory const src_chain    = req->getParameter ("src_chain");
	ConstMemory const src_path     = req->getParameter ("src_path");
	ConstMemory const src_uri      = req->getParameter ("src_uri");
	ConstMemory const src_playlist = req->getParameter ("src_playlist");

	{
	    logD_ (_func, "set_channel:");
	    if (src_chain.len())
		logD_ (_func, "    src_chain: ", src_chain);
	    if (src_path.len())
		logD_ (_func, "    src_path: ", src_path);
	    if (src_uri.len())
		logD_ (_func, "    src_uri: ", src_uri);
	    if (src_playlist.len())
		logD_ (_func, "    src_playlist: ", src_playlist);
	}

	{
	    unsigned count = 0;
	    if (src_chain.len())
		++count;
	    if (src_path.len())
		++count;
	    if (src_uri.len())
		++count;
	    if (src_playlist.len())
		++count;

	    if (count == 0) {
		logE_ (_func, "set_channel: no video source");
		goto _bad_request;
	    }

	    if (count > 1) {
		logE_ (_func, "set_channel: multiple video sources in a single request");
		goto _bad_request;
	    }
	}

	if (src_chain.len()) {
	    self->createStreamChannel (channel_name,
				       ConstMemory() /* channel_desc */,
				       src_chain,
				       true /* is_chain */,
				       false /* recording */,
				       ConstMemory() /* record_path */,
                                       self->default_connect_on_demand,
                                       self->default_connect_on_demand_timeout);
	} else
	if (src_path.len()) {
	    self->createStreamChannel (channel_name,
				       ConstMemory() /* channel_desc */,
				       makeString ("file://", src_path)->mem(),
				       false /* is_chain */,
				       false /* recording */,
				       ConstMemory() /* record_path */,
                                       self->default_connect_on_demand,
                                       self->default_connect_on_demand_timeout);
	} else
	if (src_uri.len()) {
	    self->createStreamChannel (channel_name,
				       ConstMemory() /* channel_desc */,
				       src_uri,
				       false /* is_chain */,
				       false /* recording */,
				       ConstMemory() /* record_path */,
                                       self->default_connect_on_demand,
                                       self->default_connect_on_demand_timeout);
	} else
	if (src_playlist.len()) {
	    self->createPlaylistChannel (channel_name,
					 ConstMemory() /* channel_desc */,
					 src_playlist,
					 false /* recording */,
					 ConstMemory() /* record_path */,
                                         self->default_connect_on_demand,
                                         self->default_connect_on_demand_timeout);
	}
    } else
    if (req->getNumPathElems() >= 2
	&& equal (req->getPath (1), "delete_channel"))
    {
	// TODO
    } else
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

	    logA_ ("gst_admin 500 ", req->getClientAddress(), " ", req->getRequestLine());
	    goto _return;
	}

	ConstMemory const reply_body = "OK";
	conn_sender->send (self->page_pool,
			   true /* do_flush */,
			   MOMENT_GST__OK_HEADERS ("text/plain", reply_body.len()),
			   "\r\n",
			   reply_body);

	logA_ ("gst_admin 200 ", req->getClientAddress(), " ", req->getRequestLine());
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

	    logA_ ("gst_admin 500 ", req->getClientAddress(), " ", req->getRequestLine());
	    goto _return;
	}

	ConstMemory const reply_body = "OK";
	conn_sender->send (self->page_pool,
			   true /* do_flush */,
			   MOMENT_GST__OK_HEADERS ("text/plain", reply_body.len()),
			   "\r\n",
			   reply_body);

	logA_ ("gst_admin 200 ", req->getClientAddress(), " ", req->getRequestLine());
    } else
    if (req->getNumPathElems() >= 2
	&& equal (req->getPath (1), "channel_info"))
    {
	ConstMemory const channel_name = req->getParameter ("name");
	if (channel_name.mem() == NULL) {
	    logE_ (_func, "set_channel: no channel name (\"name\" parameter)\n");
	    goto _bad_request;
	}

	self->mutex.lock ();

	ChannelEntry * const channel_entry = self->channel_entry_hash.lookup (channel_name);
	if (!channel_entry) {
	    self->mutex.unlock ();
	    logE_ (_func, "Channel not found: ", channel_name);
	    return Result::Failure;
	}

	PagePool::PageListHead page_list;
	self->printChannelInfoJson (&page_list, channel_entry);

	self->mutex.unlock ();

      // TODO Below is the common code for finishing HTTP requests.
      //      Put it into an utility method.

	Size content_len = 0;
	{
	    PagePool::Page *page = page_list.first;
	    while (page) {
		content_len += page->data_len;
		page = page->getNextMsgPage();
	    }
	}

	conn_sender->send (self->page_pool,
			   false /* do_flush */,
			   MOMENT_GST__OK_HEADERS ("text/plain", content_len),
			   "\r\n");
	conn_sender->sendPages (self->page_pool, &page_list, true /* do_flush */);

	logA_ ("mod_gst 200 ", req->getClientAddress(), " ", req->getRequestLine());
    } else
    if (req->getNumPathElems() >= 2
	&& equal (req->getPath (1), "channel_list"))
    {
	PagePool::PageListHead page_list;

	static char const prefix [] = "[\n";
	self->page_pool->getFillPages (&page_list, prefix);

	self->mutex.lock ();

	{
	    ChannelEntryHash::iter iter (self->channel_entry_hash);
	    while (!self->channel_entry_hash.iter_done (iter)) {
		ChannelEntry * const channel_entry = self->channel_entry_hash.iter_next (iter);

		self->printChannelInfoJson (&page_list, channel_entry);

		static char const comma_str [] = ",\n";
		self->page_pool->getFillPages (&page_list, comma_str);
	    }
	}

	self->mutex.unlock ();

	static char const suffix [] = "]\n";
	self->page_pool->getFillPages (&page_list, suffix);

	Size content_len = 0;
	{
	    PagePool::Page *page = page_list.first;
	    while (page) {
		content_len += page->data_len;
		page = page->getNextMsgPage();
	    }
	}

	conn_sender->send (self->page_pool,
			   false /* do_flush */,
			   MOMENT_GST__OK_HEADERS ("text/plain", content_len),
			   "\r\n");
	conn_sender->sendPages (self->page_pool, &page_list, true /* do_flush */);

	logA_ ("mod_gst 200 ", req->getClientAddress(), " ", req->getRequestLine());
    } else
    if (req->getNumPathElems() >= 2
	&& equal (req->getPath (1), "channels_stat_reset"))
    {
	{
	    ChannelEntryHash::iter iter (self->channel_entry_hash);
	    while (!self->channel_entry_hash.iter_done (iter)) {
		ChannelEntry * const channel_entry = self->channel_entry_hash.iter_next (iter);

		if (!channel_entry->channel)
		    continue;

		channel_entry->channel->resetTrafficStats();
	    }
	}

	conn_sender->send (self->page_pool,
			   true /* do_flush */,
			   "HTTP/1.1 302 Found\r\n"
			   "Location: channels_stat\r\n"
			   "Content-length: 0\r\n"
			   "\r\n");
	logA_ ("mod_gst 302 ", req->getClientAddress(), " ", req->getRequestLine());
    } else
    if (req->getNumPathElems() >= 2
	&& equal (req->getPath (1), "channel_reconnect"))
    {
	ConstMemory const channel_name = req->getParameter ("name");
	if (channel_name.mem() == NULL) {
	    logE_ (_func, "set_channel: no channel name (\"name\" parameter)\n");
	    goto _channel_reconnect__done;
	}

	self->mutex.lock ();

	{
	    ChannelEntry * const channel_entry = self->channel_entry_hash.lookup (channel_name);
	    if (!channel_entry) {
		self->mutex.unlock ();
		logE_ (_func, "Channel not found: ", channel_name);
		goto _channel_reconnect__done;
	    }

	    channel_entry->channel->restartStream ();
//	    channel_entry->channel->resetTrafficStats ();
	}

	self->mutex.unlock ();

_channel_reconnect__done:
	conn_sender->send (self->page_pool,
			   true /* do_flush */,
			   "HTTP/1.1 302 Found\r\n"
			   "Location: channels_stat\r\n"
			   "Content-length: 0\r\n"
			   "\r\n");
	logA_ ("mod_gst 302 ", req->getClientAddress(), " ", req->getRequestLine());
    } else
    if (req->getNumPathElems() >= 2
	&& equal (req->getPath (1), "channels_stat"))
    {
	return httpGetChannelsStat (req, conn_sender, _self);
    } else {
	logE_ (_func, "Unknown admin HTTP request: ", req->getFullPath());

	ConstMemory const reply_body = "Unknown command";
	conn_sender->send (self->page_pool,
			   true /* do_flush */,
			   MOMENT_GST__404_HEADERS (reply_body.len()),
			   "\r\n",
			   reply_body);

	logA_ ("gst_admin 404 ", req->getClientAddress(), " ", req->getRequestLine());
    }

_return:
    return Result::Success;

_bad_request:
    {
	MOMENT_GST__HEADERS_DATE
	ConstMemory const reply_body = "400 Bad Request";
	conn_sender->send (
		self->page_pool,
		true /* do_flush */,
		MOMENT_GST__400_HEADERS (reply_body.len()),
		"\r\n",
		reply_body);
	if (!req->getKeepalive())
	    conn_sender->closeAfterFlush();
    }

    return Result::Success;
}

HttpService::HttpHandler MomentGstModule::http_handler = {
    httpRequest,
    NULL /* httpMessageBody */
};

Result
MomentGstModule::httpRequest (HttpRequest  * const mt_nonnull req,
			      Sender       * const mt_nonnull conn_sender,
			      Memory const & /* msg_body */,
			      void        ** const mt_nonnull /* ret_msg_data */,
			      void         * const _self)
{
    MomentGstModule * const self = static_cast <MomentGstModule*> (_self);

    logD_ (_func_);

    MOMENT_GST__HEADERS_DATE

    if (req->getNumPathElems() >= 2
	&& equal (req->getPath (1), "wall"))
    {
	PagePool::PageListHead page_list;

	static char const prefix [] =
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n"
		"<html style=\"height: 100%\" xmlns=\"http://www.w3.org/1999/xhtml\">\n"
		"<head>\n"
		"  <meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\"/>\n"
		"  <title>Wall</title>\n"
		"</head>\n"
		"<body bgcolor=\"#444444\" style=\"font-family: sans-serif\">\n"
		"<table border=\"0\" cellpadding=\"0\" cellspacing=\"0\">\n";

	static char const suffix [] =
		"</table>\n"
		"</body>\n"
		"</html>\n";

	self->page_pool->getFillPages (&page_list, ConstMemory (prefix, sizeof (prefix) - 1));

	self->mutex.lock ();

	{
	    ChannelEntryHash::iter iter (self->channel_entry_hash);
	    unsigned row_cnt = 0;
	    unsigned const row_size = 3;
	    while (!self->channel_entry_hash.iter_done (iter)) {
		ChannelEntry * const channel_entry = self->channel_entry_hash.iter_next (iter);

		if (row_cnt == 0) {
		    static char const row_prefix [] = "<tr>\n";
		    self->page_pool->getFillPages (&page_list, ConstMemory (row_prefix, sizeof (row_prefix) - 1));
		}

		Ref<String> const flashvars = makeString (
			"server=rtmpt://", this_rtmpt_server_addr->mem(),
			"&stream=", channel_entry->channel_name->mem(),
			"?paused&play_duration=20&buffer=0.0&shadow=0.4");

		static char const entry_a [] =
		        "<td style=\"vertical-align: top\">\n"
			"<div style=\"position: relative; width: 320px; height: 240px; margin-left: auto; margin-right: auto\">\n"
			"  <object classid=\"clsid:d27cdb6e-ae6d-11cf-96b8-444553540000\"\n"
			"        width=\"100%\"\n"
			"        height=\"100%\"\n"
			"        id=\"BasicPlayer\"\n"
			"        align=\"Default\">\n"
			"    <param name=\"movie\" value=\"/basic/BasicPlayer.swf\"/>\n"
			"    <param name=\"bgcolor\" value=\"#000000\"/>\n"
			"    <param name=\"scale\" value=\"noscale\"/>\n"
			"    <param name=\"quality\" value=\"high\"/>\n"
			"    <param name=\"allowfullscreen\" value=\"true\"/>\n"
			"    <param name=\"allowscriptaccess\" value=\"always\"/>\n"
			"    <param name=\"FlashVars\" value=\"";
		self->page_pool->getFillPages (&page_list, ConstMemory (entry_a, sizeof (entry_a) - 1));
		self->page_pool->getFillPages (&page_list, flashvars->mem());

		static char const entry_b [] = "\"/>\n"
			"    <embed src=\"/basic/BasicPlayer.swf\"\n"
			"        FlashVars=\"";
		self->page_pool->getFillPages (&page_list, ConstMemory (entry_b, sizeof (entry_b) - 1));
		self->page_pool->getFillPages (&page_list, flashvars->mem());

		static char const entry_c [] = "\"\n"
			"        name=\"BasicPlayer\"\n"
			"        align=\"Default\"\n"
			"        width=\"100%\"\n"
			"        height=\"100%\"\n"
			"        bgcolor=\"#000000\"\n"
			"        scale=\"noscale\"\n"
			"        quality=\"high\"\n"
			"        allowfullscreen=\"true\"\n"
			"        allowscriptaccess=\"always\"\n"
			"        type=\"application/x-shockwave-flash\"\n"
			"        pluginspage=\"http://www.adobe.com/shockwave/download/index.cgi?P1_Prod_Version=ShockwaveFlash\"/>\n"
			"  </object>\n"
			"</div>\n";
		self->page_pool->getFillPages (&page_list, ConstMemory (entry_c, sizeof (entry_c) - 1));

		static char const entry_a0 [] =
		        "<div style=\"width: 300px; padding-bottom: 15px; padding-left: 20px; color: white;\">";
		self->page_pool->getFillPages (&page_list, ConstMemory (entry_a0, sizeof (entry_a0) - 1));
		self->page_pool->getFillPages (&page_list, channel_entry->channel_name->mem());
		static char const entry_a1 [] =
			"&nbsp;&nbsp; ";
		self->page_pool->getFillPages (&page_list, ConstMemory (entry_a1, sizeof (entry_a1) - 1));
		self->page_pool->getFillPages (&page_list, channel_entry->channel_desc->mem());
		static char const entry_a2 [] =
			"</div>\n"
			"</td>\n";
		self->page_pool->getFillPages (&page_list, ConstMemory (entry_a2, sizeof (entry_a2) - 1));

		++row_cnt;
		if (row_cnt == row_size) {
		    row_cnt = 0;

		    static char const row_suffix [] = "</tr>\n";
		    self->page_pool->getFillPages (&page_list, ConstMemory (row_suffix, sizeof (row_suffix) - 1));
		}
	    }

	    if (row_cnt != 0) {
		static char const row_suffix [] = "</tr>\n";
		self->page_pool->getFillPages (&page_list, ConstMemory (row_suffix, sizeof (row_suffix) - 1));
	    }
	}

	self->mutex.unlock ();

	self->page_pool->getFillPages (&page_list, ConstMemory (suffix, sizeof (suffix) - 1));

	Size content_len = 0;
	{
	    PagePool::Page *page = page_list.first;
	    while (page) {
		content_len += page->data_len;
		page = page->getNextMsgPage();
	    }
	}

	conn_sender->send (self->page_pool,
			   false /* do_flush */,
			   MOMENT_GST__OK_HEADERS ("text/html", content_len),
			   "\r\n");
	conn_sender->sendPages (self->page_pool, &page_list, true /* do_flush */);

	logA_ ("mod_gst 200 ", req->getClientAddress(), " ", req->getRequestLine());
    } else {
	logE_ (_func, "Unknown admin request: ", req->getFullPath());

	ConstMemory const reply_body = "404 Not Found (mod_gst)";
	conn_sender->send (self->page_pool,
			   true /* do_flush */,
			   MOMENT_GST__404_HEADERS (reply_body.len()),
			   "\r\n",
			   reply_body);

	logA_ ("gst_admin 404 ", req->getClientAddress(), " ", req->getRequestLine());
    }

    return Result::Success;

#if 0
_bad_request:
    {
	MOMENT_GST__HEADERS_DATE
	ConstMemory const reply_body = "400 Bad Request (mod_gst)";
	conn_sender->send (
		self->page_pool,
		true /* do_flush */,
		MOMENT_GST__400_HEADERS (reply_body.len()),
		"\r\n",
		reply_body);
	if (!req->getKeepalive())
	    conn_sender->closeAfterFlush();
    }

    return Result::Success;
#endif
}

void
MomentGstModule::parseSourcesConfigSection ()
{
    logD_ (_func_);

    MConfig::Config * const config = moment->getConfig();

    MConfig::Section * const src_section = config->getSection ("mod_gst/sources");
    if (!src_section) {
//	logI_ ("No video stream sources specified "
//	       "(\"mod_gst/sources\" config section is missing).");
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
			         ConstMemory() /* channel_desc */,
				 stream_uri,
				 false /* is_chain */,
				 false /* recording */,
				 ConstMemory() /* record_filename */,
                                 default_connect_on_demand,
                                 default_connect_on_demand_timeout);
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
//	logI_ ("No custom chains specified "
//	       "(\"mod_gst/chains\" config section is missing).");
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
			         ConstMemory() /* channel_desc */,
				 chain_spec,
				 true /* is_chain */,
				 false /* recording */,
				 ConstMemory() /* record_filename */,
                                 default_connect_on_demand,
                                 default_connect_on_demand_timeout);
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
			         ConstMemory() /* channel_desc */,
				 chain_spec,
				 true /* is_chain */,
				 got_record_path /* recording */,
				 record_path,
                                 default_connect_on_demand,
                                 default_connect_on_demand_timeout);
	}
    }
}

Result
MomentGstModule::parseStreamsConfigSection ()
{
    logD_ (_func_);

    MConfig::Config * const config = moment->getConfig();

    MConfig::Section * const streams_section = config->getSection ("mod_gst/streams");
    if (!streams_section)
	return Result::Success;;

    MConfig::Section::iter streams_iter (*streams_section);
    while (!streams_section->iter_done (streams_iter)) {
	MConfig::SectionEntry * const item_entry = streams_section->iter_next (streams_iter);
	if (item_entry->getType() == MConfig::SectionEntry::Type_Section) {
	    MConfig::Section* const item_section = static_cast <MConfig::Section*> (item_entry);

	    logD_ (_func, "section");

	    Ref<String> stream_name;
	    {
		MConfig::Option * const opt = item_section->getOption ("name");
		if (opt && opt->getValue()) {
		    stream_name = opt->getValue()->getAsString();
		    logD_ (_func, "stream_name: ", stream_name);
		}

		if (stream_name.isNull()) {
		    logW_ (_func, "Unnamed stream in section mod_gst/streams");
		    stream_name = grab (new String);
		}
	    }

	    Ref<String> channel_desc;
	    {
		MConfig::Option * const opt = item_section->getOption ("desc");
		if (opt && opt->getValue()) {
		    channel_desc = opt->getValue()->getAsString();
		    logD_ (_func, "channel_desc: ", channel_desc);
		}

		if (channel_desc.isNull())
		    channel_desc = grab (new String);
	    }

	    Ref<String> chain;
	    Ref<String> uri;
	    Ref<String> playlist;
	    {
		int num_set_opts = 0;

		{
		    MConfig::Option * const opt = item_section->getOption ("chain");
		    if (opt && opt->getValue()) {
			chain = opt->getValue()->getAsString();
			logD_ (_func, "chain: ", chain);
			++num_set_opts;
		    }
		}

		{
		    MConfig::Option * const opt = item_section->getOption ("uri");
		    if (opt && opt->getValue()) {
			uri = opt->getValue()->getAsString();
			logD_ (_func, "uri: ", uri);
			++num_set_opts;
		    }
		}

		{
		    MConfig::Option * const opt = item_section->getOption ("playlist");
		    if (opt && opt->getValue()) {
			playlist = opt->getValue()->getAsString();
			logD_ (_func, "playlist: ", playlist);
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
		if (opt && opt->getValue()) {
		    record_path = opt->getValue()->getAsString();
		    logD_ (_func, "record_path: ", record_path);
		}
	    }

            bool connect_on_demand = default_connect_on_demand;
            {
                ConstMemory const opt_name = "connect_on_demand";
                MConfig::Option * const opt = item_section->getOption (opt_name);
                if (opt) {
                    MConfig::BooleanValue const val = opt->getBoolean();
                    if (val == MConfig::Boolean_Invalid) {
                        logE_ (_func, "Invalid value for ", opt_name, ": ", config->getString (opt_name));
                        return Result::Failure;
                    }

                    if (val == MConfig::Boolean_True)
                        connect_on_demand = true;
                    else
                    if (val == MConfig::Boolean_False)
                        connect_on_demand = false;
                    else
                        assert (val == MConfig::Boolean_Default);
                }
            }

            Time connect_on_demand_timeout = default_connect_on_demand_timeout;
            {
                ConstMemory const opt_name = "connect_on_demand_timeout";
                MConfig::Option * const opt = item_section->getOption (opt_name);
                if (opt && opt->getValue()) {
                    Uint64 tmp_uint64;
                    if (!opt->getValue()->getAsUint64 (&tmp_uint64)) {
                        logE_ (_func, "Bad value for \"", opt_name, "\" option: ", opt->getValue()->mem());
                        return Result::Failure;
                    }
                    connect_on_demand_timeout = (Time) tmp_uint64;
                }
            }

            Ref<PushAgent> push_agent;
            {
                Ref<String> push_uri;
                {
                    ConstMemory const opt_name = "push_uri";
                    MConfig::Option * const opt = item_section->getOption (opt_name);
                    if (opt && opt->getValue()) {
                        push_uri = opt->getValue()->getAsString();
                        logD_ (_func, opt_name, ": ", push_uri);
                    } else {
                        push_uri = grab (new String);
                    }
                }

#if 0
// TODO Unused?
                Ref<String> push_server;
                {
                    ConstMemory const opt_name = "push_server";
                    MConfig::Option * const opt = item_section->getOption (opt_name);
                    if (opt && opt->getValue()) {
                        push_server = opt->getValue()->getAsString();
                        logD_ (_func, opt_name, ": ", push_server);
                    } else {
                        push_server = grab (new String);
                    }
                }

                Uint64 push_port = 1935;
                {
                    ConstMemory const opt_name = "push_port";
                    MConfig::Option * const opt = item_section->getOption (opt_name);
                    if (opt && opt->getValue()) {
                        if (!opt->getValue()->getAsUint64 (&push_port)) {
                            logE_ (_func, "Bad value for \"", opt_name, "\" option: ", opt->getValue()->mem());
                            return Result::Failure;
                        }
                        logD_ (_func, opt_name, ": ", push_port);
                    } else {
                        if (push_server && !push_server->isNull())
                            logD_ (_func, "Default push_port: ", push_port);
                    }
                }
#endif

                Ref<String> push_username;
                {
                    ConstMemory const opt_name = "push_username";
                    MConfig::Option * const opt = item_section->getOption (opt_name);
                    if (opt && opt->getValue()) {
                        push_username = opt->getValue()->getAsString();
                        logD_ (_func, opt_name, ": ", push_username);
                    } else {
                        push_username = grab (new String);
                    }
                }

                Ref<String> push_password;
                {
                    ConstMemory const opt_name = "push_password";
                    MConfig::Option * const opt = item_section->getOption (opt_name);
                    if (opt && opt->getValue()) {
                        push_password = opt->getValue()->getAsString();
                        logD_ (_func, opt_name, ": ", push_password);
                    } else {
                        push_password = grab (new String);
                    }
                }

                Ref<PushProtocol> const push_protocol = moment->getPushProtocolForUri (push_uri->mem());
                if (push_protocol) {
                    push_agent = grab (new PushAgent);
                    push_agent->init (stream_name->mem(),
                                      push_protocol,
                                      push_uri      ? push_uri->mem()      : ConstMemory(),
                                      push_username ? push_username->mem() : ConstMemory(),
                                      push_password ? push_password->mem() : ConstMemory());
                }
            }

	    if (chain && !chain->isNull()) {
		createStreamChannel (stream_name->mem(),
				     channel_desc->mem(),
				     chain->mem(),
				     true /* is_chain */,
				     record_path ? true : false /* recording */,
				     record_path ? record_path->mem() : ConstMemory(),
                                     connect_on_demand,
                                     connect_on_demand_timeout,
                                     push_agent);
	    } else
	    if (uri && !uri->isNull()) {
		createStreamChannel (stream_name->mem(),
				     channel_desc->mem(),
				     uri->mem(),
				     false /* is_chain */,
				     record_path ? true : false /* recording */,
				     record_path ? record_path->mem() : ConstMemory(),
                                     connect_on_demand,
                                     connect_on_demand_timeout,
                                     push_agent);
	    } else
	    if (playlist && !playlist->isNull()) {
		logD_ (_func, "playlist: ", playlist);
		createPlaylistChannel (stream_name->mem(),
				       channel_desc->mem(),
				       playlist->mem(),
				       record_path ? true : false /* recording */,
				       record_path ? record_path->mem() : ConstMemory(),
                                       connect_on_demand,
                                       connect_on_demand_timeout,
                                       push_agent);
	    } else {
		logW_ (_func, "None of chain/uri/playlist specified for stream \"", stream_name, "\"");
		createDummyChannel (stream_name->mem(), channel_desc->mem(), push_agent);
	    }

	}
    }

    return Result::Success;
}

void
MomentGstModule::parseRecordingsConfigSection ()
{
    MConfig::Config * const config = moment->getConfig();

    MConfig::Section * const recordings_section = config->getSection ("mod_gst/recordings");
    if (!recordings_section)
	return;

    MConfig::Section::iter recordings_iter (*recordings_section);
    while (!recordings_section->iter_done (recordings_iter)) {
	MConfig::SectionEntry * const recorder_entry = recordings_section->iter_next (recordings_iter);
	if (recorder_entry->getType() == MConfig::SectionEntry::Type_Section) {
	    MConfig::Section* const recorder_section = static_cast <MConfig::Section*> (recorder_entry);

	    ConstMemory const recorder_name = recorder_section->getName();
	    if (!recorder_name.len())
		logW_ (_func, "Unnamed recorder in section mod_gst/recordings");

	    Ref<String> channel_name;
	    Ref<String> playlist_filename;
	    {
		int num_set_opts = 0;

		{
		    MConfig::Option * const opt = recorder_section->getOption ("channel");
		    if (opt && opt->getValue()) {
			channel_name = opt->getValue()->getAsString();
			++num_set_opts;
		    }
		}

		{
		    MConfig::Option * const opt = recorder_section->getOption ("playlist");
		    if (opt && opt->getValue()) {
			playlist_filename = opt->getValue()->getAsString();
			++num_set_opts;
		    }
		}

		if (num_set_opts > 1) {
		    logW_ (_func, "Only one of channel/playlist "
			   "should be specified for recorder \"", recorder_name, "\"");
		}
	    }

	    Ref<String> record_path;
	    {
		MConfig::Option * const opt = recorder_section->getOption ("record_path");
		if (opt && opt->getValue())
		    record_path = opt->getValue()->getAsString();
	    }

	    if (channel_name && !channel_name->isNull()) {
		createChannelRecorder (recorder_name,
				       channel_name->mem(),
				       record_path ? record_path->mem() : ConstMemory());
	    } else
	    if (playlist_filename && !playlist_filename->isNull()) {
		createPlaylistRecorder (recorder_name,
					playlist_filename->mem(),
					record_path ? record_path->mem() : ConstMemory());
	    } else {
		logW_ (_func, "None of channel/playlist specified for recorder \"", recorder_name, "\"");
	    }
	}
    }
}

// TODO Always succeeds currently.
Result
MomentGstModule::init (MomentServer * const moment)
{
    this->moment = moment;
    this->timers = moment->getServerApp()->getServerContext()->getTimers();
    this->page_pool = moment->getPagePool();

  // Opening video streams.

    MConfig::Config * const config = moment->getConfig();

    {
	ConstMemory const opt_name = "mod_gst/send_metadata";
	MConfig::BooleanValue const val = config->getBoolean (opt_name);
	if (val == MConfig::Boolean_Invalid) {
	    logE_ (_func, "Invalid value for ", opt_name, ": ", config->getString (opt_name));
	    return Result::Failure;
	}

	if (val == MConfig::Boolean_True) {
	    send_metadata = true;
	} else {
	    send_metadata = false;
//	    logI_ (_func, "onMetaData messages will not be generated by mod_gst. "
//		   "Set \"", opt_name, "\" option to \"yes\" to enable sending of onMetaData messages.");
	}
    }

    {
	ConstMemory const opt_name = "mod_gst/prechunking";
	MConfig::BooleanValue const val = config->getBoolean (opt_name);
	if (val == MConfig::Boolean_Invalid) {
	    logE_ (_func, "Invalid value for ", opt_name, ": ", config->getString (opt_name));
	    return Result::Failure;
	}

	if (val == MConfig::Boolean_False) {
	    enable_prechunking = false;
	} else {
	    enable_prechunking = true;
	}

        logD_ (_func, opt_name, ": ", enable_prechunking);
    }

    {
	ConstMemory const opt_name = "mod_gst/keep_video_streams";
	MConfig::BooleanValue const val = config->getBoolean (opt_name);
	if (val == MConfig::Boolean_Invalid) {
	    logE_ (_func, "Invalid value for ", opt_name, ": ", config->getString (opt_name));
	    return Result::Failure;
	}

	if (val == MConfig::Boolean_True) {
	    keep_video_streams = true;
	} else {
	    keep_video_streams = false;
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
		opt_name, &default_height, default_height);
	if (!res) {
	    logE_ (_func, "bad value for ", opt_name);
	    return Result::Failure;
	}
    }

    {
	ConstMemory const opt_name = "mod_gst/bitrate";
	MConfig::GetResult const res = config->getUint64_default (
		opt_name, &default_bitrate, default_bitrate);
	if (!res) {
	    logE_ (_func, "bad value for ", opt_name);
	    return Result::Failure;
	}
    }

    {
	ConstMemory const opt_name = "mod_gst/no_video_timeout";
	Uint64 tmp_uint64;
	MConfig::GetResult const res = config->getUint64_default (
		opt_name, &tmp_uint64, no_video_timeout);
	if (!res) {
	    logE_ (_func, "bad value for ", opt_name);
	    return Result::Failure;
	}
	no_video_timeout = (Time) tmp_uint64;
    }

    {
        ConstMemory const opt_name = "mod_gst/connect_on_demand";
        MConfig::BooleanValue const val = config->getBoolean (opt_name);
        if (val == MConfig::Boolean_Invalid) {
            logE_ (_func, "Invalid value for ", opt_name, ": ", config->getString (opt_name));
            return Result::Failure;
        }

        if (val == MConfig::Boolean_True) {
            default_connect_on_demand = true;
        } else {
            default_connect_on_demand = false;
        }
    }

    {
        ConstMemory const opt_name = "mod_gst/connect_on_demand_timeout";
        Uint64 tmp_uint64;
        MConfig::GetResult const res = config->getUint64_default (
                opt_name, &tmp_uint64, default_connect_on_demand_timeout);
        if (!res) {
            logE_ (_func, "Invalid value for ", opt_name, ": ", config->getString (opt_name));
            return Result::Failure;
        }
        default_connect_on_demand_timeout = (Time) tmp_uint64;
    }

    {
	ConstMemory const opt_name = "moment/this_rtmpt_server_addr";
	ConstMemory const opt_val = config->getString (opt_name);
	logI_ (_func, opt_name, ": ", opt_val);
	if (!opt_val.isNull()) {
	    this_rtmpt_server_addr = grab (new String (opt_val));
	} else {
	    this_rtmpt_server_addr = grab (new String ("127.0.0.1:8081"));
	    logI_ (_func, opt_name, " config parameter is not set. "
		   "Defaulting to ", this_rtmpt_server_addr);
	}
    }

    moment->getAdminHttpService()->addHttpHandler (
	    CbDesc<HttpService::HttpHandler> (
		    &admin_http_handler, this /* cb_data */, NULL /* coderef_container */),
	    "moment_admin",
	    true    /* preassembly */,
	    1 << 20 /* 1 Mb */ /* preassembly_limit */,
	    true    /* parse_body_params */);

    moment->getHttpService()->addHttpHandler (
	    CbDesc<HttpService::HttpHandler> (
		    &http_handler, this /* cb_data */, NULL /* coderef_container */),
	    "moment_gst",
	    true /* preassembly */,
	    1 << 20 /* 1 Mb */ /* preassembly_limit */,
	    true /* parse_body_params */);

    parseSourcesConfigSection ();
    parseChainsConfigSection ();

    if (!parseStreamsConfigSection ())
        return Result::Failure;

    parseRecordingsConfigSection ();

    return Result::Success;
}

MomentGstModule::MomentGstModule()
    : moment (NULL),
      timers (NULL),
      page_pool (NULL),
      send_metadata (false),
      enable_prechunking (false),
      keep_video_streams (false),
      default_connect_on_demand (false),
      default_connect_on_demand_timeout (60),
      default_width (0),
      default_height (0),
      default_bitrate (500000),
      no_video_timeout (60)
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

    {
	RecorderEntryHash::iter iter (recorder_entry_hash);
	while (!recorder_entry_hash.iter_done (iter)) {
	    RecorderEntry * const recorder_entry = recorder_entry_hash.iter_next (iter);
	    delete recorder_entry;
	}
    }
}

} // namespace Moment

