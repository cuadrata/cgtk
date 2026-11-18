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

/* A pen draws paths of lines, arcs and curves, filled or stroked,
 * clipped, in a solid colour or a linear gradient, translucent, through
 * a transformation, as cairo does: Clarus's drawing, from Clearlooks,
 * was written for cairo and is drawn with it as it was.
 *
 * A pen draws into a layer of its own, the area it was made for, at the
 * window's scale, and lays the layer over the window when it is freed.
 * Everything is laid over what is there. Lines and caps are GDK's
 * GdkCapStyle and GdkJoinStyle, GDK_CAP_PROJECTING being a square cap;
 * GDK_CAP_NOT_LAST draws as GDK_CAP_BUTT.
 *
 * It is private to the library and its theme engines.
 */

#ifndef __GDK_PEN_H__
#define __GDK_PEN_H__

#include <gdk/gdk.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct _GdkPen        GdkPen;
typedef struct _GdkPenPattern GdkPenPattern;

/* x' = xx x + xy y + x0, y' = yx x + yy y + y0 */
typedef struct
{
  gdouble xx, yx;
  gdouble xy, yy;
  gdouble x0, y0;
} GdkPenMatrix;

/* A pen drawing on window: what is drawn shows only inside x, y,
 * width, height, and inside area if that is not NULL. It starts black,
 * with a line one pixel wide, butt caps and mitred joins. Freeing it
 * lays what it drew over the window. */
GdkPen  *_gdk_pen_new                (GdkWindow    *window,
                                      GdkRectangle *area,
                                      gint          x,
                                      gint          y,
                                      gint          width,
                                      gint          height);
void     _gdk_pen_free               (GdkPen       *pen);

void     _gdk_pen_save               (GdkPen       *pen);
void     _gdk_pen_restore            (GdkPen       *pen);

void     _gdk_pen_translate          (GdkPen       *pen,
                                      gdouble       tx,
                                      gdouble       ty);
void     _gdk_pen_rotate             (GdkPen       *pen,
                                      gdouble       angle);
void     _gdk_pen_transform          (GdkPen       *pen,
                                      const GdkPenMatrix *matrix);
void     _gdk_pen_identity_matrix    (GdkPen       *pen);

void     _gdk_pen_matrix_init        (GdkPenMatrix *matrix,
                                      gdouble       xx,
                                      gdouble       yx,
                                      gdouble       xy,
                                      gdouble       yy,
                                      gdouble       x0,
                                      gdouble       y0);
void     _gdk_pen_matrix_init_identity (GdkPenMatrix *matrix);
void     _gdk_pen_matrix_scale       (GdkPenMatrix *matrix,
                                      gdouble       sx,
                                      gdouble       sy);

void     _gdk_pen_new_path           (GdkPen       *pen);
void     _gdk_pen_move_to            (GdkPen       *pen,
                                      gdouble       x,
                                      gdouble       y);
void     _gdk_pen_line_to            (GdkPen       *pen,
                                      gdouble       x,
                                      gdouble       y);
void     _gdk_pen_rel_line_to        (GdkPen       *pen,
                                      gdouble       dx,
                                      gdouble       dy);
void     _gdk_pen_curve_to           (GdkPen       *pen,
                                      gdouble       x1,
                                      gdouble       y1,
                                      gdouble       x2,
                                      gdouble       y2,
                                      gdouble       x3,
                                      gdouble       y3);
/* Angles in radians, clockwise from three o'clock, as y grows down */
void     _gdk_pen_arc                (GdkPen       *pen,
                                      gdouble       xc,
                                      gdouble       yc,
                                      gdouble       radius,
                                      gdouble       angle1,
                                      gdouble       angle2);
void     _gdk_pen_arc_negative       (GdkPen       *pen,
                                      gdouble       xc,
                                      gdouble       yc,
                                      gdouble       radius,
                                      gdouble       angle1,
                                      gdouble       angle2);
void     _gdk_pen_rectangle          (GdkPen       *pen,
                                      gdouble       x,
                                      gdouble       y,
                                      gdouble       width,
                                      gdouble       height);
void     _gdk_pen_close_path         (GdkPen       *pen);

/* Colours and the pattern; components from 0 to 1 */
void     _gdk_pen_set_rgb            (GdkPen       *pen,
                                      gdouble       red,
                                      gdouble       green,
                                      gdouble       blue);
void     _gdk_pen_set_rgba           (GdkPen       *pen,
                                      gdouble       red,
                                      gdouble       green,
                                      gdouble       blue,
                                      gdouble       alpha);
/* The pen keeps a reference; the pattern keeps to the user space it
 * was set in */
void     _gdk_pen_set_pattern        (GdkPen        *pen,
                                      GdkPenPattern *pattern);

/* A gradient along the line from (x0, y0) to (x1, y1), padded with
 * its end colours */
GdkPenPattern *_gdk_pen_pattern_new_linear   (gdouble x0,
                                              gdouble y0,
                                              gdouble x1,
                                              gdouble y1);
void     _gdk_pen_pattern_add_stop_rgb  (GdkPenPattern *pattern,
                                         gdouble        offset,
                                         gdouble        red,
                                         gdouble        green,
                                         gdouble        blue);
void     _gdk_pen_pattern_add_stop_rgba (GdkPenPattern *pattern,
                                         gdouble        offset,
                                         gdouble        red,
                                         gdouble        green,
                                         gdouble        blue,
                                         gdouble        alpha);
void     _gdk_pen_pattern_unref         (GdkPenPattern *pattern);

void     _gdk_pen_set_line_width     (GdkPen       *pen,
                                      gdouble       width);
gdouble  _gdk_pen_get_line_width     (GdkPen       *pen);
void     _gdk_pen_set_line_cap       (GdkPen       *pen,
                                      GdkCapStyle   cap);
void     _gdk_pen_set_line_join      (GdkPen       *pen,
                                      GdkJoinStyle  join);
void     _gdk_pen_set_fill_rule      (GdkPen       *pen,
                                      GdkFillRule   rule);
/* Dash lengths in user space; an odd number of them repeats */
void     _gdk_pen_set_dash           (GdkPen        *pen,
                                      const gdouble *dashes,
                                      gint           n_dashes,
                                      gdouble        offset);

void     _gdk_pen_fill               (GdkPen       *pen);
void     _gdk_pen_fill_preserve      (GdkPen       *pen);
void     _gdk_pen_stroke             (GdkPen       *pen);
void     _gdk_pen_stroke_preserve    (GdkPen       *pen);
void     _gdk_pen_paint              (GdkPen       *pen);
void     _gdk_pen_clip               (GdkPen       *pen);
void     _gdk_pen_clip_preserve      (GdkPen       *pen);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GDK_PEN_H__ */
