/* GDK - The GIMP Drawing Kit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
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
 * Modified by the GTK+ Team and others 1997-1999. See the AUTHORS
 * file for a list of people on the GTK+ Team. See the ChangeLog
 * files for a list of changes. These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/.
 */

/*
 * Graphics contexts. A GC is nothing but the values set on it, read
 * by the rasteriser when something is drawn with it; there is no
 * server to send them to. The defaults are the X server's.
 */

#include <string.h>
#include "gdk.h"
#include "gdkprivate.h"
#include "gdkframebuffer.h"

static void
gdk_gc_unref_values (GdkGCPrivate *private)
{
  GdkGCValues *v = &private->values;

  if (v->font)
    gdk_font_unref (v->font);
  if (v->tile)
    gdk_pixmap_unref (v->tile);
  if (v->stipple)
    gdk_pixmap_unref (v->stipple);
  if (v->clip_mask)
    gdk_pixmap_unref (v->clip_mask);
  v->font = NULL;
  v->tile = v->stipple = v->clip_mask = NULL;
  if (private->clip_region)
    gdk_region_destroy (private->clip_region);
  private->clip_region = NULL;
  g_free (private->dashes);
  private->dashes = NULL;
  private->n_dashes = 0;
  private->dash_offset = 0;
}

GdkGC*
gdk_gc_new (GdkWindow *window)
{
  return gdk_gc_new_with_values (window, NULL, 0);
}

GdkGC*
gdk_gc_new_with_values (GdkWindow       *window,
                        GdkGCValues     *values,
                        GdkGCValuesMask  values_mask)
{
  GdkWindowPrivate *window_private;
  GdkGCPrivate *private;
  GdkGCValues *v;

  g_return_val_if_fail (window != NULL, NULL);

  window_private = (GdkWindowPrivate*) window;
  if (window_private->destroyed)
    return NULL;

  private = g_new0 (GdkGCPrivate, 1);
  private->ref_count = 1;
  private->alpha = 1.0;
  v = &private->values;

  /* The X defaults: black on white, solid, copy, thin solid lines. */
  v->foreground.pixel = 0;
  v->background.pixel = 0xffffff;
  v->function = GDK_COPY;
  v->fill = GDK_SOLID;
  v->subwindow_mode = GDK_CLIP_BY_CHILDREN;
  v->graphics_exposures = TRUE;
  v->line_width = 0;
  v->line_style = GDK_LINE_SOLID;
  v->cap_style = GDK_CAP_BUTT;
  v->join_style = GDK_JOIN_MITER;

  if (values_mask & GDK_GC_FOREGROUND)
    gdk_gc_set_foreground ((GdkGC *) private, &values->foreground);
  if (values_mask & GDK_GC_BACKGROUND)
    gdk_gc_set_background ((GdkGC *) private, &values->background);
  if ((values_mask & GDK_GC_FONT) && values->font)
    gdk_gc_set_font ((GdkGC *) private, values->font);
  if (values_mask & GDK_GC_FUNCTION)
    v->function = values->function;
  if (values_mask & GDK_GC_FILL)
    v->fill = values->fill;
  if ((values_mask & GDK_GC_TILE) && values->tile)
    gdk_gc_set_tile ((GdkGC *) private, values->tile);
  if ((values_mask & GDK_GC_STIPPLE) && values->stipple)
    gdk_gc_set_stipple ((GdkGC *) private, values->stipple);
  if ((values_mask & GDK_GC_CLIP_MASK) && values->clip_mask)
    gdk_gc_set_clip_mask ((GdkGC *) private, values->clip_mask);
  if (values_mask & GDK_GC_SUBWINDOW)
    v->subwindow_mode = values->subwindow_mode;
  if (values_mask & GDK_GC_TS_X_ORIGIN)
    v->ts_x_origin = values->ts_x_origin;
  if (values_mask & GDK_GC_TS_Y_ORIGIN)
    v->ts_y_origin = values->ts_y_origin;
  if (values_mask & GDK_GC_CLIP_X_ORIGIN)
    v->clip_x_origin = values->clip_x_origin;
  if (values_mask & GDK_GC_CLIP_Y_ORIGIN)
    v->clip_y_origin = values->clip_y_origin;
  if (values_mask & GDK_GC_EXPOSURES)
    v->graphics_exposures = values->graphics_exposures;
  if (values_mask & GDK_GC_LINE_WIDTH)
    v->line_width = values->line_width;
  if (values_mask & GDK_GC_LINE_STYLE)
    v->line_style = values->line_style;
  if (values_mask & GDK_GC_CAP_STYLE)
    v->cap_style = values->cap_style;
  if (values_mask & GDK_GC_JOIN_STYLE)
    v->join_style = values->join_style;

  return (GdkGC*) private;
}

