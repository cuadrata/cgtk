/* GTK - The GIMP Toolkit
 * Metallum, a theme engine after GTK+ 1's Metal
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

/* GTK+ 1's Metal engine drew the look of Java's Metal: edges etched as
 * a dark line with a white one a pixel inside it, flat fills, square
 * tabs with one corner cut off, and on a scrollbar's thumb and a
 * scale's slider a pattern of dots in two shades. Metallum is written
 * anew to draw the same. Every colour comes from the style:
 *
 *   bg[SELECTED]   the accent: selections, a scrollbar's thumb, a
 *                  scale's slider and trough, a pressed toggle button,
 *                  a progress bar, a menu's border, the focus
 *   bg[PRELIGHT]   a widget under the pointer, and the light dots and
 *                  inner edge of a thumb or slider
 *   fg[PRELIGHT]   the dark dots and outer edge of a thumb or slider
 *
 * Metal's greys -- a scrollbar's trough, a notebook and its tabs, a
 * handle -- were shades of white. Here they are shades of bg[NORMAL]:
 * the same greys on Metal's own background, darker on any light one,
 * lighter on a dark one. Labels and arrows are black on a light
 * background, as Metal drew them, and the style's foreground on a dark
 * one. A dark theme wanting thumbs and sliders like a light one's gives
 * GtkRange a style with the light theme's bg and fg[PRELIGHT].
 */

#include <string.h>

#include "gtkgc.h"
#include "gtkhscale.h"
#include "gtkhscrollbar.h"
#include "gtkmenuitem.h"
#include "gtknotebook.h"
#include "gtkprogressbar.h"
#include "gtkrc.h"
#include "gtkscale.h"
#include "gtkscrollbar.h"
#include "gtkstyle.h"
#include "gtkthemes.h"
#include "gtkthemebuiltin.h"
#include "gtkthemecommon.h"
#include "gtktogglebutton.h"
#include "gtkwidget.h"

#define DETAIL(xx) ((detail) && (!strcmp ((xx), (detail))))

/* ---------------------------------------------------------------------
 * Colours
 */

/* The colours a draw needs beyond the style's own */
typedef struct
{
  GdkGC *face;          /* a notebook, the current tab, a handle */
  GdkGC *mid;           /* the tabs behind it, insensitive text */
  GdkGC *edge;          /* the dark half of an etched edge */
  GdkGC *trough;        /* a scrollbar's trough */
  GdkGC *outline;       /* the light half of an etched edge */
  GdkGC *ink;           /* arrows, ticks, dots and labels */
} MetallumColors;

static void
metallum_colors_get (GtkStyle *style, MetallumColors *c)
{
  GdkColor *bg = &style->bg[GTK_STATE_NORMAL];

  if (_gtk_theme_is_dark (style))
    {
      c->face = _gtk_theme_shade_gc (style, bg, 1.0, 1.0, 1.0);
      c->mid = _gtk_theme_shade_gc (style, bg, 1.15, 1.15, 1.15);
      c->edge = _gtk_theme_shade_gc (style, bg, 1.35, 1.35, 1.35);
      c->trough = _gtk_theme_shade_gc (style, bg, 0.6, 0.6, 0.6);
      c->outline = style->light_gc[GTK_STATE_NORMAL];
      c->ink = style->fg_gc[GTK_STATE_NORMAL];
    }
  else
    {
      c->face = _gtk_theme_shade_gc (style, bg, 1.0, 1.0, 1.0);
      c->mid = _gtk_theme_shade_gc (style, bg, 0.75, 0.75, 0.75);
      c->edge = _gtk_theme_shade_gc (style, bg, 0.625, 0.625, 0.625);
      c->trough = _gtk_theme_shade_gc (style, bg, 1.0, 1.0, 1.0);
      c->outline = style->white_gc;
      c->ink = style->black_gc;
    }
}

static void
metallum_colors_release (MetallumColors *c)
{
  gtk_gc_release (c->face);
  gtk_gc_release (c->mid);
  gtk_gc_release (c->edge);
  gtk_gc_release (c->trough);
}

/* ---------------------------------------------------------------------
 * Drawing
 */

/* An etched edge: DARK round the outside's top and left, LIGHT a pixel
 * further in, so that it shows below and to the right. */
static void
metallum_etch (GdkWindow *window, GdkRectangle *area, GdkGC *dark,
               GdkGC *light, gint x, gint y, gint width, gint height)
{
  _gtk_theme_rectangle (window, dark, area, FALSE, x, y, width - 2,
                        height - 2);
  _gtk_theme_rectangle (window, light, area, FALSE, x + 1, y + 1, width - 2,
                        height - 2);
}

/* Metal's texture: a diagonal of dots, DARK and LIGHT in turn, every
 * fourth pixel, from the top left of the area */
static void
metallum_dots (GdkWindow *window, GdkRectangle *area, GdkGC *dark,
               GdkGC *light, gint x, gint y, gint width, gint height)
{
  GdkPoint *points[2];
  gint n[2] = { 0, 0 };
  gint i, j;

  if (width <= 0 || height <= 0)
    return;
  points[0] = g_new (GdkPoint, (width / 2 + 1) * (height / 4 + 1));
  points[1] = g_new (GdkPoint, (width / 2 + 1) * (height / 4 + 1));
  for (j = 0; j < height; j++)
    for (i = j % 4; i < width; i += 4)
      {
        gint k = i % 2;

        points[k][n[k]].x = x + i;
        points[k][n[k]].y = y + j;
        n[k]++;
      }

  _gtk_theme_clip (dark, area);
  gdk_draw_points (window, dark, points[0], n[0]);
  _gtk_theme_unclip (dark, area);
  _gtk_theme_clip (light, area);
  gdk_draw_points (window, light, points[1], n[1]);
  _gtk_theme_unclip (light, area);
  g_free (points[0]);
  g_free (points[1]);
}

/* Metal's bumps, a handle's and a split pane's: every eight pixels
 * across and three down, a light dot with a dark one below and to its
 * right, the next row shifted three left */
