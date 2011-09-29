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


namespace MomentGst {

using namespace M;
using namespace Moment;

class Recorder : public Object
{
private:
#if 0
    Playback playback;

    AvRecorder recorder;
    FlvMuxer muxer;

    mt_iface (Playback::Frontend)
    mt_begin

      static Playback::Fronented playback_frontend;

      void startRecorderItem (Playlist::Item *item,
			      Time            seek,
			      AdvanceTicket  *advance_ticket,
			      void           *_rec);

      void stopRecorderItem (void *_rec);

    mt_end
#endif

public:
};

}


#endif /* __MOMENT_GST__RECORDER__H__ */

