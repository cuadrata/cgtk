/* GTK - The GIMP Toolkit
 * GdkPen: drawing paths, antialiased, for the theme engines
 *
 * Copyright (C) 2026 The CGTK authors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <https://www.gnu.org/licenses/>.
 */

/* How it draws. A path is kept in device pixels of the layer, its
 * arcs and curves already broken into lines a tenth of a pixel from
 * true, as cairo's are. Filling it is GDK's shapes by coverage: how
 * much of each pixel the path covers, row by row. Stroking it makes
 * the shape the pen sweeps -- a band along each line, the joins and
 * the caps -- and fills that. Each pixel then takes the pen's colour,
 * as much of it as is covered and inside the clip, over what the
 * layer has.
 *
 * The layer is kept in tiles, made only where something is drawn, so
 * that a frame round a whole window costs only its edges.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "gdkpen.h"
#include "gdkframebuffer.h"
#include "gdkrgb.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TOLERANCE       0.1     /* how far a flattened curve may stray */
#define MITER_LIMIT     10.0    /* cairo's default */

/* ---------------------------------------------------------------------
 * Types
 */

typedef struct
{
  gdouble x, y;
} Point;

typedef struct
{
  Point *points;
  gint n, alloc;
  gboolean closed;
} SubPath;

typedef struct
{
  gdouble offset;
  gdouble r, g, b, a;
} Stop;

struct _GdkPenPattern
{
  gint ref_count;
  gboolean linear;
  gdouble r, g, b, a;           /* a solid pattern's colour */
  gdouble x0, y0, x1, y1;       /* a linear one's line */
  Stop *stops;
  gint n_stops;
};

/* The clip: a rectangle of device pixels, and within it, if not NULL,
 * how much of each is inside */
typedef struct
{
  gint x, y, width, height;
  gfloat *mask;
} Clip;

typedef struct _State State;

struct _State
{
  GdkPenMatrix ctm;
  GdkPenPattern *source;
  GdkPenMatrix source_inverse;   /* device to the pattern's space */
  gdouble line_width;
  GdkCapStyle cap;
  GdkJoinStyle join;
  GdkFillRule fill_rule;
  gdouble *dashes;
  gint n_dashes;
  gdouble dash_offset;
  Clip clip;
  State *next;
};

#define TILE_SIZE(scale) (60 % (scale) == 0 ? 60 : 16 * (scale))

struct _GdkPen
{
  GdkWindow *window;
  gint scale;
  gint lx, ly;                  /* the layer's origin, in the window */
  gint width, height;           /* its size in device pixels */

  gint tile, tiles_x, tiles_y;
  gfloat **tiles;               /* premultiplied RGBA, or NULL */

  SubPath *subpaths;
  gint n_subpaths, alloc_subpaths;
  gboolean has_current;
  gboolean need_subpath;        /* after close_path */
  Point current, start;

  State *state;
};

/* ---------------------------------------------------------------------
 * Matrices
 */

void
_gdk_pen_matrix_init (GdkPenMatrix *m,
                      gdouble xx, gdouble yx,
                      gdouble xy, gdouble yy,
                      gdouble x0, gdouble y0)
{
  m->xx = xx;  m->yx = yx;
  m->xy = xy;  m->yy = yy;
  m->x0 = x0;  m->y0 = y0;
}

void
_gdk_pen_matrix_init_identity (GdkPenMatrix *m)
{
  _gdk_pen_matrix_init (m, 1, 0, 0, 1, 0, 0);
}

/* r = a then b: a point goes through a first */
static void
matrix_multiply (GdkPenMatrix *r, const GdkPenMatrix *a,
                 const GdkPenMatrix *b)
{
  GdkPenMatrix t;

  t.xx = a->xx * b->xx + a->yx * b->xy;
  t.yx = a->xx * b->yx + a->yx * b->yy;
  t.xy = a->xy * b->xx + a->yy * b->xy;
  t.yy = a->xy * b->yx + a->yy * b->yy;
  t.x0 = a->x0 * b->xx + a->y0 * b->xy + b->x0;
  t.y0 = a->x0 * b->yx + a->y0 * b->yy + b->y0;
  *r = t;
}

void
_gdk_pen_matrix_scale (GdkPenMatrix *m, gdouble sx, gdouble sy)
{
  GdkPenMatrix t;

  _gdk_pen_matrix_init (&t, sx, 0, 0, sy, 0, 0);
  matrix_multiply (m, &t, m);
}

static void
matrix_apply (const GdkPenMatrix *m, gdouble x, gdouble y,
              gdouble *ox, gdouble *oy)
{
  *ox = m->xx * x + m->xy * y + m->x0;
  *oy = m->yx * x + m->yy * y + m->y0;
}

static gboolean
matrix_invert (const GdkPenMatrix *m, GdkPenMatrix *inv)
{
  gdouble det = m->xx * m->yy - m->xy * m->yx;

  if (fabs (det) < 1e-12)
    return FALSE;
  inv->xx = m->yy / det;
  inv->xy = -m->xy / det;
  inv->yx = -m->yx / det;
  inv->yy = m->xx / det;
  inv->x0 = -(inv->xx * m->x0 + inv->xy * m->y0);
  inv->y0 = -(inv->yx * m->x0 + inv->yy * m->y0);
  return TRUE;
}

