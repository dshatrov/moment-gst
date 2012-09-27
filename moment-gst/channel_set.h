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


#ifndef __MOMENT_GST__CHANNEL_SET__H__
#define __MOMENT_GST__CHANNEL_SET__H__


#include <moment/libmoment.h>

#include <moment-gst/channel.h>


namespace MomentGst {

using namespace M;
using namespace Moment;

class ChannelSet
{
private:
    StateMutex mutex;

    class ChannelEntry : public HashEntry<>
    {
    public:
	Ref<Channel> channel;
	Ref<String> channel_name;
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

    mt_mutex (mutex) ChannelEntryHash channel_entry_hash;

public:
    typedef ChannelEntry* ChannelKey;

    ChannelKey addChannel (Channel     *channel,
			   ConstMemory  channel_name);

    void removeChannel (ChannelKey channel_key);

    Ref<Channel> lookupChannel (ConstMemory channel_name);

    ~ChannelSet ();
};

}


#endif /* __MOMENT_GST__CHANNEL_SET__H__ */

