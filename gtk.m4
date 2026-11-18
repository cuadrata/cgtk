# Configure paths for GTK+, from its pkg-config files.

dnl AM_PATH_GTK([MINIMUM-VERSION, [ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND [, MODULES]]]])
dnl Test for GTK, and define GTK_CFLAGS and GTK_LIBS. MODULES may name
dnl gthread. --with-gtk-prefix (or --with-gtk-exec-prefix) looks in that
dnl prefix's lib/pkgconfig first. --disable-gtktest skips linking a test
dnl program. GTK_CONFIG is gtk-config, for scripts that still ask it.
dnl
AC_DEFUN([AM_PATH_GTK],
[dnl
AC_ARG_WITH(gtk-prefix,[  --with-gtk-prefix=PFX   Prefix where GTK is installed (optional)],
            gtk_config_prefix="$withval", gtk_config_prefix="")
AC_ARG_WITH(gtk-exec-prefix,[  --with-gtk-exec-prefix=PFX Exec prefix where GTK is installed (optional)],
            gtk_config_exec_prefix="$withval", gtk_config_exec_prefix="")
AC_ARG_ENABLE(gtktest, [  --disable-gtktest       Do not try to link a test GTK program],
                    , enable_gtktest=yes)

  gtk_modules="gtk+-1.4"
  gtk_config_args=
  for module in . $4
  do
      case "$module" in
         gthread)
             gtk_modules="$gtk_modules gthread-1.4"
             gtk_config_args="$gtk_config_args gthread"
         ;;
      esac
  done

  for gtk_prefix in "$gtk_config_prefix" "$gtk_config_exec_prefix"; do
    if test -n "$gtk_prefix"; then
      PKG_CONFIG_PATH="$gtk_prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
      export PKG_CONFIG_PATH
      if test x${GTK_CONFIG+set} != xset ; then
        GTK_CONFIG=$gtk_prefix/bin/gtk-config
      fi
    fi
  done

  if test -z "$PKG_CONFIG"; then
    AC_PATH_PROG(PKG_CONFIG, pkg-config, no)
  fi
  AC_PATH_PROG(GTK_CONFIG, gtk-config, no)

  min_gtk_version=ifelse([$1], ,0.99.7,$1)
  AC_MSG_CHECKING(for GTK - version >= $min_gtk_version)
  no_gtk=""
  if test "x$PKG_CONFIG" = "xno" || test -z "$PKG_CONFIG"; then
    no_gtk=yes
    gtk_why="pkg-config was not found"
  elif ! $PKG_CONFIG --exists $gtk_modules; then
    no_gtk=yes
    gtk_why="pkg-config cannot find $gtk_modules"
  elif ! $PKG_CONFIG --atleast-version=$min_gtk_version gtk+-1.4; then
    no_gtk=yes
    gtk_why="GTK+ `$PKG_CONFIG --modversion gtk+-1.4` is older"
  else
    GTK_CFLAGS=`$PKG_CONFIG --cflags $gtk_modules`
    GTK_LIBS=`$PKG_CONFIG --libs $gtk_modules`
    if test "x$enable_gtktest" = "xyes" ; then
      ac_save_CFLAGS="$CFLAGS"
      ac_save_LIBS="$LIBS"
      CFLAGS="$CFLAGS $GTK_CFLAGS"
      LIBS="$GTK_LIBS $LIBS"
      AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <gtk/gtk.h>]],
          [[return gtk_major_version != GTK_MAJOR_VERSION;]])],,
        [no_gtk=yes
         gtk_why="a test program did not link with $GTK_LIBS"])
      CFLAGS="$ac_save_CFLAGS"
      LIBS="$ac_save_LIBS"
    fi
  fi
  if test "x$no_gtk" = x ; then
     gtk_found_version=`$PKG_CONFIG --modversion gtk+-1.4`
     AC_MSG_RESULT([yes ($gtk_found_version)])
     ifelse([$2], , :, [$2])
  else
     AC_MSG_RESULT([no: $gtk_why])
     GTK_CFLAGS=""
     GTK_LIBS=""
     ifelse([$3], , :, [$3])
  fi
  AC_SUBST(GTK_CFLAGS)
  AC_SUBST(GTK_LIBS)
])
