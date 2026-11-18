dnl Process this file with autoreconf -vif to produce configure.
dnl
dnl GLib and GTK+ build and release together, with one version.

AC_PREREQ([2.71])

m4_define([gtk_major_version], [1])
m4_define([gtk_minor_version], [4])
m4_define([gtk_micro_version], [0])
m4_define([gtk_interface_age], [0])
m4_define([gtk_binary_age], [0])
m4_define([gtk_version],
          [gtk_major_version.gtk_minor_version.gtk_micro_version])

AC_INIT([CGTK], [gtk_version], [], [cgtk])
AC_CONFIG_SRCDIR([gdk/gdktypes.h])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])

# Save this value here, since automake will set cflags later
cflags_set=${CFLAGS+set}

AM_INIT_AUTOMAKE([1.16 foreign no-define subdir-objects])
AC_CANONICAL_HOST

# Making releases: see HACKING. GLib, GModule, GThread, GDK and GTK+
# share one version, one interface age and one binary age.
GLIB_MAJOR_VERSION=gtk_major_version
GLIB_MINOR_VERSION=gtk_minor_version
GLIB_MICRO_VERSION=gtk_micro_version
GLIB_INTERFACE_AGE=gtk_interface_age
GLIB_BINARY_AGE=gtk_binary_age
GTK_MAJOR_VERSION=gtk_major_version
GTK_MINOR_VERSION=gtk_minor_version
GTK_MICRO_VERSION=gtk_micro_version
GTK_INTERFACE_AGE=gtk_interface_age
GTK_BINARY_AGE=gtk_binary_age
AC_SUBST(GTK_MAJOR_VERSION)
AC_SUBST(GTK_MINOR_VERSION)
AC_SUBST(GTK_MICRO_VERSION)
AC_SUBST(GTK_INTERFACE_AGE)
AC_SUBST(GTK_BINARY_AGE)
AC_DEFINE_UNQUOTED(GLIB_MAJOR_VERSION, $GLIB_MAJOR_VERSION)
AC_DEFINE_UNQUOTED(GLIB_MINOR_VERSION, $GLIB_MINOR_VERSION)
AC_DEFINE_UNQUOTED(GLIB_MICRO_VERSION, $GLIB_MICRO_VERSION)
AC_DEFINE_UNQUOTED(GLIB_INTERFACE_AGE, $GLIB_INTERFACE_AGE)
AC_DEFINE_UNQUOTED(GLIB_BINARY_AGE, $GLIB_BINARY_AGE)

# libtool versioning
LT_CURRENT=`expr $GTK_MICRO_VERSION - $GTK_INTERFACE_AGE`
LT_REVISION=$GTK_INTERFACE_AGE
LT_AGE=`expr $GTK_BINARY_AGE - $GTK_INTERFACE_AGE`
AC_SUBST(LT_CURRENT)
AC_SUBST(LT_REVISION)
AC_SUBST(LT_AGE)

dnl Install the 1.2 pkg-config names as aliases of the 1.4 ones, so
dnl software written for 1.2 finds 1.4. A system that also has a
dnl stock 1.2 installed can leave those names to it.
AC_ARG_ENABLE(compat-pc,
  [AS_HELP_STRING([--disable-compat-pc],
    [do not install the 1.2 pkg-config names (glib, gmodule, gthread, gdk, gtk+)])],,
  enable_compat_pc=yes)
if test "x$enable_compat_pc" = "xyes"; then
  GLIB_COMPAT_PC="glib.pc gmodule.pc gthread.pc"
  GTK_COMPAT_PC="gdk.pc gtk+.pc"
else
  GLIB_COMPAT_PC=
  GTK_COMPAT_PC=
fi
AC_SUBST(GLIB_COMPAT_PC)
AC_SUBST(GTK_COMPAT_PC)

if test `expr $GTK_MINOR_VERSION \% 2` = 1 ; then
  debug_default=yes
else
  debug_default=minimum
fi
AC_ARG_ENABLE(debug,
  [AS_HELP_STRING([--enable-debug=@<:@no/minimum/yes@:>@],
    [turn on debugging @<:@default=minimum@:>@])],,
  enable_debug=$debug_default)
