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
 * Drawing into framebuffers. Internal to GDK; not installed.
 *
 *   gdkframebuffer.c  Framebuffers themselves, and the pieces of a
 *                     region the rasteriser and the backends need.
 *   gdkraster.c       A paint: a framebuffer and a GC read once into
 *                     the form the primitives want, and the primitives.
 *                     Knows nothing about windows or the screen.
 *   gdkdraw.c         The public drawing functions, which make a paint
 *                     for a drawable and record what was touched.
 */

#ifndef __GDK_FRAMEBUFFER_H__
#define __GDK_FRAMEBUFFER_H__

#include <gdk/gdkprivate.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Region internals: these change the region they are given. */
void       _gdk_region_clear          (GdkRegion          *region);
void       _gdk_region_add_rect       (GdkRegion          *region,
                                       const GdkRectangle *rect);
void       _gdk_region_subtract_rect  (GdkRegion          *region,
                                       const GdkRectangle *rect);
void       _gdk_region_intersect_rect (GdkRegion          *region,
                                       const GdkRectangle *rect);
GdkRegion *_gdk_region_copy           (GdkRegion          *region);
GdkRegion *_gdk_region_from_rect      (const GdkRectangle *rect);

#define GDK_REGION_N_RECTS(r) (((GdkRegionPrivate *) (r))->n_rects)
#define GDK_REGION_RECTS(r)   (((GdkRegionPrivate *) (r))->rects)
#define GDK_REGION_EXTENTS(r) (((GdkRegionPrivate *) (r))->extents)

/* Framebuffers. */
GdkFramebuffer *_gdk_framebuffer_new  (gint width, gint height,
                                       gint depth, gint scale);
void            _gdk_framebuffer_free (GdkFramebuffer *fb);

/* The framebuffer of a drawable, made if it does not exist yet. */
GdkFramebuffer *_gdk_drawable_fb      (GdkDrawable *drawable);

/*
 * The pixel value of a colour on a framebuffer: the colour's own for a
 * colour buffer, and its lowest bit for a bitmap.
 */
#define GDK_FB_PIXEL(fb, pixel) ((fb)->depth == 1 ? ((pixel) & 1) : ((pixel) & 0xffffff))

/*
 * A paint: everything a primitive needs, worked out once from a GC.
 * Coordinates given to the primitives are in the drawable's pixels,
 * offset by (dx, dy), and are scaled into the framebuffer's device
 * pixels by the primitives. Every primitive grows damage by what it
 * touched, in device pixels.
 */
typedef struct _GdkPaint GdkPaint;

struct _GdkPaint
{
  GdkFramebuffer *fb;
  gint            scale;
  gint            dx;
  gint            dy;

  GdkFunction     function;
  GdkFill         fill;
  guint32         fg;
  guint32         bg;
  GdkFramebuffer *tile;
  GdkFramebuffer *stipple;
  gint            ts_x;
  gint            ts_y;

  /* Where drawing may land: the clip, in device pixels, never NULL. */
  GdkRegion      *clip;
  GdkFramebuffer *mask;
  gint            mask_x;
  gint            mask_y;

  gint            line_width;
  GdkLineStyle    line_style;
  GdkCapStyle     cap_style;
  const gint8    *dashes;
  gint            n_dashes;
  gint            dash_offset;

  /* Shapes drawn by coverage: antialiased or not, how opaque (0-255),
   * and the width of their outlines, in device pixels */
  gboolean        antialias;
  guint           alpha;
  gint            aa_line_width;

  GdkRectangle    damage;
};

/*
 * Returns FALSE when nothing can be drawn (an empty clip). The GC may
 * be NULL for a plain copy; clip, in device pixels, limits drawing to
 * the part of the framebuffer that is the drawable's, and may be NULL
 * for all of it. _gdk_paint_finish frees what init made.
 */
gboolean _gdk_paint_init   (GdkPaint *paint, GdkFramebuffer *fb,
                            GdkGC *gc, gint dx, gint dy, GdkRegion *clip);
void     _gdk_paint_finish (GdkPaint *paint);

void _gdk_paint_point     (GdkPaint *paint, gint x, gint y);
void _gdk_paint_points    (GdkPaint *paint, GdkPoint *points, gint npoints);
void _gdk_paint_line      (GdkPaint *paint, gint x1, gint y1,
                           gint x2, gint y2);