static void
metallum_bumps (GdkWindow *window, GdkRectangle *area, MetallumColors *c,
                gint x, gint y, gint width, gint height)
{
  gint i, j;

  _gtk_theme_clip (c->outline, area);
  _gtk_theme_clip (c->ink, area);
  for (j = 0; j < height; j++)
    for (i = 0; i < width; i++)
      {
        gint u = i % 8, v = j % 3;

        if ((u == 3 && v == 0) || (u == 0 && v == 1))
          gdk_draw_point (window, c->outline, x + i, y + j);
        else if ((u == 4 && v == 1) || (u == 1 && v == 2))
          gdk_draw_point (window, c->ink, x + i, y + j);
      }
  _gtk_theme_unclip (c->outline, area);
  _gtk_theme_unclip (c->ink, area);
}

/* A solid triangle pointing ARROW_TYPE, centred in the area */
static void
metallum_triangle (GdkWindow *window, GdkGC *gc, GdkRectangle *area,
                   GtkArrowType arrow_type, gint base, gint x, gint y,
                   gint width, gint height)
{
  gint span, i, x0, y0;

  if (base % 2 == 0)
    base++;
  span = base / 2 + 1;

  _gtk_theme_clip (gc, area);
  switch (arrow_type)
    {
    case GTK_ARROW_UP:
    case GTK_ARROW_DOWN:
      x0 = x + (width - base) / 2;
      if (arrow_type == GTK_ARROW_UP)
        y0 = y + (height + span) / 2 - 1;
      else
        y0 = y + (height - span) / 2;
      for (i = 0; i < span; i++)
        {
          gint row = arrow_type == GTK_ARROW_UP ? y0 - i : y0 + i;

          gdk_draw_line (window, gc, x0 + i, row, x0 + base - 1 - i, row);
        }
      break;
    case GTK_ARROW_LEFT:
    case GTK_ARROW_RIGHT:
      y0 = y + (height - base) / 2;
      if (arrow_type == GTK_ARROW_LEFT)
        x0 = x + (width + span) / 2 - 1;
      else
        x0 = x + (width - span) / 2;
      for (i = 0; i < span; i++)
        {
          gint col = arrow_type == GTK_ARROW_LEFT ? x0 - i : x0 + i;

          gdk_draw_line (window, gc, col, y0 + i, col, y0 + base - 1 - i);
        }
      break;
    }
  _gtk_theme_unclip (gc, area);
}

/* ---------------------------------------------------------------------
 * Pieces
 */

/* A button: flat, etched. A toggle button is filled with the accent
 * when it is down; any other button under the pointer with
 * bg[PRELIGHT]. The
 * frame round a default button is drawn the same way. */
static void
metallum_button (GtkStyle *style, GdkWindow *window, GtkStateType state_type,
                 GdkRectangle *area, GtkWidget *widget, gboolean frame,
                 gint x, gint y, gint width, gint height)
{
  GtkStateType fill = state_type;

  /* GTK+ 1 shrinks a focused button to draw the focus round it; here
   * it keeps its size, and the focus goes inside. The default frame
   * round it is where it always is. */
  if (!frame && widget && GTK_WIDGET_HAS_FOCUS (widget))
    {
      x -= 1;
      y -= 1;
      width += 2;
      height += 2;
    }

  /* a toggle button that is down stays in the accent under the pointer,
   * or it would look up */
  if (widget && GTK_IS_TOGGLE_BUTTON (widget) &&
      (state_type != GTK_STATE_PRELIGHT || GTK_TOGGLE_BUTTON (widget)->active))
    fill = GTK_TOGGLE_BUTTON (widget)->active ? GTK_STATE_SELECTED : GTK_STATE_NORMAL;

  /* Only inside the edge: the two corners the etching leaves, top right
   * and bottom left, show what is behind the button */
  _gtk_theme_rectangle (window, style->bg_gc[fill], area, TRUE, x + 1, y + 1,
                        width - 2, height - 2);
  metallum_etch (window, area, style->dark_gc[state_type],
                 style->light_gc[state_type], x, y, width, height);
}

static void
metallum_scrollbar_trough (GtkStyle *style, GdkWindow *window,
                           GdkRectangle *area, GtkWidget *widget,
                           gint x, gint y, gint width, gint height)
{
  MetallumColors c;

  metallum_colors_get (style, &c);
  _gtk_theme_rectangle (window, c.trough, area, TRUE, x, y, width, height);
  metallum_etch (window, area, c.edge, c.outline, x, y, width, height);
  /* in its shadow along the top, or the left */
  if (GTK_IS_HSCROLLBAR (widget))
    _gtk_theme_line (window, c.mid, area, x + 1, y + 1, x + width - 2, y + 1);
  else
    _gtk_theme_line (window, c.mid, area, x + 1, y + 1, x + 1, y + height - 2);
  metallum_colors_release (&c);
}

/* A scrollbar's thumb, or a scale's slider, with a groove across its
 * middle */
static void
metallum_thumb (GtkStyle *style, GdkWindow *window, GtkStateType state_type,
                GdkRectangle *area, gboolean horizontal, gboolean groove,
                gint x, gint y, gint width, gint height)
{
  gboolean prelight = state_type == GTK_STATE_PRELIGHT;
  GdkGC *edge = style->bg_gc[GTK_STATE_PRELIGHT];
  GdkGC *fill = prelight ? edge : style->bg_gc[GTK_STATE_SELECTED];
  GdkGC *light = prelight ? style->white_gc : edge;
  GdkGC *dark = style->fg_gc[GTK_STATE_PRELIGHT];
  MetallumColors c;
  gint w, h;

  metallum_colors_get (style, &c);

  _gtk_theme_rectangle (window, fill, area, TRUE, x, y, width, height);
  _gtk_theme_rectangle (window, edge, area, FALSE, x + 1, y + 1, width - 2,
                        height - 2);
  _gtk_theme_rectangle (window, dark, area, FALSE, x, y, width - 2,
                        height - 2);
  if (horizontal)
    {
      _gtk_theme_line (window, c.outline, area, x, y + height - 1,
                       x + width - 1, y + height - 1);
      _gtk_theme_line (window, fill, area, x + width - 1, y + 1, x + width - 1,
                       y + height - 2);
      w = width - (width % 2 ? 11 : 10);
      h = height - (height % 2 ? 7 : 8);
      metallum_dots (window, area, dark, light, x + 5, y + 3, w, h);
    }
  else
    {
      _gtk_theme_line (window, c.outline, area, x + width - 1, y,
                       x + width - 1, y + height - 1);
      _gtk_theme_line (window, fill, area, x, y + height - 1, x + width - 2,
                       y + height - 1);
      w = width - (width % 2 ? 7 : 8);
      h = height - (height % 2 ? 11 : 10);
      metallum_dots (window, area, dark, light, x + 3, y + 5, w, h);
    }

  if (groove)
    {
      GdkGC *groove_light = prelight ? style->white_gc : edge;

      if (horizontal)
        {
          _gtk_theme_line (window, dark, area, x + width / 2, y + 2,
                           x + width / 2, y + height - 4);
          _gtk_theme_line (window, groove_light, area, x + width / 2 + 1,
                           y + 2, x + width / 2 + 1, y + height - 4);
        }
      else
        {
          _gtk_theme_line (window, dark, area, x + 2, y + height / 2,
                           x + width - 4, y + height / 2);
          _gtk_theme_line (window, groove_light, area, x + 2,
                           y + height / 2 + 1, x + width - 4,
                           y + height / 2 + 1);
        }
    }

  metallum_colors_release (&c);
}

