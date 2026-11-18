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
 * Regions: sets of rectangles that do not overlap. The rectangles are
 * kept in no particular order; the operations run over all of them,
 * which is fine for the sizes GDK uses regions at (a clip, a few
 * exposed areas) and keeps the code short. Adjacent rectangles are
 * merged after each operation so a region does not fragment over time.
 */

#include "gdk.h"
#include "gdkprivate.h"
#include "gdkframebuffer.h"

#define REGION(r) ((GdkRegionPrivate *) (r))

static void
region_update_extents (GdkRegionPrivate *region)
{
  gint i, x1, y1, x2, y2;

  if (region->n_rects == 0)
    {
      region->extents.x = region->extents.y = 0;
      region->extents.width = region->extents.height = 0;
      return;
    }

  x1 = region->rects[0].x;
  y1 = region->rects[0].y;
  x2 = x1 + region->rects[0].width;
  y2 = y1 + region->rects[0].height;
  for (i = 1; i < region->n_rects; i++)
    {
      GdkRectangle *r = &region->rects[i];
      x1 = MIN (x1, r->x);
      y1 = MIN (y1, r->y);
      x2 = MAX (x2, r->x + r->width);
      y2 = MAX (y2, r->y + r->height);
    }
  region->extents.x = x1;
  region->extents.y = y1;
  region->extents.width = x2 - x1;
  region->extents.height = y2 - y1;
}

static void
region_append (GdkRegionPrivate *region, const GdkRectangle *rect)
{
  if (rect->width <= 0 || rect->height <= 0)
    return;
  if (region->n_rects == region->n_alloc)
    {
      region->n_alloc = MAX (8, region->n_alloc * 2);
      region->rects = g_renew (GdkRectangle, region->rects, region->n_alloc);
    }
  region->rects[region->n_rects++] = *rect;
}

/* Merge rectangles that together make a rectangle. */
static void
region_coalesce (GdkRegionPrivate *region)
{
  gboolean merged;
  gint i, j;

  do
    {
      merged = FALSE;
      for (i = 0; i < region->n_rects; i++)
        for (j = i + 1; j < region->n_rects; j++)
          {
            GdkRectangle *a = &region->rects[i];
            GdkRectangle *b = &region->rects[j];

            if (a->y == b->y && a->height == b->height &&
                (a->x + a->width == b->x || b->x + b->width == a->x))
              {
                a->x = MIN (a->x, b->x);
                a->width += b->width;
              }
            else if (a->x == b->x && a->width == b->width &&
                     (a->y + a->height == b->y || b->y + b->height == a->y))
              {
                a->y = MIN (a->y, b->y);
                a->height += b->height;
              }
            else
              continue;

            region->rects[j] = region->rects[--region->n_rects];
            merged = TRUE;
            j--;
          }
    }
  while (merged);

  region_update_extents (region);
}

/* rect minus cut: up to four pieces appended to out. */
static void
rect_subtract (const GdkRectangle *rect,
               const GdkRectangle *cut,
               GdkRegionPrivate   *out)
{
  GdkRectangle both, piece;

  if (!gdk_rectangle_intersect ((GdkRectangle *) rect,
                                (GdkRectangle *) cut, &both))
    {
      region_append (out, rect);
      return;
    }

  /* above */
  piece.x = rect->x;
  piece.y = rect->y;
  piece.width = rect->width;
  piece.height = both.y - rect->y;
  region_append (out, &piece);

  /* below */
  piece.y = both.y + both.height;
  piece.height = rect->y + rect->height - piece.y;
  region_append (out, &piece);

  /* left, within the band */
  piece.x = rect->x;
  piece.y = both.y;
  piece.width = both.x - rect->x;
  piece.height = both.height;
  region_append (out, &piece);

  /* right */
  piece.x = both.x + both.width;
  piece.width = rect->x + rect->width - piece.x;
  region_append (out, &piece);
}

void
_gdk_region_clear (GdkRegion *region)
{
  REGION (region)->n_rects = 0;
  region_update_extents (REGION (region));
}

void
_gdk_region_add_rect (GdkRegion *region, const GdkRectangle *rect)
{
  GdkRegionPrivate *private = REGION (region);
  GdkRegionPrivate pieces = { { NULL }, NULL, 0, 0, { 0, 0, 0, 0 } };
  GdkRegionPrivate next;
  gint i, j;

  if (rect->width <= 0 || rect->height <= 0)
    return;

  region_append (&pieces, rect);
  for (i = 0; i < private->n_rects && pieces.n_rects > 0; i++)
    {
      next.rects = NULL;
      next.n_rects = next.n_alloc = 0;
      for (j = 0; j < pieces.n_rects; j++)
        rect_subtract (&pieces.rects[j], &private->rects[i], &next);
      g_free (pieces.rects);
      pieces = next;
    }

  for (j = 0; j < pieces.n_rects; j++)
    region_append (private, &pieces.rects[j]);
  g_free (pieces.rects);

  region_coalesce (private);
}