/* User space to the layer's device pixels */
static void
device_matrix (GdkPen *pen, GdkPenMatrix *m)
{
  GdkPenMatrix d;

  _gdk_pen_matrix_init (&d, pen->scale, 0, 0, pen->scale,
                        -pen->lx * pen->scale, -pen->ly * pen->scale);
  matrix_multiply (m, &pen->state->ctm, &d);
}

static void
to_device (GdkPen *pen, gdouble x, gdouble y, Point *p)
{
  GdkPenMatrix m;

  device_matrix (pen, &m);
  matrix_apply (&m, x, y, &p->x, &p->y);
}

/* How much the user space is scaled on the device */
static gdouble
device_scale (GdkPen *pen)
{
  GdkPenMatrix *m = &pen->state->ctm;

  return pen->scale * sqrt (fabs (m->xx * m->yy - m->xy * m->yx));
}

void
_gdk_pen_translate (GdkPen *pen, gdouble tx, gdouble ty)
{
  GdkPenMatrix t;

  _gdk_pen_matrix_init (&t, 1, 0, 0, 1, tx, ty);
  matrix_multiply (&pen->state->ctm, &t, &pen->state->ctm);
}

void
_gdk_pen_rotate (GdkPen *pen, gdouble angle)
{
  GdkPenMatrix t;
  gdouble c = cos (angle), s = sin (angle);

  _gdk_pen_matrix_init (&t, c, s, -s, c, 0, 0);
  matrix_multiply (&pen->state->ctm, &t, &pen->state->ctm);
}

void
_gdk_pen_transform (GdkPen *pen, const GdkPenMatrix *m)
{
  matrix_multiply (&pen->state->ctm, m, &pen->state->ctm);
}

void
_gdk_pen_identity_matrix (GdkPen *pen)
{
  _gdk_pen_matrix_init_identity (&pen->state->ctm);
}

/* ---------------------------------------------------------------------
 * Patterns
 */

static GdkPenPattern *
pattern_create_rgba (gdouble r, gdouble g, gdouble b, gdouble a)
{
  GdkPenPattern *p = g_new0 (GdkPenPattern, 1);

  p->ref_count = 1;
  p->r = CLAMP (r, 0.0, 1.0);
  p->g = CLAMP (g, 0.0, 1.0);
  p->b = CLAMP (b, 0.0, 1.0);
  p->a = CLAMP (a, 0.0, 1.0);
  return p;
}

GdkPenPattern *
_gdk_pen_pattern_new_linear (gdouble x0, gdouble y0, gdouble x1,
                             gdouble y1)
{
  GdkPenPattern *p = g_new0 (GdkPenPattern, 1);

  p->ref_count = 1;
  p->linear = TRUE;
  p->x0 = x0;
  p->y0 = y0;
  p->x1 = x1;
  p->y1 = y1;
  return p;
}

void
_gdk_pen_pattern_add_stop_rgba (GdkPenPattern *p,
                                gdouble offset, gdouble r,
                                gdouble g, gdouble b, gdouble a)
{
  gint i;

  if (!p->linear)
    return;
  p->stops = g_renew (Stop, p->stops, p->n_stops + 1);
  /* in order of offset; one at the same offset as another goes after */
  offset = CLAMP (offset, 0.0, 1.0);
  for (i = p->n_stops; i > 0 && p->stops[i - 1].offset > offset; i--)
    p->stops[i] = p->stops[i - 1];
  p->stops[i].offset = offset;
  p->stops[i].r = CLAMP (r, 0.0, 1.0);
  p->stops[i].g = CLAMP (g, 0.0, 1.0);
  p->stops[i].b = CLAMP (b, 0.0, 1.0);
  p->stops[i].a = CLAMP (a, 0.0, 1.0);
  p->n_stops++;
}

void
_gdk_pen_pattern_add_stop_rgb (GdkPenPattern *p,
                               gdouble offset, gdouble r,
                               gdouble g, gdouble b)
{
  _gdk_pen_pattern_add_stop_rgba (p, offset, r, g, b, 1.0);
}

static GdkPenPattern *
pattern_ref (GdkPenPattern *p)
{
  if (p)
    p->ref_count++;
  return p;
}

void
_gdk_pen_pattern_unref (GdkPenPattern *p)
{
  if (!p || --p->ref_count > 0)
    return;
  g_free (p->stops);
  g_free (p);
}

/* The colour, not premultiplied, at offset t along a gradient, which
 * is padded with its end colours */
