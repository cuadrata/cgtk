/* GtkGLArea - an OpenGL widget for GTK+
 * Copyright (C) 2026 The CGTK authors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, see
 * <https://www.gnu.org/licenses/>.
 */

/*
 * A drawing area OpenGL draws in. The interface is that of Janne
 * Lof's GtkGLArea for GTK+ 1.2: see gdkgl.h for what is behind it and
 * where it differs.
 */

#ifndef __GTK_GL_AREA_H__
#define __GTK_GL_AREA_H__

#include <gdk/gdk.h>
#include <gtkgl/gdkgl.h>
#include <gtk/gtkdrawingarea.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GTK_TYPE_GL_AREA            (gtk_gl_area_get_type ())
#define GTK_GL_AREA(obj)            (GTK_CHECK_CAST ((obj), GTK_TYPE_GL_AREA, GtkGLArea))
#define GTK_GL_AREA_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), GTK_TYPE_GL_AREA, GtkGLAreaClass))
#define GTK_IS_GL_AREA(obj)         (GTK_CHECK_TYPE ((obj), GTK_TYPE_GL_AREA))
#define GTK_IS_GL_AREA_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), GTK_TYPE_GL_AREA))

typedef struct _GtkGLArea       GtkGLArea;
typedef struct _GtkGLAreaClass  GtkGLAreaClass;

struct _GtkGLArea
{
  GtkDrawingArea darea;
  GdkGLContext *glcontext;
};

struct _GtkGLAreaClass
{
  GtkDrawingAreaClass parent_class;
};

GtkType    gtk_gl_area_get_type     (void);

/*
 * A new area for what the attribute list asks: see gdkgl.h for the
 * list. NULL if the system has nothing to answer it with. An area
 * made with share has the textures and display lists of that one.
 */
GtkWidget *gtk_gl_area_new          (int       *attrList);
GtkWidget *gtk_gl_area_share_new    (int       *attrList,
                                     GtkGLArea *share);
GtkWidget *gtk_gl_area_new_vargs    (GtkGLArea *share,
                                     ...);

/* GL draws in the area from here on; FALSE if it cannot. */
gint       gtk_gl_area_make_current (GtkGLArea *glarea);
/* What was drawn is shown. */
void       gtk_gl_area_swap_buffers (GtkGLArea *glarea);

/*
 * The size of what GL draws the area in, in the device's pixels:
 * what a viewport is set from, in the handler of configure_event. It
 * is the allocation times the scale of the display the area is on.
 */
void       gtk_gl_area_get_drawable_size (GtkGLArea *glarea,
                                          gint      *width,
                                          gint      *height);

/* As they were named before 1.2.2, and still are in old programs. */
void       gtk_gl_area_swapbuffers  (GtkGLArea *glarea);
gint       gtk_gl_area_begingl      (GtkGLArea *glarea);
void       gtk_gl_area_endgl        (GtkGLArea *glarea);
void       gtk_gl_area_size         (GtkGLArea *glarea,
                                     gint       width,
                                     gint       height);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GTK_GL_AREA_H__ */
