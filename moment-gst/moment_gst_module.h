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
    class StreamTicket : public Referenced
    {
    };

    // TODO Playback should be a separate object, not a nested class.
    class Playback : public HashEntry<>,
		     public Object
    {
    public:
	mt_const WeakRef<MomentGstModule> weak_module;
	mt_const Timers *timers;

	mt_const Ref<String> stream_name;
	mt_const Ref<String> playlist_filename;

	mt_const Ref<GstStream> stream;

	mt_mutex (mutex)
	mt_begin

	  Playlist playlist;

	  Playlist::Item *cur_item;
	  // TODO Unused
	  Time cur_item_start_time;

	  Ref<StreamTicket> cur_stream_ticket;

	  Timers::TimerKey playback_timer;

	  bool delayed_start;
	  Playlist::Item *delayed_item;
	  Time delayed_seek;
	  Time delayed_duration;
	  bool delayed_duration_full;

	mt_end
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

    class Channel : public HashEntry<>,
		    public Referenced
    {
    public:
	enum Type {
	    Type_Playback,
	    Type_Stream
	};

    private:
	Type const type;

    public:
	Type getType() const
	{
	    return type;
	}

	Ref<String> channel_name;

	Channel (Type const type)
	    : type (type)
	{
	}
    };

    typedef Hash< Channel,
		  Memory,
		  MemberExtractor< Channel,
				   Ref<String>,
				   &Channel::channel_name,
				   Memory,
				   AccessorExtractor< String,
						      Memory,
						      &String::mem > >,
		  MemoryComparator<> >
	    ChannelHash;

    class Channel_Playback : public Channel
    {
    public:
	Playback *playback;

	Channel_Playback ()
	    : Channel (Channel::Type_Playback)
	{
	}
    };

    class Channel_Stream : public Channel
    {
    public:
	GstStream *stream;

	Channel_Stream ()
	    : Channel (Channel::Type_Stream)
	{
	}
    };

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

    mt_mutex (mutex) ChannelHash channel_hash;

    void parseSourcesConfigSection ();
    void parseChainsConfigSection ();
    void parseStreamsConfigSection ();

    void createPlayback (ConstMemory stream_name,
			 ConstMemory playlist_filename,
			 bool        recording,
			 ConstMemory ecord_filename);

    mt_mutex (playback->mutex) static void stopCurItemPlayback (Playback *playback);

    mt_mutex (playback->mutex) static void advancePlayback (Playback *playback);

    mt_mutex (playback->mutex) static void startPlayback (Playback       *playback,
							  Playlist::Item *item,
							  Time            seek,
							  Time            duration,
							  bool            duration_full);

    static void playbackTimerTick (void *_playback);

    Result updatePlaylist (ConstMemory  playlist_name,
			   bool         keep_cur_item,
			   Ref<String> * mt_nonnull ret_err_msg);

    Result setPosition (ConstMemory channel_name,
			ConstMemory item_name,
			bool        item_name_is_id,
			ConstMemory seek_str);

    void createStream (ConstMemory stream_name,
		       ConstMemory stream_spec,
		       bool        is_chain,
		       bool        recording,
		       ConstMemory record_filename);

    mt_iface (GstStream::Frontend)
    mt_begin

      static GstStream::Frontend gst_stream_frontend;

      static void streamError (void *stream_ticket,
			       void *_playback);

      static void streamEos (void *stream_ticket,
			     void *_playback);

    mt_end

    mt_iface (HttpService::Frontend)
    mt_begin

      static HttpService::HttpHandler admin_http_handler;

      static Result adminHttpRequest (HttpRequest  * mt_nonnull req,
				      Sender       * mt_nonnull conn_sender,
				      Memory const &msg_body,
				      void        ** mt_nonnull ret_msg_data,
				      void         *_self);

    mt_end

public:
    Result init (MomentServer *moment);

    MomentGstModule ();

    ~MomentGstModule ();
};

}


#endif /* __MOMENT_GST__MOMENT_GST_MODULE__H__ */

