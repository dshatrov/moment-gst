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
using namespace Moment;

namespace MomentGst {

static MomentGstModule gst_module;

static void glibLoopThreadFunc (void * const /* cb_data */)
{
    updateTime();
//    logD_ (_func, "g_main_context_default(): 0x", fmt_hex, (UintPtr) g_main_context_default());
//    logD_ (_func, "g_main_context_get_thread_default(): 0x", fmt_hex, (UintPtr) g_main_context_get_thread_default());

    GMainLoop * const loop = g_main_loop_new (g_main_context_default() /* same as NULL? */, FALSE);
    g_main_loop_run (loop);
    g_main_loop_unref (loop);
    logI_ (_func, "done");
}

static void doMomentGstInit ()
{
    logI_ ("Initializing mod_gst");

    MomentServer * const moment = MomentServer::getInstance();
    MConfig::Config * const config = moment->getConfig();

    {
	ConstMemory const opt_name = "mod_gst/enable";
	MConfig::Config::BooleanValue const enable = config->getBoolean (opt_name);
	if (enable == MConfig::Config::Boolean_Invalid) {
	    logE_ (_func, "Invalid value for ", opt_name, ": ", config->getString (opt_name));
	    return;
	}

	if (enable == MConfig::Config::Boolean_False) {
	    logI_ (_func, "GStreamer module is not enabled. "
		   "Set \"", opt_name, "\" option to \"y\" to enable.");
	    return;
	}
    }

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
		(char*) "moment",
		(char*) "--gst-debug=*:3",
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

// Initialization from a separate thread to cure deadlocks with glibc 1.14
// has been tried, but did no good.
static void momentGstInit_threadFunc (void * const /* cb_data */)
{
    doMomentGstInit ();
}

static void momentGstInit ()
{
    {
	Ref<Thread> const thread = grab (new Thread (
		CbDesc<Thread::ThreadFunc> (glibLoopThreadFunc, NULL, NULL)));
	if (!thread->spawn (false /* joinable */))
	    logE_ (_func, "Failed to spawn glib main loop thread: ", exc->toString());
    }

#if 0
    {
	Ref<Thread> const thread = grab (new Thread (
		CbDesc<Thread::ThreadFunc> (momentGstInit_threadFunc, NULL, NULL)));
	if (!thread->spawn (false /* joinable */))
	    logE_ (_func, "Failed to spawn initializer thread: ", exc->toString());
    }
#endif

    doMomentGstInit ();
}

static void momentGstUnload ()
{
    logI_ ("Unloading mod_gst");
}

} // namespace MomentGst


namespace M {

void libMary_moduleInit ()
{
    MomentGst::momentGstInit ();
}

void libMary_moduleUnload ()
{
    MomentGst::momentGstUnload ();
}

}

