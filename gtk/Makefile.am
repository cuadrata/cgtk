## Makefile.am for gtk+/gtk

AM_CPPFLAGS = \
        -DG_LOG_DOMAIN=\"Gtk\"                          \
        -DGTK_DISABLE_COMPAT_H                          \
        -DGTK_LIBDIR=\"$(libdir)\"                      \
        -DGTK_DATA_PREFIX=\"$(prefix)\"                 \
        -DGTK_SYSCONFDIR=\"$(sysconfdir)\"              \
        -DGTK_DEFAULT_THEME=\"$(GTK_DEFAULT_THEME)\"    \
        -DGTK_LOCALEDIR=\"$(gtklocaledir)\"             \
        -I$(top_srcdir)                                 \
        @GTK_DEBUG_FLAGS@                               \
        @GLIB_CFLAGS@                                   \
        @x_cflags@



#
# libraries to compile and install
#
lib_LTLIBRARIES = libgtk-1.4.la

# libtool stuff: set version and export symbols for resolving
libgtkincludedir = $(includedir)/gtk-1.4/gtk
libgtk_1_4_la_LDFLAGS = \
        -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)    \
        -export-dynamic                                         \
        @x_ldflags@
libgtk_1_4_la_LIBADD = \
        $(top_builddir)/gdk/libgdk-1.4.la                       \
        @GLIB_DEPLIBS@                                          \
        @x_libs@                                                \
        -lm


#
# setup source file variables
#
# GTK+ header files for public installation (non-generated)
gtk_public_h_sources = \
        gtk.h                   \
        gtkaccelgroup.h         \
        gtkaccellabel.h         \
        gtkadjustment.h         \
        gtkimcontext.h          \
        gtkimcontextsimple.h    \
        gtkimmulticontext.h     \
        gtkalignment.h          \
        gtkarg.h                \
        gtkarrow.h              \
        gtkaspectframe.h        \
        gtkbin.h                \
        gtkbindings.h           \
        gtkbbox.h               \
        gtkbox.h                \
        gtkbutton.h             \
        gtkcalendar.h           \
        gtkcheckbutton.h        \
        gtkcheckmenuitem.h      \
        gtkclist.h              \
        gtkcolorsel.h           \
        gtkcombo.h              \
        gtkcompat.h             \
        gtkcontainer.h          \
        gtkctree.h              \
        gtkcurve.h              \
        gtkdata.h               \
        gtkdebug.h              \
        gtkdialog.h             \
        gtkmessagedialog.h      \
        gtkdnd.h                \
        gtkdrawingarea.h        \
        gtkeditable.h           \
        gtkentry.h              \
        gtkenums.h              \
        gtkeventbox.h           \
        gtkfilesel.h            \
        gtkfixed.h              \
        gtkfontsel.h            \
        gtkframe.h              \
        gtkgamma.h              \
        gtkgc.h                 \
        gtkhandlebox.h          \
        gtkhbbox.h              \
        gtkhbox.h               \
        gtkhpaned.h             \
        gtkhruler.h             \
        gtkhscale.h             \
        gtkhscrollbar.h         \
        gtkhseparator.h         \
        gtkimage.h              \
        gtkinputdialog.h        \
        gtkinvisible.h          \
        gtkitem.h               \
        gtkitemfactory.h        \
        gtklabel.h              \
        gtklayout.h             \
        gtklist.h               \
        gtklistitem.h           \
        gtkmain.h               \
        gtkmenu.h               \
        gtkmenubar.h            \
        gtkmenufactory.h        \
        gtkmenuitem.h           \
        gtkmenushell.h          \
        gtkmisc.h               \
        gtknotebook.h           \
        gtkobject.h             \
        gtkoptionmenu.h         \
        gtkpacker.h             \
        gtkpaned.h              \
        gtkpixmap.h             \
        gtkplug.h               \
        gtkpreview.h            \
        gtkprivate.h            \
        gtkprogress.h           \
        gtkprogressbar.h        \
        gtkradiobutton.h        \
        gtkradiomenuitem.h      \
        gtkrange.h              \
        gtkrc.h                 \
        gtkruler.h              \
        gtkscale.h              \
        gtkscrollbar.h          \
        gtkscrolledwindow.h     \
        gtkselection.h          \
        gtkseparator.h          \
        gtksignal.h             \
        gtksocket.h             \
        gtkspinbutton.h         \
        gtkstyle.h              \
        gtkstatusbar.h          \
        gtktable.h              \
        gtktearoffmenuitem.h    \
        gtktext.h               \
        gtkthemes.h             \
        gtktipsquery.h          \
        gtktogglebutton.h       \
        gtktoolbar.h            \
        gtktooltips.h           \
        gtktree.h               \
        gtktreeitem.h           \
        gtktypeutils.h          \
        gtkvbbox.h              \
        gtkvbox.h               \
        gtkviewport.h           \
        gtkvpaned.h             \
        gtkvruler.h             \
        gtkvscale.h             \
        gtkvscrollbar.h         \
        gtkvseparator.h         \
        gtkwidget.h             \
        gtkwindow.h