static void
gradient_color (GdkPenPattern *p, gdouble t, gdouble *c)
{
  gint i;

  if (p->n_stops == 0)
    {
      c[0] = c[1] = c[2] = c[3] = 0;
      return;
    }
  if (t <= p->stops[0].offset)
    i = -1;
  else
    for (i = 0; i < p->n_stops - 1 && t > p->stops[i + 1].offset; i++)
      ;
  if (i < 0 || i >= p->n_stops - 1)
    {
      Stop *s = &p->stops[i < 0 ? 0 : p->n_stops - 1];

      c[0] = s->r;  c[1] = s->g;  c[2] = s->b;  c[3] = s->a;
    }
  else
    {
      Stop *s0 = &p->stops[i], *s1 = &p->stops[i + 1];
      gdouble span = s1->offset - s0->offset;
      gdouble f = span > 0 ? (t - s0->offset) / span : 1.0;

      c[0] = s0->r + (s1->r - s0->r) * f;
      c[1] = s0->g + (s1->g - s0->g) * f;
      c[2] = s0->b + (s1->b - s0->b) * f;
      c[3] = s0->a + (s1->a - s0->a) * f;
    }
}

/* ---------------------------------------------------------------------
 * The context and its state
 */

static void
clip_copy (Clip *dest, const Clip *src)
{
  *dest = *src;
  if (src->mask)
    dest->mask = g_memdup (src->mask,
                           sizeof (gfloat) * src->width * src->height);
}

GdkPen *
_gdk_pen_new (GdkWindow    *window,
              GdkRectangle *area,
              gint          x,
              gint          y,
              gint          width,
              gint          height)
{
  GdkPen *pen = g_new0 (GdkPen, 1);
  GdkRectangle bounds = { x, y, width, height }, win = { 0, 0, 0, 0 };
  gint ww, wh;

  pen->window = window;
  pen->scale = MAX (1, gdk_window_get_scale_factor (window));

  gdk_window_get_size (window, &ww, &wh);
  win.width = ww;
  win.height = wh;
  if (width <= 0 || height <= 0 ||
      !gdk_rectangle_intersect (&bounds, &win, &bounds) ||
      (area && !gdk_rectangle_intersect (&bounds, area, &bounds)))
    bounds.width = bounds.height = 0;

  pen->lx = bounds.x;
  pen->ly = bounds.y;
  pen->width = bounds.width * pen->scale;
  pen->height = bounds.height * pen->scale;
  pen->tile = TILE_SIZE (pen->scale);
  pen->tiles_x = (pen->width + pen->tile - 1) / pen->tile;
  pen->tiles_y = (pen->height + pen->tile - 1) / pen->tile;
  if (pen->tiles_x * pen->tiles_y > 0)
    pen->tiles = g_new0 (gfloat *, pen->tiles_x * pen->tiles_y);

  pen->state = g_new0 (State, 1);
  _gdk_pen_matrix_init_identity (&pen->state->ctm);
  pen->state->source = pattern_create_rgba (0, 0, 0, 1);
  _gdk_pen_matrix_init_identity (&pen->state->source_inverse);
  pen->state->line_width = 1.0;
  pen->state->cap = GDK_CAP_BUTT;
  pen->state->join = GDK_JOIN_MITER;
  pen->state->fill_rule = GDK_WINDING_RULE;
  pen->state->clip.width = pen->width;
  pen->state->clip.height = pen->height;

  return pen;
}

static void
state_free (State *s)
{
  _gdk_pen_pattern_unref (s->source);
  g_free (s->dashes);
  g_free (s->clip.mask);
  g_free (s);
}

void
_gdk_pen_save (GdkPen *pen)
{
  State *s = g_new (State, 1);

  *s = *pen->state;
  pattern_ref (s->source);
  if (s->n_dashes)
    s->dashes = g_memdup (s->dashes, sizeof (gdouble) * s->n_dashes);
  clip_copy (&s->clip, &pen->state->clip);
  s->next = pen->state;
  pen->state = s;
}

void
_gdk_pen_restore (GdkPen *pen)
{
  State *s = pen->state;

  if (!s->next)
    return;
  pen->state = s->next;
  state_free (s);
}

static void
path_clear (GdkPen *pen)
{
  gint i;

  for (i = 0; i < pen->n_subpaths; i++)
    g_free (pen->subpaths[i].points);
  pen->n_subpaths = 0;
  pen->has_current = FALSE;
  pen->need_subpath = FALSE;
}

/* Lays the layer over the window: each tile drawn on, as an image */
void
_gdk_pen_free (GdkPen *pen)
{
  GdkGC *gc = NULL;
  gint i, j;

  for (j = 0; j < pen->tiles_y; j++)
    for (i = 0; i < pen->tiles_x; i++)
      {
        gfloat *t = pen->tiles[j * pen->tiles_x + i];
        gint w, h, n;
        guchar *rgba;

        if (!t)
          continue;
        w = MIN (pen->tile, pen->width - i * pen->tile);
        h = MIN (pen->tile, pen->height - j * pen->tile);
        rgba = g_new (guchar, w * h * 4);
        for (n = 0; n < h; n++)
          {
            gfloat *src = t + n * pen->tile * 4;
            guchar *dst = rgba + n * w * 4;
            gint k;

            for (k = 0; k < w * 4; k++)
              dst[k] = (guchar) CLAMP (src[k] * 255.0f + 0.5f, 0, 255);
          }
        if (!gc)
          gc = gdk_gc_new (pen->window);
        gdk_draw_rgba_image_scaled (pen->window, gc,
                                    pen->lx + i * pen->tile / pen->scale,
                                    pen->ly + j * pen->tile / pen->scale,
                                    w / pen->scale, h / pen->scale, pen->scale,
                                    rgba, w * 4);
        g_free (rgba);
        g_free (t);
      }
  if (gc)
    gdk_gc_unref (gc);

  while (pen->state)
    {
      State *s = pen->state;

      pen->state = s->next;
      state_free (s);
    }
  path_clear (pen);
  g_free (pen->subpaths);
  g_free (pen->tiles);
  g_free (pen);
}