/* A scale's trough: a band of the accent along its middle */
static void
metallum_scale_trough (GtkStyle *style, GdkWindow *window, GdkRectangle *area,
                       GtkWidget *widget, gint x, gint y, gint width,
                       gint height)
{
  GdkGC *fill = style->bg_gc[GTK_STATE_SELECTED];
  MetallumColors c;

  metallum_colors_get (style, &c);
  if (GTK_IS_HSCALE (widget))
    {
      _gtk_theme_rectangle (window, fill, area, TRUE, x, y + 4, width - 2, 9);
      _gtk_theme_rectangle (window, c.edge, area, FALSE, x, y + 4, width - 2,
                            7);
      _gtk_theme_rectangle (window, c.outline, area, FALSE, x + 1, y + 5,
                            width - 2, 7);
    }
  else
    {
      _gtk_theme_rectangle (window, fill, area, TRUE, x + 4, y, 9, height - 2);
      _gtk_theme_rectangle (window, c.edge, area, FALSE, x + 4, y, 7,
                            height - 2);
      _gtk_theme_rectangle (window, c.outline, area, FALSE, x + 5, y + 1, 7,
                            height - 2);
    }
  metallum_colors_release (&c);
}

/* Where a notebook's tab is: whether it is the first, and whether it
 * is the current one */
static void
metallum_tab_place (GtkWidget *widget, GtkStateType state_type, gint x, gint y,
                    gboolean *first, gboolean *current)
{
  GList *list;

  *first = FALSE;
  *current = state_type == GTK_STATE_NORMAL;
  if (!widget || !GTK_IS_NOTEBOOK (widget))
    return;

  for (list = GTK_NOTEBOOK (widget)->children; list; list = list->next)
    {
      GtkNotebookPage *page = list->data;

      if (page->allocation.x == x && page->allocation.y == y)
        {
          *first = list == GTK_NOTEBOOK (widget)->children;
          *current = page == GTK_NOTEBOOK (widget)->cur_page;
          return;
        }
    }
}

/* A tab: square, with the corner nearest the start of the row cut off */
static void
metallum_tab (GtkStyle *style, GdkWindow *window, GtkStateType state_type,
              GdkRectangle *area, GtkWidget *widget, gint x, gint y,
              gint width, gint height)
{
  GtkPositionType pos = GTK_POS_TOP;
  gboolean first, current;
  MetallumColors c;
  GdkGC *fill, *dark, *light;
  GdkPoint p[5];
  gint r = x + width - 1, b = y + height - 1;

  if (widget && GTK_IS_NOTEBOOK (widget))
    pos = GTK_NOTEBOOK (widget)->tab_pos;
  metallum_tab_place (widget, state_type, x, y, &first, &current);
  metallum_colors_get (style, &c);
  fill = current ? c.face : c.mid;
  dark = c.edge;
  light = c.outline;

  _gtk_theme_rectangle (window, c.face, area, TRUE, x, y, width, height);

  switch (pos)
    {
    case GTK_POS_TOP:
    case GTK_POS_LEFT:
      p[0].x = x + 2;  p[0].y = y + height;
      p[1].x = x + 2;  p[1].y = y + 6;
      p[2].x = x + 6;  p[2].y = y + 2;
      p[3].x = r;      p[3].y = y + 2;
      p[4].x = r;      p[4].y = y + height;
      break;
    case GTK_POS_RIGHT:
      p[0].x = r - 1;  p[0].y = b;
      p[1].x = r - 1;  p[1].y = y + 6;
      p[2].x = r - 5;  p[2].y = y + 2;
      p[3].x = x - 1;  p[3].y = y + 2;
      p[4].x = x - 1;  p[4].y = b;
      break;
    case GTK_POS_BOTTOM:
      p[0].x = x + 2;  p[0].y = y;
      p[1].x = x + 2;  p[1].y = b - 5;
      p[2].x = x + 6;  p[2].y = b - 1;
      p[3].x = r;      p[3].y = b - 1;
      p[4].x = r;      p[4].y = y;
      break;
    }
  _gtk_theme_clip (fill, area);
  gdk_draw_polygon (window, fill, TRUE, p, 5);
  _gtk_theme_unclip (fill, area);

  switch (pos)
    {
    case GTK_POS_TOP:
      /* the first tab's edge runs on down into the notebook's */
      if (first || current)
        _gtk_theme_line (window, dark, area, x, y + 6, x, first ? b + 2 : b);
      _gtk_theme_line (window, dark, area, x, y + 6, x + 6, y);
      _gtk_theme_line (window, dark, area, x + 6, y, r - 1, y);
      _gtk_theme_line (window, dark, area, r, y + 1, r, b);
      _gtk_theme_line (window, light, area, x + 1, y + 6, x + 1,
                       first ? b + 2 : b);
      _gtk_theme_line (window, light, area, x + 1, y + 6, x + 6, y + 1);
      _gtk_theme_line (window, light, area, x + 6, y + 1, r - 1, y + 1);
      break;
    case GTK_POS_LEFT:
      _gtk_theme_line (window, dark, area, x, y + 6, x, b);
      _gtk_theme_line (window, dark, area, x, y + 6, x + 6, y);
      _gtk_theme_line (window, dark, area, x + 6, y, first ? r + 2 : r, y);
      _gtk_theme_line (window, dark, area, x, b, r, b);
      _gtk_theme_line (window, light, area, x + 1, y + 6, x + 6, y + 1);
      _gtk_theme_line (window, light, area, x + 6, y + 1, first ? r + 2 : r,
                       y + 1);
      break;
    case GTK_POS_RIGHT:
      _gtk_theme_line (window, dark, area, r, y + 6, r, b);
      _gtk_theme_line (window, dark, area, r, y + 6, r - 6, y);
      _gtk_theme_line (window, dark, area, first ? x - 2 : x - 1, y, r - 6, y);
      _gtk_theme_line (window, dark, area, x - 1, b, r, b);
      _gtk_theme_line (window, light, area, r - 1, y + 6, r - 6, y + 1);
      _gtk_theme_line (window, light, area, r - 6, y + 1,
                       first ? x - 2 : x - 1, y + 1);
      break;
    case GTK_POS_BOTTOM:
      if (first || current)
        _gtk_theme_line (window, dark, area, x, b - 5, x,
                         first ? y - 2 : y - 1);
      _gtk_theme_line (window, dark, area, x, b - 5, x + 6, b + 1);
      _gtk_theme_line (window, dark, area, x + 5, b, r - 1, b);
      _gtk_theme_line (window, dark, area, r, b, r, y - 1);
      _gtk_theme_line (window, light, area, x + 1, b - 5, x + 1,
                       first ? y - 2 : y - 1);
      _gtk_theme_line (window, light, area, x + 1, b - 5, x + 5, b - 1);
      break;
    }

  metallum_colors_release (&c);
}

