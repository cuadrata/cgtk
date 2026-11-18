/* GDK - The GIMP Drawing Kit
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
 * The rasteriser: how GDK's drawing primitives become pixels in a
 * framebuffer. It draws what the X server drew for GDK, the same way
 * where that matters -- one pixel wide lines are Bresenham lines,
 * nothing is antialiased, and every pixel goes through the GC's
 * function, so XOR drawing undoes itself -- and the same to look at
 * where it does not.
 *
 * All coordinates arriving at a primitive are in the drawable's own
 * pixels; they are offset by the paint's (dx, dy) and multiplied by its
 * scale on the way in, and from there on everything is device pixels.
 *
 * At a scale above one, what the X server drew is drawn that many
 * times bigger, but at the device's resolution: a thin line is the
 * Bresenham line between the scaled endpoints drawn with a pen of
 * scale by scale device pixels, so a horizontal or vertical one is the
 * scale 1 line enlarged and a diagonal is as smooth as the device
 * allows, which is what the theme engines' arrows and corners want; a
 * wide line is that many times wider; a filled shape is scan converted
 * in device pixels. A pixmap drawn is enlarged by sampling, and text
 * is rasterised afresh for the device, which is the point of the
 * exercise.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "gdk.h"
#include "gdkprivate.h"
#include "gdkframebuffer.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const gint8 default_dashes[2] = { 4, 4 };

/* ---------------------------------------------------------------------
 * Pixels
 */

static inline guint32
function_apply (GdkFunction function, guint32 src, guint32 dst)
{
  switch (function)
    {
    case GDK_COPY:        return src;
    case GDK_INVERT:      return ~dst;
    case GDK_XOR:         return src ^ dst;
    case GDK_CLEAR:       return 0;
    case GDK_AND:         return src & dst;
    case GDK_AND_REVERSE: return src & ~dst;
    case GDK_AND_INVERT:  return ~src & dst;
    case GDK_NOOP:        return dst;
    case GDK_OR:          return src | dst;
    case GDK_EQUIV:       return ~src ^ dst;
    case GDK_OR_REVERSE:  return src | ~dst;
    case GDK_COPY_INVERT: return ~src;
    case GDK_OR_INVERT:   return ~src | dst;
    case GDK_NAND:        return ~src | ~dst;
    case GDK_SET:         return ~0u;
    }
  return src;
}

#define DEPTH_MASK(fb) ((fb)->depth == 1 ? 1u : 0xffffffu)

static inline guint32
fb_get (GdkFramebuffer *fb, gint x, gint y)
{
  return fb->pixels[(gsize) y * fb->stride + x];
}

/* Wrapped into [0, n). */
static inline gint
wrap (gint v, gint n)
{
  v %= n;
  return v < 0 ? v + n : v;
}