void
_gdk_pen_set_rgba (GdkPen *pen, gdouble r, gdouble g,
                   gdouble b, gdouble a)
{
  _gdk_pen_pattern_unref (pen->state->source);
  pen->state->source = pattern_create_rgba (r, g, b, a);
}

void
_gdk_pen_set_rgb (GdkPen *pen, gdouble r, gdouble g,
                  gdouble b)
{
  _gdk_pen_set_rgba (pen, r, g, b, 1.0);
}

/* As cairo's, the pattern keeps to the user space it was set in */
void
_gdk_pen_set_pattern (GdkPen *pen, GdkPenPattern *pattern)
{
  GdkPenMatrix m;

  pattern_ref (pattern);
  _gdk_pen_pattern_unref (pen->state->source);
  pen->state->source = pattern;
  device_matrix (pen, &m);
  if (!matrix_invert (&m, &pen->state->source_inverse))
    _gdk_pen_matrix_init_identity (&pen->state->source_inverse);
}

void
_gdk_pen_set_line_width (GdkPen *pen, gdouble width)
{
  pen->state->line_width = MAX (width, 0.0);
}

gdouble
_gdk_pen_get_line_width (GdkPen *pen)
{
  return pen->state->line_width;
}

void
_gdk_pen_set_line_cap (GdkPen *pen, GdkCapStyle cap)
{
  pen->state->cap = cap;
}

void
_gdk_pen_set_line_join (GdkPen *pen, GdkJoinStyle join)
{
  pen->state->join = join;
}

void
_gdk_pen_set_fill_rule (GdkPen *pen, GdkFillRule rule)
{
  pen->state->fill_rule = rule;
}

void
_gdk_pen_set_dash (GdkPen *pen, const gdouble *dashes,
                   gint n_dashes, gdouble offset)
{
  gdouble total = 0;
  gint i;

  g_free (pen->state->dashes);
  pen->state->dashes = NULL;
  pen->state->n_dashes = 0;
  for (i = 0; i < n_dashes; i++)
    total += MAX (dashes[i], 0);
  if (n_dashes <= 0 || total <= 0)
    return;
  /* an odd number of dashes repeats, as cairo has it */
  pen->state->n_dashes = n_dashes % 2 ? 2 * n_dashes : n_dashes;
  pen->state->dashes = g_new (gdouble, pen->state->n_dashes);
  for (i = 0; i < pen->state->n_dashes; i++)
    pen->state->dashes[i] = MAX (dashes[i % n_dashes], 0);
  pen->state->dash_offset = offset;
}

/* ---------------------------------------------------------------------
 * Paths
 */

static SubPath *
current_subpath (GdkPen *pen)
{
  return pen->n_subpaths ? &pen->subpaths[pen->n_subpaths - 1] : NULL;
}

static void
subpath_add (SubPath *sp, const Point *p)
{
  if (sp->n && fabs (sp->points[sp->n - 1].x - p->x) < 1e-9 &&
      fabs (sp->points[sp->n - 1].y - p->y) < 1e-9)
    return;
  if (sp->n == sp->alloc)
    {
      sp->alloc = MAX (16, sp->alloc * 2);
      sp->points = g_renew (Point, sp->points, sp->alloc);
    }
  sp->points[sp->n++] = *p;
}

static void
device_move_to (GdkPen *pen, const Point *p)
{
  SubPath *sp;

  if (pen->n_subpaths == pen->alloc_subpaths)
    {
      pen->alloc_subpaths = MAX (4, pen->alloc_subpaths * 2);
      pen->subpaths = g_renew (SubPath, pen->subpaths, pen->alloc_subpaths);
    }
  sp = &pen->subpaths[pen->n_subpaths++];
  memset (sp, 0, sizeof (SubPath));
  subpath_add (sp, p);
  pen->current = pen->start = *p;
  pen->has_current = TRUE;
  pen->need_subpath = FALSE;
}

static void
device_line_to (GdkPen *pen, const Point *p)
{
  if (!pen->has_current)
    {
      device_move_to (pen, p);
      return;
    }
  if (pen->need_subpath)
    {
      Point start = pen->start;

      device_move_to (pen, &start);
    }
  subpath_add (current_subpath (pen), p);
  pen->current = *p;
}

void
_gdk_pen_new_path (GdkPen *pen)
{
  path_clear (pen);
}

void
_gdk_pen_move_to (GdkPen *pen, gdouble x, gdouble y)
{
  Point p;

  to_device (pen, x, y, &p);
  device_move_to (pen, &p);
}

void
_gdk_pen_line_to (GdkPen *pen, gdouble x, gdouble y)
{
  Point p;

  to_device (pen, x, y, &p);
  device_line_to (pen, &p);
}

