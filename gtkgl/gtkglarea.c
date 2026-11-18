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
 * A drawing area with a GL context: a window GL draws in, which is
 * sized, shown and sent its events like any drawing area's. The
 * program draws from its expose handler, between
 * gtk_gl_area_make_current and gtk_gl_area_swap_buffers, and sets its
 * viewport from its configure handler; the widget does no more than
 * hold the context and tell GDK the window is GL's.
 */

#include "config.h"

#include <stdarg.h>
#include <gtk/gtk.h>

#include "gdkgl.h"
#include "gtkglarea.h"

static void gtk_gl_area_class_init (GtkGLAreaClass *klass);
static void gtk_gl_area_init       (GtkGLArea      *glarea);
static void gtk_gl_area_destroy    (GtkObject      *object);
static void gtk_gl_area_realize    (GtkWidget      *widget);

static GtkDrawingAreaClass *parent_class = NULL;

GtkType
gtk_gl_area_get_type (void)
{
  static GtkType gl_area_type = 0;

  if (!gl_area_type)
    {
      static const GtkTypeInfo gl_area_info =
      {
        "GtkGLArea",
        sizeof (GtkGLArea),
        sizeof (GtkGLAreaClass),
        (GtkClassInitFunc) gtk_gl_area_class_init,
        (GtkObjectInitFunc) gtk_gl_area_init,
        /* reserved_1 */ NULL,
        /* reserved_2 */ NULL,
        (GtkClassInitFunc) NULL,
      };

      gl_area_type = gtk_type_unique (GTK_TYPE_DRAWING_AREA, &gl_area_info);
    }

  return gl_area_type;
}

static void
gtk_gl_area_class_init (GtkGLAreaClass *klass)
{
  GtkObjectClass *object_class = (GtkObjectClass *) klass;
  GtkWidgetClass *widget_class = (GtkWidgetClass *) klass;

  parent_class = gtk_type_class (GTK_TYPE_DRAWING_AREA);

  object_class->destroy = gtk_gl_area_destroy;
  widget_class->realize = gtk_gl_area_realize;
}

static void
gtk_gl_area_init (GtkGLArea *glarea)
{
  glarea->glcontext = NULL;
}

GtkWidget *
gtk_gl_area_new (int *attrlist)
{
  return gtk_gl_area_share_new (attrlist, NULL);
}

GtkWidget *
gtk_gl_area_share_new (int *attrlist, GtkGLArea *share)
{
  GdkVisual *visual;
  GdkGLContext *glcontext;
  GtkGLArea *glarea;

  g_return_val_if_fail (attrlist != NULL, NULL);
  g_return_val_if_fail (share == NULL || GTK_IS_GL_AREA (share), NULL);

  visual = gdk_gl_choose_visual (attrlist);
  if (visual == NULL)
    return NULL;

  glcontext = gdk_gl_context_share_new (visual, share ? share->glcontext : NULL, TRUE);
  if (glcontext == NULL)
    return NULL;

  glarea = gtk_type_new (GTK_TYPE_GL_AREA);
  glarea->glcontext = glcontext;

  return GTK_WIDGET (glarea);
}

/* Whether an attribute stands alone in a list; every other has its
 * value after it, which may be zero and is not the end of the list. */
static gboolean
attribute_stands_alone (int attribute)
{
  return attribute == GDK_GL_USE_GL || attribute == GDK_GL_RGBA ||
    attribute == GDK_GL_DOUBLEBUFFER || attribute == GDK_GL_STEREO;
}

GtkWidget *
gtk_gl_area_new_vargs (GtkGLArea *share, ...)
{
  GtkWidget *glarea;
  va_list ap;
  int *attrlist;
  int attribute, i;

  va_start (ap, share);
  i = 1;
  while ((attribute = va_arg (ap, int)) != GDK_GL_NONE)
    {
      i++;
      if (!attribute_stands_alone (attribute))
        {
          (void) va_arg (ap, int);
          i++;
        }
    }
  va_end (ap);

  attrlist = g_new (int, i);

  va_start (ap, share);
  i = 0;
  while ((attrlist[i] = va_arg (ap, int)) != GDK_GL_NONE)
    {
      if (!attribute_stands_alone (attrlist[i++]))
        attrlist[i++] = va_arg (ap, int);
    }
  va_end (ap);

  glarea = gtk_gl_area_share_new (attrlist, share);

  g_free (attrlist);

  return glarea;
}

