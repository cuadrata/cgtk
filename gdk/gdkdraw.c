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
 * The drawing functions. Each one makes a paint for the drawable's
 * framebuffer, hands it to the rasteriser, and records what was
 * touched as damage for the backend to put on the screen.
 *
 * A window draws into its toplevel's framebuffer, clipped to the part
 * of it that shows; a GC in GDK_INCLUDE_INFERIORS mode is clipped by
 * the window's children no more than the X server clipped it.
 */

#include <string.h>
#include "gdk.h"
#include "gdkprivate.h"
#include "gdkrgb.h"
#include "gdkframebuffer.h"
#include "gdkftfont.h"

typedef void (*GdkDrawFunc) (GdkPaint *paint, gpointer data);

static void
draw (GdkDrawable *drawable,
      GdkGC       *gc,
      GdkDrawFunc  func,
      gpointer     data)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) drawable;
  GdkWindowPrivate *native;
  GdkRegion *clip = NULL;
  GdkPaint paint;
  gint dx, dy;

  g_return_if_fail (drawable != NULL);
  g_return_if_fail (gc != NULL);

  if (private->destroyed || private->depth == 0 || !_gdk_drawable_fb (drawable))
    return;

  native = _gdk_window_native (private, &dx, &dy);
  if (native->window_type != GDK_WINDOW_PIXMAP)
    {
      clip = _gdk_window_visible_region (private,
                                         ((GdkGCPrivate *) gc)->values.subwindow_mode
                                         == GDK_INCLUDE_INFERIORS);
      if (gdk_region_empty (clip))
        {
          gdk_region_destroy (clip);
          return;
        }
    }

  if (_gdk_paint_init (&paint, native->fb, gc, dx, dy, clip))
    {
      func (&paint, data);
      if (paint.damage.width > 0 && paint.damage.height > 0)
        _gdk_window_damage (native, &paint.damage);
      _gdk_paint_finish (&paint);
    }
  if (clip)
    gdk_region_destroy (clip);
}

/* ---------------------------------------------------------------------- */

typedef struct { gint x, y; } PointArgs;

static void
draw_point_func (GdkPaint *paint, gpointer data)
{
  PointArgs *a = data;
  _gdk_paint_point (paint, a->x, a->y);
}

void
gdk_draw_point (GdkDrawable *drawable,
                GdkGC       *gc,
                gint         x,
                gint         y)
{
  PointArgs a = { x, y };
  draw (drawable, gc, draw_point_func, &a);
}

typedef struct { gint x1, y1, x2, y2; } LineArgs;

static void
draw_line_func (GdkPaint *paint, gpointer data)
{
  LineArgs *a = data;
  _gdk_paint_line (paint, a->x1, a->y1, a->x2, a->y2);
}

void
gdk_draw_line (GdkDrawable *drawable,
               GdkGC       *gc,
               gint         x1,
               gint         y1,
               gint         x2,
               gint         y2)
{
  LineArgs a = { x1, y1, x2, y2 };
  draw (drawable, gc, draw_line_func, &a);
}

typedef struct { gboolean filled; gint x, y, width, height, angle1, angle2; } RectArgs;

static void
draw_rectangle_func (GdkPaint *paint, gpointer data)
{
  RectArgs *a = data;
  _gdk_paint_rectangle (paint, a->filled, a->x, a->y, a->width, a->height);
}

void
gdk_draw_rectangle (GdkDrawable *drawable,
                    GdkGC       *gc,
                    gint         filled,
                    gint         x,
                    gint         y,
                    gint         width,
                    gint         height)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) drawable;
  RectArgs a;

  g_return_if_fail (drawable != NULL);

  if (width == -1)
    width = private->width;
  if (height == -1)
    height = private->height;

  a.filled = filled;
  a.x = x;
  a.y = y;
  a.width = width;
  a.height = height;
  draw (drawable, gc, draw_rectangle_func, &a);
}

static void
draw_arc_func (GdkPaint *paint, gpointer data)
{
  RectArgs *a = data;
  _gdk_paint_arc (paint, a->filled, a->x, a->y, a->width, a->height,
                  a->angle1, a->angle2);
}

void
gdk_draw_arc (GdkDrawable *drawable,
              GdkGC       *gc,
              gint         filled,
              gint         x,
              gint         y,
              gint         width,
              gint         height,
              gint         angle1,
              gint         angle2)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) drawable;
  RectArgs a;

  g_return_if_fail (drawable != NULL);

  if (width == -1)
    width = private->width;
  if (height == -1)
    height = private->height;

  a.filled = filled;
  a.x = x;
  a.y = y;
  a.width = width;
  a.height = height;
  a.angle1 = angle1;
  a.angle2 = angle2;
  draw (drawable, gc, draw_arc_func, &a);
}