# GTK+ header files that don't get installed
gtk_private_h_sources = \
        gtkthemebuiltin.h

# GTK+ C sources to build the library from
gtk_c_sources = \
        gtkaccelgroup.c         \
        gtkaccellabel.c         \
        gtkadjustment.c         \
        gtkimcontext.c          \
        gtkimcontextsimple.c    \
        gtkimmulticontext.c     \
        gtkalignment.c          \
        gtkarg.c                \
        gtkarrow.c              \
        gtkaspectframe.c        \
        gtkbin.c                \
        gtkbindings.c           \
        gtkbbox.c               \
        gtkbox.c                \
        gtkbutton.c             \
        gtkcalendar.c           \
        gtkcheckbutton.c        \
        gtkcheckmenuitem.c      \
        gtkclist.c              \
        gtkcolorsel.c           \
        gtkcombo.c              \
        gtkcontainer.c          \
        gtkctree.c              \
        gtkcurve.c              \
        gtkdata.c               \
        gtkdialog.c             \
        gtkmessagedialog.c      \
        gtkdnd.c                \
        gtkdrawingarea.c        \
        gtkeditable.c           \
        gtkentry.c              \
        gtkeventbox.c           \
        gtkfilesel.c            \
        gtkfixed.c              \
        gtkfontsel.c            \
        gtkframe.c              \
        gtkgamma.c              \
        gtkgc.c                 \
        gtkhandlebox.c          \
        gtkhbbox.c              \
        gtkhbox.c               \
        gtkhpaned.c             \
        gtkhruler.c             \
        gtkhscale.c             \
        gtkhscrollbar.c         \
        gtkhseparator.c         \
        gtkimage.c              \
        gtkinputdialog.c        \
        gtkintl.h               \
        gtkinvisible.c          \
        gtkitem.c               \
        gtkitemfactory.c        \
        gtklabel.c              \
        gtklayout.c             \
        gtklist.c               \
        gtklistitem.c           \
        gtkmain.c               \
        gtkmenu.c               \
        gtkmenubar.c            \
        gtkmenufactory.c        \
        gtkmenuitem.c           \
        gtkmenushell.c          \
        gtkmisc.c               \
        gtknotebook.c           \
        gtkobject.c             \
        gtkoptionmenu.c         \
        gtkpacker.c             \
        gtkpaned.c              \
        gtkpixmap.c             \
        gtkplug.c               \
        gtkpreview.c            \
        gtkprogress.c           \
        gtkprogressbar.c        \
        gtkradiobutton.c        \
        gtkradiomenuitem.c      \
        gtkrange.c              \
        gtkrc.c                 \
        gtkruler.c              \
        gtkscale.c              \
        gtkscrollbar.c          \
        gtkscrolledwindow.c     \
        gtkselection.c          \
        gtkseparator.c          \
        gtksignal.c             \
        gtksocket.c             \
        gtkspinbutton.c         \
        gtkstyle.c              \
        gtkstatusbar.c          \
        gtktable.c              \
        gtktearoffmenuitem.c    \
        gtktext.c               \
        gtkthemes.c             \
        gtkthemealtus.c         \
        gtkthemecommon.c        \
        gtkthemecommon.h        \
        gtkthemeclarus.c        \
        gtkthemeclarus.h        \
        gtkthemeclarusclassic.c \
        gtkthemeclarusglossy.c  \
        gtkthemeclarusgummy.c   \
        gtkthemeclarusinverted.c \
        gtkthemeclarussupport.c \
        gtkthemefabrica.c       \
        gtkthemefenestramm.c    \
        gtkthemegelidus.c       \
        gtkthememetallum.c      \
        gtkthemeunicus.c        \
        gtkthemepriscus.c       \
        gtktipsquery.c          \
        gtktogglebutton.c       \
        gtktoolbar.c            \
        gtktooltips.c           \
        gtktree.c               \
        gtktreeitem.c           \
        gtktypeutils.c          \
        gtkvbbox.c              \
        gtkvbox.c               \
        gtkviewport.c           \
        gtkvpaned.c             \
        gtkvruler.c             \
        gtkvscale.c             \
        gtkvscrollbar.c         \
        gtkvseparator.c         \
        gtkwidget.c             \
        gtkwindow.c

