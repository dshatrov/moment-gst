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
#include <moment-gst/channel.h>
#include <moment-gst/channel_set.h>
#include <moment-gst/recorder.h>


namespace MomentGst {

using namespace M;
using namespace Moment;

class MomentGstModule : public Object
{
private:
    class ChannelEntry : public HashEntry<>
    {
    public:
	mt_const Ref<Channel> channel;

	mt_const Ref<String> channel_name;
	mt_const Ref<String> channel_desc;
	mt_const Ref<String> playlist_filename;

        mt_const Ref<PushAgent> push_agent;
    };

    typedef Hash< ChannelEntry,
		  Memory,
		  MemberExtractor< ChannelEntry,
				   Ref<String>,
				   &ChannelEntry::channel_name,
				   Memory,
				   AccessorExtractor< String,
						      Memory,
						      &String::mem > >,
		  MemoryComparator<> >
	    ChannelEntryHash;

    class RecorderEntry : public HashEntry<>
    {
    public:
	mt_const Ref<Recorder> recorder;

	mt_const Ref<String> recorder_name;
	mt_const Ref<String> playlist_filename;
    };

    typedef Hash< RecorderEntry,
		  Memory,
		  MemberExtractor< RecorderEntry,
				   Ref<String>,
				   &RecorderEntry::recorder_name,
				   Memory,
				   AccessorExtractor< String,
						      Memory,
						      &String::mem > >,
		  MemoryComparator<> >
	    RecorderEntryHash;

    mt_const MomentServer *moment;
    mt_const Timers *timers;
    mt_const PagePool *page_pool;

    mt_const bool send_metadata;
    mt_const bool enable_prechunking;
    mt_const bool keep_video_streams;
    mt_const bool default_connect_on_demand;
    mt_const Time default_connect_on_demand_timeout;

    mt_const Uint64 default_width;
    mt_const Uint64 default_height;
    mt_const Uint64 default_bitrate;

    mt_const Time no_video_timeout;

    mt_mutex (mutex) ChannelEntryHash channel_entry_hash;
    mt_mutex (mutex) RecorderEntryHash recorder_entry_hash;

    ChannelSet channel_set;

    Result updatePlaylist (ConstMemory  channel_name,
			   bool         keep_cur_item,
			   Ref<String> * mt_nonnull ret_err_msg);

    Result setPosition (ConstMemory channel_name,
			ConstMemory item_name,
			bool        item_name_is_id,
			ConstMemory seek_str);

    void printChannelInfoJson (PagePool::PageListHead *page_list,
			       ChannelEntry           *channel_entry);

    static Result httpGetChannelsStat (HttpRequest  * mt_nonnull req,
				       Sender       * mt_nonnull conn_sender,
				       void         *_self);

    mt_iface (HttpService::Frontend)
    mt_begin

      static HttpService::HttpHandler admin_http_handler;

      static Result adminHttpRequest (HttpRequest  * mt_nonnull req,
				      Sender       * mt_nonnull conn_sender,
				      Memory const &msg_body,
				      void        ** mt_nonnull ret_msg_data,
				      void         *_self);

      static HttpService::HttpHandler http_handler;

      static Result httpRequest (HttpRequest * mt_nonnull req,
				 Sender       * mt_nonnull conn_sender,
				 Memory const &msg_body,
				 void        ** mt_nonnull ret_msg_data,
				 void         *_self);

    mt_end

    void createPlaylistChannel (ConstMemory  channel_name,
				ConstMemory  channel_desc,
				ConstMemory  playlist_filename,
				bool         recording,
				ConstMemory  record_filename,
                                bool         connect_on_demand,
                                Time         connect_on_demand_timeout,
                                PushAgent   *push_agent = NULL);

    void createStreamChannel (ConstMemory  stream_name,
			      ConstMemory  channel_desc,
			      ConstMemory  stream_spec,
			      bool         is_chain,
			      bool         recording,
			      ConstMemory  record_filename,
                              bool         connect_on_demand,
                              Time         connect_on_demand_timeout,
                              PushAgent   *push_agent = NULL);

    void createDummyChannel (ConstMemory  channel_name,
			     ConstMemory  channel_desc,
                             PushAgent   *push_agent = NULL);

    void createPlaylistRecorder (ConstMemory recorder_name,
				 ConstMemory playlist_filename,
				 ConstMemory filename_prefix);

    void createChannelRecorder (ConstMemory recorder_name,
				ConstMemory channel_name,
				ConstMemory filename_prefix);

    void parseSourcesConfigSection ();
    void parseChainsConfigSection ();
    Result parseStreamsConfigSection ();
    void parseRecordingsConfigSection ();

public:
    Result init (MomentServer *moment);

    MomentGstModule ();

    ~MomentGstModule ();
};

}


#endif /* __MOMENT_GST__MOMENT_GST_MODULE__H__ */

