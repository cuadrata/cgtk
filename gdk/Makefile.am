## Makefile.am for gtk+/gdk

AM_CPPFLAGS = \
        -DG_LOG_DOMAIN=\"Gdk\"  \
        -I$(top_srcdir)         \
        -I$(srcdir)             \
        -I$(srcdir)/x11         \
        -I$(srcdir)/wayland     \
        -I.                     \
        @GTK_DEBUG_FLAGS@       \
        @GTK_XIM_FLAGS@         \
        @GLIB_CFLAGS@           \
        @SCALABLE_FONTS_CFLAGS@ \
        @PNG_CFLAGS@    \
        @WAYLAND_CFLAGS@        \
        @x_cflags@


#
# libraries to compile and install
#
lib_LTLIBRARIES = libgdk-1.4.la

# libtool stuff: set version and export symbols for resolving
libgdkincludedir = $(includedir)/gtk-1.4/gdk
libgdk_1_4_la_LDFLAGS = \
        -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
        -export-dynamic \
        @x_ldflags@


#
# setup source file variables
#
# GDK header files for public installation (non-generated)
gdk_public_h_sources = \
        gdk.h           \
        gdkcursors.h    \
        gdkrgb.h        \
        gdki18n.h       \
        gdkkeysyms.h    \
        gdkprivate.h    \
        gdktypes.h      \
        @GDK_X11_PUBLIC_H@      \
        @GDK_WAYLAND_PUBLIC_H@

gdk_c_sources = \
        gdkbackend.c    \
        gdkbackend.h    \
        gdkcc.c \
        gdkcolor.c      \
        gdkcolornames.h \
        gdkdraw.c       \
        gdkbroadcast.c  \
        gdkevents.c     \
        gdkfont.c       \
        gdkframebuffer.c        \
        gdkframebuffer.h        \
        gdkftdraw.c     \
        gdkftfont.c     \
        gdkftfont.h     \
        gdkftname.c     \
        gdkgc.c \
        gdkglobals.c    \
        gdkimage.c      \
        gdkmain.c       \
        gdkpen.c        \
        gdkpen.h        \
        gdkpixmap.c     \
        gdkicon.c       \
        gdkpng.c        \
        gdksvg.c        \
        nanosvg.h       \
        nanosvgrast.h   \
        gdkraster.c     \
        gdkrgb.c        \
        gdkrectangle.c  \
        gdkinputcontext.c       \
        gdkregion.c     \
        gdkvisual.c     \
        gdkwindow.c

gdk_x11_c_sources = \
        x11/gdkbackend-x11.c \
        x11/gdkcursor-x11.c \
        x11/gdkdnd-x11.c \
        x11/gdkevents-x11.c \
        x11/gdkglobals-x11.c \
        x11/gdkim-x11.c \
        x11/gdkinput-x11.c \
        x11/gdkmain-x11.c \
        x11/gdkproperty-x11.c \
        x11/gdkpublish-x11.c \
        x11/gdkselection-x11.c \
        x11/gdkwindow-x11.c \
        x11/gdkxid.c

gdk_wayland_c_sources = \
        wayland/gdkbackend-wayland.c \
        wayland/gdkevents-wayland.c \
        wayland/gdkmain-wayland.c \
        wayland/gdkselection-wayland.c \
        wayland/gdkinputcontext-wayland.c \
        wayland/gdkdecor-wayland.c \
        wayland/gdkwindow-wayland.c

gdk_wayland_h_sources = \
        wayland/gdkprivate-wayland.h \
        wayland/gdkbackend-wayland.h

gdk_x11_h_sources = \
        x11/gdkprivate-x11.h \
        x11/gdkbackend-x11.h \
        x11/MwmUtil.h


#
# setup GDK sources and their dependancies
#
libgdkinclude_HEADERS = $(gdk_public_h_sources)
nodist_libgdkinclude_HEADERS = gdkconfig.h
libgdk_1_4_la_SOURCES = $(gdk_c_sources)
libgdk_1_4_la_LIBADD = \
        $(top_builddir)/glib/libglib-1.4.la \
        @x_libs@                \
        @SCALABLE_FONTS_LIBS@   \
        @PNG_LIBS@              \
        @WAYLAND_LIBS@          \
        -lm