void
gdk_gc_destroy (GdkGC *gc)
{
  gdk_gc_unref (gc);
}

GdkGC *
gdk_gc_ref (GdkGC *gc)
{
  GdkGCPrivate *private = (GdkGCPrivate*) gc;

  g_return_val_if_fail (gc != NULL, NULL);
  private->ref_count += 1;

  return gc;
}

void
gdk_gc_unref (GdkGC *gc)
{
  GdkGCPrivate *private = (GdkGCPrivate*) gc;

  g_return_if_fail (gc != NULL);
  g_return_if_fail (private->ref_count > 0);

  if (private->ref_count > 1)
    private->ref_count -= 1;
  else
    {
      gdk_gc_unref_values (private);
      if (private->impl)
        _gdk_backend_gc_free (private);
      memset (gc, 0, sizeof (GdkGCPrivate));
      g_free (gc);
    }
}

/*
 * The values as they were set. The pixmaps and font handed back are
 * still the GC's; a caller that keeps them refs them, as before.
 */
void
gdk_gc_get_values (GdkGC       *gc,
                   GdkGCValues *values)
{
  g_return_if_fail (gc != NULL);
  g_return_if_fail (values != NULL);

  *values = ((GdkGCPrivate*) gc)->values;
}

void
gdk_gc_set_foreground (GdkGC    *gc,
                       GdkColor *color)
{
  g_return_if_fail (gc != NULL);
  g_return_if_fail (color != NULL);

  ((GdkGCPrivate*) gc)->values.foreground = *color;
}

void
gdk_gc_set_background (GdkGC    *gc,
                       GdkColor *color)
{
  g_return_if_fail (gc != NULL);
  g_return_if_fail (color != NULL);

  ((GdkGCPrivate*) gc)->values.background = *color;
}

void
gdk_gc_set_font (GdkGC   *gc,
                 GdkFont *font)
{
  GdkGCPrivate *private;

  g_return_if_fail (gc != NULL);
  g_return_if_fail (font != NULL);

  private = (GdkGCPrivate*) gc;
  gdk_font_ref (font);
  if (private->values.font)
    gdk_font_unref (private->values.font);
  private->values.font = font;
}

void
gdk_gc_set_function (GdkGC       *gc,
                     GdkFunction  function)
{
  g_return_if_fail (gc != NULL);

  ((GdkGCPrivate*) gc)->values.function = function;
}

void
gdk_gc_set_fill (GdkGC   *gc,
                 GdkFill  fill)
{
  g_return_if_fail (gc != NULL);

  ((GdkGCPrivate*) gc)->values.fill = fill;
}

void
gdk_gc_set_tile (GdkGC     *gc,
                 GdkPixmap *tile)
{
  GdkGCPrivate *private;

  g_return_if_fail (gc != NULL);

  private = (GdkGCPrivate*) gc;
  if (tile)
    gdk_pixmap_ref (tile);
  if (private->values.tile)
    gdk_pixmap_unref (private->values.tile);
  private->values.tile = tile;
}

void
gdk_gc_set_stipple (GdkGC     *gc,
                    GdkPixmap *stipple)
{
  GdkGCPrivate *private;

  g_return_if_fail (gc != NULL);

  private = (GdkGCPrivate*) gc;
  if (stipple)
    gdk_pixmap_ref (stipple);
  if (private->values.stipple)
    gdk_pixmap_unref (private->values.stipple);
  private->values.stipple = stipple;
}

void
gdk_gc_set_ts_origin (GdkGC *gc,
                      gint   x,
                      gint   y)
{
  g_return_if_fail (gc != NULL);

  ((GdkGCPrivate*) gc)->values.ts_x_origin = x;
  ((GdkGCPrivate*) gc)->values.ts_y_origin = y;
}

void
gdk_gc_set_clip_origin (GdkGC *gc,
                        gint   x,
                        gint   y)
{
  g_return_if_fail (gc != NULL);

  ((GdkGCPrivate*) gc)->values.clip_x_origin = x;
  ((GdkGCPrivate*) gc)->values.clip_y_origin = y;
}

/*
 * A clip is a bitmap or a region, whichever was set last; the origin
 * is separate and stays as it was, as it did with X.
 */
void
gdk_gc_set_clip_mask (GdkGC     *gc,
                      GdkBitmap *mask)
{
  GdkGCPrivate *private;

  g_return_if_fail (gc != NULL);

  private = (GdkGCPrivate*) gc;
  if (mask)
    {
      if (((GdkWindowPrivate*) mask)->destroyed)
        return;
      gdk_pixmap_ref (mask);
    }
  if (private->values.clip_mask)
    gdk_pixmap_unref (private->values.clip_mask);
  private->values.clip_mask = mask;
  if (private->clip_region)
    gdk_region_destroy (private->clip_region);
  private->clip_region = NULL;
}