void
_gdk_region_subtract_rect (GdkRegion *region, const GdkRectangle *rect)
{
  GdkRegionPrivate *private = REGION (region);
  GdkRegionPrivate out = { { NULL }, NULL, 0, 0, { 0, 0, 0, 0 } };
  gint i;

  for (i = 0; i < private->n_rects; i++)
    rect_subtract (&private->rects[i], rect, &out);

  g_free (private->rects);
  private->rects = out.rects;
  private->n_rects = out.n_rects;
  private->n_alloc = out.n_alloc;
  region_coalesce (private);
}

void
_gdk_region_intersect_rect (GdkRegion *region, const GdkRectangle *rect)
{
  GdkRegionPrivate *private = REGION (region);
  gint i, n = 0;

  for (i = 0; i < private->n_rects; i++)
    {
      GdkRectangle both;

      if (gdk_rectangle_intersect (&private->rects[i],
                                   (GdkRectangle *) rect, &both))
        private->rects[n++] = both;
    }
  private->n_rects = n;
  region_coalesce (private);
}

GdkRegion *
_gdk_region_copy (GdkRegion *region)
{
  GdkRegionPrivate *src = REGION (region);
  GdkRegionPrivate *dst = REGION (gdk_region_new ());
  gint i;

  for (i = 0; i < src->n_rects; i++)
    region_append (dst, &src->rects[i]);
  dst->extents = src->extents;

  return (GdkRegion *) dst;
}

GdkRegion *
_gdk_region_from_rect (const GdkRectangle *rect)
{
  GdkRegion *region = gdk_region_new ();

  _gdk_region_add_rect (region, rect);
  return region;
}

GdkRegion*
gdk_region_new (void)
{
  GdkRegionPrivate *private;

  private = g_new0 (GdkRegionPrivate, 1);
  private->region.user_data = NULL;

  return (GdkRegion*) private;
}

void
gdk_region_destroy (GdkRegion *region)
{
  g_return_if_fail (region != NULL);

  g_free (REGION (region)->rects);
  g_free (region);
}

gboolean
gdk_region_empty (GdkRegion *region)
{
  g_return_val_if_fail (region != NULL, 0);

  return REGION (region)->n_rects == 0;
}

gboolean
gdk_region_equal (GdkRegion *region1,
                  GdkRegion *region2)
{
  GdkRegion *diff;
  gboolean equal;

  g_return_val_if_fail (region1 != NULL, 0);
  g_return_val_if_fail (region2 != NULL, 0);

  diff = gdk_regions_xor (region1, region2);
  equal = gdk_region_empty (diff);
  gdk_region_destroy (diff);

  return equal;
}

void
gdk_region_get_clipbox (GdkRegion    *region,
                        GdkRectangle *rectangle)
{
  g_return_if_fail (region != NULL);
  g_return_if_fail (rectangle != NULL);

  *rectangle = REGION (region)->extents;
}

gboolean
gdk_region_point_in (GdkRegion *region,
                     gint       x,
                     gint       y)
{
  GdkRegionPrivate *private;
  gint i;

  g_return_val_if_fail (region != NULL, 0);

  private = REGION (region);
  for (i = 0; i < private->n_rects; i++)
    {
      GdkRectangle *r = &private->rects[i];
      if (x >= r->x && x < r->x + r->width &&
          y >= r->y && y < r->y + r->height)
        return TRUE;
    }
  return FALSE;
}

GdkOverlapType
gdk_region_rect_in (GdkRegion    *region,
                    GdkRectangle *rect)
{
  GdkRegion *inside;
  GdkRegionPrivate *private;
  gint i, area = 0;
  GdkOverlapType result;

  g_return_val_if_fail (region != NULL, 0);
  g_return_val_if_fail (rect != NULL, 0);

  inside = _gdk_region_copy (region);
  _gdk_region_intersect_rect (inside, rect);
  private = REGION (inside);
  for (i = 0; i < private->n_rects; i++)
    area += private->rects[i].width * private->rects[i].height;
  gdk_region_destroy (inside);

  if (area == 0)
    result = GDK_OVERLAP_RECTANGLE_OUT;
  else if (area == rect->width * rect->height)
    result = GDK_OVERLAP_RECTANGLE_IN;
  else
    result = GDK_OVERLAP_RECTANGLE_PART;

  return result;
}

