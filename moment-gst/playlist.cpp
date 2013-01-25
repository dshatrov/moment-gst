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
#include <time.h>
#include <glib.h>

#include <libmary/libmary.h>
#include <libmary/libmary_thread_local.h>

#include <moment-gst/playlist.h>


using namespace M;
using namespace Moment;

static LogGroup libMary_logGroup_playlist ("mod_gst.playlist", LogLevel::D);

namespace MomentGst {

Playlist::Item*
Playlist::getNextItem (Item  * const prv_item,
		       Time    const cur_time,
		       Int64   const time_offset,
		       Time  * const mt_nonnull ret_start_rel,
		       Time  * const mt_nonnull ret_seek,
		       Time  * const mt_nonnull ret_duration,
		       bool  * const mt_nonnull ret_duration_full)
{
    logD (playlist, _func, "cur_time: ", cur_time, ", time_offset: ", time_offset);

    Item *item = prv_item;
    for (;;) {
	if (item)
	    item = ItemList::getNext (item);
	else {
	    logD (playlist, _func, "First item");
	    item = item_list.getFirst();
	}

	if (!item) {
	    logD (playlist, _func, "No next item");
	    break;
	}

	logD (playlist, _func, "item: 0x", fmt_hex, (UintPtr) item);

	logD (playlist, _func, "start_time: ", item->start_time, ", "
	      "start_immediate: ", item->start_immediate ? "true" : "false", ", "
	      "end_time: ", item->end_time, ", "
	      "got_end_time: ", item->got_end_time ? "true" : "false", ", "
	      "duration: ", item->duration, ", "
	      "duration_full: ", item->duration_full ? "true" : "false", ", "
	      "duration_default: ", item->duration_default ? "true" : "false");
	logD (playlist, _func, "chain_spec: ", item->chain_spec);
	logD (playlist, _func, "uri: ", item->uri);

	if (item->start_immediate) {
	    *ret_start_rel = 0;
	    *ret_seek = item->seek;

	    if (item->duration_full) {
		*ret_duration = (Time) -1;
		*ret_duration_full = item->duration_full;
		return item;
	    }

	    bool got_duration_limit = false;
	    Time duration_limit = (Time) -1;
	    if (!item->duration_default) {
		duration_limit = item->duration;
		got_duration_limit = true;
	    }

	    if (item->got_end_time) {
		Int64 const end_rel = (Int64) item->end_time - ((Int64) cur_time + time_offset);
		logD (playlist, _func, "end_rel: ", end_rel);

		if (end_rel <= 0)
		    continue;

		Time new_duration_limit = (Time) end_rel;
		if (got_duration_limit) {
		    if (new_duration_limit < duration_limit)
			duration_limit = new_duration_limit;
		} else {
		    duration_limit = new_duration_limit;
		}

		got_duration_limit = true;
	    }

	    if (got_duration_limit) {
		*ret_duration = duration_limit;
		*ret_duration_full = false;
	    } else {
		*ret_duration = (Time) -1;
		*ret_duration_full = true;
	    }

	    return item;
	}

	Int64 const start_rel = (Int64) item->start_time - ((Int64) cur_time + time_offset);
	logD (playlist, _func, "start_rel: ", start_rel);
	if (start_rel >= 0) {
	    *ret_start_rel = (Time) start_rel;
	    *ret_seek = item->seek;
	} else {
	    *ret_start_rel = 0;
	    *ret_seek = (Time) -start_rel + item->seek;
	}

	if (item->duration_full) {
	    *ret_duration = (Time) -1;
	    *ret_duration_full = true;
	    return item;
	}

	bool got_duration_limit = false;
	Time duration_limit = (Time) -1;
	if (!item->duration_default) {
	    if (start_rel >= 0) {
		duration_limit = item->duration;
		got_duration_limit = true;
	    } else {
		if ((Time) -start_rel >= item->duration)
		    continue;

		duration_limit = item->duration - ((Time) -start_rel);
		got_duration_limit = true;
	    }
	}

	if (item->got_end_time) {
	    Int64 const end_rel = (Int64) item->end_time - ((Int64) cur_time + time_offset);

	    if (item->end_time <= item->start_time ||
		end_rel <= 0)
	    {
		continue;
	    }

	    Time new_duration_limit;
	    if (start_rel <= 0)
		new_duration_limit = end_rel;
	    else
		new_duration_limit = item->end_time - item->start_time;

	    if (got_duration_limit) {
		if (new_duration_limit < duration_limit)
		    duration_limit = new_duration_limit;
	    } else {
		duration_limit = new_duration_limit;
	    }

	    got_duration_limit = true;
	}

	if (got_duration_limit) {
	    *ret_duration = duration_limit;
	    *ret_duration_full = false;
	} else {
	    *ret_duration = (Time) -1;
	    *ret_duration_full = true;
	}

	return item;
    }

  // End of playlist.

    logD (playlist, _func, "Returning NULL");

    *ret_start_rel = 0;
    *ret_seek = 0;
    *ret_duration = 0;
    *ret_duration_full = false;
    return NULL;
}

Playlist::Item*
Playlist::getItemById (ConstMemory id)
{
    return item_hash.lookup (id);
}

Playlist::Item*
Playlist::getNthItem (Count const idx)
{
    Item *item = NULL;

    ItemList::iter iter (item_list);
    for (Count i = 0; i < idx; ++i) {
	if (item_list.iter_done (iter))
	    return NULL;

	item = item_list.iter_next (iter);
    }

    return item;
}

void
Playlist::clear ()
{
    ItemList::iter iter (item_list);
    while (!item_list.iter_done (iter)) {
	Item * const item = item_list.iter_next (iter);
	delete item;
    }
    item_list.clear ();

    item_hash.clear ();
}

static xmlNodePtr firstXmlElementNode (xmlNodePtr node)
{
    for (;;) {
	if (node == NULL)
	    return NULL;

	if (node->type == XML_ELEMENT_NODE)
	    break;

	node = node->next;
    }

    return node;
}

void
Playlist::doParsePlaylist (xmlDocPtr doc)
{
    xmlNodePtr root_node = xmlDocGetRootElement (doc);
    if (!root_node)
	return;

    xmlNodePtr cur_node = firstXmlElementNode (root_node->children);
    while (cur_node) {
	ConstMemory const cur_node_name ((Byte const *) cur_node->name,
					 cur_node->name ? strlen ((char const *) cur_node->name) : 0);

	logD (playlist, _func, "cur_node_name: ", cur_node_name);

	if (equal (cur_node_name, "item")) {
	    Item * const item = new Item;
	    item_list.append (item);
	    parseItem (doc, cur_node, item);
	    if (item->id)
		item_hash.add (item);
	} else {
	    logW_ (_func, "Unknown subnode of <playlist>: ", cur_node_name);
	}

	cur_node = firstXmlElementNode (cur_node->next);
    }
}

void
Playlist::parseItem (xmlDocPtr   doc,
		     xmlNodePtr  item_node,
		     Item       * const mt_nonnull item)
{
    ConstMemory chain;
    ConstMemory uri;
    ConstMemory path;

    xmlNodePtr cur_node = firstXmlElementNode (item_node->children);
    while (cur_node) {
	ConstMemory const cur_node_name ((Byte const *) cur_node->name,
					 cur_node->name ? strlen ((char const *) cur_node->name) : 0);

	logD (playlist, _func, "cur_node_name: ", cur_node_name);

	if (equal (cur_node_name, "chain")) {
	    xmlChar * const val_str = xmlNodeListGetString (doc, cur_node->children, 1 /* inLine */);
	    if (val_str)
		chain = ConstMemory ((Byte const *) val_str, strlen ((char const *) val_str));
	} else
	if (equal (cur_node_name, "uri")) {
	    xmlChar * const val_str = xmlNodeListGetString (doc, cur_node->children, 1 /* inLine */);
	    if (val_str)
		uri = ConstMemory ((Byte const *) val_str, strlen ((char const *) val_str));
	} else
	if (equal (cur_node_name, "path")) {
	    xmlChar * const val_str = xmlNodeListGetString (doc, cur_node->children, 1 /* inLine */);
	    if (val_str)
		path = ConstMemory ((Byte const *) val_str, strlen ((char const *) val_str));
	} else {
	    logW_ (_func, "Unknown subnode of <item>: ", cur_node_name);
	}

	cur_node = firstXmlElementNode (cur_node->next);
    }

    {
	int i = 0;

	if (chain.len())
	    ++i;

	if (uri.len())
	    ++i;

	if (path.len())
	    ++i;

	if (i > 1)
	    logW_ (_func, "Only one of chain/uri/path should be specified.");
    }

    if (chain.len())
	item->chain_spec = grab (new String (chain));
    else
    if (uri.len())
	item->uri = grab (new String (uri));
    else
    if (path.len())
	item->uri = makeString ("file://", path);

    logD (playlist, _func, "Parsing attributes");
    parseItemAttributes (item_node, item);
}

static Result parseTime (xmlChar * const mt_nonnull time_str,
			 Time    * const mt_nonnull ret_time)
{
#if 0
// Glib's parsing of iso8601 dates is of little use, since it doesn't accept
// time without a date.

    GTimeVal time_val;
    if (g_time_val_from_iso8601 ((gchar const *) time_str, &time_val)) {
	// Time in milliseconds
	*ret_time = time_val.tv_sec * 1000 + time_val.tv_usec / 1000;
    } else {
	*ret_time = 0;
	logE_ (_func, "Couldn't parse date/time value: ",
	       ConstMemory ((Byte const *) time_str, strlen ((char const *) time_str)));
	return Result::Failure;
    }

    logD (playlist, _func, ConstMemory ((Byte const *) time_str, strlen ((char const *) time_str)),
	  " { ", time_val.tv_sec, ", ", time_val.tv_usec, " }");
#endif

    LibMary_ThreadLocal * const tlocal = libMary_getThreadLocal();

    Uint32 year  = tlocal->localtime.tm_year + 1900;
    Uint32 month = tlocal->localtime.tm_mon;
    Uint32 day   = tlocal->localtime.tm_mday;

    Uint32 hour    = 0;
    Uint32 minute  = 0;
    Uint32 seconds = 0;

    char const *str = (char const *) time_str;

    bool got_date = false;
    bool got_time = false;

    while (*str == ' ')
	++str;

    for (int i = 0; i < 2; ++i) {
	Uint32 num [3];
	char separator = ' ';

	int j;
	for (j = 0; j < 3; ++j) {
	    while (*str == ' ')
		++str;

	    if (!strToUint32 (str, &num [j], &str, 10 /* base */)) {
		if (j > 0) {
		    logE_ (_func, "Bad date/time format: ",
			   ConstMemory ((Byte const *) time_str, strlen ((char const *) time_str)));
		    return Result::Failure;
		}

		break;
	    }

	    while (*str == ' ')
		++str;

	    if (j == 0) {
		separator = *str;
		if (separator != ':' &&
		    separator != '/')
		{
		    logE_ (_func, "Bad separator in date/time value: ",
			   ConstMemory ((Byte const *) time_str, strlen ((char const *) time_str)));
		    return Result::Failure;
		}
		++str;
	    } else
	    if (j == 1) {
		if (*str != separator) {
		    break;
		}
		++str;
	    }
	}

	if (j > 0) {
	    if (separator == '/') {
		if (got_time) {
		    logE_ (_func, "Duplicate time in date/time value: ",
			   ConstMemory ((Byte const *) time_str, strlen ((char const *) time_str)));
		    return Result::Failure;
		}
		got_time = true;

		switch (j) {
		    case 0:
			break;
		    case 1:
			// TODO Year: either the current year or the next year, depending on current yy/mm.
			month = num [0];
			day   = num [1];
			break;
		    case 2:
			unreachable ();
			break;
		    case 3:
			year  = num [0];
			month = num [1];
			day   = num [2];
		}

		if (month > 0)
		    --month;
	    } else
	    if (separator == ':') {
		if (got_date) {
		    logE_ (_func, "Duplicate date in date/time value: ",
			   ConstMemory ((Byte const *) time_str, strlen ((char const *) time_str)));
		    return Result::Failure;
		}
		got_date = true;

		switch (j) {
		    case 0:
			break;
		    case 1:
			hour   = num [0];
			minute = num [1];
			break;
		    case 2:
			unreachable ();
			break;
		    case 3:
			hour    = num [0];
			minute  = num [1];
			seconds = num [2];
		}
	    } else {
		unreachable ();
	    }
	}

	while (*str == ' ')
	    ++str;
    }

    while (*str == ' ')
	++str;

    if (*str) {
	logE_ (_func, "Couldn't parse date/time value: ",
	       ConstMemory ((Byte const *) time_str, strlen ((char const *) time_str)));
	return Result::Failure;
    }

    logD (playlist, _func, year, "/", month + 1, "/", day, " ", hour, ":", minute, ":", seconds);

    if (!got_date && !got_time) {
	logE_ (_func, "Empty date/time");
	return Result::Failure;
    }

    {
#if 0
// g_date_time_to_unix() appears to produce incorrect results.
// Note also that it is bound to UTC somehow.
	GDateTime * const date_time = g_date_time_new_local (year, month, day, hour, minute, seconds);
	*ret_time = g_date_time_to_unix (date_time);
	g_date_time_unref (date_time);
#endif

	{
	  // mktime() gives good results.

	    struct tm t;
	    memset (&t, 0, sizeof (t));
	    t.tm_sec   = seconds;
	    t.tm_min   = minute;
	    t.tm_hour  = hour;
	    t.tm_mday  = day;
	    t.tm_mon   = month;
	    t.tm_year  = year >= 1900 ? year - 1900 : 0;
	    t.tm_isdst = -1 /* auto-detect DST */;

	    time_t const res_time = mktime (&t);
	    if (res_time == (time_t) -1) {
		logE_ (_func, "Couldn't convert date/time to unixtime: ",
		       ConstMemory ((Byte const *) time_str, strlen ((char const *) time_str)));
		return Result::Failure;
	    }

	    *ret_time = res_time;
	}
    }

    logD (playlist, _func, "ret_time: ", *ret_time);

    return Result::Success;
}

void
Playlist::parseItemAttributes (xmlNodePtr  node,
			       Item       * const mt_nonnull item)
{
    {
	xmlChar * const id_val = xmlGetProp (node, (xmlChar const *) "id");
	if (id_val)
	    item->id = grab (new String ((char const *) id_val));
    }

    {
	item->start_time = 0;
	item->start_immediate = true;

	xmlChar * const start_val = xmlGetProp (node, (xmlChar const *) "start");
	if (start_val) {
	    item->start_immediate = false;
	    if (!parseTime (start_val, &item->start_time))
		item->start_immediate = true;
	} else {
	    item->start_time = 0;
	    item->start_immediate = true;
	}
    }

    {
	item->end_time = 0;
	item->got_end_time = false;

	xmlChar * const end_val = xmlGetProp (node, (xmlChar const *) "end");
	if (end_val) {
	    logD (playlist, _func, "end_val: ", ConstMemory ((Byte const *) end_val, strlen ((char const *) end_val)));
	    item->got_end_time = true;
	    if (!parseTime (end_val, &item->end_time))
		item->got_end_time = false;
	} else {
	    item->end_time = 0;
	    item->got_end_time = false;
	}
    }

    {
	item->duration_default = true;
	item->duration_full = false;
	item->duration = (Time) -1;

	xmlChar * const duration_val = xmlGetProp (node, (xmlChar const *) "duration");
	if (duration_val) {
	    ConstMemory const duration_mem ((Byte const *) duration_val, strlen ((char const *) duration_val));
	    if (equal (duration_mem, "full")) {
		logD (playlist, _func, "duration=\"full\"");
		item->duration_default = false;
		item->duration_full = true;
		item->duration = (Time) -1;
	    } else {
		item->duration_full = false;
		item->duration_default = false;
		if (!parseDuration (duration_mem, &item->duration)) {
		    item->duration_default = true;
		    item->duration_full = false;
		    item->duration = (Time) -1;
		    logE_ (_func, "Couldn't parse duration: ", duration_mem);
		}
	    }
	} else {
	    logD (playlist, _func, "No duration");
	    item->duration_default = true;
	    item->duration_full = false;
	    item->duration = (Time) -1;
	}
    }

    {
	item->seek = 0;

	xmlChar const * seek_val = xmlGetProp (node, (xmlChar const *) "seek");
	if (seek_val) {
	    ConstMemory const seek_mem ((Byte const *) seek_val, strlen ((char const *) seek_val));
	    if (!parseDuration (seek_mem, &item->seek)) {
		item->seek = 0;
		logE_ (_func, "Couldn't parse seek: ", seek_mem);
	    }
	} else {
	    item->seek = 0;
	}
    }

#if 0
    {
	xmlChar * const class_val = xmlGetProp (node, (xmlChar const *) "class");
	if (class_val)
	    item->class_name = grab (new String (
		    ConstMemory ((Byte const *) class_val, strlen ((char const *) class_val))));
    }
#endif
}

static void reportXmlParsingError (ConstMemory   const filename,
				   Ref<String> * const ret_err_msg)
{
    xmlErrorPtr err = xmlGetLastError();
    if (err) {
	ConstMemory const err_msg_mem (err->message, err->message ? strlen (err->message) : 0);
	logE_ (_func, "Playlist parsing error",
	       filename.len() ? " (file " : "", filename, filename.len() ? ")" : "",
	       err_msg_mem.len() ? ": " : "", err_msg_mem);
	if (ret_err_msg)
	    *ret_err_msg = grab (new String (err_msg_mem));

	xmlResetError (err);
    } else {
	logE_ (_func, "Could not parse playlist", filename.len() ? " file " : "", filename);
	if (ret_err_msg)
	    *ret_err_msg = NULL;
    }
}

void
Playlist::setSingleItem (ConstMemory const stream_spec,
			 bool        const is_chain,
                         bool        const force_transcode,
                         bool        const force_transcode_audio,
                         bool        const force_transcode_video)
{
    Item * const item = new (std::nothrow) Item;
    assert (item);
    item->force_transcode = force_transcode;
    item->force_transcode_audio = force_transcode_audio;
    item->force_transcode_video = force_transcode_video;

    if (is_chain)
	item->chain_spec = grab (new String (stream_spec));
    else
	item->uri = grab (new String (stream_spec));

    item_list.append (item);
}

void
Playlist::setSingleChannelRecorder (ConstMemory const channel_name)
{
    Item * const item = new Item;
    item->id = grab (new String (channel_name));
    item_list.append (item);
}

mt_throws Result
Playlist::parsePlaylistFile (ConstMemory   const filename,
			     Ref<String> * const ret_err_msg)
{
    Byte filename_cstr [filename.len() + 1];
    memcpy (filename_cstr, filename.mem(), filename.len());
    filename_cstr [filename.len()] = 0;

    xmlDocPtr doc = xmlReadFile ((char const *) filename_cstr, NULL /* encoding */, XML_PARSE_NONET);
    if (!doc) {
	reportXmlParsingError (filename, ret_err_msg);
	exc_throw (InternalException, InternalException::BackendError);
	return Result::Failure;
    }

    doParsePlaylist (doc);

    xmlFreeDoc (doc);
    return Result::Success;
}

mt_throws Result
Playlist::parsePlaylistMem (ConstMemory   const mem,
			    Ref<String> * const ret_err_msg)
{
    xmlDocPtr doc = xmlReadMemory (
	    (char const *) mem.mem(), mem.len(), "" /* URL */, NULL /* encoding */, XML_PARSE_NONET);
    if (!doc) {
	reportXmlParsingError (ConstMemory() /* filename */, ret_err_msg);
	exc_throw (InternalException, InternalException::BackendError);
	return Result::Failure;
    }

    doParsePlaylist (doc);

    xmlFreeDoc (doc);
    return Result::Success;
}

Playlist::~Playlist ()
{
    clear ();
}

}