/* A notebook's page, or a handle box's bar: the face, etched */
static void
metallum_panel (GtkStyle *style, GdkWindow *window, GdkRectangle *area,
                gint x, gint y, gint width, gint height)
{
  MetallumColors c;

  metallum_colors_get (style, &c);
  _gtk_theme_rectangle (window, c.face, area, TRUE, x, y, width, height);
  metallum_etch (window, area, c.edge, c.outline, x, y, width, height);
  metallum_colors_release (&c);
}

/* ---------------------------------------------------------------------
 * The style class
 */

static void
metallum_draw_shadow (GtkStyle      *style,
                      GdkWindow     *window,
                      GtkStateType   state_type,
                      GtkShadowType  shadow_type,
                      GdkRectangle  *area,
                      GtkWidget     *widget,
                      gchar         *detail,
                      gint           x,
                      gint           y,
                      gint           width,
                      gint           height)
{
  GdkGC *light, *dark, *black, *bg;
  gint r, b;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  if (shadow_type == GTK_SHADOW_NONE)
    return;
  _gtk_theme_sanitize_size (window, &width, &height);
  r = x + width - 1;
  b = y + height - 1;

  if (DETAIL ("frame"))
    {
      _gtk_theme_rectangle (window, style->dark_gc[state_type], area, FALSE, x,
                            y, width - 1, height - 1);
      return;
    }
  if (DETAIL ("optionmenutab"))
    {
      gint i;

      GdkGC *ink = _gtk_theme_is_dark (style) ? style->fg_gc[state_type] :
                                              style->black_gc;

      /* a triangle eleven pixels wide, its top at y */
      _gtk_theme_clip (ink, area);
      for (i = 0; i < 6; i++)
        gdk_draw_line (window, ink, x + i, y + i, x + 10 - i, y + i);
      _gtk_theme_unclip (ink, area);
      return;
    }
  if (DETAIL ("button") || DETAIL ("buttondefault") || DETAIL ("optionmenu") ||
      DETAIL ("handlebox_bin"))
    shadow_type = GTK_SHADOW_ETCHED_IN;

  light = style->light_gc[state_type];
  dark = style->dark_gc[state_type];
  black = style->black_gc;
  bg = style->bg_gc[state_type];

  switch (shadow_type)
    {
    case GTK_SHADOW_IN:
      _gtk_theme_line (window, light, area, x, b, r, b);
      _gtk_theme_line (window, light, area, r, y, r, b);
      _gtk_theme_line (window, bg, area, x + 1, b - 1, r - 1, b - 1);
      _gtk_theme_line (window, bg, area, r - 1, y + 1, r - 1, b - 1);
      _gtk_theme_line (window, black, area, x + 1, y + 1, r - 1, y + 1);
      _gtk_theme_line (window, black, area, x + 1, y + 1, x + 1, b - 1);
      _gtk_theme_line (window, dark, area, x, y, r, y);
      _gtk_theme_line (window, dark, area, x, y, x, b);
      break;
    case GTK_SHADOW_OUT:
      _gtk_theme_line (window, dark, area, x + 1, b - 1, r - 1, b - 1);
      _gtk_theme_line (window, dark, area, r - 1, y + 1, r - 1, b - 1);
      _gtk_theme_line (window, light, area, x, y, r, y);
      _gtk_theme_line (window, light, area, x, y, x, b);
      _gtk_theme_line (window, bg, area, x + 1, y + 1, r - 1, y + 1);
      _gtk_theme_line (window, bg, area, x + 1, y + 1, x + 1, b - 1);
      _gtk_theme_line (window, black, area, x, b, r, b);
      _gtk_theme_line (window, black, area, r, y, r, b);
      break;
    case GTK_SHADOW_ETCHED_IN:
    case GTK_SHADOW_ETCHED_OUT:
      {
        GdkGC *outer = shadow_type == GTK_SHADOW_ETCHED_IN ? light : dark;
        GdkGC *inner = shadow_type == GTK_SHADOW_ETCHED_IN ? dark : light;

        _gtk_theme_line (window, outer, area, x, b, r, b);
        _gtk_theme_line (window, outer, area, r, y, r, b);
        _gtk_theme_line (window, inner, area, x, y, r - 1, y);
        _gtk_theme_line (window, inner, area, x, y, x, b - 1);
        _gtk_theme_line (window, outer, area, x + 1, y + 1, r - 1, y + 1);
        _gtk_theme_line (window, outer, area, x + 1, y + 1, x + 1, b - 1);
        _gtk_theme_line (window, inner, area, x + 1, b - 1, r - 1, b - 1);
        _gtk_theme_line (window, inner, area, r - 1, y + 1, r - 1, b - 1);
      }
      break;
    default:
      break;
    }
}