typedef struct { gboolean filled; GdkPoint *points; gint npoints; gboolean closed; } PointsArgs;

static void
draw_polygon_func (GdkPaint *paint, gpointer data)
{
  PointsArgs *a = data;
  _gdk_paint_polygon (paint, a->filled, a->points, a->npoints);
}

void
gdk_draw_polygon (GdkDrawable *drawable,
                  GdkGC       *gc,
                  gint         filled,
                  GdkPoint    *points,
                  gint         npoints)
{
  PointsArgs a;

  g_return_if_fail (points != NULL);

  a.filled = filled;
  a.points = points;
  a.npoints = npoints;
  draw (drawable, gc, draw_polygon_func, &a);
}

typedef struct { gint filled; GdkPointF *points; gint npoints; } PointsFArgs;

static void
draw_polygon_f_func (GdkPaint *paint, gpointer data)
{
  PointsFArgs *a = data;
  _gdk_paint_polygon_f (paint, a->filled, a->points, a->npoints);
}

/*
 * A polygon whose corners need not lie on pixels, drawn through
 * coverage: antialiased if the GC says so, at the GC's alpha. Filled,
 * what the outline encloses, by the non-zero rule; unfilled, each side
 * a band as wide as the line.
 */
void
gdk_draw_polygon_f (GdkDrawable *drawable,
                    GdkGC       *gc,
                    gint         filled,
                    GdkPointF   *points,
                    gint         npoints)
{
  PointsFArgs a;

  g_return_if_fail (points != NULL);

  a.filled = filled;
  a.points = points;
  a.npoints = npoints;
  draw (drawable, gc, draw_polygon_f_func, &a);
}

typedef struct { gint filled, x, y, width, height; gdouble radius; } RoundedArgs;

static void
draw_rounded_func (GdkPaint *paint, gpointer data)
{
  RoundedArgs *a = data;
  _gdk_paint_rounded_rectangle (paint, a->filled, a->x, a->y,
                                a->width, a->height, a->radius);
}

/*
 * A rectangle with rounded corners, drawn through coverage: antialiased
 * if the GC says so, at the GC's alpha. Filled, it covers the pixels x,
 * y, width, height but for its corners; unlike gdk_draw_rectangle's, its
 * outline lies inside those pixels too, as wide as the line. radius is
 * the corners' on the outside.
 */
void
gdk_draw_rounded_rectangle (GdkDrawable *drawable,
                            GdkGC       *gc,
                            gint         filled,
                            gint         x,
                            gint         y,
                            gint         width,
                            gint         height,
                            gdouble      radius)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) drawable;
  RoundedArgs a;

  g_return_if_fail (drawable != NULL);

  if (width == -1)
    width = private->width;
  if (height == -1)
    height = private->height;

  a.filled = filled;
  a.x = x;
  a.y = y;
  a.width = width;
  a.height = height;
  a.radius = radius;
  draw (drawable, gc, draw_rounded_func, &a);
}

/* ---------------------------------------------------------------------- */

typedef struct { GdkFont *font; gint x, y; const guint32 *chars; gint n; } TextArgs;

static void
draw_text_func (GdkPaint *paint, gpointer data)
{
  TextArgs *a = data;
  _gdk_ft_draw_text (paint, a->font, a->x, a->y, a->chars, a->n);
}

/* Text as the font reads it: characters, drawn with the pen at x, y. */
static void
draw_chars (GdkDrawable   *drawable,
            GdkFont       *font,
            GdkGC         *gc,
            gint           x,
            gint           y,
            const guint32 *chars,
            gint           n)
{
  TextArgs a;

  if (n <= 0 || !((GdkFontPrivate *) font)->ft)
    return;

  a.font = font;
  a.x = x;
  a.y = y;
  a.chars = chars;
  a.n = n;
  draw (drawable, gc, draw_text_func, &a);
}

void
gdk_draw_string (GdkDrawable *drawable,
                 GdkFont     *font,
                 GdkGC       *gc,
                 gint         x,
                 gint         y,
                 const gchar *string)
{
  g_return_if_fail (string != NULL);

  gdk_draw_text (drawable, font, gc, x, y, string, strlen (string));
}

