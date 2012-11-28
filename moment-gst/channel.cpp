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


#include <moment-gst/channel.h>


using namespace M;
using namespace Moment;

namespace MomentGst {

GstStreamCtl::Frontend Channel::gst_stream_ctl_frontend = {
    newVideoStream,
    streamError,
    streamEos
};

void
Channel::newVideoStream (void * const /* _advance_ticket */,
			 void * const _self)
{
    Channel * const self = static_cast <Channel*> (_self);
    self->fireNewVideoStream ();
}

void
Channel::streamError (void * const _advance_ticket,
		      void * const _self)
{
    Channel * const self = static_cast <Channel*> (_self);
    Playback::AdvanceTicket * const advance_ticket = static_cast <Playback::AdvanceTicket*> (_advance_ticket);

    logD_ (_func, "channel 0x", fmt_hex, (UintPtr) self, ", "
	   "advance_ticket: 0x", (UintPtr) advance_ticket);

  // No-op
}

void
Channel::streamEos (void * const _advance_ticket,
		    void * const _self)
{
    Channel * const self = static_cast <Channel*> (_self);
    Playback::AdvanceTicket * const advance_ticket = static_cast <Playback::AdvanceTicket*> (_advance_ticket);

    logD_ (_func, "channel 0x", fmt_hex, (UintPtr) self, ", "
	   "advance_ticket: 0x", (UintPtr) advance_ticket);

    self->playback.advance (advance_ticket);
}

Playback::Frontend Channel::playback_frontend = {
    startPlaybackItem,
    stopPlaybackItem
};

void
Channel::startPlaybackItem (Playlist::Item          * const item,
			    Time                      const seek,
			    Playback::AdvanceTicket * const advance_ticket,
			    void                    * const _self)
{
    logD_ (_func_, ", seek: ", seek);

    Channel * const self = static_cast <Channel*> (_self);

    bool const got_chain_spec = item->chain_spec && !item->chain_spec.isNull();
    bool const got_uri = item->uri && !item->uri.isNull();

    if (got_chain_spec && got_uri) {
	logW_ (_func, "Both chain spec and uri are specified for a playlist item. "
	       "Ignoring the uri.");
    }

    bool item_started = true;
    if (got_chain_spec) {
	self->stream_ctl->beginVideoStream (item->chain_spec->mem(),
					    true /* is_chain */,
					    advance_ticket /* stream_ticket */,
					    advance_ticket /* stream_ticket_ref */,
					    seek);
    } else
    if (got_uri) {
	self->stream_ctl->beginVideoStream (item->uri->mem(),
					    false /* is_chain */,
					    advance_ticket /* stream_ticket */,
					    advance_ticket /* stream_ticket_ref */,
					    seek);
    } else {
	logW_ (_func, "Nor chain spec, no uri is specified for a playlist item.");
	self->playback.advance (advance_ticket);
	item_started = false;
    }

    if (item_started) {
	logD_ (_func, "firing startItem");
	self->fireStartItem ();
    }
}

void
Channel::stopPlaybackItem (void * const _self)
{
    logD_ (_func_);

    Channel * const self = static_cast <Channel*> (_self);
    self->stream_ctl->endVideoStream ();

    self->fireStopItem ();
}

void
Channel::informStartItem (ChannelEvents * const events,
			  void          * const cb_data,
			  void          * const /* inform_data */)
{
    if (events->startItem)
        events->startItem (cb_data);
}

void
Channel::informStopItem (ChannelEvents * const events,
			 void          * const cb_data,
			 void          * const /* inform_data */)
{
    if (events->stopItem)
        events->stopItem (cb_data);
}

void
Channel::informNewVideoStream (ChannelEvents * const events,
			       void          * const cb_data,
			       void          * const /* inform_data */)
{
    if (events->newVideoStream)
        events->newVideoStream (cb_data);
}

void
Channel::fireStartItem ()
{
    event_informer.informAll (informStartItem, NULL /* inform_data */);
}

void
Channel::fireStopItem ()
{
    event_informer.informAll (informStopItem, NULL /* inform_data */);
}

void
Channel::fireNewVideoStream ()
{
    event_informer.informAll (informNewVideoStream, NULL /* inform_data */);
}

mt_const void
Channel::init (MomentServer * const moment,
	       ConstMemory    const channel_name,
               bool           const no_audio,
               bool           const no_video,
	       bool           const send_metadata,
               bool           const enable_prechunking,
	       bool           const keep_video_stream,
               bool           const continuous_playback,
               bool           const connect_on_demand,
               Time           const connect_on_demand_timeout,
	       Size           const default_width,
	       Size           const default_height,
	       Size           const default_bitrate,
	       Time           const no_video_timeout,
               Uint64         const min_playlist_duration_sec)
{
    playback.init (moment->getServerApp()->getServerContext()->getTimers(),
                   min_playlist_duration_sec);
    playback.setFrontend (CbDesc<Playback::Frontend> (
	    &playback_frontend, this /* cb_data */, this /* coderef_container */));

    stream_ctl = grab (new GstStreamCtl);
    stream_ctl->init (moment,
		      moment->getServerApp()->getMainThreadContext()->getDeferredProcessor(),
		      channel_name,
                      no_audio,
                      no_video,
		      send_metadata,
                      enable_prechunking,
		      keep_video_stream,
                      continuous_playback,
                      connect_on_demand,
                      connect_on_demand_timeout,
		      default_width,
		      default_height,
		      default_bitrate,
		      no_video_timeout);

    stream_ctl->setFrontend (CbDesc<GstStreamCtl::Frontend> (
	    &gst_stream_ctl_frontend, this /* cb_data */, this /* coderef_container */));
}

Channel::Channel ()
    : playback (this /* coderef_container */),
      event_informer (this, &mutex)
{
}

}