AC_ARG_WITH(default-theme,
  [AS_HELP_STRING([--with-default-theme=NAME],
    [theme in use until one is chosen, or no for none @<:@default=Altus@:>@])],,
  with_default_theme=yes)
case "$with_default_theme" in
  yes) GTK_DEFAULT_THEME=Altus ;;
  no)  GTK_DEFAULT_THEME= ;;
  *)   GTK_DEFAULT_THEME=$with_default_theme ;;
esac
AC_SUBST(GTK_DEFAULT_THEME)

AC_PROG_CC

dnl Build as ISO C99 unless the user chose a language standard. GTK+'s
dnl callback typedefs rely on what C99 means by an empty parameter list,
dnl which C23 (GCC 15's default) changes. POSIX interfaces are requested
dnl the standard way, with _XOPEN_SOURCE=600 (SUSv3, aligned with C99).
case " $CC $CFLAGS " in
*" -std="*) ;;
*)
  AC_MSG_CHECKING([whether $CC accepts -std=c99])
  glib_save_CC="$CC"
  CC="$CC -std=c99"
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [return 0;])],
    [AC_MSG_RESULT(yes)], [AC_MSG_RESULT(no); CC="$glib_save_CC"])
  ;;
esac
case " $CC $CPPFLAGS $CFLAGS " in
*" -D_XOPEN_SOURCE"* | *" -D_POSIX_C_SOURCE"*) ;;
*) CPPFLAGS="$CPPFLAGS -D_XOPEN_SOURCE=600" ;;
esac

if test "x$GCC" = "xyes"; then
  case " $CFLAGS " in
  *" -Wall "*) ;;
  *) CFLAGS="$CFLAGS -Wall" ;;
  esac
fi

LT_INIT
AC_PROG_INSTALL
AC_PROG_MAKE_SET
PKG_PROG_PKG_CONFIG

dnl libtool's install runs only "ldconfig -n" on the library directory.
dnl That makes the soname links but leaves glibc's cache alone, and glibc
dnl finds libraries in a directory such as /usr/local/lib only through
dnl that cache. Find ldconfig so that "make install" can refresh it
dnl (see Makefile.am). musl keeps no cache, and a cross build's cache is
dnl not this machine's.
if test "x$cross_compiling" != xyes; then
  case "$host_os" in
    linux-gnu*)
      AC_PATH_PROG([LDCONFIG], [ldconfig], [], [$PATH:/sbin:/usr/sbin])
      ;;
  esac
fi
AC_SUBST(LDCONFIG)


dnl *** Templates for config.h
AH_TEMPLATE([GLIB_MAJOR_VERSION], [Used by GLib.])
AH_TEMPLATE([GLIB_MINOR_VERSION], [Used by GLib.])
AH_TEMPLATE([GLIB_MICRO_VERSION], [Used by GLib.])
AH_TEMPLATE([GLIB_INTERFACE_AGE], [Used by GLib.])
AH_TEMPLATE([GLIB_BINARY_AGE], [Used by GLib.])
AH_TEMPLATE([HAVE_SHAPE_EXT], [Used by GTK+.])
AH_TEMPLATE([HAVE_XRANDR], [Used by GTK+.])
AH_TEMPLATE([USE_X11R6_XIM], [Used by GTK+.])

dnl *** GLib


if test "x$enable_debug" = "xyes"; then
  test "$cflags_set" = set || CFLAGS="$CFLAGS -g"
  GLIB_DEBUG_FLAGS="-DG_ENABLE_DEBUG"
else
  if test "x$enable_debug" = "xno"; then
    GLIB_DEBUG_FLAGS="-DG_DISABLE_ASSERT -DG_DISABLE_CHECKS"
  fi
fi

dnl The byte order, for glibconfig.h when the compiler does not say it.
AC_C_BIGENDIAN([G_BIG_ENDIAN=1], [G_BIG_ENDIAN=0])
AC_SUBST(G_BIG_ENDIAN)