void
gdk_draw_text (GdkDrawable *drawable,
               GdkFont     *font,
               GdkGC       *gc,
               gint         x,
               gint         y,
               const gchar *text,
               gint         text_length)
{
  guint32 *chars;
  gint n;

  g_return_if_fail (drawable != NULL);
  g_return_if_fail (font != NULL);
  g_return_if_fail (gc != NULL);
  g_return_if_fail (text != NULL);

  if (text_length <= 0)
    return;

  chars = g_new (guint32, text_length);
  n = _gdk_font_ft_decode (font, text, text_length, chars);
  draw_chars (drawable, font, gc, x, y, chars, n);
  g_free (chars);
}

void
gdk_draw_text_wc (GdkDrawable    *drawable,
                  GdkFont        *font,
                  GdkGC          *gc,
                  gint            x,
                  gint            y,
                  const GdkWChar *text,
                  gint            text_length)
{
  g_return_if_fail (drawable != NULL);
  g_return_if_fail (font != NULL);
  g_return_if_fail (gc != NULL);
  g_return_if_fail (text != NULL);

  /* Wide characters are Unicode already. */
  draw_chars (drawable, font, gc, x, y, text, text_length);
}

/* ---------------------------------------------------------------------- */

typedef struct { GdkFramebuffer *src; GdkImage *image; gint xsrc, ysrc, xdest, ydest, width, height; } CopyArgs;

static void
draw_pixmap_func (GdkPaint *paint, gpointer data)
{
  CopyArgs *a = data;
  _gdk_paint_copy (paint, a->src, a->xsrc, a->ysrc, a->xdest, a->ydest,
                   a->width, a->height);
}

void
gdk_draw_pixmap (GdkDrawable *drawable,
                 GdkGC       *gc,
                 GdkPixmap   *src,
                 gint         xsrc,
                 gint         ysrc,
                 gint         xdest,
                 gint         ydest,
                 gint         width,
                 gint         height)
{
  GdkWindowPrivate *src_private;
  CopyArgs a;

  g_return_if_fail (drawable != NULL);
  g_return_if_fail (src != NULL);
  g_return_if_fail (gc != NULL);

  src_private = (GdkWindowPrivate*) src;
  if (src_private->destroyed)
    return;

  if (width == -1)
    width = src_private->width;
  if (height == -1)
    height = src_private->height;

  if (src_private->window_type != GDK_WINDOW_PIXMAP)
    {
      /*
       * A window gives what is within it and no more, as in X: around
       * it in its toplevel's pixels are its neighbours. What would
       * have come from outside it is left as it is.
       */
      if (xsrc < 0)
        {
          xdest -= xsrc;
          width += xsrc;
          xsrc = 0;
        }
      if (ysrc < 0)
        {
          ydest -= ysrc;
          height += ysrc;
          ysrc = 0;
        }
      width = MIN (width, (gint) src_private->width - xsrc);
      height = MIN (height, (gint) src_private->height - ysrc);
      if (width <= 0 || height <= 0)
        return;
    }

  a.src = _gdk_drawable_fb (src);
  if (!a.src)
    return;
  /* A child window's pixels are at its offset in its toplevel's. */
  _gdk_window_native (src_private, &a.xsrc, &a.ysrc);
  a.xsrc += xsrc;
  a.ysrc += ysrc;
  a.xdest = xdest;
  a.ydest = ydest;
  a.width = width;
  a.height = height;
  draw (drawable, gc, draw_pixmap_func, &a);
}

/*
 * An RGB image with scale device pixels to each of the toolkit's, for a
 * program that makes its pixels for the device: rgb_buf holds
 * width * scale by height * scale pixels, where x, y, width and height
 * are the toolkit's, as everywhere else. Drawn on a drawable of the
 * same scale -- gdk_window_get_scale_factor() -- the pixels land as
 * they are; on another they are resampled.
 */
void
gdk_draw_rgb_image_scaled (GdkDrawable  *drawable,
                           GdkGC        *gc,
                           gint          x,
                           gint          y,
                           gint          width,
                           gint          height,
                           gint          scale,
                           GdkRgbDither  dith,
                           guchar       *rgb_buf,
                           gint          rowstride)
{
  GdkFramebuffer *fb;
  CopyArgs a;
  gint row, col;

  g_return_if_fail (drawable != NULL);
  g_return_if_fail (gc != NULL);
  g_return_if_fail (rgb_buf != NULL);
  g_return_if_fail (scale >= 1);

  if (width <= 0 || height <= 0)
    return;

  fb = _gdk_framebuffer_new (width, height, 24, scale);
  for (row = 0; row < fb->height; row++)
    {
      const guchar *in = rgb_buf + (gsize) row * rowstride;
      guint32 *out = fb->pixels + (gsize) row * fb->stride;

      for (col = 0; col < fb->width; col++, in += 3)
        out[col] = (guint32) in[0] << 16 | (guint32) in[1] << 8 | in[2];
    }

  a.src = fb;
  a.image = NULL;
  a.xsrc = 0;
  a.ysrc = 0;
  a.xdest = x;
  a.ydest = y;
  a.width = width;
  a.height = height;
  draw (drawable, gc, draw_pixmap_func, &a);

  _gdk_framebuffer_free (fb);
}

