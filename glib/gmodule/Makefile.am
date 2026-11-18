## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = -I$(top_srcdir)/glib -I$(top_builddir)/glib -I$(top_srcdir)/glib/gmodule \
        -DG_LOG_DOMAIN=\"GModule\" @GLIB_DEBUG_FLAGS@

glibincludedir=$(includedir)/glib-1.4
glibinclude_HEADERS =           \
                gmodule.h

libglib = $(top_builddir)/glib/libglib-1.4.la # -lglib-1.4

lib_LTLIBRARIES = libgmodule-1.4.la

# Modules testgmodule loads; -rpath makes them shared, but not installed.
noinst_LTLIBRARIES = libgplugin_a.la libgplugin_b.la

libgmodule_1_4_la_SOURCES = gmodule.c
libgmodule_1_4_la_LDFLAGS = \
        @G_MODULE_LDFLAGS@ \
        -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
        -export-dynamic

libgmodule_1_4_la_LIBADD = $(libglib) @G_MODULE_LIBS@

libgplugin_a_la_SOURCES = libgplugin_a.c
libgplugin_a_la_LDFLAGS = @G_MODULE_LDFLAGS@ -avoid-version -module -rpath $(abs_builddir)
libgplugin_a_la_LIBADD = @G_MODULE_LIBS@ # $(libglib)

libgplugin_b_la_SOURCES = libgplugin_b.c
libgplugin_b_la_LDFLAGS = @G_MODULE_LDFLAGS@ -avoid-version -module -rpath $(abs_builddir)
libgplugin_b_la_LIBADD = @G_MODULE_LIBS@ # $(libglib)

noinst_PROGRAMS = testgmodule
TESTS = testgmodule
testgmodule_LDFLAGS = @G_MODULE_LDFLAGS@
testgmodule_LDADD = libgmodule-1.4.la $(libglib) @G_MODULE_LIBS@