static void
metallum_draw_box (GtkStyle      *style,
                   GdkWindow     *window,
                   GtkStateType   state_type,
                   GtkShadowType  shadow_type,
                   GdkRectangle  *area,
                   GtkWidget     *widget,
                   gchar         *detail,
                   gint           x,
                   gint           y,
                   gint           width,
                   gint           height)
{
  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  /* A split pane's divider, as Metal drew it: the window's background
   * covered in bumps */
  if (DETAIL ("hpaned") || DETAIL ("vpaned"))
    {
      MetallumColors c;

      metallum_colors_get (style, &c);
      _gtk_theme_rectangle (window, style->bg_gc[state_type], area, TRUE,
                            x, y, width, height);
      metallum_bumps (window, area, &c, x + 1, y + 1, width - 2, height - 2);
      metallum_colors_release (&c);
      return;
    }

  if (DETAIL ("trough"))
    {
      if (widget && GTK_IS_PROGRESS_BAR (widget))
        {
          _gtk_theme_rectangle (window, style->light_gc[GTK_STATE_NORMAL],
                                area, TRUE, x, y, width, height);
          gtk_paint_shadow (style, window, state_type, shadow_type, area,
                            widget, detail, x, y, width, height);
        }
      else if (widget && GTK_IS_SCROLLBAR (widget))
        metallum_scrollbar_trough (style, window, area, widget,
                                   x, y, width, height);
      else if (widget && GTK_IS_SCALE (widget))
        metallum_scale_trough (style, window, area, widget,
                               x, y, width, height);
    }
  else if (DETAIL ("menu"))
    {
      MetallumColors c;

      /* a line of the accent, and white inside it */
      metallum_colors_get (style, &c);
      _gtk_theme_rectangle (window, c.outline, area, FALSE, x + 1, y + 1,
                            width - 2, height - 2);
      _gtk_theme_rectangle (window, style->bg_gc[GTK_STATE_SELECTED], area,
                            FALSE, x, y, width - 1, height - 1);
      metallum_colors_release (&c);
    }
  else if (DETAIL ("menuitem"))
    {
      _gtk_theme_rectangle (window, style->bg_gc[GTK_STATE_SELECTED], area,
                            TRUE, x, y, width, height);
      _gtk_theme_line (window, style->dark_gc[GTK_STATE_SELECTED], area, x, y,
                       x + width, y);
      _gtk_theme_line (window, style->light_gc[GTK_STATE_SELECTED], area, x,
                       y + height - 1, x + width, y + height - 1);
    }
  else if (DETAIL ("bar"))
    _gtk_theme_rectangle (window, style->bg_gc[GTK_STATE_SELECTED], area, TRUE,
                          x + 1, y + 1, width - 2, height - 2);
  else if (DETAIL ("menubar"))
    _gtk_theme_rectangle (window, style->bg_gc[state_type], area, TRUE, x, y,
                          width, height);
  else if (DETAIL ("slider"))
    {
      if (widget && GTK_IS_SCROLLBAR (widget))
        metallum_thumb (style, window, state_type, area,
                        GTK_IS_HSCROLLBAR (widget), FALSE, x, y, width, height);
    }
  else if (DETAIL ("notebook"))
    metallum_panel (style, window, area, x, y, width, height);
  else if (DETAIL ("tab"))
    metallum_tab (style, window, state_type, area, widget, x, y, width, height);
  else if (DETAIL ("button") || DETAIL ("togglebutton") || DETAIL ("buttondefault"))
    metallum_button (style, window, state_type, area, widget,
                     DETAIL ("buttondefault"), x, y, width, height);
  else
    {
      if (!style->bg_pixmap[state_type] ||
          gdk_window_get_type (window) == GDK_WINDOW_PIXMAP)
        _gtk_theme_rectangle (window, style->bg_gc[state_type], area, TRUE, x,
                              y, width, height);
      else
        gtk_style_apply_default_background (style, window, TRUE, state_type, area,
                                            x, y, width, height);
      gtk_paint_shadow (style, window, state_type, shadow_type, area, widget,
                        detail, x, y, width, height);
    }
}

static void
metallum_draw_flat_box (GtkStyle      *style,
                        GdkWindow     *window,
                        GtkStateType   state_type,
                        GtkShadowType  shadow_type,
                        GdkRectangle  *area,
                        GtkWidget     *widget,
                        gchar         *detail,
                        gint           x,
                        gint           y,
                        gint           width,
                        gint           height)
{
  GdkGC *gc = style->bg_gc[state_type];

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  if (DETAIL ("selected") || (DETAIL ("text") && state_type == GTK_STATE_SELECTED))
    gc = style->bg_gc[GTK_STATE_SELECTED];
  else if (DETAIL ("viewportbin"))
    gc = style->bg_gc[GTK_STATE_NORMAL];

  if (gc != style->bg_gc[state_type] || !style->bg_pixmap[state_type] ||
      gdk_window_get_type (window) == GDK_WINDOW_PIXMAP)
    _gtk_theme_rectangle (window, gc, area, TRUE, x, y, width, height);
  else
    gtk_style_apply_default_background (style, window, TRUE, state_type, area,
                                        x, y, width, height);

  if (DETAIL ("tooltip"))
    _gtk_theme_rectangle (window, style->black_gc, area, FALSE, x, y,
                          width - 1, height - 1);
}

static void
metallum_draw_string (GtkStyle     *style,
                      GdkWindow    *window,
                      GtkStateType  state_type,
                      GdkRectangle *area,
                      GtkWidget    *widget,
                      gchar        *detail,
                      gint          x,
                      gint          y,
                      const gchar  *string)
{
  MetallumColors c;
  GdkGC *gc;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  metallum_colors_get (style, &c);
  /* Metal's engraving, a shade of the background, can hardly be read on
   * a dark one: there the theme's own insensitive colour */
  if (state_type == GTK_STATE_INSENSITIVE && _gtk_theme_is_dark (style))
    gc = style->fg_gc[GTK_STATE_INSENSITIVE];
  else if (state_type == GTK_STATE_INSENSITIVE)
    {
      _gtk_theme_clip (c.outline, area);
      gdk_draw_string (window, style->font, c.outline, x + 1, y + 1, string);
      _gtk_theme_unclip (c.outline, area);
      gc = c.mid;
    }
  else if (DETAIL ("label") && !_gtk_theme_is_dark (style))
    gc = style->black_gc;
  else
    gc = style->fg_gc[state_type];

  _gtk_theme_clip (gc, area);
  gdk_draw_string (window, style->font, gc, x, y, string);
  _gtk_theme_unclip (gc, area);
  metallum_colors_release (&c);
}