/* A distance in user space as it is on the device */
static void
delta_to_device (GdkPen *pen, gdouble dx, gdouble dy, Point *d)
{
  GdkPenMatrix m;

  device_matrix (pen, &m);
  d->x = m.xx * dx + m.xy * dy;
  d->y = m.yx * dx + m.yy * dy;
}

void
_gdk_pen_rel_line_to (GdkPen *pen, gdouble dx, gdouble dy)
{
  Point d, p;

  if (!pen->has_current)
    return;
  delta_to_device (pen, dx, dy, &d);
  p.x = pen->current.x + d.x;
  p.y = pen->current.y + d.y;
  device_line_to (pen, &p);
}

void
_gdk_pen_curve_to (GdkPen *pen, gdouble x1, gdouble y1,
                   gdouble x2, gdouble y2, gdouble x3, gdouble y3)
{
  Point p0, p1, p2, p3;
  gdouble length;
  gint i, n;

  if (!pen->has_current)
    _gdk_pen_move_to (pen, x1, y1);
  if (pen->need_subpath)
    {
      Point start = pen->start;

      device_move_to (pen, &start);
    }
  p0 = pen->current;
  to_device (pen, x1, y1, &p1);
  to_device (pen, x2, y2, &p2);
  to_device (pen, x3, y3, &p3);

  length = hypot (p1.x - p0.x, p1.y - p0.y) + hypot (p2.x - p1.x, p2.y - p1.y) +
           hypot (p3.x - p2.x, p3.y - p2.y);
  n = CLAMP ((gint) ceil (sqrt (length / TOLERANCE) / 2), 1, 100);
  for (i = 1; i <= n; i++)
    {
      gdouble t = (gdouble) i / n, u = 1 - t;
      Point p;

      p.x = u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x +
            t * t * t * p3.x;
      p.y = u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y +
            t * t * t * p3.y;
      device_line_to (pen, &p);
    }
}

/* From angle1 to angle2, which may be either side of it */
static void
arc_points (GdkPen *pen, gdouble xc, gdouble yc, gdouble radius,
            gdouble angle1, gdouble angle2)
{
  gdouble r = radius * device_scale (pen);
  gdouble step;
  gint i, n;

  if (r > TOLERANCE)
    step = 2 * acos (1 - TOLERANCE / r);
  else
    step = M_PI / 2;
  n = CLAMP ((gint) ceil (fabs (angle2 - angle1) / step), 1, 1000);
  for (i = 0; i <= n; i++)
    {
      gdouble a = angle1 + (angle2 - angle1) * i / n;

      _gdk_pen_line_to (pen, xc + radius * cos (a),
                        yc + radius * sin (a));
    }
}

void
_gdk_pen_arc (GdkPen *pen, gdouble xc, gdouble yc,
              gdouble radius, gdouble angle1, gdouble angle2)
{
  while (angle2 < angle1)
    angle2 += 2 * M_PI;
  arc_points (pen, xc, yc, radius, angle1, angle2);
}

void
_gdk_pen_arc_negative (GdkPen *pen, gdouble xc, gdouble yc,
                       gdouble radius, gdouble angle1, gdouble angle2)
{
  while (angle2 > angle1)
    angle2 -= 2 * M_PI;
  arc_points (pen, xc, yc, radius, angle1, angle2);
}

void
_gdk_pen_rectangle (GdkPen *pen, gdouble x, gdouble y,
                    gdouble width, gdouble height)
{
  _gdk_pen_move_to (pen, x, y);
  _gdk_pen_rel_line_to (pen, width, 0);
  _gdk_pen_rel_line_to (pen, 0, height);
  _gdk_pen_rel_line_to (pen, -width, 0);
  _gdk_pen_close_path (pen);
}

void
_gdk_pen_close_path (GdkPen *pen)
{
  SubPath *sp = current_subpath (pen);

  if (!pen->has_current || !sp || pen->need_subpath)
    return;
  sp->closed = TRUE;
  pen->current = pen->start;
  pen->need_subpath = TRUE;
}

/* ---------------------------------------------------------------------
 * Shapes
 */

/* A closed outline; with positive set, turned to wind positively, so
 * that pieces of a stroke add up, overlapping as they will */
static void
shape_polygon (GdkShape *s, const Point *p, gint n, gboolean positive)
{
  gdouble area = 0;
  gint i;

  if (n < 3)
    return;
  if (positive)
    for (i = 0; i < n; i++)
      area += p[i].x * p[(i + 1) % n].y - p[(i + 1) % n].x * p[i].y;

  for (i = 0; i < n; i++)
    {
      const Point *a = &p[i], *b = &p[(i + 1) % n];

      if (area < 0)
        _gdk_shape_edge (s, b->x, b->y, a->x, a->y);
      else
        _gdk_shape_edge (s, a->x, a->y, b->x, b->y);
    }
}

/* ---------------------------------------------------------------------
 * Drawing into the layer
 */

