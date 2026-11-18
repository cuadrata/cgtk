## Process this file with automake to produce Makefile.in

SUBDIRS = . gmodule gthread tests

man_MANS = docs/glib-config.1

configincludedir = $(libdir)/glib-1.4/include

bin_SCRIPTS=glib-config

AM_CPPFLAGS = -DG_LOG_DOMAIN=g_log_domain_glib @GLIB_DEBUG_FLAGS@

EXTRA_DIST = \
        glib.m4                 \
        glib-1.4.pc.in          \
        gmodule-1.4.pc.in               \
        gthread-1.4.pc.in \
        glib.pc.in gmodule.pc.in gthread.pc.in


lib_LTLIBRARIES = libglib-1.4.la

libglib_1_4_la_SOURCES = \
        garray.c        \
        gcache.c        \
        gcompletion.c   \
        gconvert.c      \
        gdataset.c      \
        gdate.c         \
        gerror.c        \
        gfileutils.c    \
        gkeyfile.c      \
        ghash.c         \
        ghook.c         \
        giochannel.c    \
        giounix.c       \
        glist.c         \
        gmain.c         \
        gmem.c          \
        gmessages.c     \
        gmutex.c        \
        gnode.c         \
        gprimes.c       \
        grel.c          \
        gscanner.c      \
        gslist.c        \
        gspawn.c        \
        gstrfuncs.c     \
        gstring.c       \
        gtimer.c        \
        gtree.c         \
        gutf8.c         \
        gutils.c

glibincludedir=$(includedir)/glib-1.4
glibinclude_HEADERS =   \
        glib.h

configinclude_DATA = \
        glibconfig.h

libglib_1_4_la_LDFLAGS = \
        -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
        -export-dynamic

# iconv(), where it is not in the C library.
libglib_1_4_la_LIBADD = $(LTLIBICONV)

noinst_PROGRAMS = testglib testgdate testgdateparser
# testgdateparser asks for dates to parse; the other two check themselves.
TESTS = testglib testgdate
testglib_LDADD = libglib-1.4.la
testgdate_LDADD = libglib-1.4.la
testgdateparser_LDADD = libglib-1.4.la

m4datadir = $(datadir)/aclocal
m4data_DATA = glib.m4

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = glib-1.4.pc gmodule-1.4.pc gthread-1.4.pc @GLIB_COMPAT_PC@