static void
metallum_draw_arrow (GtkStyle      *style,
                     GdkWindow     *window,
                     GtkStateType   state_type,
                     GtkShadowType  shadow_type,
                     GdkRectangle  *area,
                     GtkWidget     *widget,
                     gchar         *detail,
                     GtkArrowType   arrow_type,
                     gint           fill,
                     gint           x,
                     gint           y,
                     gint           width,
                     gint           height)
{
  MetallumColors c;
  GdkGC *gc;
  gint base;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);
  metallum_colors_get (style, &c);

  if (DETAIL ("menuitem"))
    gc = style->fg_gc[state_type];
  else
    {
      gc = _gtk_theme_is_dark (style) ? style->fg_gc[state_type] : c.ink;
      /* a scrollbar's or spin button's stepper is a box */
      if (fill)
        metallum_draw_box (style, window, state_type, GTK_SHADOW_ETCHED_IN,
                           area, widget, detail, x, y, width, height);
    }

  if (arrow_type == GTK_ARROW_UP || arrow_type == GTK_ARROW_DOWN)
    base = width / 2;
  else if (arrow_type == GTK_ARROW_RIGHT && widget && GTK_IS_MENU_ITEM (widget))
    base = 7;          /* a submenu's */
  else
    base = height / 2;
  /* a submenu's arrow a pixel lower than centred in the box GTK+ gives
   * it, which puts it level with the label */
  if (DETAIL ("menuitem") && arrow_type == GTK_ARROW_RIGHT)
    y += 1;
  metallum_triangle (window, gc, area, arrow_type, base, x, y, width, height);

  metallum_colors_release (&c);
}

/* The lines of a radio button's ring, in pixels from its top left:
 * dark round the top left, white round the bottom right */
typedef struct { gint8 x1, y1, x2, y2; } MetallumRun;
#define N_RUNS(runs) ((gint) (sizeof (runs) / sizeof ((runs)[0])))

static const MetallumRun radio_dark[] = {
  { 4, 0, 7, 0 }, { 2, 1, 3, 1 }, { 8, 1, 9, 1 }, { 2, 10, 3, 10 },
  { 8, 10, 9, 10 }, { 4, 11, 7, 11 }, { 0, 4, 0, 7 }, { 1, 2, 1, 3 },
  { 1, 8, 1, 9 }, { 10, 2, 10, 3 }, { 10, 8, 10, 9 }, { 11, 4, 11, 7 }
};
static const MetallumRun radio_light[] = {
  { 4, 1, 7, 1 }, { 2, 2, 3, 2 }, { 8, 2, 9, 2 }, { 2, 11, 3, 11 },
  { 8, 11, 9, 11 }, { 4, 12, 7, 12 }, { 1, 4, 1, 7 }, { 2, 2, 2, 3 },
  { 2, 8, 2, 9 }, { 11, 2, 11, 3 }, { 11, 8, 11, 9 }, { 12, 4, 12, 7 },
  { 10, 1, 10, 1 }, { 10, 10, 10, 10 }
};
static const MetallumRun menu_radio_dark[] = {
  { 2, 0, 6, 0 }, { 1, 1, 1, 1 }, { 7, 1, 7, 1 }, { 2, 8, 6, 8 },
  { 7, 7, 7, 7 }, { 0, 2, 0, 6 }, { 8, 2, 8, 6 }
};
static const MetallumRun menu_radio_light[] = {
  { 3, 1, 6, 1 }, { 8, 1, 8, 1 }, { 2, 2, 2, 2 }, { 1, 3, 1, 6 },
  { 9, 2, 9, 7 }, { 1, 8, 1, 8 }, { 8, 8, 8, 8 }, { 2, 9, 7, 9 }
};

static void
metallum_runs (GdkWindow *window, GdkGC *gc, GdkRectangle *area,
               const MetallumRun *runs, gint n, gint x, gint y)
{
  gint i;

  _gtk_theme_clip (gc, area);
  for (i = 0; i < n; i++)
    gdk_draw_line (window, gc, x + runs[i].x1, y + runs[i].y1,
                   x + runs[i].x2, y + runs[i].y2);
  _gtk_theme_unclip (gc, area);
}

static void
metallum_draw_option (GtkStyle      *style,
                      GdkWindow     *window,
                      GtkStateType   state_type,
                      GtkShadowType  shadow_type,
                      GdkRectangle  *area,
                      GtkWidget     *widget,
                      gchar         *detail,
                      gint           x,
                      gint           y,
                      gint           width,
                      gint           height)
{
  MetallumColors c;
  GdkGC *dark = style->dark_gc[GTK_STATE_NORMAL];

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  metallum_colors_get (style, &c);
  x -= 1;
  y -= 1;

  if (widget && GTK_IS_MENU_ITEM (widget))
    {
      metallum_runs (window, dark, area, menu_radio_dark,
                     N_RUNS (menu_radio_dark), x, y);
      metallum_runs (window, c.outline, area, menu_radio_light,
                     N_RUNS (menu_radio_light), x, y);
      if (shadow_type == GTK_SHADOW_IN)
        {
          _gtk_theme_rectangle (window, c.ink, area, TRUE, x + 2, y + 3, 5, 3);
          _gtk_theme_rectangle (window, c.ink, area, TRUE, x + 3, y + 2, 3, 5);
        }
    }
  else
    {
      _gtk_theme_rectangle (window, style->bg_gc[GTK_STATE_NORMAL], area, TRUE,
                            x, y, width + 2, height + 2);
      metallum_runs (window, dark, area, radio_dark,
                     N_RUNS (radio_dark), x, y);
      metallum_runs (window, c.outline, area, radio_light,
                     N_RUNS (radio_light), x, y);
      if (shadow_type == GTK_SHADOW_IN)
        {
          _gtk_theme_rectangle (window, c.ink, area, TRUE, x + 3, y + 4, 6, 4);
          _gtk_theme_rectangle (window, c.ink, area, TRUE, x + 4, y + 3, 4, 6);
        }
    }
  metallum_colors_release (&c);
}