static gfloat *
layer_pixel (GdkPen *pen, gint x, gint y)
{
  gint tx = x / pen->tile, ty = y / pen->tile;
  gfloat **t = &pen->tiles[ty * pen->tiles_x + tx];

  if (!*t)
    *t = g_new0 (gfloat, pen->tile * pen->tile * 4);
  return *t + ((y - ty * pen->tile) * pen->tile + (x - tx * pen->tile)) * 4;
}

static gfloat
clip_at (const Clip *clip, gint x, gint y)
{
  if (x < clip->x || y < clip->y || x >= clip->x + clip->width ||
      y >= clip->y + clip->height)
    return 0;
  if (!clip->mask)
    return 1;
  return clip->mask[(y - clip->y) * clip->width + (x - clip->x)];
}

/* The source's premultiplied colour at the centre of device pixel x, y */
static void
source_at (GdkPen *pen, gint x, gint y, gdouble *c)
{
  GdkPenPattern *p = pen->state->source;

  if (!p->linear)
    {
      c[0] = p->r;  c[1] = p->g;  c[2] = p->b;  c[3] = p->a;
    }
  else
    {
      gdouble ux, uy, dx = p->x1 - p->x0, dy = p->y1 - p->y0;
      gdouble len2 = dx * dx + dy * dy, t;

      matrix_apply (&pen->state->source_inverse, x + 0.5, y + 0.5, &ux, &uy);
      t = len2 > 0 ? ((ux - p->x0) * dx + (uy - p->y0) * dy) / len2 : 0;
      gradient_color (p, CLAMP (t, 0.0, 1.0), c);
    }
  c[0] *= c[3];
  c[1] *= c[3];
  c[2] *= c[3];
}

/* The source over the layer, as much as coverage and the clip let it */
static void
composite_row (GdkPen *pen, gint y, gint x0, gint w, const gdouble *coverage)
{
  gboolean solid = !pen->state->source->linear;
  gdouble c[4];
  gint i;

  if (solid)
    source_at (pen, 0, 0, c);
  for (i = 0; i < w; i++)
    {
      gfloat m = coverage ? (gfloat) coverage[i] : 1.0f;
      gfloat *p, a;

      if (m <= 0)
        continue;
      m *= clip_at (&pen->state->clip, x0 + i, y);
      if (m <= 0)
        continue;
      if (!solid)
        source_at (pen, x0 + i, y, c);
      a = c[3] * m;
      if (a <= 0)
        continue;
      p = layer_pixel (pen, x0 + i, y);
      p[0] = c[0] * m + p[0] * (1 - a);
      p[1] = c[1] * m + p[1] * (1 - a);
      p[2] = c[2] * m + p[2] * (1 - a);
      p[3] = a + p[3] * (1 - a);
    }
}

static void
composite_rows (gint y, gint x, gint width, const gdouble *coverage,
                gpointer data)
{
  composite_row (data, y, x, width, coverage);
}

static void
clip_limit (GdkPen *pen, GdkRectangle *limit)
{
  limit->x = pen->state->clip.x;
  limit->y = pen->state->clip.y;
  limit->width = pen->state->clip.width;
  limit->height = pen->state->clip.height;
}

static void
draw_shape (GdkPen *pen, GdkShape *s, GdkFillRule rule)
{
  GdkRectangle limit;

  clip_limit (pen, &limit);
  _gdk_shape_rows (s, rule, &limit, composite_rows, pen);
}

/* The path as a shape to fill: each subpath closed */
static void
path_shape (GdkPen *pen, GdkShape *s)
{
  gint i;

  _gdk_shape_init (s);
  for (i = 0; i < pen->n_subpaths; i++)
    shape_polygon (s, pen->subpaths[i].points, pen->subpaths[i].n, FALSE);
}

void
_gdk_pen_fill_preserve (GdkPen *pen)
{
  GdkShape s;

  path_shape (pen, &s);
  draw_shape (pen, &s, pen->state->fill_rule);
  _gdk_shape_free (&s);
}

void
_gdk_pen_fill (GdkPen *pen)
{
  _gdk_pen_fill_preserve (pen);
  path_clear (pen);
}

void
_gdk_pen_paint (GdkPen *pen)
{
  Clip *clip = &pen->state->clip;
  gint y;

  for (y = clip->y; y < clip->y + clip->height; y++)
    composite_row (pen, y, clip->x, clip->width, NULL);
}

/* ---------------------------------------------------------------------
 * Strokes: the shape the pen sweeps
 */

static void
circle (GdkShape *s, const Point *c, gdouble r)
{
  Point p[64];
  gdouble step = r > TOLERANCE ? 2 * acos (1 - TOLERANCE / r) : M_PI / 2;
  gint i, n = CLAMP ((gint) ceil (2 * M_PI / step), 8, 64);

  for (i = 0; i < n; i++)
    {
      p[i].x = c->x + r * cos (2 * M_PI * i / n);
      p[i].y = c->y + r * sin (2 * M_PI * i / n);
    }
  shape_polygon (s, p, n, TRUE);
}

/* The join at p between a line coming in along d1 and one going out
 * along d2, both unit vectors */