BUILT_SOURCES =
CLEANFILES =
if USE_X11_BACKEND
libgdk_1_4_la_SOURCES += $(gdk_x11_c_sources)
endif
if USE_WAYLAND_BACKEND
libgdk_1_4_la_SOURCES += $(gdk_wayland_c_sources)
wayland_protocol_sources = \
        xdg-shell-client-protocol.h \
        xdg-shell-protocol.c \
        xdg-decoration-unstable-v1-client-protocol.h \
        xdg-decoration-unstable-v1-protocol.c \
        primary-selection-unstable-v1-client-protocol.h \
        primary-selection-unstable-v1-protocol.c \
        text-input-unstable-v3-client-protocol.h \
        text-input-unstable-v3-protocol.c
if HAVE_XDG_TOPLEVEL_ICON
wayland_protocol_sources += \
        xdg-toplevel-icon-v1-client-protocol.h \
        xdg-toplevel-icon-v1-protocol.c
endif
nodist_libgdk_1_4_la_SOURCES = $(wayland_protocol_sources)
BUILT_SOURCES += $(wayland_protocol_sources)
CLEANFILES += $(wayland_protocol_sources)
endif
CLEANFILES += testpng.tmp testsvg.tmp
MAINTAINERCLEANFILES =
EXTRA_HEADERS =
EXTRA_DIST = $(gdk_x11_c_sources) $(gdk_wayland_c_sources) $(gdk_x11_h_sources) $(gdk_wayland_h_sources) gdkx.h gdkwayland.h

#
# Programs that check themselves, built and run by "make check". They
# need no display and no fonts.
#
check_PROGRAMS = testftname testftfont testpng testsvg
TESTS = $(check_PROGRAMS)
LDADDS = \
        @x_ldflags@     \
        @x_libs@        \
        @GLIB_LIBS@     \
        -lm

testftname_SOURCES = testftname.c
testftname_LDADD = libgdk-1.4.la $(LDADDS)

testftfont_SOURCES = testftfont.c
testftfont_LDADD = libgdk-1.4.la $(LDADDS)

testpng_SOURCES = testpng.c
testpng_LDADD = libgdk-1.4.la $(LDADDS)

testsvg_SOURCES = testsvg.c
testsvg_LDADD = libgdk-1.4.la $(LDADDS)



#
# The X11 backend lives in x11/; its objects are built here beside the
# generic ones so that one libgdk comes out of one Makefile.
#

#
# The Wayland backend, in wayland/, with the protocol code the scanner
# makes from the protocol descriptions the system provides.
#
xdg-shell-client-protocol.h: $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml
	$(WAYLAND_SCANNER) client-header $< $@
xdg-shell-protocol.c: $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml
	$(WAYLAND_SCANNER) private-code $< $@
xdg-decoration-unstable-v1-client-protocol.h: $(WAYLAND_PROTOCOLS_DIR)/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml
	$(WAYLAND_SCANNER) client-header $< $@
xdg-decoration-unstable-v1-protocol.c: $(WAYLAND_PROTOCOLS_DIR)/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml
	$(WAYLAND_SCANNER) private-code $< $@
primary-selection-unstable-v1-client-protocol.h: $(WAYLAND_PROTOCOLS_DIR)/unstable/primary-selection/primary-selection-unstable-v1.xml
	$(WAYLAND_SCANNER) client-header $< $@
primary-selection-unstable-v1-protocol.c: $(WAYLAND_PROTOCOLS_DIR)/unstable/primary-selection/primary-selection-unstable-v1.xml
	$(WAYLAND_SCANNER) private-code $< $@
text-input-unstable-v3-client-protocol.h: $(WAYLAND_PROTOCOLS_DIR)/unstable/text-input/text-input-unstable-v3.xml
	$(WAYLAND_SCANNER) client-header $< $@
text-input-unstable-v3-protocol.c: $(WAYLAND_PROTOCOLS_DIR)/unstable/text-input/text-input-unstable-v3.xml
	$(WAYLAND_SCANNER) private-code $< $@
xdg-toplevel-icon-v1-client-protocol.h: $(WAYLAND_PROTOCOLS_DIR)/staging/xdg-toplevel-icon/xdg-toplevel-icon-v1.xml
	$(WAYLAND_SCANNER) client-header $< $@
xdg-toplevel-icon-v1-protocol.c: $(WAYLAND_PROTOCOLS_DIR)/staging/xdg-toplevel-icon/xdg-toplevel-icon-v1.xml
	$(WAYLAND_SCANNER) private-code $< $@
