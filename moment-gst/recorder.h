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


#ifndef __MOMENT_GST__RECORDER__H__
#define __MOMENT_GST__RECORDER__H__


#include <moment/libmoment.h>

#include <moment-gst/playback.h>
#include <moment-gst/channel.h>
#include <moment-gst/channel_set.h>


namespace MomentGst {

using namespace M;
using namespace Moment;

class Recorder : public Object
{
private:
    StateMutex mutex;

    class Recording : public Referenced
    {
    public:
	Recorder *recorder;
    };

    mt_const MomentServer *moment;
    mt_const ChannelSet *channel_set;
    mt_const Ref<String> filename_prefix;
    mt_const ServerThreadContext *recorder_thread_ctx;

    mt_async Playback playback;

    mt_async AvRecorder recorder;
    mt_async FlvMuxer flv_muxer;

    mt_mutex (mutex) bool recording_now;
    mt_mutex (mutex) Ref<Recording> cur_recording;
    mt_mutex (mutex) Ref<String> cur_channel_name;
    mt_mutex (mutex) WeakRef<Channel> weak_cur_channel;
    mt_mutex (mutex) WeakRef<VideoStream> weak_cur_video_stream;
    mt_mutex (mutex) GenericInformer::SubscriptionKey channel_sbn;

    mt_iface (Playback::Frontend)
    mt_begin

      static Playback::Frontend playback_frontend;

      static void startPlaybackItem (Playlist::Item          *item,
				     Time                     seek,
				     Playback::AdvanceTicket *advance_ticket,
				     void                    *_self);

      static void stopPlaybackItem (void *_self);

    mt_end

    mt_iface (Channel::ChannelEvents)
    mt_begin

      static Channel::ChannelEvents channel_events;

      static void startChannelItem (void *_recording);

      static void stopChannelItem (void *_recording);

      static void newVideoStream (void *_recording);

    mt_end

    mt_mutex (mutex) void doStartItem ();

    mt_mutex (mutex) void doStopItem ();

public:
    void setSingleChannel (ConstMemory const channel_name)
    {
	playback.setSingleChannelRecorder (channel_name);
    }

    Result loadPlaylistFile (ConstMemory   const filename,
			     bool          const keep_cur_item,
			     Ref<String> * const ret_err_msg)
    {
	return playback.loadPlaylistFile (filename, keep_cur_item, ret_err_msg);
    }

    Result loadPlaylistMem (ConstMemory   const mem,
			    bool          const keep_cur_item,
			    Ref<String> * const ret_err_msg)
    {
	return playback.loadPlaylistMem (mem, keep_cur_item, ret_err_msg);
    }

    mt_const void init (MomentServer *moment,
			PagePool     *page_pool,
			ChannelSet   *channel_set,
			ConstMemory   filename_prefix);

    Recorder ();

    ~Recorder ();
};

}


#endif /* __MOMENT_GST__RECORDER__H__ */