dnl ***********************
dnl *** g_module checks ***
dnl ***********************
dnl GModule is dlopen (): in the C library, or in -ldl. Programs that
dnl look up their own symbols need them exported, which -rdynamic does on
dnl Linux.
glib_save_LIBS="$LIBS"
LIBS=
AC_SEARCH_LIBS([dlopen], [dl], [],
  [AC_MSG_ERROR([*** GModule needs dlopen (), and it was not found.])])
G_MODULE_LIBS="$LIBS"
LIBS="$glib_save_LIBS"
G_MODULE_LDFLAGS=
case "$host_os" in
  linux*) G_MODULE_LDFLAGS='-rdynamic' ;;
esac
AC_SUBST(G_MODULE_LIBS)
AC_SUBST(G_MODULE_LDFLAGS)
AC_SUBST(GLIB_DEBUG_FLAGS)


dnl ***********************
dnl *** g_thread checks ***
dnl ***********************

dnl Threads are POSIX threads, and always there, with the reentrant
dnl functions POSIX has (getpwuid_r, localtime_r, rand_r). G_THREAD_LIBS
dnl is -lpthread where the C library keeps them apart, else empty.
AC_CHECK_HEADER([pthread.h], [],
  [AC_MSG_ERROR([*** GLib needs POSIX threads: pthread.h was not found.])])
glib_save_LIBS="$LIBS"
LIBS=
AC_SEARCH_LIBS([pthread_create], [pthread], [],
  [AC_MSG_ERROR([*** No library with pthread_create was found.])])
G_THREAD_LIBS="$LIBS"
LIBS="$glib_save_LIBS"
G_THREAD_CFLAGS="-D_REENTRANT"
AC_SUBST(G_THREAD_CFLAGS)
AC_SUBST(G_THREAD_LIBS)

CFLAGS="$CFLAGS $G_THREAD_CFLAGS"

dnl *** GTK+
AC_ARG_ENABLE(x11-backend,
  [AS_HELP_STRING([--enable-x11-backend],
    [build the X11 backend @<:@default=auto: if Xlib is found@:>@])],,
  enable_x11_backend=auto)
AC_ARG_ENABLE(wayland-backend,
  [AS_HELP_STRING([--enable-wayland-backend],
    [build the Wayland backend @<:@default=auto: if its requirements are found@:>@])],,
  enable_wayland_backend=auto)
AC_ARG_ENABLE(gl,
  [AS_HELP_STRING([--enable-gl],
    [build GtkGLArea, the OpenGL widget, which needs EGL @<:@default=auto: if EGL is found@:>@])],,
  enable_gl=auto)
AC_ARG_ENABLE(xim, [  --enable-xim            support XIM [default=yes]],
                        , enable_xim="yes")
AC_ARG_ENABLE(xim_inst, [  --disable-xim-inst      does not use xim instantiate callback],
                        , enable_xim_inst="maybe")


if test "x$enable_debug" = "xyes"; then
  test "$cflags_set" = set || CFLAGS="$CFLAGS -g"
  GTK_DEBUG_FLAGS="-DG_ENABLE_DEBUG"
else
  if test "x$enable_debug" = "xno"; then
    GTK_DEBUG_FLAGS="-DG_DISABLE_ASSERT -DG_DISABLE_CHECKS -DGTK_NO_CHECK_CASTS"
  else
    GTK_DEBUG_FLAGS="-DGTK_NO_CHECK_CASTS"
  fi
fi