# Generated at build time, from the public headers and gtkmarshal.list:
# the signal marshallers, and the type and enumeration tables. They
# live in the build directory and are not distributed.
gtk_built_sources = \
        gtk.defs                \
        gtkmarshal.c            \
        gtktypebuiltins_vars.c  \
        gtktypebuiltins_ids.c   \
        gtktypebuiltins_evals.c

# ... and those installed with the headers.
gtk_built_public_sources = \
        gtkmarshal.h            \
        gtktypebuiltins.h

# non-header sources (headers should be specified in the above variables)
# that don't serve as direct make target sources, i.e. they don't have
# their own .lo rules and don't get publically installed
gtk_extra_sources = \
        gtkfeatures.h.in        \
        makeenums.awk           \
        maketypes.awk           \
        makeenums.h             \
        gtkargcollector.c       \
        gtk-boxed.defs          \
        genmarshal.awk          \
        gtkmarshal.list

# Extra headers that are used for enum type array/id generation
gdk_headers = \
        ../gdk/gdktypes.h       \
        ../gdk/gdkrgb.h


#
# setup GTK+ sources and their dependancies
#
libgtkinclude_HEADERS = $(gtk_public_h_sources)
nodist_libgtkinclude_HEADERS = $(gtk_built_public_sources) gtkfeatures.h
libgtk_1_4_la_SOURCES = $(gtk_c_sources)
nodist_libgtk_1_4_la_SOURCES = gtkmarshal.c
EXTRA_DIST = $(gtk_private_h_sources)
EXTRA_DIST += $(gtk_extra_sources)

#
# rules to generate built sources
#
# The headers and gtk.defs are only replaced when their contents
# change, with stamp files recording that they are up to date, so that
# editing a header does not rebuild all of GTK+.
BUILT_SOURCES = $(gtk_built_public_sources) $(gtk_built_sources)
CLEANFILES = $(gtk_built_public_sources) $(gtk_built_sources) \
        stamp-gtk.defs stamp-gtkmarshal.h stamp-gtktypebuiltins.h \
        xgen-gdef xgen-gmh xgen-gmc xgen-gtbh

stamp-gtk.defs: makeenums.awk gtk-boxed.defs $(gtk_public_h_sources) $(gdk_headers)
	$(AM_V_GEN)(cd $(srcdir) \
	   && LC_ALL=C $(AWK) -v mode=defs -f makeenums.awk \
	        $(gtk_public_h_sources) $(gdk_headers) \
	   && cat gtk-boxed.defs) > xgen-gdef \
	&& (cmp -s xgen-gdef gtk.defs || cp xgen-gdef gtk.defs) \
	&& rm -f xgen-gdef \
	&& echo timestamp > $@
gtk.defs: stamp-gtk.defs
	@test -f $@ || { rm -f stamp-gtk.defs; $(MAKE) $(AM_MAKEFLAGS) stamp-gtk.defs; }

stamp-gtkmarshal.h: genmarshal.awk gtkmarshal.list
	$(AM_V_GEN)LC_ALL=C $(AWK) -v out_h=xgen-gmh -v out_c=xgen-gmc \
	        -f $(srcdir)/genmarshal.awk $(srcdir)/gtkmarshal.list \
	&& (cmp -s xgen-gmh gtkmarshal.h || cp xgen-gmh gtkmarshal.h) \
	&& (cmp -s xgen-gmc gtkmarshal.c || cp xgen-gmc gtkmarshal.c) \
	&& rm -f xgen-gmh xgen-gmc \
	&& echo timestamp > $@
