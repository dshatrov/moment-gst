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


#ifndef __MOMENT_GST__CHANNEL__H__
#define __MOMENT_GST__CHANNEL__H__


#include <moment/libmoment.h>

#include <moment-gst/gst_stream_ctl.h>
#include <moment-gst/playback.h>


namespace MomentGst {

using namespace M;
using namespace Moment;

class Channel : public Object
{
public:
    struct ChannelEvents
    {
	void (*startItem) (void *cb_data);

	void (*stopItem) (void *cb_data);

	void (*newVideoStream) (void *cb_data);
    };

private:
    mt_const Ref<String> channel_name;

    mt_const Ref<GstStreamCtl> stream_ctl;

    Playback playback;

    Informer_<ChannelEvents> event_informer;

    mt_iface (GstStreamCtl::Frontend)
    mt_begin

      static GstStreamCtl::Frontend gst_stream_ctl_frontend;

      static void newVideoStream (void *_advance_ticket,
				  void *_self);

      static void streamError (void *_advance_ticket,
			       void *_self);

      static void streamEos (void *_advance_ticket,
			     void *_self);

    mt_end

    mt_iface (Playback::Frontend)
    mt_begin

      static Playback::Frontend playback_frontend;

      static void startPlaybackItem (Playlist::Item          *item,
				     Time                     seek,
				     Playback::AdvanceTicket *advance_ticket,
				     void                    *_self);

      static void stopPlaybackItem (void *_self);

    mt_end

    static void informStartItem (ChannelEvents *events,
				 void          *cb_data,
				 void          *inform_data);

    static void informStopItem (ChannelEvents *events,
				void          *cb_data,
				void          *inform_data);

    static void informNewVideoStream (ChannelEvents *events,
				      void          *cb_data,
				      void          *inform_data);

    void fireStartItem ();

    void fireStopItem ();

    void fireNewVideoStream ();


public:
    Result setPosition_Id (ConstMemory const id,
			   Time        const seek)
    {
	return playback.setPosition_Id (id, seek);
    }

    Result setPosition_Index (Count const idx,
			      Time  const seek)
    {
	return playback.setPosition_Index (idx, seek);
    }

    void setSingleItem (ConstMemory const stream_spec,
			bool        const is_chain)
    {
	playback.setSingleItem (stream_spec, is_chain);
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

    Informer_<ChannelEvents>* getEventInformer ()
    {
	return &event_informer;
    }

    mt_const void init (MomentServer *moment,
			ConstMemory   channel_name,
			bool          send_metadata,
			Size          default_width,
			Size          default_height,
			Size          default_bitrate,
			Time          no_video_timeout);

    Channel ();
};

}


#endif /* __MOMENT_GST__CHANNEL__H__ */

