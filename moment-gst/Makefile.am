COMMON_CFLAGS =				\
	-D_POSIX_C_SOURCE=199309L	\
	-D_XOPEN_SOURCE=600		\
	-ggdb				\
	-Wno-long-long -Wall -Wextra	\
	$(THIS_CFLAGS)

if PLATFORM_WIN32
    COMMON_CFLAGS += -DPLATFORM_WIN32
else
    # For backtraces
    COMMON_CFLAGS += -rdynamic -pthread
endif

AM_CFLAGS = -std=c99 $(COMMON_CFLAGS)
AM_CXXFLAGS = -std=c++0x $(COMMON_CFLAGS)

if PLATFORM_WIN32
else
endif

INCLUDES = -I$(top_srcdir)

moment_gst_private_headers =

moment_gst_target_headers =	\
	moment_gst_module.h

moment_gst_includedir = $(includedir)/moment-gst-1.0/moment-gst
moment_gst_include_HEADERS = $(moment_gst_target_headers)

moment_gstdir = $(libdir)/moment-1.0
moment_gst_LTLIBRARIES = libmoment-gst-1.0.la
libmoment_gst_1_0_la_SOURCES =	\
	moment_gst_module.cpp	\
	mod_gst.cpp

moment_gst_extra_dist =

#libmoment_gst_1_0_ladir = $(libdir)/moment-1.0/
libmoment_gst_1_0_la_LDFLAGS = -no-undefined -version-info "0:0:0"
libmoment_gst_1_0_la_LIBADD = $(THIS_LIBS)
if PLATFORM_WIN32
libmoment_gst_1_0_la_LIBADD += -lws2_32
endif

EXTRA_DIST = $(moment_gst_private_headers) $(moment_gst_extra_dist)