static void
gtk_gl_area_destroy (GtkObject *object)
{
  GtkGLArea *glarea;

  g_return_if_fail (object != NULL);
  g_return_if_fail (GTK_IS_GL_AREA (object));

  glarea = GTK_GL_AREA (object);
  if (glarea->glcontext)
    gdk_gl_context_unref (glarea->glcontext);
  glarea->glcontext = NULL;

  if (GTK_OBJECT_CLASS (parent_class)->destroy)
    (* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}

static void
gtk_gl_area_realize (GtkWidget *widget)
{
  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_GL_AREA (widget));

  GTK_WIDGET_CLASS (parent_class)->realize (widget);

  /*
   * The window is GL's from the start, whether or not the program has
   * drawn yet: GDK keeps it in its place and paints nothing there. A
   * program that made the area current as it was realized has had
   * this done already.
   */
  if (!gdk_window_is_external (widget->window))
    gdk_window_set_external (widget->window, NULL, NULL);
}

gint
gtk_gl_area_make_current (GtkGLArea *glarea)
{
  g_return_val_if_fail (glarea != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_GL_AREA (glarea), FALSE);
  g_return_val_if_fail (GTK_WIDGET_REALIZED (glarea), FALSE);

  return gdk_gl_make_current (GTK_WIDGET (glarea)->window, glarea->glcontext);
}

void
gtk_gl_area_swap_buffers (GtkGLArea *glarea)
{
  g_return_if_fail (glarea != NULL);
  g_return_if_fail (GTK_IS_GL_AREA (glarea));
  g_return_if_fail (GTK_WIDGET_REALIZED (glarea));

  gdk_gl_swap_buffers (GTK_WIDGET (glarea)->window);
}

void
gtk_gl_area_get_drawable_size (GtkGLArea *glarea,
                               gint      *width,
                               gint      *height)
{
  GtkWidget *widget;

  g_return_if_fail (glarea != NULL);
  g_return_if_fail (GTK_IS_GL_AREA (glarea));

  widget = GTK_WIDGET (glarea);
  if (GTK_WIDGET_REALIZED (widget))
    gdk_gl_get_drawable_size (widget->window, width, height);
  else
    {
      /* Not on any display yet: the screen's scale is the best guess. */
      gint scale = gdk_screen_scale_factor ();

      if (width)
        *width = MAX (widget->allocation.width, 1) * scale;
      if (height)
        *height = MAX (widget->allocation.height, 1) * scale;
    }
}

/* ---------------------------------------------------------------------
 * The names of before 1.2.2
 */

void
gtk_gl_area_swapbuffers (GtkGLArea *glarea)
{
  gtk_gl_area_swap_buffers (glarea);
}

gint
gtk_gl_area_begingl (GtkGLArea *glarea)
{
  return gtk_gl_area_make_current (glarea);
}

void
gtk_gl_area_endgl (GtkGLArea *glarea)
{
  static GdkGLProc flush = NULL;

  g_return_if_fail (glarea != NULL);
  g_return_if_fail (GTK_IS_GL_AREA (glarea));

  /* glFlush, found by name: the library it is in is the program's. */
  if (!flush)
    flush = gdk_gl_get_proc_address ("glFlush");
  if (flush)
    flush ();
}

void
gtk_gl_area_size (GtkGLArea *glarea, gint width, gint height)
{
  g_return_if_fail (glarea != NULL);
  g_return_if_fail (GTK_IS_GL_AREA (glarea));

  gtk_widget_set_usize (GTK_WIDGET (glarea), width, height);
}
