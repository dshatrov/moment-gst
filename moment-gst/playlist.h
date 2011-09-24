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


#ifndef __MOMENT_GST__PLAYLIST__H__
#define __MOMENT_GST__PLAYLIST__H__


#include <moment/libmoment.h>


namespace MomentGst {

using namespace M;
using namespace Moment;

mt_unsafe class Playlist
{
public:
    class Item : public IntrusiveListElement<>,
		 public HashEntry<>
    {
    public:
	// Valid if 'start_immediate' is false.
	Time start_time;
	bool start_immediate;

	// Valid if 'got_end_time' is true.
	Time end_time;
	bool got_end_time;

	// Valid if both 'duration_full' and 'duration_default' are false.
	Time duration;
	bool duration_full;
	bool duration_default;

	Time seek;

	Ref<String> chain_spec;
	Ref<String> uri;

	Ref<String> id;

	void reset ()
	{
	    start_time = 0;
	    start_immediate = true;

	    end_time = 0;
	    got_end_time = false;

	    duration = 0;
	    duration_full = false;
	    duration_default = true;

	    seek = 0;
	}

	Item ()
	{
	    reset ();
	}
    };

    typedef IntrusiveList<Item> ItemList;

    typedef Hash< Item,
		  Memory,
		  MemberExtractor< Item,
				   Ref<String>,
				   &Item::id,
				   Memory,
				   AccessorExtractor< String,
						      Memory,
						      &String::mem > >,
		  MemoryComparator<> >
	    ItemHash;

    ItemList item_list;
    ItemHash item_hash;

    void doParsePlaylist (xmlDocPtr doc);

    static void parseItem (xmlDocPtr   doc,
			   xmlNodePtr  media_node,
			   Item       * mt_nonnull item);

    static void parseItemAttributes (xmlNodePtr      node,
				     Playlist::Item * mt_nonnull item);

public:
    // @cur_time - Current unixtime.
    Item* getNextItem (Item   *prv_item,
		       Time    cur_time,
		       Int64   time_offset,
		       Time   * mt_nonnull ret_start_rel,
		       Time   * mt_nonnull ret_seek,
		       Time   * mt_nonnull ret_duration,
		       bool   * mt_nonnull ret_duration_full);

    Item* getItemById (ConstMemory id);

    Item* getNthItem (Count idx);

    void clear ();

    mt_throws Result parsePlaylistFile (ConstMemory  filename,
					Ref<String> *ret_err_msg);

    mt_throws Result parsePlaylistMem (ConstMemory  mem,
				       Ref<String> *ret_err_msg);

    ~Playlist ();
};

}


#endif /* __MOMENT_GST__PLAYLIST__H__ */