static void
polygon_span (gint y, gint x1, gint x2, gpointer data)
{
  GdkRectangle rect;

  rect.x = x1;
  rect.y = y;
  rect.width = x2 - x1;
  rect.height = 1;
  _gdk_region_add_rect ((GdkRegion *) data, &rect);
}

GdkRegion *
gdk_region_polygon (GdkPoint    *points,
                    gint         npoints,
                    GdkFillRule  fill_rule)
{
  GdkRegion *region;

  g_return_val_if_fail (points != NULL, NULL);
  g_return_val_if_fail (npoints != 0, NULL);

  region = gdk_region_new ();
  _gdk_polygon_spans (points, npoints, fill_rule == GDK_WINDING_RULE,
                      polygon_span, region);

  return region;
}

void
gdk_region_offset (GdkRegion *region,
                   gint       dx,
                   gint       dy)
{
  GdkRegionPrivate *private;
  gint i;

  g_return_if_fail (region != NULL);

  private = REGION (region);
  for (i = 0; i < private->n_rects; i++)
    {
      private->rects[i].x += dx;
      private->rects[i].y += dy;
    }
  private->extents.x += dx;
  private->extents.y += dy;
}

/*
 * Each rectangle shrunk by dx and dy on every side, as X did it. A
 * negative amount grows instead.
 */
void
gdk_region_shrink (GdkRegion *region,
                   gint       dx,
                   gint       dy)
{
  GdkRegionPrivate *private;
  GdkRegion *result;
  gint i;

  g_return_if_fail (region != NULL);

  private = REGION (region);
  result = gdk_region_new ();
  for (i = 0; i < private->n_rects; i++)
    {
      GdkRectangle r = private->rects[i];

      r.x += dx;
      r.y += dy;
      r.width -= 2 * dx;
      r.height -= 2 * dy;
      _gdk_region_add_rect (result, &r);
    }

  g_free (private->rects);
  *private = *REGION (result);
  g_free (result);
}

GdkRegion*
gdk_region_union_with_rect (GdkRegion    *region,
                            GdkRectangle *rect)
{
  GdkRegion *result;

  g_return_val_if_fail (region != NULL, NULL);
  g_return_val_if_fail (rect != NULL, NULL);

  result = _gdk_region_copy (region);
  _gdk_region_add_rect (result, rect);

  return result;
}

GdkRegion*
gdk_regions_intersect (GdkRegion *source1,
                       GdkRegion *source2)
{
  GdkRegionPrivate *a, *b;
  GdkRegion *result;
  gint i, j;

  g_return_val_if_fail (source1 != NULL, NULL);
  g_return_val_if_fail (source2 != NULL, NULL);

  a = REGION (source1);
  b = REGION (source2);
  result = gdk_region_new ();
  for (i = 0; i < a->n_rects; i++)
    for (j = 0; j < b->n_rects; j++)
      {
        GdkRectangle both;

        if (gdk_rectangle_intersect (&a->rects[i], &b->rects[j], &both))
          region_append (REGION (result), &both);
      }
  region_coalesce (REGION (result));

  return result;
}

GdkRegion*
gdk_regions_union (GdkRegion *source1,
                   GdkRegion *source2)
{
  GdkRegionPrivate *b;
  GdkRegion *result;
  gint i;

  g_return_val_if_fail (source1 != NULL, NULL);
  g_return_val_if_fail (source2 != NULL, NULL);

  b = REGION (source2);
  result = _gdk_region_copy (source1);
  for (i = 0; i < b->n_rects; i++)
    _gdk_region_add_rect (result, &b->rects[i]);

  return result;
}

GdkRegion*
gdk_regions_subtract (GdkRegion *source1,
                      GdkRegion *source2)
{
  GdkRegionPrivate *b;
  GdkRegion *result;
  gint i;

  g_return_val_if_fail (source1 != NULL, NULL);
  g_return_val_if_fail (source2 != NULL, NULL);

  b = REGION (source2);
  result = _gdk_region_copy (source1);
  for (i = 0; i < b->n_rects; i++)
    _gdk_region_subtract_rect (result, &b->rects[i]);

  return result;
}

GdkRegion*
gdk_regions_xor (GdkRegion *source1,
                 GdkRegion *source2)
{
  GdkRegion *a_minus_b, *b_minus_a, *result;

  g_return_val_if_fail (source1 != NULL, NULL);
  g_return_val_if_fail (source2 != NULL, NULL);

  a_minus_b = gdk_regions_subtract (source1, source2);
  b_minus_a = gdk_regions_subtract (source2, source1);
  result = gdk_regions_union (a_minus_b, b_minus_a);
  gdk_region_destroy (a_minus_b);
  gdk_region_destroy (b_minus_a);

  return result;
}
