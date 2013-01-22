/*  Moment-Gst - GStreamer support module for Moment Video Server
    Copyright (C) 2013 Dmitry Shatrov

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


#ifndef MOMENT_GST__CHANNEL_OPTIONS__H__
#define MOMENT_GST__CHANNEL_OPTIONS__H__


#include <moment/libmoment.h>


namespace MomentGst {

using namespace M;
using namespace Moment;

class ChannelOptions : public Referenced
{
public:
  mt_const
  mt_begin

    StRef<String> channel_name;
    StRef<String> channel_title;
    StRef<String> channel_desc;

    StRef<String> stream_spec;
    bool          is_chain;
    bool          force_transcode;
    bool          force_transcode_audio;
    bool          force_transcode_video;

    bool          no_audio;
    bool          no_video;

    bool          send_metadata;
    bool          enable_prechunking;

    bool          keep_video_stream;
    bool          continuous_playback;

    bool          recording;
    StRef<String> record_path;

    bool          connect_on_demand;
    Time          connect_on_demand_timeout;

    Uint64        default_width;
    Uint64        default_height;
    Uint64        default_bitrate;

    Time          no_video_timeout;
    Time          min_playlist_duration_sec;

  mt_end

    ChannelOptions ()
        : channel_name  (st_grab (new (std::nothrow) String)),
          channel_title (st_grab (new (std::nothrow) String)),
          channel_desc  (st_grab (new (std::nothrow) String)),
            
          stream_spec (st_grab (new (std::nothrow) String)),
          is_chain (false),
          force_transcode (false),
          force_transcode_audio (false),
          force_transcode_video (false),

          no_audio (false),
          no_video (false),

          send_metadata (false),
          enable_prechunking (true),

          keep_video_stream (false),
          continuous_playback (false),

          recording (false),
          record_path (st_grab (new (std::nothrow) String)),

          connect_on_demand (false),
          connect_on_demand_timeout (60),

          default_width (0),
          default_height (0),
          default_bitrate (500000),

          no_video_timeout (60),
          min_playlist_duration_sec (10)
    {
    }
};

}


#endif /* MOMENT_GST__CHANNEL_OPTIONS__H__ */

