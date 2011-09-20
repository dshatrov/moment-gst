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


#include <moment/libmoment.h>

#include <moment-gst/gst_stream.h>
#include <moment-gst/playlist.h>


namespace MomentGst {

using namespace M;
using namespace Moment;

class MomentGstModule : public Object
{
private:
    class Playback : public HashEntry<>,
		     public Object
    {
    public:
	mt_const WeakRef<MomentGstModule> weak_module;
	mt_const Timers *timers;

	mt_const Ref<String> stream_name;

	mt_const Ref<GstStream> stream;

	mt_mutex (mutex) Playlist playlist;

	mt_mutex (mutex) Playlist::Item *cur_item;
	mt_mutex (mutex) Time cur_item_start_time;

	mt_mutex (mutex) Timers::TimerKey playback_timer;
    };

    typedef Hash< Playback,
		  Memory,
		  MemberExtractor< Playback,
				   Ref<String>,
				   &Playback::stream_name,
				   Memory,
				   AccessorExtractor< String,
						      Memory,
						      &String::mem > >,
		  MemoryComparator<> >
	    PlaybackHash;

    mt_const MomentServer *moment;
    mt_const Timers *timers;
    mt_const PagePool *page_pool;

    mt_const bool send_metadata;

    mt_const Uint64 default_width;
    mt_const Uint64 default_height;
    mt_const Uint64 default_bitrate;

    mt_mutex (mutex) PlaybackHash playback_hash;

    typedef IntrusiveList<GstStream> StreamList;
    mt_mutex (mutex) StreamList stream_list;

    void parseSourcesConfigSection ();
    void parseChainsConfigSection ();
    void parseStreamsConfigSection ();

    void createPlayback (ConstMemory stream_name,
			 ConstMemory playlist_filename,
			 bool        recording,
			 ConstMemory ecord_filename);

    static void playbackTimerTick (void *_playback);

    void createStream (ConstMemory stream_name,
		       ConstMemory stream_spec,
		       bool        is_chain,
		       bool        recording,
		       ConstMemory record_filename);

public:
    Result init (MomentServer *moment);

    MomentGstModule ();

    ~MomentGstModule ();
};

}


#endif /* __MOMENT_GST__MOMENT_GST_MODULE__H__ */