static void
metallum_draw_check (GtkStyle      *style,
                     GdkWindow     *window,
                     GtkStateType   state_type,
                     GtkShadowType  shadow_type,
                     GdkRectangle  *area,
                     GtkWidget     *widget,
                     gchar         *detail,
                     gint           x,
                     gint           y,
                     gint           width,
                     gint           height)
{
  MetallumColors c;
  gint size;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  metallum_colors_get (style, &c);
  if (widget && GTK_IS_MENU_ITEM (widget))
    {
      size = 8;
      metallum_etch (window, area, style->dark_gc[state_type],
                     style->light_gc[state_type], x - 2, y - 2, size + 2, size + 2);
      if (shadow_type == GTK_SHADOW_IN)
        {
          _gtk_theme_line (window, c.ink, area, x + 1, y, x + 1, y + 4);
          _gtk_theme_line (window, c.ink, area, x + 2, y, x + 2, y + 4);
          _gtk_theme_line (window, c.ink, area, x + 3, y + 3, x + 7, y - 1);
          _gtk_theme_line (window, c.ink, area, x + 3, y + 2, x + 7, y - 2);
        }
    }
  else
    {
      size = 11;
      _gtk_theme_rectangle (window, style->bg_gc[GTK_STATE_NORMAL], area, TRUE,
                            x, y, width, height);
      metallum_etch (window, area, style->dark_gc[state_type],
                     style->light_gc[state_type], x - 2, y - 2, size + 2, size + 2);
      if (shadow_type == GTK_SHADOW_IN)
        {
          _gtk_theme_line (window, c.ink, area, x + 1, y + 3, x + 1, y + 7);
          _gtk_theme_line (window, c.ink, area, x + 2, y + 3, x + 2, y + 7);
          _gtk_theme_line (window, c.ink, area, x + 3, y + 6, x + 7, y + 2);
          _gtk_theme_line (window, c.ink, area, x + 3, y + 5, x + 7, y + 1);
        }
    }
  metallum_colors_release (&c);
}

static void
metallum_draw_tab (GtkStyle      *style,
                   GdkWindow     *window,
                   GtkStateType   state_type,
                   GtkShadowType  shadow_type,
                   GdkRectangle  *area,
                   GtkWidget     *widget,
                   gchar         *detail,
                   gint           x,
                   gint           y,
                   gint           width,
                   gint           height)
{
  gtk_paint_box (style, window, state_type, shadow_type, area, widget, detail,
                 x, y, width, height);
}

/* The background round a gap in a frame or notebook, where a tab or a
 * frame's label meets it */
static void
metallum_gap (GtkStyle *style, GdkWindow *window, GtkStateType state_type,
              GdkRectangle *area, gint x, gint y, gint width, gint height,
              GtkPositionType gap_side, gint gap_x, gint gap_width)
{
  GdkRectangle gap = { 0, 0, 0, 0 };

  switch (gap_side)
    {
    case GTK_POS_TOP:
      gap.x = x + gap_x;  gap.y = y;
      gap.width = gap_width;  gap.height = 2;
      break;
    case GTK_POS_BOTTOM:
      gap.x = x + gap_x;  gap.y = y + height - 2;
      gap.width = gap_width;  gap.height = 2;
      break;
    case GTK_POS_LEFT:
      gap.x = x;  gap.y = y + gap_x;
      gap.width = 2;  gap.height = gap_width;
      break;
    case GTK_POS_RIGHT:
      gap.x = x + width - 2;  gap.y = y + gap_x;
      gap.width = 2;  gap.height = gap_width;
      break;
    }
  gtk_style_apply_default_background (style, window, TRUE, state_type, area,
                                      gap.x, gap.y, gap.width, gap.height);
}

static void
metallum_draw_shadow_gap (GtkStyle        *style,
                          GdkWindow       *window,
                          GtkStateType     state_type,
                          GtkShadowType    shadow_type,
                          GdkRectangle    *area,
                          GtkWidget       *widget,
                          gchar           *detail,
                          gint             x,
                          gint             y,
                          gint             width,
                          gint             height,
                          GtkPositionType  gap_side,
                          gint             gap_x,
                          gint             gap_width)
{
  gtk_paint_shadow (style, window, state_type, shadow_type, area, widget, detail,
                    x, y, width, height);
  metallum_gap (style, window, state_type, area, x, y, width, height,
                gap_side, gap_x, gap_width);
}

static void
metallum_draw_box_gap (GtkStyle        *style,
                       GdkWindow       *window,
                       GtkStateType     state_type,
                       GtkShadowType    shadow_type,
                       GdkRectangle    *area,
                       GtkWidget       *widget,
                       gchar           *detail,
                       gint             x,
                       gint             y,
                       gint             width,
                       gint             height,
                       GtkPositionType  gap_side,
                       gint             gap_x,
                       gint             gap_width)
{
  gtk_paint_box (style, window, state_type, shadow_type, area, widget, detail,
                 x, y, width, height);
  metallum_gap (style, window, state_type, area, x, y, width, height,
                gap_side, gap_x, gap_width);
}

static void
metallum_draw_extension (GtkStyle        *style,
                         GdkWindow       *window,
                         GtkStateType     state_type,
                         GtkShadowType    shadow_type,
                         GdkRectangle    *area,
                         GtkWidget       *widget,
                         gchar           *detail,
                         gint             x,
                         gint             y,
                         gint             width,
                         gint             height,
                         GtkPositionType  gap_side)
{
  gint xt = style->klass->xthickness, yt = style->klass->ythickness;
  GdkRectangle gap = { 0, 0, 0, 0 };

  gtk_paint_box (style, window, state_type, shadow_type, area, widget, detail,
                 x, y, width, height);

  /* the edge it shares with the notebook is left open */
  switch (gap_side)
    {
    case GTK_POS_TOP:
      gap.x = x + xt;  gap.y = y;
      gap.width = width - 2 * xt;  gap.height = yt;
      break;
    case GTK_POS_BOTTOM:
      gap.x = x + xt;  gap.y = y + height - yt;
      gap.width = width - 2 * xt;  gap.height = yt;
      break;
    case GTK_POS_LEFT:
      gap.x = x;  gap.y = y + yt;
      gap.width = xt;  gap.height = height - 2 * yt;
      break;
    case GTK_POS_RIGHT:
      gap.x = x + width - xt;  gap.y = y + yt;
      gap.width = xt;  gap.height = height - 2 * yt;
      break;
    }
  gtk_style_apply_default_background (style, window, TRUE, state_type, area,
                                      gap.x, gap.y, gap.width, gap.height);
}

