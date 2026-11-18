## Makefile.am for cgtk-theme, which chooses and edits themes

AM_CPPFLAGS = \
        -I$(top_srcdir)         \
        -I$(top_builddir)       \
        @GTK_DEBUG_FLAGS@       \
        @GLIB_CFLAGS@           \
        @x_cflags@


bin_PROGRAMS = cgtk-theme

cgtk_theme_SOURCES = \
        cgtk-theme.c                            \
        theme.c                                 \
        theme.h


cgtk_theme_DEPENDENCIES = \
        $(top_builddir)/gtk/libgtk-1.4.la               \
        $(top_builddir)/gdk/libgdk-1.4.la


cgtk_theme_LDADD = \
        $(top_builddir)/gtk/libgtk-1.4.la               \
        $(top_builddir)/gdk/libgdk-1.4.la               \
        @GLIB_LIBS@


man_MANS = cgtk-theme.1

EXTRA_DIST = \
        cgtk-theme.1
