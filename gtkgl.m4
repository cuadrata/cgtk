# Configure paths for GtkGLArea

dnl AM_PATH_GTKGL([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for GtkGLArea, and define GTKGL_CFLAGS and GTKGL_LIBS. As the
dnl macro of this name always did, GTKGL_LIBS has the library GL itself
dnl is in as well: OpenGL where the system has it, which is GL and
dnl nothing of a window system, else GL.
dnl
AC_DEFUN([AM_PATH_GTKGL],
[AC_REQUIRE([AM_PATH_GTK])dnl
  GTKGL_CFLAGS=""
  GTKGL_LIBS=""
  AC_MSG_CHECKING([for GtkGLArea])
  if test -n "$PKG_CONFIG" && test "x$PKG_CONFIG" != "xno" && \
     $PKG_CONFIG --exists $gtk_modules gtkgl-1.4; then
    GTKGL_CFLAGS=`$PKG_CONFIG --cflags $gtk_modules gtkgl-1.4`
    GTKGL_LIBS=`$PKG_CONFIG --libs $gtk_modules gtkgl-1.4`
    AC_MSG_RESULT([yes])

    gtkgl_gl_libs="-lGL"
    for gtkgl_gl in opengl gl; do
      if $PKG_CONFIG --exists $gtkgl_gl 2> /dev/null; then
        GTKGL_CFLAGS="$GTKGL_CFLAGS `$PKG_CONFIG --cflags $gtkgl_gl`"
        gtkgl_gl_libs=`$PKG_CONFIG --libs $gtkgl_gl`
        break
      fi
    done
    GTKGL_LIBS="$GTKGL_LIBS $gtkgl_gl_libs"
    ifelse([$1], , :, [$1])
  else
    AC_MSG_RESULT([no])
    ifelse([$2], , :, [$2])
  fi
  AC_SUBST(GTKGL_CFLAGS)
  AC_SUBST(GTKGL_LIBS)
])