static void
metallum_draw_focus (GtkStyle     *style,
                     GdkWindow    *window,
                     GdkRectangle *area,
                     GtkWidget    *widget,
                     gchar        *detail,
                     gint          x,
                     gint          y,
                     gint          width,
                     gint          height)
{
  GdkGC *gc = style->bg_gc[GTK_STATE_SELECTED];

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  /* Only buttons, tabs, and check and radio buttons show it: these round
   * their check box and label both */
  if (DETAIL ("button") || DETAIL ("togglebutton") || DETAIL ("buttondefault") ||
      DETAIL ("tab"))
    _gtk_theme_rectangle (window, gc, area, FALSE, x + 2, y + 2, width - 4,
                          height - 4);
  else if (DETAIL ("checkbutton") || DETAIL ("radiobutton"))
    _gtk_theme_rectangle (window, gc, area, FALSE, x, y, width, height);
}

static void
metallum_draw_slider (GtkStyle       *style,
                      GdkWindow      *window,
                      GtkStateType    state_type,
                      GtkShadowType   shadow_type,
                      GdkRectangle   *area,
                      GtkWidget      *widget,
                      gchar          *detail,
                      gint            x,
                      gint            y,
                      gint            width,
                      gint            height,
                      GtkOrientation  orientation)
{
  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);
  metallum_thumb (style, window, state_type, area,
                  widget ? GTK_IS_HSCALE (widget) : orientation == GTK_ORIENTATION_HORIZONTAL,
                  TRUE, x, y, width, height);
}

/* A handle: the face, etched, with a pattern of white and black dots */
static void
metallum_draw_handle (GtkStyle       *style,
                      GdkWindow      *window,
                      GtkStateType    state_type,
                      GtkShadowType   shadow_type,
                      GdkRectangle   *area,
                      GtkWidget      *widget,
                      gchar          *detail,
                      gint            x,
                      gint            y,
                      gint            width,
                      gint            height,
                      GtkOrientation  orientation)
{
  MetallumColors c;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);
  metallum_colors_get (style, &c);

  _gtk_theme_rectangle (window, c.face, area, TRUE, x, y, width, height);
  _gtk_theme_rectangle (window, c.outline, area, FALSE, x + 1, y + 1,
                        width - 2, height - 2);
  _gtk_theme_rectangle (window, c.edge, area, FALSE, x, y, width - 2,
                        height - 2);

  metallum_bumps (window, area, &c, x + 2, y + 2, width - 4, height - 4);

  metallum_colors_release (&c);
}

/* ---------------------------------------------------------------------
 * The engine
 */

static GtkStyleClass *metallum_class = NULL;

/* Metal's metrics: wide scrollbars, their steppers square, the thumb
 * against them and never shorter than it is wide, and two pixels of
 * trough round them */
static void
metallum_set_props (GtkStyle *style)
{
  gtk_style_set_prop_experimental (style, "GtkRange::slider_width", 17);
  gtk_style_set_prop_experimental (style, "GtkRange::stepper_size", 17);
  gtk_style_set_prop_experimental (style, "GtkRange::stepper_spacing", 0);
  gtk_style_set_prop_experimental (style, "GtkRange::min_slider_size", 17);
  gtk_style_set_prop_experimental (style, "GtkRange::trough_border", 2);
  gtk_style_set_prop_experimental (style, "GtkPaned::handle_full_size", 1);
  gtk_style_set_prop_experimental (style, "GtkPaned::handle_width", 8);
}

static guint
metallum_parse_rc_style (GScanner   *scanner,
                         GtkRcStyle *rc_style)
{
  /* It takes no options: its block is empty */
  if (g_scanner_get_next_token (scanner) != G_TOKEN_RIGHT_CURLY)
    return G_TOKEN_RIGHT_CURLY;
  rc_style->engine_data = NULL;

  return G_TOKEN_NONE;
}

static void
metallum_merge_rc_style (GtkRcStyle *dest,
                         GtkRcStyle *src)
{
}

static void
metallum_rc_style_to_style (GtkStyle   *style,
                            GtkRcStyle *rc_style)
{
  if (!metallum_class)
    {
      metallum_class = g_new (GtkStyleClass, 1);
      *metallum_class = *style->klass;
      metallum_class->xthickness = 2;
      metallum_class->ythickness = 2;
      metallum_class->draw_shadow = metallum_draw_shadow;
      metallum_class->draw_polygon = _gtk_theme_draw_polygon;
      metallum_class->draw_arrow = metallum_draw_arrow;
      metallum_class->draw_diamond = _gtk_theme_draw_diamond;
      metallum_class->draw_string = metallum_draw_string;
      metallum_class->draw_box = metallum_draw_box;
      metallum_class->draw_flat_box = metallum_draw_flat_box;
      metallum_class->draw_check = metallum_draw_check;
      metallum_class->draw_option = metallum_draw_option;
      metallum_class->draw_tab = metallum_draw_tab;
      metallum_class->draw_shadow_gap = metallum_draw_shadow_gap;
      metallum_class->draw_box_gap = metallum_draw_box_gap;
      metallum_class->draw_extension = metallum_draw_extension;
      metallum_class->draw_focus = metallum_draw_focus;
      metallum_class->draw_slider = metallum_draw_slider;
      metallum_class->draw_handle = metallum_draw_handle;
    }

  style->klass = metallum_class;
  metallum_set_props (style);
}

static void
metallum_duplicate_style (GtkStyle *dest,
                          GtkStyle *src)
{
  dest->klass = src->klass;
  metallum_set_props (dest);
}

void
gtk_theme_metallum_init (GtkThemeEngine *engine)
{
  engine->parse_rc_style = metallum_parse_rc_style;
  engine->merge_rc_style = metallum_merge_rc_style;
  engine->rc_style_to_style = metallum_rc_style_to_style;
  engine->duplicate_style = metallum_duplicate_style;
}