void _gdk_paint_lines     (GdkPaint *paint, GdkPoint *points, gint npoints,
                           gboolean closed);
void _gdk_paint_segments  (GdkPaint *paint, GdkSegment *segs, gint nsegs);
void _gdk_paint_rectangle (GdkPaint *paint, gboolean filled,
                           gint x, gint y, gint width, gint height);
void _gdk_paint_arc       (GdkPaint *paint, gboolean filled,
                           gint x, gint y, gint width, gint height,
                           gint angle1, gint angle2);
void _gdk_paint_polygon   (GdkPaint *paint, gboolean filled,
                           GdkPoint *points, gint npoints);
void _gdk_paint_polygon_f (GdkPaint *paint, gboolean filled,
                           GdkPointF *points, gint npoints);
void _gdk_paint_rounded_rectangle (GdkPaint *paint, gboolean filled,
                                   gint x, gint y, gint width, gint height,
                                   gdouble radius);
void _gdk_paint_copy      (GdkPaint *paint, GdkFramebuffer *src,
                           gint xsrc, gint ysrc, gint xdest, gint ydest,
                           gint width, gint height);
void _gdk_paint_image     (GdkPaint *paint, GdkImage *image,
                           gint xsrc, gint ysrc, gint xdest, gint ydest,
                           gint width, gint height);
/*
 * Premultiplied RGBA laid over the drawable: width by height of the
 * toolkit's pixels at (xdest, ydest), with scale of the image's pixels
 * to each of them, sampled nearest where that is not the paint's.
 */
void _gdk_paint_rgba      (GdkPaint *paint, const guchar *rgba,
                           gint stride, gint scale, gint xdest, gint ydest,
                           gint width, gint height);
/*
 * 8 bit coverage, one byte a pixel, drawn in the foreground. The
 * image is in device pixels, its top left at (xoffset, yoffset) device
 * pixels from the drawable's pixel (xdest, ydest): text is rasterised
 * for the device, and placed by the pen.
 */
void _gdk_paint_coverage  (GdkPaint *paint, const guchar *coverage,
                           gint stride, gint width, gint height,
                           gint xdest, gint ydest,
                           gint xoffset, gint yoffset);

/*
 * Scan conversion of a polygon, in continuous coordinates where pixel
 * (x, y) covers [x, x+1) by [y, y+1): a pixel is filled when its centre
 * is inside. Spans are [x1, x2) on row y.
 */
typedef void (*GdkSpanFunc) (gint y, gint x1, gint x2, gpointer data);

void _gdk_polygon_spans   (GdkPoint *points, gint npoints, gboolean winding,
                           GdkSpanFunc func, gpointer data);

/*
 * Shapes by coverage: outlines of straight edges, anywhere to a
 * fraction of a pixel, in device pixels. _gdk_shape_rows works out,
 * a row at a time, how much of each pixel inside limit the shape
 * covers, filled by the rule: across a row exactly, down it in sixteen
 * steps. func is given the rows something crosses, a coverage from 0
 * to 1 for each of the width pixels from x. Antialiased drawing and
 * GdkPen both fill this way.
 */
typedef struct
{
  gdouble x1, y1, x2, y2;
} GdkShapeEdge;

typedef struct
{
  GdkShapeEdge *edges;
  gint n;
  gint alloc;
  gdouble minx, miny, maxx, maxy;
} GdkShape;

typedef void (*GdkShapeRowFunc) (gint y, gint x, gint width,
                                 const gdouble *coverage, gpointer data);

void     _gdk_shape_init   (GdkShape *shape);
void     _gdk_shape_free   (GdkShape *shape);
void     _gdk_shape_edge   (GdkShape *shape, gdouble x1, gdouble y1,
                            gdouble x2, gdouble y2);
/* The pixels inside limit that the shape may cover; FALSE for none */
gboolean _gdk_shape_bounds (GdkShape *shape, const GdkRectangle *limit,
                            GdkRectangle *bounds);
void     _gdk_shape_rows   (GdkShape *shape, GdkFillRule rule,
                            const GdkRectangle *limit, GdkShapeRowFunc func,
                            gpointer data);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GDK_FRAMEBUFFER_H__ */
