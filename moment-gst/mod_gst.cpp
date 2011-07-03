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


#include <moment-gst/moment_gst_module.h>


using namespace M;

namespace Moment {

namespace {

MomentGstModule gst_module;

void momentGstInit ()
{
    logD_ ("GST MODULE INIT");

    MomentServer * const moment = MomentServer::getInstance();
    MConfig::Config * const config = moment->getConfig();

    {
	ConstMemory const opt_name = "mod_gst/gst_debug";
	MConfig::Config::BooleanValue const gst_debug = config->getBoolean (opt_name);
	if (gst_debug == MConfig::Config::Boolean_Invalid) {
	    logE_ (_func, "Invalid value for ", opt_name);
	    return;
	}

	if (gst_debug == MConfig::Config::Boolean_True) {
	  // Initialization with gstreamer debugging output enabled.

	    int argc = 2;
	    char* argv [] = {
		"moment",
		"--gst-debug=*:3",
		NULL
	    };

	    char **argv_ = argv;
	    gst_init (&argc, &argv_);
	} else {
	    gst_init (NULL /* argc */, NULL /* argv */);
	}
    }

    if (!gst_module.init (MomentServer::getInstance()))
	logE_ (_func, "gst_module.init() failed");
}

void momentGstUnload ()
{
    logD_ ("GST MODULE UNLOAD");
}

} // namespace {}

} // namespace Moment


namespace M {

void libMary_moduleInit ()
{
    Moment::momentGstInit ();
}

void libMary_moduleUnload ()
{
    Moment::momentGstUnload ();
}

}