typedef struct
{
  guchar *rgba;
  gint stride, scale, x, y, width, height;
} RgbaArgs;

static void
draw_rgba_func (GdkPaint *paint, gpointer data)
{
  RgbaArgs *a = data;
  _gdk_paint_rgba (paint, a->rgba, a->stride, a->scale, a->x, a->y,
                   a->width, a->height);
}

/*
 * An RGBA image, premultiplied, laid over what the drawable shows:
 * rgba_buf holds width * scale by height * scale pixels, as for
 * gdk_draw_rgb_image_scaled().
 */
void
gdk_draw_rgba_image_scaled (GdkDrawable  *drawable,
                            GdkGC        *gc,
                            gint          x,
                            gint          y,
                            gint          width,
                            gint          height,
                            gint          scale,
                            guchar       *rgba_buf,
                            gint          rowstride)
{
  RgbaArgs a;

  g_return_if_fail (drawable != NULL);
  g_return_if_fail (gc != NULL);
  g_return_if_fail (rgba_buf != NULL);
  g_return_if_fail (scale >= 1);

  if (width <= 0 || height <= 0)
    return;

  a.rgba = rgba_buf;
  a.stride = rowstride;
  a.scale = scale;
  a.x = x;
  a.y = y;
  a.width = width;
  a.height = height;
  draw (drawable, gc, draw_rgba_func, &a);
}

static void
draw_image_func (GdkPaint *paint, gpointer data)
{
  CopyArgs *a = data;
  _gdk_paint_image (paint, a->image, a->xsrc, a->ysrc, a->xdest, a->ydest,
                    a->width, a->height);
}

void
gdk_draw_image (GdkDrawable *drawable,
                GdkGC       *gc,
                GdkImage    *image,
                gint         xsrc,
                gint         ysrc,
                gint         xdest,
                gint         ydest,
                gint         width,
                gint         height)
{
  CopyArgs a;

  g_return_if_fail (drawable != NULL);
  g_return_if_fail (image != NULL);
  g_return_if_fail (gc != NULL);

  if (width == -1)
    width = image->width;
  if (height == -1)
    height = image->height;

  a.image = image;
  a.xsrc = xsrc;
  a.ysrc = ysrc;
  a.xdest = xdest;
  a.ydest = ydest;
  a.width = width;
  a.height = height;
  draw (drawable, gc, draw_image_func, &a);
}

/* ---------------------------------------------------------------------- */

static void
draw_points_func (GdkPaint *paint, gpointer data)
{
  PointsArgs *a = data;
  _gdk_paint_points (paint, a->points, a->npoints);
}

void
gdk_draw_points (GdkDrawable *drawable,
                 GdkGC       *gc,
                 GdkPoint    *points,
                 gint         npoints)
{
  PointsArgs a;

  g_return_if_fail ((points != NULL) && (npoints > 0));

  a.points = points;
  a.npoints = npoints;
  draw (drawable, gc, draw_points_func, &a);
}

typedef struct { GdkSegment *segs; gint nsegs; } SegsArgs;

static void
draw_segments_func (GdkPaint *paint, gpointer data)
{
  SegsArgs *a = data;
  _gdk_paint_segments (paint, a->segs, a->nsegs);
}

void
gdk_draw_segments (GdkDrawable *drawable,
                   GdkGC       *gc,
                   GdkSegment  *segs,
                   gint         nsegs)
{
  SegsArgs a;

  if (nsegs <= 0)
    return;

  g_return_if_fail (segs != NULL);

  a.segs = segs;
  a.nsegs = nsegs;
  draw (drawable, gc, draw_segments_func, &a);
}

static void
draw_lines_func (GdkPaint *paint, gpointer data)
{
  PointsArgs *a = data;
  _gdk_paint_lines (paint, a->points, a->npoints, FALSE);
}

void
gdk_draw_lines (GdkDrawable *drawable,
                GdkGC       *gc,
                GdkPoint    *points,
                gint         npoints)
{
  PointsArgs a;

  if (npoints <= 0)
    return;

  g_return_if_fail (points != NULL);

  a.points = points;
  a.npoints = npoints;
  draw (drawable, gc, draw_lines_func, &a);
}
