# Configure paths for GLib, from its pkg-config files.

dnl AM_PATH_GLIB([MINIMUM-VERSION, [ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND [, MODULES]]]])
dnl Test for GLib, and define GLIB_CFLAGS and GLIB_LIBS. MODULES may name
dnl gmodule and gthread. --with-glib-prefix (or --with-glib-exec-prefix)
dnl looks in that prefix's lib/pkgconfig first. --disable-glibtest skips
dnl linking a test program. GLIB_CONFIG is glib-config, for scripts that
dnl still ask it.
dnl
AC_DEFUN([AM_PATH_GLIB],
[dnl
AC_ARG_WITH(glib-prefix,[  --with-glib-prefix=PFX   Prefix where GLIB is installed (optional)],
            glib_config_prefix="$withval", glib_config_prefix="")
AC_ARG_WITH(glib-exec-prefix,[  --with-glib-exec-prefix=PFX Exec prefix where GLIB is installed (optional)],
            glib_config_exec_prefix="$withval", glib_config_exec_prefix="")
AC_ARG_ENABLE(glibtest, [  --disable-glibtest       Do not try to link a test GLIB program],
                    , enable_glibtest=yes)

  glib_modules="glib-1.4"
  glib_config_args=
  for module in . $4
  do
      case "$module" in
         gmodule)
             glib_modules="$glib_modules gmodule-1.4"
             glib_config_args="$glib_config_args gmodule"
         ;;
         gthread)
             glib_modules="$glib_modules gthread-1.4"
             glib_config_args="$glib_config_args gthread"
         ;;
      esac
  done

  for glib_prefix in "$glib_config_prefix" "$glib_config_exec_prefix"; do
    if test -n "$glib_prefix"; then
      PKG_CONFIG_PATH="$glib_prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
      export PKG_CONFIG_PATH
      if test x${GLIB_CONFIG+set} != xset ; then
        GLIB_CONFIG=$glib_prefix/bin/glib-config
      fi
    fi
  done

  if test -z "$PKG_CONFIG"; then
    AC_PATH_PROG(PKG_CONFIG, pkg-config, no)
  fi
  AC_PATH_PROG(GLIB_CONFIG, glib-config, no)

  min_glib_version=ifelse([$1], ,0.99.7,$1)
  AC_MSG_CHECKING(for GLIB - version >= $min_glib_version)
  no_glib=""
  if test "x$PKG_CONFIG" = "xno" || test -z "$PKG_CONFIG"; then
    no_glib=yes
    glib_why="pkg-config was not found"
  elif ! $PKG_CONFIG --exists $glib_modules; then
    no_glib=yes
    glib_why="pkg-config cannot find $glib_modules"
  elif ! $PKG_CONFIG --atleast-version=$min_glib_version glib-1.4; then
    no_glib=yes
    glib_why="GLib `$PKG_CONFIG --modversion glib-1.4` is older"
  else
    GLIB_CFLAGS=`$PKG_CONFIG --cflags $glib_modules`
    GLIB_LIBS=`$PKG_CONFIG --libs $glib_modules`
    if test "x$enable_glibtest" = "xyes" ; then
      ac_save_CFLAGS="$CFLAGS"
      ac_save_LIBS="$LIBS"
      CFLAGS="$CFLAGS $GLIB_CFLAGS"
      LIBS="$GLIB_LIBS $LIBS"
      AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <glib.h>]],
          [[return glib_major_version != GLIB_MAJOR_VERSION;]])],,
        [no_glib=yes
         glib_why="a test program did not link with $GLIB_LIBS"])
      CFLAGS="$ac_save_CFLAGS"
      LIBS="$ac_save_LIBS"
    fi
  fi
  if test "x$no_glib" = x ; then
     glib_found_version=`$PKG_CONFIG --modversion glib-1.4`
     AC_MSG_RESULT([yes ($glib_found_version)])
     ifelse([$2], , :, [$2])
  else
     AC_MSG_RESULT([no: $glib_why])
     GLIB_CFLAGS=""
     GLIB_LIBS=""
     ifelse([$3], , :, [$3])
  fi
  AC_SUBST(GLIB_CFLAGS)
  AC_SUBST(GLIB_LIBS)
])
