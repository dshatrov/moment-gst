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


#include <moment-gst/playback.h>


using namespace M;
using namespace Moment;

namespace MomentGst {

static LogGroup libMary_logGroup_playback ("moment-gst_playback", LogLevel::I);

mt_unlocks_locks (mutex) void
Playback::advancePlayback ()
{
    logD (playback, _func_);

    if (advancing) {
	logD (playback, _func, "already advancing");
	return;
    }
    advancing = true;

    assert (got_next);
    bool first_iteration = true;
    while (got_next) {
	if (first_iteration) {
	    if (playback_timer) {
		logD (playback, _func, "Releasing playback timer");
		timers->deleteTimer (playback_timer);
		playback_timer = NULL;
	    }

	    first_iteration = false;

	    advance_ticket = grab (new AdvanceTicket (this));

	    logD (playback, _func, "calling frontend->stopItem");
	    if (frontend)
		mt_unlocks_locks (mutex) frontend.call_mutex (frontend->stopItem, mutex);
	} else {
	    assert (!playback_timer);
	}

	// Resetting 'got_next' after a call to frontend->stopItem.
	got_next = false;

	logD (playback, _func, "next_item: 0x", fmt_hex, (UintPtr) next_item);

	cur_item = next_item;
	if (next_item == NULL) {
	    next_item = playlist.getNextItem (NULL /* prv_item */,
					      getUnixtime(),
					      0 /* time_offset */,
					      &next_start_rel,
					      &next_seek,
					      &next_duration,
					      &next_duration_full);
	    if (next_item == NULL) {
		logD (playback, _func, "Empty playlist");
		goto _return;
	    }

	    {
		Time const unixtime = getUnixtime ();

		Time const min_duration = 10;
		Time const pause_time = 10;
		if (unixtime >= last_playlist_end_time
		    && unixtime - last_playlist_end_time < min_duration)
		{
		    logW_ (_func, "Playlist is shorter than ", min_duration, " seconds. "
			   "Pausing for ", pause_time, " seconds.");

		    playback_timer = timers->addTimer (
			    CbDesc<Timers::TimerCallback> (playbackTimerTick,
							   advance_ticket /* cb_data */,
							   getCoderefContainer() /* coderef_container */,
							   advance_ticket /* ref_data */),
			    pause_time,
			    false /* periodical */);
		    goto _return;
		}

		last_playlist_end_time = unixtime;
	    }

	    // Jumping to the start of the playlist.
	    got_next = true;
	    continue;
	}

	logD (playback, _func, "next_start_rel: ", next_start_rel);
	if (next_start_rel > 0) {
	    logD (playback, _func, "Setting playback timer to ", next_start_rel, " (next_start_rel)");
	    playback_timer = timers->addTimer (
		    CbDesc<Timers::TimerCallback> (playbackTimerTick,
						   advance_ticket /* cb_data */,
						   getCoderefContainer() /* coderef_container */,
						   advance_ticket /* ref_data */),
		    next_start_rel,
		    false /* periodical */);

	    next_start_rel = 0;
	    got_next = true;
	    goto _return;
	}

	if (!next_duration_full) {
	    logD (playback, _func, "Setting playback timer to ", next_duration, " (duration)");
	    playback_timer = timers->addTimer (
		    CbDesc<Timers::TimerCallback> (playbackTimerTick,
						   advance_ticket /* cb_data */,
						   getCoderefContainer() /* coderef_container */,
						   advance_ticket),
		    next_duration,
		    false /* periodical */);
	}

	logD (playback, _func, "Calling frontend->startItem");
	if (frontend) {
	    Ref<AdvanceTicket> const tmp_advance_ticket = advance_ticket;
	    frontend.call_mutex (frontend->startItem, mutex, next_item, next_seek, tmp_advance_ticket);
	}
    } // while (got_next)

_return:
    advancing = false;
}

void
Playback::playbackTimerTick (void * const _advance_ticket)
{
    AdvanceTicket * const advance_ticket = static_cast <AdvanceTicket*> (_advance_ticket);
    Playback * const self = advance_ticket->playback;

    logD (playback, _func_);

    self->mutex.lock ();
    if (self->advance_ticket != advance_ticket) {
	self->mutex.unlock ();
	return;
    }

    if (!self->got_next) {
	self->next_item = self->playlist.getNextItem (self->cur_item,
						      getUnixtime(),
						      0 /* time_offset */,
						      &self->next_start_rel,
						      &self->next_seek,
						      &self->next_duration,
						      &self->next_duration_full);
	self->got_next = true;
    }

    self->advancePlayback ();
    self->mutex.unlock ();
}

mt_mutex (mutex) void
Playback::doSetPosition (Playlist::Item * const item,
			 Time             const seek)
{
    logD (playback, _func_, ", seek: ", seek);

    Time duration = 0;
    if (!item->duration_full) {
	if (item->duration >= seek)
	    duration = item->duration - seek;
	else
	    duration = 0;
    }

    got_next = true;
    next_item = item;
    next_start_rel = 0;
    next_seek = seek;
    next_duration = duration;
    next_duration_full = item->duration_full || item->duration_default;

    advancePlayback ();
}

Result
Playback::doLoadPlaylist (ConstMemory   const src,
			  bool          const keep_cur_item,
			  Ref<String> * const ret_err_msg,
			  bool          const is_file)
{
    logD (playback, _func_);

    mutex.lock ();

    playlist.clear ();

    cur_item = NULL;

    Ref<String> err_msg;
    Result res;
    if (is_file)
	res = playlist.parsePlaylistFile (src, &err_msg);
    else
	res = playlist.parsePlaylistMem (src, &err_msg);

    if (!res) {
	mutex.unlock ();
	logE_ (_func, "parsePlaylistFile() failed: ", err_msg);

	if (ret_err_msg)
	    *ret_err_msg = makeString ("Playlist parsing error: ", err_msg->mem());

	return Result::Failure;
    }

    if (!keep_cur_item) {
	next_item = playlist.getNextItem (NULL /* prv_item */,
					  getUnixtime(),
					  0 /* time_offset */,
					  &next_start_rel,
					  &next_seek,
					  &next_duration,
					  &next_duration_full);
	got_next = true;

	advancePlayback ();
    }

    mutex.unlock ();

    return Result::Success;
}

void
Playback::advance (AdvanceTicket * const user_advance_ticket)
{
    logD (playback, _func_);

    mutex.lock ();
    if (user_advance_ticket != advance_ticket) {
	mutex.unlock ();
	return;
    }

    next_item = playlist.getNextItem (cur_item,
				      getUnixtime(),
				      0 /* time_offset */,
				      &next_start_rel,
				      &next_seek,
				      &next_duration,
				      &next_duration_full);
    got_next = true;

    advancePlayback ();
    mutex.unlock ();
}

Result
Playback::setPosition_Id (ConstMemory const id,
			  Time        const seek)
{
    mutex.lock ();

    Playlist::Item * const item = playlist.getItemById (id);
    if (!item) {
	mutex.unlock ();
	logE_ (_func, "Item with id \"", id, "\" not found");
	return Result::Failure;
    }

    doSetPosition (item, seek);
    mutex.unlock ();

    return Result::Success;
}

Result
Playback::setPosition_Index (Count const idx,
			     Time  const seek)
{
    mutex.lock ();

    Playlist::Item * const item = playlist.getNthItem (idx);
    if (!item) {
	mutex.unlock ();
	logE_ (_func, "Item #", idx, " not found");
	return Result::Failure;
    }

    doSetPosition (item, seek);
    mutex.unlock ();

    return Result::Success;
}

void
Playback::setSingleItem (ConstMemory const stream_spec,
			 bool        const is_chain)
{
    logD (playback, _func_);

    mutex.lock ();
    playlist.clear ();
    playlist.setSingleItem (stream_spec, is_chain);

    next_item = playlist.getNextItem (NULL /* prv_item */,
				      getUnixtime(),
				      0 /* time_offset */,
				      &next_start_rel,
				      &next_seek,
				      &next_duration,
				      &next_duration_full);
    got_next = true;

    advancePlayback ();
    mutex.unlock ();
}

void
Playback::setSingleChannelRecorder (ConstMemory const channel_name)
{
    logD (playback, _func_);

    mutex.lock ();
    playlist.clear ();
    playlist.setSingleChannelRecorder (channel_name);

    next_item = playlist.getNextItem (NULL /* prv_item */,
				      getUnixtime(),
				      0 /* time_offset */,
				      &next_start_rel,
				      &next_seek,
				      &next_duration,
				      &next_duration_full);
    got_next = true;

    advancePlayback ();
    mutex.unlock ();
}

Result
Playback::loadPlaylistFile (ConstMemory   const filename,
			    bool          const keep_cur_item,
			    Ref<String> * const ret_err_msg)
{
    return doLoadPlaylist (filename, keep_cur_item, ret_err_msg, true /* is_file */);
}

Result
Playback::loadPlaylistMem (ConstMemory    const mem,
			   bool           const keep_cur_item,
			    Ref<String> * const ret_err_msg)
{
    return doLoadPlaylist (mem, keep_cur_item, ret_err_msg, false /* is_file */);
}

void
Playback::init (Timers * const timers)
{
    this->timers = timers;
}

Playback::Playback (Object * const coderef_container)
    : DependentCodeReferenced (coderef_container),

      timers (NULL),

      cur_item (NULL),

      playback_timer (NULL),

      got_next (false),
      next_item (NULL),
      next_start_rel (0),
      next_seek (0),
      next_duration (0),
      next_duration_full (false),

      last_playlist_end_time (0),

      advancing (false)
{
}

Playback::~Playback ()
{
    mutex.lock ();

    if (playback_timer) {
	timers->deleteTimer (playback_timer);
	playback_timer = NULL;
    }

    mutex.unlock ();
}

}