static void
stroke_join (GdkShape *s, State *st, const Point *p, const Point *d1,
             const Point *d2, gdouble hw)
{
  gdouble cross = d1->x * d2->y - d1->y * d2->x;
  gdouble dot = d1->x * d2->x + d1->y * d2->y;
  gdouble o;
  Point q[4], n1, n2;

  if (fabs (cross) < 1e-9 && dot > 0)
    return;                     /* straight on */
  if (st->join == GDK_JOIN_ROUND)
    {
      circle (s, p, hw);
      return;
    }

  o = cross > 0 ? -1 : 1;       /* the outside of the turn */
  n1.x = -d1->y * hw * o;  n1.y = d1->x * hw * o;
  n2.x = -d2->y * hw * o;  n2.y = d2->x * hw * o;

  q[0] = *p;
  q[1].x = p->x + n1.x;  q[1].y = p->y + n1.y;
  if (st->join == GDK_JOIN_MITER && 1 + dot > 1e-9 &&
      sqrt (2 / (1 + dot)) <= MITER_LIMIT)
    {
      q[2].x = p->x + (n1.x + n2.x) / (1 + dot);
      q[2].y = p->y + (n1.y + n2.y) / (1 + dot);
      q[3].x = p->x + n2.x;  q[3].y = p->y + n2.y;
      shape_polygon (s, q, 4, TRUE);
    }
  else
    {
      q[2].x = p->x + n2.x;  q[2].y = p->y + n2.y;
      shape_polygon (s, q, 3, TRUE);
    }
}

/* A polyline of n points, closed or not, stroked into s */
static void
stroke_polyline (GdkShape *s, State *st, const Point *pts, gint n,
                 gboolean closed, gdouble hw)
{
  Point *p, *d;
  gint i, segments;

  if (n == 1)
    {
      if (st->cap == GDK_CAP_ROUND && !closed)
        circle (s, &pts[0], hw);
      return;
    }
  if (closed && n < 3)
    closed = FALSE;

  p = g_new (Point, n + 1);
  memcpy (p, pts, sizeof (Point) * n);
  if (closed)
    p[n] = p[0];
  segments = closed ? n : n - 1;

  d = g_new (Point, segments);
  for (i = 0; i < segments; i++)
    {
      gdouble len = hypot (p[i + 1].x - p[i].x, p[i + 1].y - p[i].y);

      d[i].x = (p[i + 1].x - p[i].x) / len;
      d[i].y = (p[i + 1].y - p[i].y) / len;
    }

  /* square caps lengthen the ends */
  if (!closed && st->cap == GDK_CAP_PROJECTING)
    {
      p[0].x -= d[0].x * hw;
      p[0].y -= d[0].y * hw;
      p[n - 1].x += d[segments - 1].x * hw;
      p[n - 1].y += d[segments - 1].y * hw;
    }

  for (i = 0; i < segments; i++)
    {
      Point q[4];
      gdouble nx = -d[i].y * hw, ny = d[i].x * hw;

      q[0].x = p[i].x + nx;      q[0].y = p[i].y + ny;
      q[1].x = p[i + 1].x + nx;  q[1].y = p[i + 1].y + ny;
      q[2].x = p[i + 1].x - nx;  q[2].y = p[i + 1].y - ny;
      q[3].x = p[i].x - nx;      q[3].y = p[i].y - ny;
      shape_polygon (s, q, 4, TRUE);
      if (i > 0)
        stroke_join (s, st, &p[i], &d[i - 1], &d[i], hw);
    }
  if (closed)
    stroke_join (s, st, &p[0], &d[segments - 1], &d[0], hw);
  else if (st->cap == GDK_CAP_ROUND)
    {
      circle (s, &p[0], hw);
      circle (s, &p[n - 1], hw);
    }

  g_free (d);
  g_free (p);
}

/* The dashes of a polyline, each stroked as a polyline of its own */
static void
stroke_dashed (GdkShape *s, State *st, const Point *pts, gint n,
               gboolean closed, gdouble hw, gdouble scale)
{
  Point *dash = g_new (Point, n + 2);
  gint k = 0, segments = closed ? n : n - 1, i, nd = 0;
  gdouble total = 0, left;
  gboolean on = TRUE;

  for (i = 0; i < st->n_dashes; i++)
    total += st->dashes[i] * scale;

  /* where the pattern starts */
  left = fmod (st->dash_offset * scale, total);
  if (left < 0)
    left += total;
  while (left >= st->dashes[k] * scale)
    {
      left -= st->dashes[k] * scale;
      k = (k + 1) % st->n_dashes;
      on = !on;
    }
  left = st->dashes[k] * scale - left;

  if (on)
    dash[nd++] = pts[0];
  for (i = 0; i < segments; i++)
    {
      Point a = pts[i], b = pts[(i + 1) % n];
      gdouble len = hypot (b.x - a.x, b.y - a.y), done = 0;

      while (len - done > left)
        {
          Point m;

          done += left;
          m.x = a.x + (b.x - a.x) * done / len;
          m.y = a.y + (b.y - a.y) * done / len;
          if (on)
            {
              dash[nd++] = m;
              stroke_polyline (s, st, dash, nd, FALSE, hw);
              nd = 0;
            }
          else
            {
              nd = 0;
              dash[nd++] = m;
            }
          on = !on;
          k = (k + 1) % st->n_dashes;
          left = st->dashes[k] * scale;
          if (left <= 0)
            left = 1e-9;
        }
      left -= len - done;
      if (on)
        dash[nd++] = b;
    }
  if (on && nd > 1)
    stroke_polyline (s, st, dash, nd, FALSE, hw);
  g_free (dash);
}

