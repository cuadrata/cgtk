## Makefile.am for GTK+

if BUILD_GTKGL
GTKGL_SUBDIR = gtkgl
endif
SRC_SUBDIRS = gdk gtk $(GTKGL_SUBDIR) themes cgtk-theme
SUBDIRS = glib po $(SRC_SUBDIRS)

man_MANS = docs/gtk-config.1

ACLOCAL_AMFLAGS = -I m4

bin_SCRIPTS = gtk-config

EXTRA_DIST =    \
        gtk.m4          \
        history         \
        examples/aspectframe/Makefile  \
        examples/aspectframe/aspectframe.c  \
        examples/Makefile  \
        examples/README  \
        examples/base/Makefile  \
        examples/base/base.c  \
        examples/buttons/Makefile  \
        examples/buttons/buttons.c  \
        examples/buttons/info.xpm  \
        examples/calendar/Makefile  \
        examples/calendar/calendar.c  \
        examples/clist/Makefile  \
        examples/clist/clist.c  \
        examples/entry/Makefile  \
        examples/entry/entry.c  \
        examples/eventbox/Makefile  \
        examples/eventbox/eventbox.c  \
        examples/filesel/Makefile  \
        examples/filesel/filesel.c  \
        examples/gtkdial/Makefile  \
        examples/gtkdial/dial_test.c  \
        examples/gtkdial/gtkdial.c  \
        examples/gtkdial/gtkdial.h  \
        examples/helloworld/Makefile  \
        examples/helloworld/helloworld.c  \
        examples/helloworld2/Makefile  \
        examples/helloworld2/helloworld2.c  \
        examples/list/Makefile  \
        examples/list/list.c  \
        examples/menu/Makefile  \
        examples/menu/menu.c  \
        examples/menu/itemfactory.c  \
        examples/notebook/Makefile  \
        examples/notebook/notebook.c  \
        examples/packbox/Makefile  \
        examples/packbox/packbox.c  \
        examples/packer/Makefile  \
        examples/packer/pack.c  \
        examples/paned/Makefile  \
        examples/paned/paned.c  \
        examples/pixmap/Makefile  \
        examples/pixmap/pixmap.c  \
        examples/progressbar/Makefile  \
        examples/progressbar/progressbar.c  \
        examples/radiobuttons/Makefile  \
        examples/radiobuttons/radiobuttons.c  \
        examples/rangewidgets/Makefile  \
        examples/rangewidgets/rangewidgets.c  \
        examples/rulers/Makefile  \
        examples/rulers/rulers.c  \
        examples/scribble-simple/Makefile  \
        examples/scribble-simple/scribble-simple.c  \
        examples/scrolledwin/Makefile  \
        examples/scrolledwin/scrolledwin.c  \
        examples/selection/Makefile  \
        examples/selection/gettargets.c  \
        examples/selection/setselection.c  \
        examples/statusbar/Makefile  \
        examples/statusbar/statusbar.c  \
        examples/table/Makefile  \
        examples/table/table.c  \
        examples/text/Makefile  \
        examples/text/text.c  \
        examples/tictactoe/Makefile  \
        examples/tictactoe/tictactoe.c  \
        examples/tictactoe/tictactoe.h  \
        examples/tictactoe/ttt_test.c  \
        examples/tree/Makefile  \
        examples/tree/tree.c  \
        examples/wheelbarrow/Makefile  \
        examples/wheelbarrow/wheelbarrow.c  \
        examples/fixed/fixed.c  \
        examples/fixed/Makefile  \
        examples/frame/frame.c  \
        examples/frame/Makefile  \
        examples/spinbutton/spinbutton.c  \
        examples/spinbutton/Makefile  \
        examples/glarea/Makefile  \
        examples/glarea/glarea.c  \
        examples/glarea/glcore.c  \
        examples/glarea/glloop.c  \
        examples/find-examples.sh \
        gtkgl.m4 \
        gdk-1.4.pc.in gtk+-1.4.pc.in gdk.pc.in gtk+.pc.in gtkgl-1.4.pc.in

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA= gdk-1.4.pc gtk+-1.4.pc @GTK_COMPAT_PC@

m4datadir = $(datadir)/aclocal
m4data_DATA = gtk.m4

if BUILD_GTKGL
pkgconfig_DATA += gtkgl-1.4.pc
m4data_DATA += gtkgl.m4
endif

# Refresh glibc's dynamic linker cache after a direct install or
# uninstall as root, so that programs can load the libraries at once
# (see configure.ac). Staged installs (DESTDIR) leave this to the
# packager, and "make install LDCONFIG=" skips it.
run_ldconfig = \
        ldconfig='$(LDCONFIG)'; \
        if test -n "$$ldconfig" && test -z "$(DESTDIR)" && \
            test "`id -u`" = 0; then \
          echo " $$ldconfig"; \
          $$ldconfig || echo "warning: $$ldconfig failed; run it by hand" >&2; \
        fi

install-exec-hook:
	@$(run_ldconfig)

uninstall-hook:
	@$(run_ldconfig)