# GTK+'s signal marshallers and type and enumeration tables are
# generated at build time, with awk.
AC_PROG_AWK
if test -z "$AWK"; then
  AC_MSG_ERROR([*** awk is needed to generate GTK+'s type tables.])
fi





# i18n stuff
dnl The gettext macros come from autopoint, which autoreconf -i runs.
dnl Newer gettext no longer installs them where aclocal looks, and newer
dnl autoreconf skips autopoint silently when it is missing, so say so
dnl here rather than fail on an undefined macro.
m4_ifndef([AM_GNU_GETTEXT_NEED],
  [m4_fatal([the gettext macros are missing: install autopoint (a package of its own on Debian) and run autoreconf -vif])])
AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION([0.19.6])
AM_GNU_GETTEXT_REQUIRE_VERSION([0.19.6])
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["$PACKAGE"],
  [The gettext domain of the library's messages, as po/ installs it])
INTLLIBS="$LIBINTL"

dnl iconv(), for g_convert(). With gettext in the C library the macros
dnl above have not looked for it. It is in the C library too on glibc,
dnl musl and the BSDs, and in libiconv elsewhere.
AM_ICONV
if test "x$am_cv_func_iconv" != xyes; then
  AC_MSG_ERROR([*** GLib converts text between character sets with iconv(), and none was found.])
fi
AC_SUBST(INTLLIBS)
LIBS="$LIBS $LIBINTL"

# the directory where the translations are installed
gtklocaledir='${localedir}'
AC_SUBST(gtklocaledir)



dnl GTK+ builds against the GLib in glib/.
GLIB_CFLAGS='-I$(top_srcdir)/glib -I$(top_builddir)/glib -I$(top_srcdir)/glib/gmodule -I$(top_builddir)/glib/gmodule'
GLIB_LIBS='$(top_builddir)/glib/gmodule/libgmodule-1.4.la $(top_builddir)/glib/libglib-1.4.la'
GLIB_DEPLIBS="$GLIB_LIBS"
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)
AC_SUBST(GLIB_DEPLIBS)



saved_cflags="$CFLAGS"
saved_ldflags="$LDFLAGS"

# X is looked for only for the X11 backend: without it, nothing in GDK or
# GTK uses X, and a system with no X installed builds a GDK that is only
# Wayland. The backend is built whenever Xlib is found, unless
# --disable-x11-backend is given; --enable-x11-backend makes it a
# requirement.
if test "x$enable_x11_backend" != "xno"; then
  AC_PATH_X
  AC_PATH_XTRA

  have_x11=no
  if test "x$no_x" != "xyes"; then
    x11_saved_cflags="$CFLAGS"
    x11_saved_ldflags="$LDFLAGS"
    CFLAGS="$CFLAGS $X_CFLAGS"
    LDFLAGS="$LDFLAGS $X_LIBS"
    AC_CHECK_LIB(X11, XOpenDisplay, have_x11=yes, , $X_EXTRA_LIBS)
    CFLAGS="$x11_saved_cflags"
    LDFLAGS="$x11_saved_ldflags"
  fi

  if test "x$have_x11" = "xyes"; then
    enable_x11_backend=yes
  elif test "x$enable_x11_backend" = "xyes"; then
    AC_MSG_ERROR([
*** The X11 backend needs Xlib's headers and library, which were not
*** found. Check 'config.log' for more details.])
  else
    enable_x11_backend=no
  fi
fi

GDK_WAYLAND_PUBLIC_H=""
GDK_X11_PUBLIC_H=""
if test "x$enable_x11_backend" = "xyes"; then

GDK_X11_PUBLIC_H="gdkx.h"

CFLAGS="$CFLAGS $X_CFLAGS"
LDFLAGS="$LDFLAGS $X_LIBS"

# Checks for libraries.
# Check for the X11 library
AC_CHECK_LIB(X11, XOpenDisplay, x_libs="-lX11 $X_EXTRA_LIBS",
  AC_MSG_ERROR([*** libX11 not found. Check 'config.log' for more details.]),
  $X_EXTRA_LIBS)

# Check for shaped window extension

AC_CHECK_LIB(Xext, XShapeCombineMask,
      if test -z "`echo $x_libs | grep "\-lXext" 2> /dev/null`"; then
           x_libs="-lXext $x_libs"
      fi
      AC_DEFINE(HAVE_SHAPE_EXT),
      ,
      $x_libs)

# Check for the RandR extension, which says how often a screen is
# refreshed. Without it nothing can.

AC_ARG_ENABLE(xrandr,
  [AS_HELP_STRING([--disable-xrandr],
    [do not use the X RandR extension for refresh rates])],,
  [enable_xrandr=auto])
if test "x$enable_xrandr" != "xno"; then
  have_xrandr=no
  AC_CHECK_HEADER(X11/extensions/Xrandr.h,
    [AC_CHECK_LIB(Xrandr, XRRGetScreenResourcesCurrent,
      [x_libs="-lXrandr $x_libs"
       AC_DEFINE(HAVE_XRANDR)
       have_xrandr=yes],
      , $x_libs)],
    , [#include <X11/Xlib.h>])
  if test "x$enable_xrandr" = "xyes" && test "x$have_xrandr" != "xyes"; then
    AC_MSG_ERROR([*** --enable-xrandr needs libXrandr 1.3 or later and its header.])
  fi
fi

# Check for XIM support.

AC_CHECK_LIB(X11, XUnregisterIMInstantiateCallback,
            : ,
            enable_xim_inst="no",
            $x_libs)

if test x$enable_xim_inst = xmaybe ; then
  enable_xim_inst="yes"
fi

if test "x$enable_xim" = "xyes"; then
  GTK_XIM_FLAGS="-DUSE_XIM"
  if test "x$enable_xim_inst" = "xyes"; then
    AC_DEFINE(USE_X11R6_XIM)
  fi
fi

fi # enable_x11_backend
AC_SUBST(GDK_X11_PUBLIC_H)

dnl Fonts: FreeType draws all text, and fontconfig finds the fonts.
dnl Both are found with pkg-config; freetype-config was dropped by
dnl FreeType itself.
scalable_fonts_modules="freetype2 fontconfig"
AC_MSG_CHECKING(for $scalable_fonts_modules)
if test -n "$PKG_CONFIG" && $PKG_CONFIG --exists $scalable_fonts_modules; then
  SCALABLE_FONTS_CFLAGS=`$PKG_CONFIG --cflags $scalable_fonts_modules`
  SCALABLE_FONTS_LIBS=`$PKG_CONFIG --libs $scalable_fonts_modules`
  AC_MSG_RESULT(yes)
else
  AC_MSG_RESULT(no)
  AC_MSG_ERROR([
*** CGTK draws text with FreeType, and finds fonts with fontconfig:
*** $scalable_fonts_modules, found with pkg-config.])
fi
AC_SUBST(SCALABLE_FONTS_CFLAGS)
AC_SUBST(SCALABLE_FONTS_LIBS)

dnl Pictures: PNG files are read with libpng, by its simplified API,
dnl which came with 1.6.
AC_MSG_CHECKING(for libpng 1.6 or later)
if test -n "$PKG_CONFIG" && $PKG_CONFIG --exists "libpng >= 1.6"; then
  PNG_CFLAGS=`$PKG_CONFIG --cflags libpng`
  PNG_LIBS=`$PKG_CONFIG --libs libpng`
  AC_MSG_RESULT(yes)
else
  AC_MSG_RESULT(no)
  AC_MSG_ERROR([
*** CGTK reads PNG files with libpng, 1.6 or later, found with
*** pkg-config.])
fi
AC_SUBST(PNG_CFLAGS)
AC_SUBST(PNG_LIBS)

dnl What the Wayland backend is built with.
WAYLAND_CFLAGS=""
WAYLAND_LIBS=""
WAYLAND_PROTOCOLS_DIR=""
dnl The Wayland backend is built whenever what it needs is found, unless
dnl --disable-wayland-backend is given; --enable-wayland-backend makes
dnl it a requirement.
dnl wayland-scanner runs during the build, so when cross-compiling it is
dnl the build machine's, from PATH, not the one the host's pkg-config
dnl names; WAYLAND_SCANNER set by hand wins either way.
AC_ARG_VAR([WAYLAND_SCANNER], [the wayland-scanner to run during the build])
if test "x$enable_wayland_backend" != "xno"; then
  wayland_modules="wayland-client wayland-cursor xkbcommon"
  AC_MSG_CHECKING([for $wayland_modules and wayland-protocols 1.20])
  have_wayland=no
  if test -n "$PKG_CONFIG" && \
     $PKG_CONFIG --exists $wayland_modules "wayland-protocols >= 1.20"; then
    WAYLAND_CFLAGS=`$PKG_CONFIG --cflags $wayland_modules`
    WAYLAND_LIBS=`$PKG_CONFIG --libs $wayland_modules`
    WAYLAND_PROTOCOLS_DIR=`$PKG_CONFIG --variable=pkgdatadir wayland-protocols`
    if test -n "$WAYLAND_PROTOCOLS_DIR"; then
      have_wayland=yes
    fi
  fi
  AC_MSG_RESULT($have_wayland)
  if test "x$have_wayland" = "xyes"; then
    if test -z "$WAYLAND_SCANNER" && test "x$cross_compiling" != xyes && \
       $PKG_CONFIG --exists wayland-scanner; then
      WAYLAND_SCANNER=`$PKG_CONFIG --variable=wayland_scanner wayland-scanner`
    fi
    AC_PATH_PROG([WAYLAND_SCANNER], [wayland-scanner])
    if test -z "$WAYLAND_SCANNER"; then
      have_wayland=no
    fi
  fi
  if test "x$have_wayland" = "xyes"; then
    enable_wayland_backend=yes
  elif test "x$enable_wayland_backend" = "xyes"; then
    AC_MSG_ERROR([*** The Wayland backend needs $wayland_modules and wayland-protocols 1.20 or later, found with pkg-config, and wayland-scanner.])
  else
    enable_wayland_backend=no
    WAYLAND_CFLAGS=
    WAYLAND_LIBS=
    WAYLAND_SCANNER=
    WAYLAND_PROTOCOLS_DIR=
  fi
fi
dnl A window's icon is given to a Wayland compositor by the
dnl xdg-toplevel-icon protocol, which wayland-protocols has had since
dnl 1.37. With an older one the backend is built without it, and a
dnl window has the icon of its desktop file, as with a compositor that
dnl does not know the protocol.
have_toplevel_icon=no
if test "x$enable_wayland_backend" = "xyes"; then
  AC_MSG_CHECKING([for the xdg-toplevel-icon protocol])
  if test -f "$WAYLAND_PROTOCOLS_DIR/staging/xdg-toplevel-icon/xdg-toplevel-icon-v1.xml"; then
    have_toplevel_icon=yes
    AC_DEFINE([HAVE_XDG_TOPLEVEL_ICON], [1],
              [Define if wayland-protocols has xdg-toplevel-icon-v1.])
  fi
  AC_MSG_RESULT($have_toplevel_icon)
fi
AM_CONDITIONAL([HAVE_XDG_TOPLEVEL_ICON], [test "x$have_toplevel_icon" = "xyes"])
AC_SUBST(WAYLAND_CFLAGS)
AC_SUBST(WAYLAND_LIBS)
AC_SUBST(WAYLAND_SCANNER)
AC_SUBST(WAYLAND_PROTOCOLS_DIR)
if test "x$enable_wayland_backend" = "xyes"; then
  GDK_WAYLAND_PUBLIC_H="gdkwayland.h"
fi
AC_SUBST(GDK_WAYLAND_PUBLIC_H)

dnl GtkGLArea, the OpenGL widget, in a library of its own. It draws
dnl through EGL on every backend, so EGL is all it needs, with
dnl wayland-egl for the Wayland backend's windows; the library GL itself
dnl is in is the business of the programs that draw with it. Built
dnl whenever those are found, unless --disable-gl is given; --enable-gl
dnl makes it a requirement.
GTKGL_CFLAGS=""
GTKGL_LIBS=""
GTKGL_NODELETE=""
if test "x$enable_gl" != "xno"; then
  gtkgl_modules="egl"
  if test "x$enable_wayland_backend" = "xyes"; then
    gtkgl_modules="$gtkgl_modules wayland-egl wayland-client"
  fi
  AC_MSG_CHECKING([for $gtkgl_modules])
  have_gl=no
  if test -n "$PKG_CONFIG" && $PKG_CONFIG --exists $gtkgl_modules; then
    GTKGL_CFLAGS=`$PKG_CONFIG --cflags $gtkgl_modules`
    GTKGL_LIBS=`$PKG_CONFIG --libs $gtkgl_modules`
    have_gl=yes
  fi
  AC_MSG_RESULT($have_gl)
  if test "x$have_gl" = "xyes"; then
    enable_gl=yes
  elif test "x$enable_gl" = "xyes"; then
    AC_MSG_ERROR([*** GtkGLArea needs $gtkgl_modules, found with pkg-config.])
  else
    enable_gl=no
  fi
fi
if test "x$enable_gl" = "xyes"; then
  dnl Once loaded the library stays: see gtkgl/Makefile.am.
  AC_MSG_CHECKING([whether the linker accepts -z nodelete])
  gtkgl_save_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS -Wl,-z,nodelete"
  AC_LINK_IFELSE([AC_LANG_PROGRAM([], [return 0;])],
    [GTKGL_NODELETE="-Wl,-z,nodelete"; AC_MSG_RESULT(yes)], [AC_MSG_RESULT(no)])
  LDFLAGS="$gtkgl_save_LDFLAGS"
fi
AC_SUBST(GTKGL_CFLAGS)
AC_SUBST(GTKGL_LIBS)
AC_SUBST(GTKGL_NODELETE)
AM_CONDITIONAL([BUILD_GTKGL], [test "x$enable_gl" = "xyes"])

x_cflags="$X_CFLAGS"
x_ldflags="$X_LDFLAGS $X_LIBS"

CFLAGS="$saved_cflags"
LDFLAGS="$saved_ldflags"

AC_SUBST(x_cflags)
AC_SUBST(x_ldflags)
AC_SUBST(x_libs)

# Checks for library functions.
AC_CHECK_FUNCS(getresuid)
AC_TYPE_UID_T


AC_SUBST(GTK_DEBUG_FLAGS)
AC_SUBST(GTK_XIM_FLAGS)

dnl The backends compiled in, for gdkconfig.h and for the Makefiles.
GDK_WINDOWING_X11_DEFINE=""
GDK_WINDOWING_WAYLAND_DEFINE=""
if test "x$enable_x11_backend" = "xyes"; then
  GDK_WINDOWING_X11_DEFINE="#define GDK_WINDOWING_X11 1"
fi
if test "x$enable_wayland_backend" = "xyes"; then
  GDK_WINDOWING_WAYLAND_DEFINE="#define GDK_WINDOWING_WAYLAND 1"
fi
if test "x$enable_x11_backend" != "xyes" && test "x$enable_wayland_backend" != "xyes"; then
  AC_MSG_ERROR([
*** GDK needs a backend, but what the X11 and the Wayland backends need
*** was not found, or both were disabled. Install the development files
*** for Xlib, or for Wayland.])
fi
AC_SUBST(GDK_WINDOWING_X11_DEFINE)
AC_SUBST(GDK_WINDOWING_WAYLAND_DEFINE)
AM_CONDITIONAL([USE_X11_BACKEND], [test "x$enable_x11_backend" = "xyes"])
AM_CONDITIONAL([USE_WAYLAND_BACKEND], [test "x$enable_wayland_backend" = "xyes"])



AC_CONFIG_FILES([
glib/glibconfig.h
Makefile
glib/Makefile
glib/glib-config
glib/gmodule/Makefile
glib/gthread/Makefile
glib/docs/glib-config.1
glib/tests/Makefile
glib/glib-1.4.pc
glib/gmodule-1.4.pc
glib/gthread-1.4.pc
glib/glib.pc
glib/gmodule.pc
glib/gthread.pc
gdk/gdkconfig.h
docs/gtk-config.1
gtk-config
po/Makefile.in
gdk/Makefile
gtk/Makefile
gtk/gtkfeatures.h
gtkgl/Makefile
gtkgl-1.4.pc
themes/Makefile
cgtk-theme/Makefile
gdk-1.4.pc
gtk+-1.4.pc
gdk.pc
gtk+.pc
])
AC_CONFIG_COMMANDS([config-scripts],
  [chmod +x gtk-config glib/glib-config])
AC_OUTPUT

gdk_backends=
test "x$enable_x11_backend" = "xyes" && gdk_backends="$gdk_backends X11"
test "x$enable_wayland_backend" = "xyes" && gdk_backends="$gdk_backends Wayland"
echo
echo "GDK backends:$gdk_backends"
echo "GtkGLArea: $enable_gl"
echo "Default theme: ${GTK_DEFAULT_THEME:-none}"