/* Rounded towards minus infinity. */
static inline gint
floor_div (gint a, gint b)
{
  gint q = a / b;
  return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

/*
 * The pixel of a framebuffer under device pixel (x, y) of a paint at
 * scale, wrapped for tiles and stipples: a pixmap at scale 1 drawn
 * through a paint at scale 2 shows each of its pixels twice.
 */
static inline guint32
fb_sample (GdkFramebuffer *fb, gint x, gint y, gint scale)
{
  if (fb->scale != scale)
    {
      x = floor_div (x * fb->scale, scale);
      y = floor_div (y * fb->scale, scale);
    }
  return fb_get (fb, wrap (x, fb->width), wrap (y, fb->height));
}

static inline gboolean
mask_allows (GdkPaint *paint, gint x, gint y)
{
  GdkFramebuffer *mask = paint->mask;

  if (!mask)
    return TRUE;
  x -= paint->mask_x;
  y -= paint->mask_y;
  if (x < 0 || y < 0)
    return FALSE;
  if (mask->scale != paint->scale)
    {
      x = x * mask->scale / paint->scale;
      y = y * mask->scale / paint->scale;
    }
  if (x >= mask->width || y >= mask->height)
    return FALSE;
  return (fb_get (mask, x, y) & 1) != 0;
}

/*
 * The source pixel for a device pixel, from the fill style. FALSE
 * means the pixel is not painted (a stippled fill's holes).
 */
static inline gboolean
source_pixel (GdkPaint *paint, gint x, gint y, guint32 *src)
{
  switch (paint->fill)
    {
    case GDK_TILED:
      {
        *src = fb_sample (paint->tile, x - paint->ts_x, y - paint->ts_y,
                          paint->scale);
        return TRUE;
      }
    case GDK_STIPPLED:
    case GDK_OPAQUE_STIPPLED:
      {
        gboolean bit = fb_sample (paint->stipple, x - paint->ts_x,
                                  y - paint->ts_y, paint->scale) & 1;
        if (bit)
          *src = paint->fg;
        else if (paint->fill == GDK_OPAQUE_STIPPLED)
          *src = paint->bg;
        else
          return FALSE;
        return TRUE;
      }
    default:
      *src = paint->fg;
      return TRUE;
    }
}

static inline void
put_pixel (GdkPaint *paint, gint x, gint y, guint32 src)
{
  guint32 *p = paint->fb->pixels + (gsize) y * paint->fb->stride + x;

  *p = function_apply (paint->function, src, *p) & DEPTH_MASK (paint->fb);
}

static void
damage_add (GdkPaint *paint, gint x, gint y, gint width, gint height)
{
  GdkRectangle rect, clipped;

  rect.x = x;
  rect.y = y;
  rect.width = width;
  rect.height = height;
  if (!gdk_rectangle_intersect (&rect, &GDK_REGION_EXTENTS (paint->clip),
                                &clipped))
    return;
  if (paint->damage.width == 0)
    paint->damage = clipped;
  else
    gdk_rectangle_union (&paint->damage, &clipped, &paint->damage);
}

/*
 * One span of device pixels, [x1, x2) on row y, through the clip, the
 * mask and the fill style. solid draws the foreground whatever the
 * fill style, for points. src_row, when given, supplies the source
 * pixels instead: src_row[x - x1].
 */
static void
span (GdkPaint      *paint,
      gint           y,
      gint           x1,
      gint           x2,
      gboolean       solid,
      const guint32 *src_row)
{
  GdkRectangle *rects = GDK_REGION_RECTS (paint->clip);
  gint n = GDK_REGION_N_RECTS (paint->clip);
  gint i;

  for (i = 0; i < n; i++)
    {
      GdkRectangle *r = &rects[i];
      gint xa, xb, x;

      if (y < r->y || y >= r->y + r->height)
        continue;
      xa = MAX (x1, r->x);
      xb = MIN (x2, r->x + r->width);
      if (xa >= xb)
        continue;

      /* Copied pixels with nothing in the way: whole rows at a time. */
      if (src_row && !paint->mask && paint->function == GDK_COPY)
        {
          memcpy (paint->fb->pixels + (gsize) y * paint->fb->stride + xa,
                  src_row + (xa - x1), (gsize) (xb - xa) * sizeof (guint32));
          continue;
        }

      if (!paint->mask && !src_row && (solid || paint->fill == GDK_SOLID))
        {
          guint32 *p = paint->fb->pixels + (gsize) y * paint->fb->stride + xa;
          guint32 mask = DEPTH_MASK (paint->fb);
          guint32 fg = paint->fg;

          if (paint->function == GDK_COPY)
            for (x = xa; x < xb; x++)
              *p++ = fg;
          else
            for (x = xa; x < xb; x++, p++)
              *p = function_apply (paint->function, fg, *p) & mask;
          continue;
        }

      for (x = xa; x < xb; x++)
        {
          guint32 src;

          if (!mask_allows (paint, x, y))
            continue;
          if (src_row)
            src = src_row[x - x1];
          else if (solid)
            src = paint->fg;
          else if (!source_pixel (paint, x, y, &src))
            continue;
          put_pixel (paint, x, y, src);
        }
    }
}

static inline void
pixel (GdkPaint *paint, gint x, gint y, gboolean solid)
{
  span (paint, y, x, x + 1, solid, NULL);
}

/* ---------------------------------------------------------------------
 * Paints
 */

static void
region_scale (GdkRegion *region, gint scale)
{
  GdkRegionPrivate *private = (GdkRegionPrivate *) region;
  gint i;

  if (scale == 1)
    return;
  for (i = 0; i < private->n_rects; i++)
    {
      private->rects[i].x *= scale;
      private->rects[i].y *= scale;
      private->rects[i].width *= scale;
      private->rects[i].height *= scale;
    }
  private->extents.x *= scale;
  private->extents.y *= scale;
  private->extents.width *= scale;
  private->extents.height *= scale;
}

gboolean
_gdk_paint_init (GdkPaint       *paint,
                 GdkFramebuffer *fb,
                 GdkGC          *gc,
                 gint            dx,
                 gint            dy,
                 GdkRegion      *clip)
{
  GdkGCPrivate *private = (GdkGCPrivate *) gc;
  GdkRectangle bounds;

  memset (paint, 0, sizeof (GdkPaint));
  paint->fb = fb;
  paint->scale = fb->scale;
  paint->dx = dx;
  paint->dy = dy;
  paint->function = GDK_COPY;
  paint->fill = GDK_SOLID;
  paint->line_style = GDK_LINE_SOLID;
  paint->cap_style = GDK_CAP_BUTT;
  paint->dashes = default_dashes;
  paint->n_dashes = 2;
  paint->alpha = 255;
  paint->aa_line_width = fb->scale;

  bounds.x = bounds.y = 0;
  bounds.width = fb->width;
  bounds.height = fb->height;
  if (clip)
    {
      paint->clip = _gdk_region_copy (clip);
      _gdk_region_intersect_rect (paint->clip, &bounds);
    }
  else
    paint->clip = _gdk_region_from_rect (&bounds);

  if (private)
    {
      GdkGCValues *v = &private->values;

      paint->function = v->function;
      paint->fill = v->fill;
      paint->fg = GDK_FB_PIXEL (fb, v->foreground.pixel);
      paint->bg = GDK_FB_PIXEL (fb, v->background.pixel);
      paint->ts_x = (v->ts_x_origin + dx) * paint->scale;
      paint->ts_y = (v->ts_y_origin + dy) * paint->scale;
      if (v->fill == GDK_TILED)
        {
          if (v->tile)
            paint->tile = _gdk_drawable_fb (v->tile);
          if (!paint->tile)
            paint->fill = GDK_SOLID;
        }
      else if (v->fill == GDK_STIPPLED || v->fill == GDK_OPAQUE_STIPPLED)
        {
          if (v->stipple)
            paint->stipple = _gdk_drawable_fb (v->stipple);
          if (!paint->stipple)
            paint->fill = GDK_SOLID;
        }

      if (private->clip_region)
        {
          GdkRegion *clip = _gdk_region_copy (private->clip_region);

          GdkRegion *both;

          gdk_region_offset (clip, v->clip_x_origin + dx, v->clip_y_origin + dy);
          region_scale (clip, paint->scale);
          both = gdk_regions_intersect (paint->clip, clip);
          gdk_region_destroy (clip);
          gdk_region_destroy (paint->clip);
          paint->clip = both;
        }
      else if (v->clip_mask)
        {
          paint->mask = _gdk_drawable_fb (v->clip_mask);
          paint->mask_x = (v->clip_x_origin + dx) * paint->scale;
          paint->mask_y = (v->clip_y_origin + dy) * paint->scale;
          if (paint->mask)
            {
              GdkRectangle extent;

              extent.x = paint->mask_x;
              extent.y = paint->mask_y;
              extent.width = paint->mask->width * paint->scale / paint->mask->scale;
              extent.height = paint->mask->height * paint->scale / paint->mask->scale;
              _gdk_region_intersect_rect (paint->clip, &extent);
            }
        }

      /*
       * Widths 0 and 1 are both thin lines, and stay thin lines at any
       * scale, drawn as blocks: a wider line grows with the scale.
       */
      paint->line_width = v->line_width <= 1 ? v->line_width
                                             : v->line_width * paint->scale;
      paint->line_style = v->line_style;
      paint->cap_style = v->cap_style;
      paint->antialias = private->antialias;
      paint->alpha = private->alpha * 255 + 0.5;
      paint->aa_line_width = MAX (v->line_width, 1) * paint->scale;
      if (private->dashes)
        {
          paint->dashes = private->dashes;
          paint->n_dashes = private->n_dashes;
          paint->dash_offset = private->dash_offset;
        }
    }

  if (gdk_region_empty (paint->clip))
    {
      _gdk_paint_finish (paint);
      return FALSE;
    }
  return TRUE;
}

void
_gdk_paint_finish (GdkPaint *paint)
{
  if (paint->clip)
    gdk_region_destroy (paint->clip);
  paint->clip = NULL;
}

#define DEV_X(paint, x) (((x) + (paint)->dx) * (paint)->scale)
#define DEV_Y(paint, y) (((y) + (paint)->dy) * (paint)->scale)

/* ---------------------------------------------------------------------
 * Points
 */

void
_gdk_paint_point (GdkPaint *paint, gint x, gint y)
{
  gint s = paint->scale;

  x = DEV_X (paint, x);
  y = DEV_Y (paint, y);
  if (s == 1)
    pixel (paint, x, y, TRUE);
  else
    {
      gint i;
      for (i = 0; i < s; i++)
        span (paint, y + i, x, x + s, TRUE, NULL);
    }
  damage_add (paint, x, y, s, s);
}

void
_gdk_paint_points (GdkPaint *paint, GdkPoint *points, gint npoints)
{
  gint i;

  for (i = 0; i < npoints; i++)
    _gdk_paint_point (paint, points[i].x, points[i].y);
}

/* ---------------------------------------------------------------------
 * Thin lines: Bresenham in device pixels, with the dash pattern walked
 * a pixel at a time. The state lets a dash run on across the segments
 * of a polyline. The pen is scale by scale device pixels, with (x, y)
 * its top left, so the dashes are that many times longer too.
 */

typedef struct
{
  gint index;      /* which dash */
  gint left;       /* pixels left in it */
  gboolean on;
  /*
   * The pen's last stamp and its first, for stamping each device pixel
   * once: the pen is scale by scale, one step on from its last stamp
   * all but its leading edge is already painted, and the last stamp of
   * a closed outline runs into the first. A function such as XOR
   * painted a pixel twice would take it back, so a thin XOR line at
   * scale 2 came out as its corners.
   */
  gboolean stamped;   /* the last pixel was stamped, at (px, py) */
  gint px, py;
  gboolean first;     /* a first stamp has been made, at (fx, fy) */
  gint fx, fy;
} DashState;

static void
dash_reset (DashState *ds)
{
  ds->stamped = FALSE;
  ds->first = FALSE;
}

/* Is device pixel (x, y) inside the pen's stamp at (bx, by)? */
static inline gboolean
in_stamp (gint s, gint bx, gint by, gint x, gint y)
{
  return x >= bx && x < bx + s && y >= by && y < by + s;
}

/*
 * The pen stamped at device pixel (x, y), in the foreground through
 * the fill: every device pixel of the stamp painted once, and none
 * that the last stamp or the first one painted.
 */
static void
block (GdkPaint *paint, DashState *ds, gint x, gint y)
{
  gint s = paint->scale;

  if (s == 1)
    pixel (paint, x, y, FALSE);
  else
    {
      gboolean near_last = ds->stamped && ABS (x - ds->px) < s && ABS (y - ds->py) < s;
      gboolean near_first = ds->first && ABS (x - ds->fx) < s && ABS (y - ds->fy) < s;

      if (!near_last && !near_first)
        {
          gint i;

          for (i = 0; i < s; i++)
            span (paint, y + i, x, x + s, FALSE, NULL);
        }
      else
        {
          gint i, j;

          for (j = 0; j < s; j++)
            for (i = 0; i < s; i++)
              if (!(near_last && in_stamp (s, ds->px, ds->py, x + i, y + j)) &&
                  !(near_first && in_stamp (s, ds->fx, ds->fy, x + i, y + j)))
                span (paint, y + j, x + i, x + i + 1, FALSE, NULL);
        }
    }
  ds->stamped = TRUE;
  ds->px = x;
  ds->py = y;
  if (!ds->first)
    {
      ds->first = TRUE;
      ds->fx = x;
      ds->fy = y;
    }
}

static void
dash_start (GdkPaint *paint, DashState *ds)
{
  gint offset = paint->dash_offset * paint->scale;

  dash_reset (ds);
  ds->index = 0;
  ds->on = TRUE;
  ds->left = MAX (paint->dashes[0], 1) * paint->scale;
  while (offset > 0)
    {
      gint take = MIN (offset, ds->left);
      offset -= take;
      ds->left -= take;
      if (ds->left == 0)
        {
          ds->index = (ds->index + 1) % paint->n_dashes;
          ds->left = MAX (paint->dashes[ds->index], 1) * paint->scale;
          ds->on = !ds->on;
        }
    }
}

static inline void
dash_pixel (GdkPaint *paint, DashState *ds, gint x, gint y)
{
  if (paint->line_style == GDK_LINE_SOLID)
    block (paint, ds, x, y);
  else
    {
      /*
       * The pen reaches scale - 1 device pixels past (x, y), so a dash
       * is stamped only while the pen still fits in it, or each dash
       * would run that much into the gap after it: the dashes are in
       * whole pixels of the drawable, and so are the pen's stamps.
       */
      gboolean fits = ds->left >= paint->scale;

      if (ds->on && fits)
        block (paint, ds, x, y);
      else if (!ds->on && fits && paint->line_style == GDK_LINE_DOUBLE_DASH)
        {
          guint32 fg = paint->fg;
          paint->fg = paint->bg;
          block (paint, ds, x, y);
          paint->fg = fg;
        }
      else
        ds->stamped = FALSE;
      if (--ds->left == 0)
        {
          ds->index = (ds->index + 1) % paint->n_dashes;
          ds->left = MAX (paint->dashes[ds->index], 1) * paint->scale;
          ds->on = !ds->on;
        }
    }
}

/* Device pixels. last says whether the end pixel is drawn. */
static void
thin_line (GdkPaint  *paint,
           DashState *ds,
           gint       x1,
           gint       y1,
           gint       x2,
           gint       y2,
           gboolean   last)
{
  gint dx = ABS (x2 - x1), sx = x1 < x2 ? 1 : -1;
  gint dy = -ABS (y2 - y1), sy = y1 < y2 ? 1 : -1;
  gint err = dx + dy;
  gint s = paint->scale;

  damage_add (paint, MIN (x1, x2), MIN (y1, y2), dx + s, -dy + s);

  for (;;)
    {
      gint e2;

      if (x1 == x2 && y1 == y2)
        {
          if (last)
            dash_pixel (paint, ds, x1, y1);
          return;
        }
      dash_pixel (paint, ds, x1, y1);
      e2 = 2 * err;
      if (e2 >= dy)
        {
          err += dy;
          x1 += sx;
        }
      if (e2 <= dx)
        {
          err += dx;
          y1 += sy;
        }
    }
}

/* ---------------------------------------------------------------------
 * Filling polygons, in continuous coordinates.
 */

typedef struct
{
  gdouble x;
  gint dir;
} Crossing;

static int
crossing_compare (const void *a, const void *b)
{
  gdouble d = ((const Crossing *) a)->x - ((const Crossing *) b)->x;
  return d < 0 ? -1 : d > 0 ? 1 : 0;
}

static void
polygon_fill (const gdouble *xs,
              const gdouble *ys,
              gint           n,
              gboolean       winding,
              GdkSpanFunc    func,
              gpointer       data)
{
  Crossing *crossings;
  gdouble ymin, ymax;
  gint i, y, y1, y2;

  if (n < 2)
    return;

  crossings = g_new (Crossing, n);
  ymin = ymax = ys[0];
  for (i = 1; i < n; i++)
    {
      ymin = MIN (ymin, ys[i]);
      ymax = MAX (ymax, ys[i]);
    }
  y1 = (gint) floor (ymin);
  y2 = (gint) ceil (ymax);

  for (y = y1; y < y2; y++)
    {
      gdouble yc = y + 0.5;
      gint nc = 0, wind = 0;

      for (i = 0; i < n; i++)
        {
          gint j = (i + 1) % n;
          gdouble ya = ys[i], yb = ys[j];

          if (ya == yb)
            continue;
          if ((yc >= ya && yc < yb) || (yc >= yb && yc < ya))
            {
              crossings[nc].x = xs[i] + (yc - ya) * (xs[j] - xs[i]) / (yb - ya);
              crossings[nc].dir = yb > ya ? 1 : -1;
              nc++;
            }
        }
      if (nc < 2)
        continue;
      qsort (crossings, nc, sizeof (Crossing), crossing_compare);

      for (i = 0; i < nc - 1; i++)
        {
          gboolean inside;

          if (winding)
            {
              wind += crossings[i].dir;
              inside = wind != 0;
            }
          else
            inside = (i % 2) == 0;

          if (inside)
            {
              gint xa = (gint) ceil (crossings[i].x - 0.5);
              gint xb = (gint) ceil (crossings[i + 1].x - 0.5);
              if (xa < xb)
                func (y, xa, xb, data);
            }
        }
    }

  g_free (crossings);
}

void
_gdk_polygon_spans (GdkPoint    *points,
                    gint         npoints,
                    gboolean     winding,
                    GdkSpanFunc  func,
                    gpointer     data)
{
  gdouble *xs = g_new (gdouble, npoints * 2);
  gdouble *ys = xs + npoints;
  gint i;

  for (i = 0; i < npoints; i++)
    {
      xs[i] = points[i].x;
      ys[i] = points[i].y;
    }
  polygon_fill (xs, ys, npoints, winding, func, data);
  g_free (xs);
}

static void
paint_span_func (gint y, gint x1, gint x2, gpointer data)
{
  span ((GdkPaint *) data, y, x1, x2, FALSE, NULL);
}

/* A polygon in device coordinates filled through the paint. */
static void
fill_polygon (GdkPaint      *paint,
              const gdouble *xs,
              const gdouble *ys,
              gint           n,
              gboolean       winding)
{
  gdouble x1, y1, x2, y2;
  gint i;

  if (n < 3)
    return;
  x1 = x2 = xs[0];
  y1 = y2 = ys[0];
  for (i = 1; i < n; i++)
    {
      x1 = MIN (x1, xs[i]); x2 = MAX (x2, xs[i]);
      y1 = MIN (y1, ys[i]); y2 = MAX (y2, ys[i]);
    }
  damage_add (paint, (gint) floor (x1), (gint) floor (y1),
              (gint) ceil (x2) - (gint) floor (x1) + 1,
              (gint) ceil (y2) - (gint) floor (y1) + 1);
  polygon_fill (xs, ys, n, winding, paint_span_func, paint);
}

/* ---------------------------------------------------------------------
 * Wide lines: each segment a quadrilateral, joined and capped by discs.
 * Device coordinates, at pixel centres.
 */

static void
fill_disc (GdkPaint *paint, gdouble cx, gdouble cy, gdouble r)
{
  gdouble xs[64], ys[64];
  gint n = 64, i;

  for (i = 0; i < n; i++)
    {
      gdouble a = 2 * M_PI * i / n;
      xs[i] = cx + r * cos (a);
      ys[i] = cy - r * sin (a);
    }
  fill_polygon (paint, xs, ys, n, TRUE);
}

static void
wide_segment (GdkPaint *paint,
              gdouble   x1,
              gdouble   y1,
              gdouble   x2,
              gdouble   y2,
              gboolean  first,
              gboolean  last)
{
  gdouble w = paint->line_width / 2.0;
  gdouble dx = x2 - x1, dy = y2 - y1;
  gdouble len = sqrt (dx * dx + dy * dy);
  gdouble nx, ny, xs[4], ys[4];

  if (len == 0)
    {
      if (paint->cap_style == GDK_CAP_ROUND)
        fill_disc (paint, x1, y1, w);
      else if (paint->cap_style == GDK_CAP_PROJECTING)
        {
          xs[0] = x1 - w; ys[0] = y1 - w;
          xs[1] = x1 + w; ys[1] = y1 - w;
          xs[2] = x1 + w; ys[2] = y1 + w;
          xs[3] = x1 - w; ys[3] = y1 + w;
          fill_polygon (paint, xs, ys, 4, TRUE);
        }
      return;
    }

  dx /= len;
  dy /= len;
  if (paint->cap_style == GDK_CAP_PROJECTING)
    {
      if (first)
        {
          x1 -= dx * w;
          y1 -= dy * w;
        }
      if (last)
        {
          x2 += dx * w;
          y2 += dy * w;
        }
    }
  nx = -dy * w;
  ny = dx * w;
  xs[0] = x1 + nx; ys[0] = y1 + ny;
  xs[1] = x2 + nx; ys[1] = y2 + ny;
  xs[2] = x2 - nx; ys[2] = y2 - ny;
  xs[3] = x1 - nx; ys[3] = y1 - ny;
  fill_polygon (paint, xs, ys, 4, TRUE);

  if (paint->cap_style == GDK_CAP_ROUND)
    {
      if (first)
        fill_disc (paint, x1, y1, w);
      if (last)
        fill_disc (paint, x2, y2, w);
    }
}

/*
 * A wide polyline, dashed by length if the line style says so. The
 * points are in device pixels; the line runs through the centres of
 * the drawable's pixels they are the corners of, and the dashes are
 * in the drawable's pixels, so they grow with the scale.
 */
static void
wide_lines (GdkPaint *paint,
            GdkPoint *points,
            gint      npoints,
            gboolean  closed)
{
  gint nseg = closed ? npoints : npoints - 1;
  gint i, dash_index = 0;
  gdouble dash_left, half = paint->scale / 2.0;
  gboolean dash_on = TRUE;

  dash_left = MAX (paint->dashes[0], 1) * paint->scale;

  for (i = 0; i < nseg; i++)
    {
      GdkPoint *a = &points[i];
      GdkPoint *b = &points[(i + 1) % npoints];
      gdouble x1 = a->x + half, y1 = a->y + half;
      gdouble x2 = b->x + half, y2 = b->y + half;
      gboolean first = !closed && i == 0;
      gboolean last = !closed && i == nseg - 1;

      if (paint->line_style == GDK_LINE_SOLID)
        wide_segment (paint, x1, y1, x2, y2, first, last);
      else
        {
          gdouble len = sqrt ((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
          gdouble t = 0;

          while (t < len)
            {
              gdouble step = MIN (dash_left, len - t);
              gdouble sx = x1 + (x2 - x1) * t / len;
              gdouble sy = y1 + (y2 - y1) * t / len;
              gdouble ex = x1 + (x2 - x1) * (t + step) / len;
              gdouble ey = y1 + (y2 - y1) * (t + step) / len;

              if (dash_on)
                wide_segment (paint, sx, sy, ex, ey, first && t == 0,
                              last && t + step >= len);
              else if (paint->line_style == GDK_LINE_DOUBLE_DASH)
                {
                  guint32 fg = paint->fg;
                  paint->fg = paint->bg;
                  wide_segment (paint, sx, sy, ex, ey, FALSE, FALSE);
                  paint->fg = fg;
                }
              t += step;
              dash_left -= step;
              if (dash_left <= 0)
                {
                  dash_index = (dash_index + 1) % paint->n_dashes;
                  dash_left = MAX (paint->dashes[dash_index], 1) * paint->scale;
                  dash_on = !dash_on;
                }
            }
        }

      /* The join: a disc does for every join style at these widths. */
      if ((closed || i < nseg - 1) && paint->line_width > 2)
        fill_disc (paint, x2, y2, paint->line_width / 2.0);
    }
}

/* ---------------------------------------------------------------------
 * Lines
 */

static void
paint_lines (GdkPaint *paint,
             GdkPoint *points,
             gint      npoints,
             gboolean  closed)
{
  DashState ds;
  gint i, nseg;

  if (npoints < 1)
    return;
  if (npoints == 1)
    {
      _gdk_paint_point (paint, points[0].x, points[0].y);
      return;
    }

  if (paint->line_width > 1)
    {
      GdkPoint *dev = g_new (GdkPoint, npoints);
      for (i = 0; i < npoints; i++)
        {
          dev[i].x = DEV_X (paint, points[i].x);
          dev[i].y = DEV_Y (paint, points[i].y);
        }
      wide_lines (paint, dev, npoints, closed);
      g_free (dev);
      return;
    }

  dash_start (paint, &ds);
  nseg = closed ? npoints : npoints - 1;
  for (i = 0; i < nseg; i++)
    {
      GdkPoint *a = &points[i];
      GdkPoint *b = &points[(i + 1) % npoints];
      gboolean last = !closed && i == nseg - 1 &&
        paint->cap_style != GDK_CAP_NOT_LAST;

      thin_line (paint, &ds, DEV_X (paint, a->x), DEV_Y (paint, a->y),
                 DEV_X (paint, b->x), DEV_Y (paint, b->y), last);
    }
}

void
_gdk_paint_line (GdkPaint *paint, gint x1, gint y1, gint x2, gint y2)
{
  GdkPoint points[2];

  points[0].x = x1; points[0].y = y1;
  points[1].x = x2; points[1].y = y2;
  paint_lines (paint, points, 2, FALSE);
}

void
_gdk_paint_lines (GdkPaint *paint, GdkPoint *points, gint npoints,
                  gboolean closed)
{
  paint_lines (paint, points, npoints, closed);
}

void
_gdk_paint_segments (GdkPaint *paint, GdkSegment *segs, gint nsegs)
{
  gint i;

  for (i = 0; i < nsegs; i++)
    _gdk_paint_line (paint, segs[i].x1, segs[i].y1, segs[i].x2, segs[i].y2);
}

/* ---------------------------------------------------------------------
 * Rectangles
 */

void
_gdk_paint_rectangle (GdkPaint *paint,
                      gboolean  filled,
                      gint      x,
                      gint      y,
                      gint      width,
                      gint      height)
{
  if (width < 0 || height < 0)
    return;

  if (filled)
    {
      gint x1 = DEV_X (paint, x), y1 = DEV_Y (paint, y);
      gint w = width * paint->scale, h = height * paint->scale;
      gint yy;

      damage_add (paint, x1, y1, w, h);
      for (yy = y1; yy < y1 + h; yy++)
        span (paint, yy, x1, x1 + w, FALSE, NULL);
    }
  else
    {
      GdkPoint points[4];

      points[0].x = x;         points[0].y = y;
      points[1].x = x + width; points[1].y = y;
      points[2].x = x + width; points[2].y = y + height;
      points[3].x = x;         points[3].y = y + height;
      paint_lines (paint, points, 4, TRUE);
    }
}

/* ---------------------------------------------------------------------
 * Arcs: on the ellipse inscribed in the box, angles in 64ths of a
 * degree counterclockwise from three o'clock.
 */

static void aa_arc (GdkPaint *paint, gboolean filled, gdouble x, gdouble y,
                    gdouble w, gdouble h, gdouble a1, gdouble a2);

void
_gdk_paint_arc (GdkPaint *paint,
                gboolean  filled,
                gint      x,
                gint      y,
                gint      width,
                gint      height,
                gint      angle1,
                gint      angle2)
{
  gdouble cx, cy, rx, ry, a1, a2, len;
  gint x1, y1, w, h, n, i;
  gint s = paint->scale;

  if (width <= 0 || height <= 0 || angle2 == 0)
    return;

  x1 = DEV_X (paint, x);
  y1 = DEV_Y (paint, y);
  w = width * s;
  h = height * s;

  if (paint->antialias)
    {
      angle2 = CLAMP (angle2, -360 * 64, 360 * 64);
      aa_arc (paint, filled, x1, y1, w, h, angle1 * M_PI / (180 * 64),
              (angle1 + angle2) * M_PI / (180 * 64));
      return;
    }

  cx = x1 + w / 2.0;
  cy = y1 + h / 2.0;
  rx = w / 2.0;
  ry = h / 2.0;

  angle2 = CLAMP (angle2, -360 * 64, 360 * 64);
  a1 = angle1 * M_PI / (180 * 64);
  a2 = (angle1 + angle2) * M_PI / (180 * 64);

  len = M_PI * (rx + ry) * ABS (angle2) / (360.0 * 64);
  n = MAX (8, (gint) ceil (len * 2));
  n = MIN (n, 4096);

  if (filled)
    {
      gboolean whole = ABS (angle2) >= 360 * 64;
      gint np = n + 1 + (whole ? 0 : 1);
      gdouble *xs = g_new (gdouble, np * 2);
      gdouble *ys = xs + np;
      gint k = 0;

      if (!whole)
        {
          xs[k] = cx;
          ys[k] = cy;
          k++;
        }
      for (i = 0; i <= n; i++)
        {
          gdouble a = a1 + (a2 - a1) * i / n;
          xs[k] = cx + rx * cos (a);
          ys[k] = cy - ry * sin (a);
          k++;
        }
      fill_polygon (paint, xs, ys, k, TRUE);
      g_free (xs);
    }
  else
    {
      GdkPoint *points = g_new (GdkPoint, n + 1);
      gint k = 0;
      gdouble ox = rx - 0.5, oy = ry - 0.5;

      for (i = 0; i <= n; i++)
        {
          gdouble a = a1 + (a2 - a1) * i / n;
          gint px = (gint) floor (cx + ox * cos (a));
          gint py = (gint) floor (cy - oy * sin (a));

          if (k == 0 || points[k - 1].x != px || points[k - 1].y != py)
            {
              points[k].x = px;
              points[k].y = py;
              k++;
            }
        }

      if (paint->line_width > 1)
        wide_lines (paint, points, k, ABS (angle2) >= 360 * 64);
      else
        {
          DashState ds;

          dash_start (paint, &ds);
          if (k == 1)
            dash_pixel (paint, &ds, points[0].x, points[0].y);
          for (i = 0; i < k - 1; i++)
            thin_line (paint, &ds, points[i].x, points[i].y,
                       points[i + 1].x, points[i + 1].y, i == k - 2);
          damage_add (paint, x1, y1, w + s, h + s);
        }
      g_free (points);
    }
}

/* ---------------------------------------------------------------------
 * Shapes by coverage: outlines of straight edges, in device pixels to
 * a fraction of a pixel. Each row of pixels takes as much of each
 * pixel as the shape covers of it: across a row exactly, down it in
 * SHAPE_ROWS steps.
 */

#define SHAPE_ROWS 16

void
_gdk_shape_init (GdkShape *shape)
{
  memset (shape, 0, sizeof (GdkShape));
  shape->minx = shape->miny = G_MAXDOUBLE;
  shape->maxx = shape->maxy = -G_MAXDOUBLE;
}

void
_gdk_shape_free (GdkShape *shape)
{
  g_free (shape->edges);
  shape->edges = NULL;
  shape->n = shape->alloc = 0;
}

void
_gdk_shape_edge (GdkShape *shape,
                 gdouble   x1,
                 gdouble   y1,
                 gdouble   x2,
                 gdouble   y2)
{
  GdkShapeEdge *e;

  if (y1 == y2)
    return;
  if (shape->n == shape->alloc)
    {
      shape->alloc = MAX (32, shape->alloc * 2);
      shape->edges = g_renew (GdkShapeEdge, shape->edges, shape->alloc);
    }
  e = &shape->edges[shape->n++];
  e->x1 = x1;
  e->y1 = y1;
  e->x2 = x2;
  e->y2 = y2;
  shape->minx = MIN (shape->minx, MIN (x1, x2));
  shape->maxx = MAX (shape->maxx, MAX (x1, x2));
  shape->miny = MIN (shape->miny, MIN (y1, y2));
  shape->maxy = MAX (shape->maxy, MAX (y1, y2));
}

gboolean
_gdk_shape_bounds (GdkShape           *shape,
                   const GdkRectangle *limit,
                   GdkRectangle       *bounds)
{
  gint x0, y0, x1, y1;

  if (shape->n == 0)
    return FALSE;
  x0 = MAX ((gint) floor (shape->minx), limit->x);
  y0 = MAX ((gint) floor (shape->miny), limit->y);
  x1 = MIN ((gint) ceil (shape->maxx), limit->x + limit->width);
  y1 = MIN ((gint) ceil (shape->maxy), limit->y + limit->height);
  if (x1 <= x0 || y1 <= y0)
    return FALSE;
  bounds->x = x0;
  bounds->y = y0;
  bounds->width = x1 - x0;
  bounds->height = y1 - y0;
  return TRUE;
}

/* Adds [xa, xb), relative to the row's start, to a row of width w */
static inline void
shape_span (gdouble *part, gdouble *diff, gint w, gdouble xa, gdouble xb)
{
  gint ia, ib;

  xa = CLAMP (xa, 0, w);
  xb = CLAMP (xb, 0, w);
  if (xb <= xa)
    return;

  ia = (gint) xa;
  ib = (gint) xb;
  if (ia == ib)
    {
      part[ia] += xb - xa;
      return;
    }
  part[ia] += ia + 1 - xa;
  diff[ia + 1] += 1;
  diff[ib] -= 1;
  if (ib < w)
    part[ib] += xb - ib;
}

void
_gdk_shape_rows (GdkShape           *shape,
                 GdkFillRule         rule,
                 const GdkRectangle *limit,
                 GdkShapeRowFunc     func,
                 gpointer            data)
{
  GdkRectangle b;
  Crossing *crossings;
  gdouble *part, *diff, *row;
  gint w, x, y, r, i;

  if (!_gdk_shape_bounds (shape, limit, &b))
    return;
  w = b.width;

  part = g_new (gdouble, w + 1);
  diff = g_new (gdouble, w + 1);
  row = g_new (gdouble, w);
  crossings = g_new (Crossing, shape->n);

  for (y = b.y; y < b.y + b.height; y++)
    {
      gdouble run = 0;
      gboolean any = FALSE;

      memset (part, 0, sizeof (gdouble) * (w + 1));
      memset (diff, 0, sizeof (gdouble) * (w + 1));

      for (r = 0; r < SHAPE_ROWS; r++)
        {
          gdouble sy = y + (r + 0.5) / SHAPE_ROWS;
          gint n = 0, wind = 0;
          gdouble start = 0;

          for (i = 0; i < shape->n; i++)
            {
              GdkShapeEdge *e = &shape->edges[i];

              if ((e->y1 <= sy && sy < e->y2) || (e->y2 <= sy && sy < e->y1))
                {
                  crossings[n].x = e->x1 + (sy - e->y1) * (e->x2 - e->x1) /
                    (e->y2 - e->y1) - b.x;
                  crossings[n].dir = e->y2 > e->y1 ? 1 : -1;
                  n++;
                }
            }
          if (n < 2)
            continue;
          qsort (crossings, n, sizeof (Crossing), crossing_compare);

          for (i = 0; i < n; i++)
            {
              gint before = wind;
              gboolean was_in, is_in;

              wind += crossings[i].dir;
              if (rule == GDK_EVEN_ODD_RULE)
                {
                  was_in = before % 2 != 0;
                  is_in = wind % 2 != 0;
                }
              else
                {
                  was_in = before != 0;
                  is_in = wind != 0;
                }
              if (!was_in && is_in)
                start = crossings[i].x;
              else if (was_in && !is_in)
                {
                  shape_span (part, diff, w, start, crossings[i].x);
                  any = TRUE;
                }
            }
        }
      if (!any)
        continue;

      for (x = 0; x < w; x++)
        {
          run += diff[x];
          row[x] = CLAMP ((part[x] + run) / SHAPE_ROWS, 0.0, 1.0);
        }
      func (y, b.x, w, row, data);
    }

  g_free (crossings);
  g_free (row);
  g_free (diff);
  g_free (part);
}

/* A closed outline through n points, xy[2i] and xy[2i + 1] */
static void
aa_contour (GdkShape *shape, const gdouble *xy, gint n)
{
  gint i;

  for (i = 0; i < n; i++)
    _gdk_shape_edge (shape, xy[2 * i], xy[2 * i + 1],
                     xy[2 * ((i + 1) % n)], xy[2 * ((i + 1) % n) + 1]);
}

typedef struct
{
  GdkPaint *paint;
  guchar *coverage;
  GdkRectangle *bounds;
} AaRows;

static void
aa_row (gint y, gint x, gint width, const gdouble *coverage, gpointer data)
{
  AaRows *rows = data;
  guchar *out = rows->coverage +
                (gsize) (y - rows->bounds->y) * rows->bounds->width +
                (x - rows->bounds->x);
  gint i;

  for (i = 0; i < width; i++)
    {
      gdouble c = coverage[i];

      if (!rows->paint->antialias)
        c = c >= 0.5 ? 1.0 : 0.0;
      out[i] = c * rows->paint->alpha + 0.5;
    }
}

/* The shape, filled by the non-zero rule, in the foreground: each
 * pixel as much of it as the shape covers, or without antialiasing,
 * whole where at least half is covered. The result goes through the
 * same blending as text. */
static void
aa_fill (GdkPaint *paint, GdkShape *shape)
{
  GdkRectangle b;
  AaRows rows;

  if (paint->alpha == 0 ||
      !_gdk_shape_bounds (shape, &GDK_REGION_EXTENTS (paint->clip), &b))
    return;

  rows.paint = paint;
  rows.coverage = g_new0 (guchar, (gsize) b.width * b.height);
  rows.bounds = &b;
  _gdk_shape_rows (shape, GDK_WINDING_RULE, &b, aa_row, &rows);

  _gdk_paint_coverage (paint, rows.coverage, b.width, b.width, b.height,
                       -paint->dx, -paint->dy, b.x, b.y);
  g_free (rows.coverage);
}

/* An ellipse's outline from angle a1 to a2, radians anticlockwise from
 * three o'clock as GDK counts them, into xy from index k; returns the
 * index after. steps is the number of lines along it. */
static gint
aa_arc_points (gdouble *xy, gint k, gdouble cx, gdouble cy,
               gdouble rx, gdouble ry, gdouble a1, gdouble a2, gint steps)
{
  gint i;

  for (i = 0; i <= steps; i++)
    {
      gdouble a = a1 + (a2 - a1) * i / steps;

      xy[2 * k] = cx + rx * cos (a);
      xy[2 * k + 1] = cy - ry * sin (a);
      k++;
    }
  return k;
}

/* Lines enough along an arc that none is longer than a device pixel */
static gint
aa_arc_steps (gdouble rx, gdouble ry, gdouble sweep)
{
  gint n = (gint) ceil (MAX (rx, ry) * ABS (sweep));

  return CLAMP (n, 2, 4096);
}

/* Antialiased arcs: filled, the ellipse in the box, or a slice of it; an
 * outline, a band as wide as the line inside the box's edge. */
static void
aa_arc (GdkPaint *paint, gboolean filled, gdouble x, gdouble y,
        gdouble w, gdouble h, gdouble a1, gdouble a2)
{
  gdouble cx = x + w / 2, cy = y + h / 2;
  gboolean whole = ABS (a2 - a1) >= 2 * M_PI - 1e-9;
  GdkShape shape;
  gdouble *xy;
  gint steps, k = 0;

  _gdk_shape_init (&shape);

  if (filled)
    {
      steps = aa_arc_steps (w / 2, h / 2, a2 - a1);
      xy = g_new (gdouble, 2 * (steps + 2));
      if (!whole)
        {
          xy[0] = cx;
          xy[1] = cy;
          k = 1;
        }
      k = aa_arc_points (xy, k, cx, cy, w / 2, h / 2, a1, a2, steps);
      aa_contour (&shape, xy, k);
    }
  else
    {
      gdouble lw = MIN (paint->aa_line_width, MIN (w, h) / 2);
      gdouble orx = w / 2, ory = h / 2;
      gdouble irx = MAX (orx - lw, 0), iry = MAX (ory - lw, 0);

      steps = aa_arc_steps (orx, ory, a2 - a1);
      xy = g_new (gdouble, 4 * (steps + 1));
      if (whole)
        {
          /* two outlines, the inner the other way round: a hole */
          k = aa_arc_points (xy, 0, cx, cy, orx, ory, a1, a2, steps);
          aa_contour (&shape, xy, k);
          k = aa_arc_points (xy, 0, cx, cy, irx, iry, a2, a1, steps);
          aa_contour (&shape, xy, k);
        }
      else
        {
          k = aa_arc_points (xy, 0, cx, cy, orx, ory, a1, a2, steps);
          k = aa_arc_points (xy, k, cx, cy, irx, iry, a2, a1, steps);
          aa_contour (&shape, xy, k);
        }
    }

  aa_fill (paint, &shape);
  _gdk_shape_free (&shape);
  g_free (xy);
}

/* A rounded rectangle's outline, clockwise, corners of radius r */
static gint
aa_rounded_points (gdouble *xy, gdouble x, gdouble y, gdouble w, gdouble h,
                   gdouble r, gboolean reverse)
{
  gint steps = r > 0 ? aa_arc_steps (r, r, M_PI / 2) : 0;
  gint k = 0, c, i;
  gdouble *tmp;

  r = MIN (r, MIN (w, h) / 2);

  /* corners from the upper left, round clockwise */
  for (c = 0; c < 4; c++)
    {
      gdouble ccx = (c == 1 || c == 2) ? x + w - r : x + r;
      gdouble ccy = (c >= 2) ? y + h - r : y + r;
      gdouble a0 = M_PI - c * M_PI / 2;

      if (r <= 0)
        {
          xy[2 * k] = ccx;
          xy[2 * k + 1] = ccy;
          k++;
          continue;
        }
      k = aa_arc_points (xy, k, ccx, ccy, r, r, a0, a0 - M_PI / 2, steps);
    }

  if (reverse)
    {
      tmp = g_memdup (xy, sizeof (gdouble) * 2 * k);
      for (i = 0; i < k; i++)
        {
          xy[2 * i] = tmp[2 * (k - 1 - i)];
          xy[2 * i + 1] = tmp[2 * (k - 1 - i) + 1];
        }
      g_free (tmp);
    }

  return k;
}

/*
 * A rectangle with rounded corners, through coverage. Filled, the
 * pixels x, y, width, height, less what the corners of the given radius
 * leave out. An outline lies inside those pixels, as wide as the line;
 * radius is its outer corners'.
 */
void
_gdk_paint_rounded_rectangle (GdkPaint *paint,
                              gboolean  filled,
                              gint      x,
                              gint      y,
                              gint      width,
                              gint      height,
                              gdouble   radius)
{
  gdouble s = paint->scale;
  gdouble dx = DEV_X (paint, x), dy = DEV_Y (paint, y);
  gdouble w = width * s, h = height * s, r = MAX (radius, 0) * s;
  gint steps = r > 0 ? aa_arc_steps (r, r, M_PI / 2) : 0;
  gdouble *xy = g_new (gdouble, 2 * 4 * (steps + 1));
  GdkShape shape;
  gint k;

  if (width <= 0 || height <= 0)
    {
      g_free (xy);
      return;
    }

  _gdk_shape_init (&shape);

  k = aa_rounded_points (xy, dx, dy, w, h, r, FALSE);
  aa_contour (&shape, xy, k);

  if (!filled)
    {
      gdouble lw = paint->aa_line_width;

      if (w > 2 * lw && h > 2 * lw)
        {
          k = aa_rounded_points (xy, dx + lw, dy + lw, w - 2 * lw, h - 2 * lw,
                                 MAX (r - lw, 0), TRUE);
          aa_contour (&shape, xy, k);
        }
    }

  aa_fill (paint, &shape);
  _gdk_shape_free (&shape);
  g_free (xy);
}

/*
 * A polygon whose corners lie anywhere, not only on pixels: a point at
 * (x, y) is the top left of the pixel x, y. Filled, what the outline
 * encloses; an outline, each side a band as wide as the line, running on
 * half its width past each corner.
 */
void
_gdk_paint_polygon_f (GdkPaint  *paint,
                      gboolean   filled,
                      GdkPointF *points,
                      gint       npoints)
{
  gdouble s = paint->scale;
  GdkShape shape;
  gdouble *xy;
  gint i;

  if (npoints < 2)
    return;

  xy = g_new (gdouble, 2 * MAX (npoints, 4));
  _gdk_shape_init (&shape);

  if (filled)
    {
      for (i = 0; i < npoints; i++)
        {
          xy[2 * i] = (points[i].x + paint->dx) * s;
          xy[2 * i + 1] = (points[i].y + paint->dy) * s;
        }
      aa_contour (&shape, xy, npoints);
    }
  else
    {
      gdouble half = paint->aa_line_width / 2.0;

      for (i = 0; i < npoints; i++)
        {
          GdkPointF *p = &points[i], *q = &points[(i + 1) % npoints];
          gdouble x1 = (p->x + paint->dx) * s, y1 = (p->y + paint->dy) * s;
          gdouble x2 = (q->x + paint->dx) * s, y2 = (q->y + paint->dy) * s;
          gdouble len = sqrt ((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
          gdouble ex, ey, nx, ny;

          if (len == 0)
            continue;
          ex = (x2 - x1) / len * half;
          ey = (y2 - y1) / len * half;
          nx = -ey;
          ny = ex;

          /* every side's band the same way round, so they add up */
          xy[0] = x1 - ex + nx; xy[1] = y1 - ey + ny;
          xy[2] = x2 + ex + nx; xy[3] = y2 + ey + ny;
          xy[4] = x2 + ex - nx; xy[5] = y2 + ey - ny;
          xy[6] = x1 - ex - nx; xy[7] = y1 - ey - ny;
          aa_contour (&shape, xy, 4);
        }
    }

  aa_fill (paint, &shape);
  _gdk_shape_free (&shape);
  g_free (xy);
}

/* ---------------------------------------------------------------------
 * Polygons
 */

void
_gdk_paint_polygon (GdkPaint *paint,
                    gboolean  filled,
                    GdkPoint *points,
                    gint      npoints)
{
  if (npoints < 1)
    return;

  if (filled)
    {
      gdouble *xs = g_new (gdouble, npoints * 2);
      gdouble *ys = xs + npoints;
      gint i;

      for (i = 0; i < npoints; i++)
        {
          xs[i] = DEV_X (paint, points[i].x);
          ys[i] = DEV_Y (paint, points[i].y);
        }
      fill_polygon (paint, xs, ys, npoints, FALSE);
      g_free (xs);
    }
  else
    paint_lines (paint, points, npoints, TRUE);
}

/* ---------------------------------------------------------------------
 * Copying
 */

/*
 * Rows of source pixels put through the paint. get_row fills a row of
 * width pixels of the source for a device row; the source may be at
 * another scale, so it samples nearest.
 */
typedef guint32 (*GetPixelFunc) (gpointer src, gint x, gint y);

static guint32 image_get_pixel (gpointer src, gint x, gint y);

static void
blit (GdkPaint     *paint,
      GetPixelFunc  get,
      gpointer      src,
      gint          src_depth,
      gint          src_scale,
      gint          xsrc,
      gint          ysrc,
      gint          xdest,
      gint          ydest,
      gint          width,
      gint          height)
{
  gint x1 = DEV_X (paint, xdest), y1 = DEV_Y (paint, ydest);
  gint w = width * paint->scale, h = height * paint->scale;
  gint sx0 = xsrc * src_scale, sy0 = ysrc * src_scale;
  /* An image of whole words is read straight from its memory, not a
   * pixel at a time through the getter. */
  GdkImage *words = get == image_get_pixel && ((GdkImage *) src)->bpp == 4
    ? src : NULL;
  guint32 *row;
  gint x, y;

  if (width <= 0 || height <= 0)
    return;

  damage_add (paint, x1, y1, w, h);
  row = g_new (guint32, w);
  for (y = 0; y < h; y++)
    {
      gint sy = sy0 + (src_scale == paint->scale ? y : y * src_scale / paint->scale);
      const guint32 *image_row = NULL;

      if (words && sy >= 0 && sy < words->height)
        image_row = (const guint32 *) ((const guchar *) words->mem
                                       + (gsize) sy * words->bpl);

      /* At one pixel to a pixel, a row is its words, in range. */
      if (image_row && src_scale == paint->scale && paint->fb->depth != 1
          && sx0 >= 0 && sx0 + w <= words->width)
        {
          const guint32 *in = image_row + sx0;

          for (x = 0; x < w; x++)
            row[x] = in[x] & 0xffffff;
          span (paint, y1 + y, x1, x1 + w, FALSE, row);
          continue;
        }

      for (x = 0; x < w; x++)
        {
          gint sx = sx0 + (src_scale == paint->scale ? x : x * src_scale / paint->scale);
          guint32 p;

          if (words)
            p = image_row && sx >= 0 && sx < words->width ? image_row[sx] : 0;
          else
            p = get (src, sx, sy);

          if (src_depth == 1 && paint->fb->depth != 1)
            p = (p & 1) ? paint->fg : paint->bg;
          else
            p = GDK_FB_PIXEL (paint->fb, p);
          row[x] = p;
        }
      span (paint, y1 + y, x1, x1 + w, FALSE, row);
    }
  g_free (row);
}

static guint32
fb_get_pixel (gpointer src, gint x, gint y)
{
  GdkFramebuffer *fb = src;

  if (x < 0 || y < 0 || x >= fb->width || y >= fb->height)
    return 0;
  return fb_get (fb, x, y);
}

void
_gdk_paint_copy (GdkPaint       *paint,
                 GdkFramebuffer *src,
                 gint            xsrc,
                 gint            ysrc,
                 gint            xdest,
                 gint            ydest,
                 gint            width,
                 gint            height)
{
  GdkFramebuffer *copy = NULL;

  if (width <= 0 || height <= 0)
    return;

  /*
   * The plain case, most of what goes on: whole rows copied, and
   * within one framebuffer copied through a temporary so that
   * overlapping scrolls come out right.
   */
  if (src == paint->fb)
    {
      GdkRectangle area, bounds;
      gint y;

      area.x = xsrc * src->scale;
      area.y = ysrc * src->scale;
      area.width = width * src->scale;
      area.height = height * src->scale;
      bounds.x = bounds.y = 0;
      bounds.width = src->width;
      bounds.height = src->height;
      if (!gdk_rectangle_intersect (&area, &bounds, &area))
        return;

      copy = _gdk_framebuffer_new (area.width, area.height, src->depth, 1);
      copy->scale = src->scale;
      for (y = 0; y < area.height; y++)
        memcpy (copy->pixels + (gsize) y * copy->stride,
                src->pixels + (gsize) (area.y + y) * src->stride + area.x,
                area.width * sizeof (guint32));

      /* What fell outside the source is not copied. */
      xdest += (area.x - xsrc * src->scale) / src->scale;
      ydest += (area.y - ysrc * src->scale) / src->scale;
      width = area.width / src->scale;
      height = area.height / src->scale;
      xsrc = ysrc = 0;
      src = copy;
    }

  if (paint->function == GDK_COPY && paint->fill == GDK_SOLID &&
      !paint->mask && src->depth == paint->fb->depth &&
      src->scale == paint->scale)
    {
      GdkRectangle *rects = GDK_REGION_RECTS (paint->clip);
      gint n = GDK_REGION_N_RECTS (paint->clip), i;
      GdkRectangle dst, sbounds;

      dst.x = DEV_X (paint, xdest);
      dst.y = DEV_Y (paint, ydest);
      dst.width = width * paint->scale;
      dst.height = height * paint->scale;

      /* Only what the source has. */
      sbounds.x = dst.x - xsrc * src->scale;
      sbounds.y = dst.y - ysrc * src->scale;
      sbounds.width = src->width;
      sbounds.height = src->height;
      if (!gdk_rectangle_intersect (&dst, &sbounds, &dst))
        {
          _gdk_framebuffer_free (copy);
          return;
        }

      damage_add (paint, dst.x, dst.y, dst.width, dst.height);
      for (i = 0; i < n; i++)
        {
          GdkRectangle r;
          gint y;

          if (!gdk_rectangle_intersect (&dst, &rects[i], &r))
            continue;
          for (y = 0; y < r.height; y++)
            memcpy (paint->fb->pixels + (gsize) (r.y + y) * paint->fb->stride + r.x,
                    src->pixels + (gsize) (r.y + y - sbounds.y) * src->stride +
                    (r.x - sbounds.x),
                    r.width * sizeof (guint32));
        }
    }
  else
    blit (paint, fb_get_pixel, src, src->depth, src->scale,
          xsrc, ysrc, xdest, ydest, width, height);

  _gdk_framebuffer_free (copy);
}

static guint32
image_get_pixel (gpointer src, gint x, gint y)
{
  GdkImage *image = src;

  if (x < 0 || y < 0 || x >= image->width || y >= image->height)
    return 0;
  return gdk_image_get_pixel (image, x, y);
}

void
_gdk_paint_image (GdkPaint *paint,
                  GdkImage *image,
                  gint      xsrc,
                  gint      ysrc,
                  gint      xdest,
                  gint      ydest,
                  gint      width,
                  gint      height)
{
  blit (paint, image_get_pixel, image, image->depth, 1,
        xsrc, ysrc, xdest, ydest, width, height);
}

/* ---------------------------------------------------------------------
 * RGBA images, premultiplied, laid over what is there: what a theme
 * engine draws with translucent shapes and gradients.
 */

void
_gdk_paint_rgba (GdkPaint     *paint,
                 const guchar *rgba,
                 gint          stride,
                 gint          scale,
                 gint          xdest,
                 gint          ydest,
                 gint          width,
                 gint          height)
{
  gint x1 = DEV_X (paint, xdest), y1 = DEV_Y (paint, ydest);
  gint w = width * paint->scale, h = height * paint->scale;
  gboolean blend = paint->fb->depth != 1 && paint->function == GDK_COPY &&
    paint->fill == GDK_SOLID;
  GdkRectangle *rects = GDK_REGION_RECTS (paint->clip);
  gint n = GDK_REGION_N_RECTS (paint->clip);
  gint i, x, y;

  if (width <= 0 || height <= 0 || scale < 1)
    return;

  damage_add (paint, x1, y1, w, h);

  for (i = 0; i < n; i++)
    {
      GdkRectangle area, r;

      area.x = x1;
      area.y = y1;
      area.width = w;
      area.height = h;
      if (!gdk_rectangle_intersect (&area, &rects[i], &r))
        continue;

      for (y = r.y; y < r.y + r.height; y++)
        {
          gint sy = scale == paint->scale ? y - y1 : (y - y1) * scale / paint->scale;
          guint32 *p = paint->fb->pixels + (gsize) y * paint->fb->stride + r.x;

          for (x = r.x; x < r.x + r.width; x++, p++)
            {
              gint sx = scale == paint->scale ? x - x1 : (x - x1) * scale / paint->scale;
              const guchar *s = rgba + (gsize) sy * stride + (gsize) sx * 4;
              guint a = s[3];

              if (a == 0 || !mask_allows (paint, x, y))
                continue;
              if (blend)
                {
                  guint32 d = *p;
                  guint ia = 255 - a;
                  guint rr = MIN (255, s[0] + ((d >> 16 & 0xff) * ia + 127) / 255);
                  guint gg = MIN (255, s[1] + ((d >> 8 & 0xff) * ia + 127) / 255);
                  guint bb = MIN (255, s[2] + ((d & 0xff) * ia + 127) / 255);

                  *p = rr << 16 | gg << 8 | bb;
                }
              else if (a >= 128)
                {
                  guint32 src = (guint32) MIN (255, s[0] * 255 / a) << 16 |
                    (guint32) MIN (255, s[1] * 255 / a) << 8 |
                    MIN (255, s[2] * 255 / a);

                  *p = function_apply (paint->function, GDK_FB_PIXEL (paint->fb, src),
                                       *p) & DEPTH_MASK (paint->fb);
                }
            }
        }
    }
}

/* ---------------------------------------------------------------------
 * Coverage: text. Blended into a colour buffer when the GC's function
 * and fill let a pixel be a mix of what was there and the foreground;
 * otherwise each pixel is either drawn or not, the way a stipple was.
 */

void
_gdk_paint_coverage (GdkPaint     *paint,
                     const guchar *coverage,
                     gint          stride,
                     gint          width,
                     gint          height,
                     gint          xdest,
                     gint          ydest,
                     gint          xoffset,
                     gint          yoffset)
{
  gint x1 = DEV_X (paint, xdest) + xoffset, y1 = DEV_Y (paint, ydest) + yoffset;
  gboolean blend = paint->fb->depth != 1 && paint->function == GDK_COPY &&
    paint->fill == GDK_SOLID;
  GdkRectangle *rects = GDK_REGION_RECTS (paint->clip);
  gint n = GDK_REGION_N_RECTS (paint->clip);
  gint i, x, y;

  damage_add (paint, x1, y1, width, height);

  for (i = 0; i < n; i++)
    {
      GdkRectangle area, r;

      area.x = x1;
      area.y = y1;
      area.width = width;
      area.height = height;
      if (!gdk_rectangle_intersect (&area, &rects[i], &r))
        continue;

      for (y = r.y; y < r.y + r.height; y++)
        {
          const guchar *cov = coverage + (gsize) (y - y1) * stride + (r.x - x1);
          guint32 *p = paint->fb->pixels + (gsize) y * paint->fb->stride + r.x;

          for (x = r.x; x < r.x + r.width; x++, cov++, p++)
            {
              guint a = *cov;

              if (a == 0 || !mask_allows (paint, x, y))
                continue;
              if (blend)
                {
                  guint32 d = *p, f = paint->fg;

                  if (a == 255)
                    *p = f;
                  else
                    {
                      guint ia = 255 - a;
                      guint rr = ((f >> 16 & 0xff) * a + (d >> 16 & 0xff) * ia + 127) / 255;
                      guint gg = ((f >> 8 & 0xff) * a + (d >> 8 & 0xff) * ia + 127) / 255;
                      guint bb = ((f & 0xff) * a + (d & 0xff) * ia + 127) / 255;
                      *p = rr << 16 | gg << 8 | bb;
                    }
                }
              else if (a >= 128)
                {
                  guint32 src;

                  if (source_pixel (paint, x, y, &src))
                    *p = function_apply (paint->function, src, *p) &
                      DEPTH_MASK (paint->fb);
                }
            }
        }
    }
}