void
_gdk_pen_stroke_preserve (GdkPen *pen)
{
  State *st = pen->state;
  gdouble scale = device_scale (pen);
  gdouble hw = st->line_width * scale / 2;
  GdkShape s;
  gint i;

  if (hw <= 0)
    return;
  _gdk_shape_init (&s);
  for (i = 0; i < pen->n_subpaths; i++)
    {
      SubPath *sp = &pen->subpaths[i];

      if (st->n_dashes)
        stroke_dashed (&s, st, sp->points, sp->n, sp->closed, hw, scale);
      else
        stroke_polyline (&s, st, sp->points, sp->n, sp->closed, hw);
    }
  draw_shape (pen, &s, GDK_WINDING_RULE);
  _gdk_shape_free (&s);
}

void
_gdk_pen_stroke (GdkPen *pen)
{
  _gdk_pen_stroke_preserve (pen);
  path_clear (pen);
}

/* ---------------------------------------------------------------------
 * Clipping
 */

typedef struct
{
  Clip *old;
  Clip *new;
} ClipRows;

static void
clip_row (gint y, gint x0, gint w, const gdouble *coverage, gpointer data)
{
  ClipRows *c = data;
  gint i;

  for (i = 0; i < w; i++)
    {
      gint x = x0 + i;

      if (x < c->new->x || x >= c->new->x + c->new->width ||
          y < c->new->y || y >= c->new->y + c->new->height)
        continue;
      c->new->mask[(y - c->new->y) * c->new->width + (x - c->new->x)] =
        (gfloat) coverage[i] * clip_at (c->old, x, y);
    }
}

/* Whether the path is one rectangle on whole device pixels, which the
 * clip can take as it is */
static gboolean
path_pixel_rectangle (GdkPen *pen, GdkRectangle *r)
{
  SubPath *sp;
  gdouble x0, y0, x1, y1;
  gint i;

  if (pen->n_subpaths != 1)
    return FALSE;
  sp = &pen->subpaths[0];
  if (sp->n != 4)
    return FALSE;
  x0 = x1 = sp->points[0].x;
  y0 = y1 = sp->points[0].y;
  for (i = 0; i < 4; i++)
    {
      Point *p = &sp->points[i], *q = &sp->points[(i + 1) % 4];

      if (p->x != q->x && p->y != q->y)
        return FALSE;
      if (p->x != floor (p->x) || p->y != floor (p->y))
        return FALSE;
      x0 = MIN (x0, p->x);  x1 = MAX (x1, p->x);
      y0 = MIN (y0, p->y);  y1 = MAX (y1, p->y);
    }
  r->x = x0;
  r->y = y0;
  r->width = x1 - x0;
  r->height = y1 - y0;
  return TRUE;
}

void
_gdk_pen_clip_preserve (GdkPen *pen)
{
  Clip *old = &pen->state->clip, new;
  GdkRectangle r, o = { old->x, old->y, old->width, old->height };
  GdkShape s;

  if (path_pixel_rectangle (pen, &r))
    {
      GdkRectangle i;
      gfloat *mask = NULL;

      if (!gdk_rectangle_intersect (&r, &o, &i))
        i.width = i.height = 0;
      if (old->mask && i.width > 0 && i.height > 0)
        {
          gint y;

          mask = g_new (gfloat, i.width * i.height);
          for (y = 0; y < i.height; y++)
            memcpy (mask + y * i.width,
                    old->mask + (i.y - old->y + y) * old->width +
                    (i.x - old->x),
                    sizeof (gfloat) * i.width);
        }
      g_free (old->mask);
      old->x = i.x;
      old->y = i.y;
      old->width = MAX (i.width, 0);
      old->height = MAX (i.height, 0);
      old->mask = mask;
      return;
    }

  path_shape (pen, &s);
  r.x = (gint) floor (s.minx);
  r.y = (gint) floor (s.miny);
  r.width = (gint) ceil (s.maxx) - r.x;
  r.height = (gint) ceil (s.maxy) - r.y;
  if (s.n == 0 || !gdk_rectangle_intersect (&r, &o, &r))
    r.x = r.y = r.width = r.height = 0;

  new.x = r.x;
  new.y = r.y;
  new.width = r.width;
  new.height = r.height;
  new.mask = g_new0 (gfloat, MAX (1, r.width * r.height));
  if (r.width > 0 && r.height > 0)
    {
      ClipRows c;

      c.old = old;
      c.new = &new;
      _gdk_shape_rows (&s, pen->state->fill_rule, &r, clip_row, &c);
    }
  _gdk_shape_free (&s);
  g_free (old->mask);
  *old = new;
}

void
_gdk_pen_clip (GdkPen *pen)
{
  _gdk_pen_clip_preserve (pen);
  path_clear (pen);
}