gtkmarshal.h gtkmarshal.c: stamp-gtkmarshal.h
	@test -f $@ || { rm -f stamp-gtkmarshal.h; $(MAKE) $(AM_MAKEFLAGS) stamp-gtkmarshal.h; }

stamp-gtktypebuiltins.h: maketypes.awk gtk.defs
	$(AM_V_GEN)LC_ALL=C $(AWK) -f $(srcdir)/maketypes.awk gtk.defs macros > xgen-gtbh \
	&& (cmp -s xgen-gtbh gtktypebuiltins.h || cp xgen-gtbh gtktypebuiltins.h) \
	&& rm -f xgen-gtbh \
	&& echo timestamp > $@
gtktypebuiltins.h: stamp-gtktypebuiltins.h
	@test -f $@ || { rm -f stamp-gtktypebuiltins.h; $(MAKE) $(AM_MAKEFLAGS) stamp-gtktypebuiltins.h; }

gtktypebuiltins_vars.c: maketypes.awk gtk.defs
	$(AM_V_GEN)LC_ALL=C $(AWK) -f $(srcdir)/maketypes.awk gtk.defs variables > $@-tmp \
	&& mv $@-tmp $@
gtktypebuiltins_ids.c: maketypes.awk gtk.defs
	$(AM_V_GEN)LC_ALL=C $(AWK) -f $(srcdir)/maketypes.awk gtk.defs entries > $@-tmp \
	&& mv $@-tmp $@
gtktypebuiltins_evals.c: makeenums.awk $(gtk_public_h_sources) $(gdk_headers)
	$(AM_V_GEN)(cd $(srcdir) \
	   && LC_ALL=C $(AWK) -v mode=arrays -f makeenums.awk \
	        $(gtk_public_h_sources) $(gdk_headers)) > $@-tmp \
	&& mv $@-tmp $@


#
# test programs, not to be installed
#
noinst_PROGRAMS = testgtk testinput testselection testrgb testdnd simple
DEPS = libgtk-1.4.la $(top_builddir)/gdk/libgdk-1.4.la
LDADDS = \
        libgtk-1.4.la                   \
        $(top_builddir)/gdk/libgdk-1.4.la       \
        @x_ldflags@                     \
        @x_libs@                        \
        @GLIB_LIBS@                     \
        -lm

testgtk_DEPENDENCIES = $(DEPS)
testinput_DEPENDENCIES = $(DEPS)
testselection_DEPENDENCIES = $(DEPS)
testrgb_DEPENDENCIES = $(DEPS)
testdnd_DEPENDENCIES = $(DEPS)
simple_DEPENDENCIES = $(DEPS)
testgtk_LDADD = $(LDADDS)
testinput_LDADD = $(LDADDS)
testselection_LDADD = $(LDADDS)
testrgb_LDADD = $(LDADDS)
testdnd_LDADD = $(LDADDS)
simple_LDADD = $(LDADDS)

# Undo in GtkEntry and GtkText; skipped where there is no display.
check_PROGRAMS = testundo
TESTS = $(check_PROGRAMS)
testundo_DEPENDENCIES = $(DEPS)
testundo_LDADD = $(LDADDS)

.PHONY: test test-debug

test: testgtk
	builddir=`pwd`; cd $(top_builddir); top_builddir=`pwd`; \
	cd $$builddir; cd $(srcdir); \
	$(SHELL) $$top_builddir/libtool --mode=execute $$builddir/testgtk

test-debug: testgtk
	builddir=`pwd`; cd $(top_builddir); top_builddir=`pwd`; \
	cd $$builddir; cd $(srcdir); \
	$(SHELL) $$top_builddir/libtool --mode=execute gdb $$builddir/testgtk

EXTRA_DIST += \
        testgtkrc               \
        testgtkrc2              \
        circles.xbm             \
        line-arrow.xbm          \
        line-wrap.xbm           \
        3DRings.xpm             \
        FilesQueue.xpm          \
        Modeller.xpm            \
        check-y.xpm             \
        check-n.xpm             \
        marble.xpm              \
        tree_minus.xpm          \
        tree_plus.xpm           \
        test.xpm                \
        $(gtkconf_DATA)
