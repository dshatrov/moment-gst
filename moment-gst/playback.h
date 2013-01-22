/*  Moment-Gst - GStreamer support module for Moment Video Server
    Copyright (C) 2011-2013 Dmitry Shatrov

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


#ifndef MOMENT_GST__PLAYBACK__H__
#define MOMENT_GST__PLAYBACK__H__


#include <libmary/libmary.h>

#include <moment-gst/playlist.h>


namespace MomentGst {

using namespace M;
using namespace Moment;

class Playback : public DependentCodeReferenced
{
private:
    StateMutex mutex;

public:
    class AdvanceTicket : public Referenced
    {
	friend class Playback;

    private:
	Playback * const playback;
	AdvanceTicket (Playback * const playback) : playback (playback) {}
    };

    struct Frontend
    {
	void (*startItem) (Playlist::Item *item,
			   Time            seek,
			   AdvanceTicket  *advance_ticket,
			   void           *cb_data);

	void (*stopItem) (void *cb_data);
    };

private:
    mt_const Cb<Frontend> frontend;

    mt_const Timers *timers;

    mt_const Uint64 min_playlist_duration_sec;

    mt_mutex (mutex)
    mt_begin

      Playlist playlist;

      Playlist::Item *cur_item;

      Timers::TimerKey playback_timer;

      bool got_next;
      Playlist::Item *next_item;
      Time next_start_rel;
      Time next_seek;
      Time next_duration;
      bool next_duration_full;

      Time last_playlist_end_time;

      Ref<AdvanceTicket> advance_ticket;

      bool advancing;

    mt_end

    mt_unlocks_locks (mutex) void advancePlayback ();

    static void playbackTimerTick (void *_advance_ticket);

    mt_mutex (mutex) void doSetPosition (Playlist::Item *item,
					 Time            seek);

    Result doLoadPlaylist (ConstMemory  src,
			   bool         keep_cur_item,
			   Ref<String> *ret_err_msg,
			   bool         is_file);

public:
    void advance (AdvanceTicket *user_advance_ticket);

    Result setPosition_Id (ConstMemory id,
			   Time        seek);

    Result setPosition_Index (Count idx,
			      Time  seek);

    void setSingleItem (ConstMemory stream_spec,
			bool        is_chain,
                        bool        force_transcode);

    void setSingleChannelRecorder (ConstMemory channel_name);

    Result loadPlaylistFile (ConstMemory  filename,
			     bool         keep_cur_item,
			     Ref<String> *ret_err_msg);

    Result loadPlaylistMem (ConstMemory  mem,
			    bool         keep_cur_item,
			    Ref<String> *ret_err_msg);

    mt_const void setFrontend (CbDesc<Frontend> const &frontend)
    {
	this->frontend = frontend;
    }

    mt_const void init (Timers *timers,
                        Uint64  min_playlist_duration_sec);

    Playback (Object *coderef_container);

    ~Playback ();
};

}


#endif /* MOMENT_GST__PLAYBACK__H__ */

