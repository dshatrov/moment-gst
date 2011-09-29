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


#include <moment-gst/channel_set.h>


using namespace M;
using namespace Moment;

namespace MomentGst {

ChannelSet::ChannelKey
ChannelSet::addChannel (Channel     * const channel,
			ConstMemory   const channel_name)
{
    ChannelEntry * const channel_entry = new ChannelEntry;
    assert (channel_entry);
    channel_entry->channel = channel;
    channel_entry->channel_name = grab (new String (channel_name));

    mutex.lock ();
    channel_entry_hash.add (channel_entry);
    mutex.unlock ();

    return channel_entry;
}

void
ChannelSet::removeChannel (ChannelKey const channel_key)
{
    mutex.lock ();
    channel_entry_hash.remove (channel_key);
    mutex.unlock ();
}

Ref<Channel>
ChannelSet::lookupChannel (ConstMemory const channel_name)
{
    mutex.lock ();

    ChannelEntry * const channel_entry = channel_entry_hash.lookup (channel_name);
    if (!channel_entry) {
	mutex.unlock ();
	return NULL;
    }

    Ref<Channel> const channel = channel_entry->channel;

    mutex.unlock ();

    return channel;
}

ChannelSet::~ChannelSet ()
{
    mutex.lock ();

    ChannelEntryHash::iter iter (channel_entry_hash);
    while (!channel_entry_hash.iter_done (iter)) {
	ChannelEntry * const channel_entry = channel_entry_hash.iter_next (iter);
	delete channel_entry;
    }

    mutex.unlock ();
}

}