void
gdk_gc_set_clip_rectangle (GdkGC        *gc,
                           GdkRectangle *rectangle)
{
  GdkGCPrivate *private;

  g_return_if_fail (gc != NULL);

  private = (GdkGCPrivate*) gc;
  gdk_gc_set_clip_mask (gc, NULL);
  if (rectangle)
    {
      private->clip_region = gdk_region_new ();
      _gdk_region_add_rect (private->clip_region, rectangle);
      gdk_gc_set_clip_origin (gc, 0, 0);
    }
}

void
gdk_gc_set_clip_region (GdkGC            *gc,
                        GdkRegion        *region)
{
  GdkGCPrivate *private;

  g_return_if_fail (gc != NULL);

  private = (GdkGCPrivate*) gc;
  gdk_gc_set_clip_mask (gc, NULL);
  if (region)
    {
      private->clip_region = _gdk_region_copy (region);
      gdk_gc_set_clip_origin (gc, 0, 0);
    }
}

void
gdk_gc_set_subwindow (GdkGC            *gc,
                      GdkSubwindowMode  mode)
{
  g_return_if_fail (gc != NULL);

  ((GdkGCPrivate*) gc)->values.subwindow_mode = mode;
}

void
gdk_gc_set_exposures (GdkGC     *gc,
                      gboolean   exposures)
{
  g_return_if_fail (gc != NULL);

  ((GdkGCPrivate*) gc)->values.graphics_exposures = exposures;
}

void
gdk_gc_set_line_attributes (GdkGC       *gc,
                            gint         line_width,
                            GdkLineStyle line_style,
                            GdkCapStyle  cap_style,
                            GdkJoinStyle join_style)
{
  GdkGCPrivate *private;

  g_return_if_fail (gc != NULL);

  private = (GdkGCPrivate*) gc;
  private->values.line_width = line_width;
  private->values.line_style = line_style;
  private->values.cap_style = cap_style;
  private->values.join_style = join_style;
}

/*
 * Whether rounded rectangles, fractional polygons and arcs are drawn
 * antialiased: each device pixel on a shape's edge takes as much of the
 * foreground as the shape covers of it. Off, as for everything else
 * GDK draws, a pixel is drawn or not.
 */
void
gdk_gc_set_antialias (GdkGC    *gc,
                      gboolean  antialias)
{
  g_return_if_fail (gc != NULL);

  ((GdkGCPrivate *) gc)->antialias = antialias;
}

/*
 * How opaque the foreground is, from 0.0 to 1.0, when rounded
 * rectangles, fractional polygons and antialiased arcs are drawn; other
 * drawing is always opaque. It is laid over what is there, when the
 * function is GDK_COPY and the fill GDK_SOLID.
 */
void
gdk_gc_set_alpha (GdkGC   *gc,
                  gdouble  alpha)
{
  g_return_if_fail (gc != NULL);

  ((GdkGCPrivate *) gc)->alpha = CLAMP (alpha, 0.0, 1.0);
}

void
gdk_gc_set_dashes (GdkGC      *gc,
                   gint        dash_offset,
                   gint8       dash_list[],
                   gint        n)
{
  GdkGCPrivate *private;

  g_return_if_fail (gc != NULL);
  g_return_if_fail (dash_list != NULL);
  g_return_if_fail (n > 0);

  private = (GdkGCPrivate*) gc;
  g_free (private->dashes);
  private->dashes = g_new (gint8, n);
  memcpy (private->dashes, dash_list, n);
  private->n_dashes = n;
  private->dash_offset = dash_offset;
}

void
gdk_gc_copy (GdkGC *dst_gc, GdkGC *src_gc)
{
  GdkGCPrivate *dst_private, *src_private;
  GdkGCValues *v;

  g_return_if_fail (dst_gc != NULL);
  g_return_if_fail (src_gc != NULL);

  src_private = (GdkGCPrivate *) src_gc;
  dst_private = (GdkGCPrivate *) dst_gc;

  gdk_gc_unref_values (dst_private);
  dst_private->values = src_private->values;
  v = &dst_private->values;
  if (v->font)
    gdk_font_ref (v->font);
  if (v->tile)
    gdk_pixmap_ref (v->tile);
  if (v->stipple)
    gdk_pixmap_ref (v->stipple);
  if (v->clip_mask)
    gdk_pixmap_ref (v->clip_mask);
  if (src_private->clip_region)
    dst_private->clip_region = _gdk_region_copy (src_private->clip_region);
  if (src_private->dashes)
    gdk_gc_set_dashes (dst_gc, src_private->dash_offset,
                       src_private->dashes, src_private->n_dashes);
  dst_private->antialias = src_private->antialias;
  dst_private->alpha = src_private->alpha;
}
