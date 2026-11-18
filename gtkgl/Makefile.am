## Makefile.am for gtk+/gtkgl
##
## GtkGLArea, in a library of its own: nothing in GDK or GTK+ uses GL,
## and a program that does not either links nothing of it.

AM_CPPFLAGS = \
        -DG_LOG_DOMAIN=\"GtkGL\"        \
        -I$(top_srcdir)                 \
        @GTK_DEBUG_FLAGS@               \
        @GLIB_CFLAGS@                   \
        @GTKGL_CFLAGS@                  \
        @x_cflags@

lib_LTLIBRARIES = libgtkgl-1.4.la

libgtkglincludedir = $(includedir)/gtk-1.4/gtkgl
libgtkglinclude_HEADERS = \
        gdkgl.h         \
        gtkglarea.h

libgtkgl_1_4_la_SOURCES = \
        gdkgl.c         \
        gtkglarea.c

# The library stays loaded once it is: a widget type it registered, and
# what it does at exit, must not outlive it in a program that loaded it
# with a plugin and then let the plugin go.
libgtkgl_1_4_la_LDFLAGS = \
        -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)    \
        -export-dynamic                                         \
        @GTKGL_NODELETE@

libgtkgl_1_4_la_LIBADD = \
        $(top_builddir)/gtk/libgtk-1.4.la       \
        $(top_builddir)/gdk/libgdk-1.4.la       \
        @GLIB_DEPLIBS@                          \
        @GTKGL_LIBS@
