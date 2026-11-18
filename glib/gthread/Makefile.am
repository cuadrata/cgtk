## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = -I$(top_srcdir)/glib -I$(top_builddir)/glib -I$(top_srcdir)/glib/gthread -DG_LOG_DOMAIN=\"GThread\"

EXTRA_DIST = gthread-posix.c

libglib = $(top_builddir)/glib/libglib-1.4.la # -lglib-1.4

lib_LTLIBRARIES = libgthread-1.4.la

libgthread_1_4_la_SOURCES = gthread.c
libgthread_1_4_la_LDFLAGS = \
        -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
        -export-dynamic

libgthread_1_4_la_LIBADD = $(libglib) @G_THREAD_LIBS@

noinst_PROGRAMS = testgthread
TESTS = testgthread
testgthread_LDADD = ../libglib-1.4.la libgthread-1.4.la
