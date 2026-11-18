/* GTK - The GIMP Toolkit
 * Gelidus, a theme engine with thin edges
 *
 * Ported from the ThinIce engine of gtk-engines 2.20.2, which is based
 * on raster's Motif theme and on the Metal theme.
 *
 * ThinIce authors: Tim Gerla <timg@rrv.net>
 *                  Tomas Ogren <stric@ing.umu.se>
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

/* ThinIce drew with cairo; here the same pixels are drawn with GDK.
 * Its radio button, a circle antialiased in two halves, is drawn with
 * GDK's antialiasing, as Altus's is. Where GTK+ 1 widgets draw
 * differently from GTK+ 2 ones -- a scrollbar's slider is a box, its
 * steppers are arrows with a shadow, an option menu has a tab of its
 * own, a paned widget's handle is a box -- the GTK+ 2 look is
 * reproduced from what GTK+ 1 asks for.
 *
 * The engine takes ThinIce's options:
 *
 *   rect_scrollbar = TRUE | FALSE          sliders square, or cut off
 *   mark_type1 = NOTHING | SLASH | INVSLASH | DOT | INVDOT
 *                                          marks on sliders and handles
 *   mark_type2 = NOTHING | SLASH | INVSLASH | DOT | INVDOT | ARROW
 *                                          marks on scrollbar steppers
 *   paned_dots = NONE | SOME | FULL        dots along a paned handle
 *
 * and ThinIce's older scrollbar_marks, scroll_button_marks and
 * handlebox_marks. Every colour comes from the style.
 */

#include <math.h>
#include <string.h>

#include "gtkcheckmenuitem.h"
#include "gtkrc.h"
#include "gtkstyle.h"
#include "gtkthemes.h"
#include "gtkthemebuiltin.h"
#include "gtkthemecommon.h"
#include "gtkmenuitem.h"
#include "gtkpaned.h"
#include "gtkrange.h"
#include "gtkvscrollbar.h"
#include "gtkwidget.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DETAIL(xx) ((detail) && (!strcmp ((xx), (detail))))


/* A slider no bigger than this each way is square and unmarked */
#define SMALLEST_HANDLE 17

enum
{
  MARKS_NOTHING,
  MARKS_SLASH,
  MARKS_INVSLASH,
  MARKS_DOT,
  MARKS_INVDOT,
  MARKS_ARROW
};

enum
{
  PANED_DOTSFULL,
  PANED_DOTSSOME,
  PANED_DOTSNONE
};

enum
{
  TOKEN_RECTSCROLLBAR = G_TOKEN_LAST + 1,
  TOKEN_SCROLLBARMARKS,
  TOKEN_SCROLLBUTTONMARKS,
  TOKEN_HANDLEBOXMARKS,
  TOKEN_MARKTYPE1,
  TOKEN_MARKTYPE2,
  TOKEN_PANEDDOTS,

  TOKEN_TRUE,
  TOKEN_FALSE,

  TOKEN_NOTHING,
  TOKEN_SLASH,
  TOKEN_INVSLASH,
  TOKEN_DOT,
  TOKEN_INVDOT,
  TOKEN_ARROW,

  TOKEN_FULL,
  TOKEN_SOME,
  TOKEN_NONE
};

static const GtkThemeSymbol gelidus_symbols[] = {
  { "rect_scrollbar",      TOKEN_RECTSCROLLBAR },
  { "scrollbar_marks",     TOKEN_SCROLLBARMARKS },
  { "scroll_button_marks", TOKEN_SCROLLBUTTONMARKS },
  { "handlebox_marks",     TOKEN_HANDLEBOXMARKS },
  { "mark_type1",          TOKEN_MARKTYPE1 },
  { "mark_type2",          TOKEN_MARKTYPE2 },
  { "paned_dots",          TOKEN_PANEDDOTS },

  { "TRUE",                TOKEN_TRUE },
  { "FALSE",               TOKEN_FALSE },

  { "NOTHING",             TOKEN_NOTHING },
  { "SLASH",               TOKEN_SLASH },
  { "INVSLASH",            TOKEN_INVSLASH },
  { "DOT",                 TOKEN_DOT },
  { "INVDOT",              TOKEN_INVDOT },
  { "ARROW",               TOKEN_ARROW },

  { "FULL",                TOKEN_FULL },
  { "SOME",                TOKEN_SOME },
  { "NONE",                TOKEN_NONE }
};

#define GELIDUS_RECT_SCROLLBAR_SET (1 << 0)
#define GELIDUS_MARK_TYPE1_SET     (1 << 1)
#define GELIDUS_MARK_TYPE2_SET     (1 << 2)
#define GELIDUS_PANED_DOTS_SET     (1 << 3)

typedef struct
{
  guint flags;
  gboolean rect_scrollbar;
  gint mark_type1;
  gint mark_type2;
  gint paned_dots;
} GelidusRcData;

static GtkStyleClass *gelidus_class = NULL;

/* ThinIce's defaults, for what a style does not say */
static void
gelidus_options (GtkStyle *style, GelidusRcData *options)
{
  GelidusRcData *data = style->rc_style ? style->rc_style->engine_data : NULL;

  options->flags = 0;
  options->rect_scrollbar = FALSE;
  options->mark_type1 = MARKS_SLASH;
  options->mark_type2 = MARKS_INVSLASH;
  options->paned_dots = PANED_DOTSFULL;

  if (!data)
    return;
  if (data->flags & GELIDUS_RECT_SCROLLBAR_SET)
    options->rect_scrollbar = data->rect_scrollbar;
  if (data->flags & GELIDUS_MARK_TYPE1_SET)
    options->mark_type1 = data->mark_type1;
  if (data->flags & GELIDUS_MARK_TYPE2_SET)
    options->mark_type2 = data->mark_type2;
  if (data->flags & GELIDUS_PANED_DOTS_SET)
    options->paned_dots = data->paned_dots;
}

/* ---------------------------------------------------------------------
 * Drawing helpers: a GC, clipped to the area being drawn
 */


static void
gelidus_polygon (GdkWindow *window, GdkGC *gc, GdkRectangle *area,
                 gboolean filled, GdkPoint *points, gint npoints)
{
  _gtk_theme_clip (gc, area);
  gdk_draw_polygon (window, gc, filled, points, npoints);
  _gtk_theme_unclip (gc, area);
}

/* A one pixel border around x, y, width, height: top and left in tl,
 * bottom and right in br, br drawn last so that it has the corners it
 * shares with tl. */


static GtkShadowType
gelidus_shadow_type (const gchar *detail, GtkShadowType requested)
{
  GtkShadowType retval = GTK_SHADOW_NONE;

  if (requested != GTK_SHADOW_NONE)
    retval = GTK_SHADOW_ETCHED_IN;

  if (DETAIL ("dockitem") || DETAIL ("handlebox_bin") ||
      DETAIL ("spinbutton_up") || DETAIL ("spinbutton_down"))
    retval = GTK_SHADOW_NONE;
  else if (DETAIL ("button") || DETAIL ("togglebutton") ||
           DETAIL ("notebook") || DETAIL ("optionmenu"))
    retval = requested;
  else if (DETAIL ("menu"))
    retval = GTK_SHADOW_ETCHED_IN;

  return retval;
}

/* Two lines, dark then light below it (or to the right) */
static void
gelidus_separator (GdkWindow *window, GdkRectangle *area,
                   GdkGC *dark, GdkGC *light, gboolean horizontal,
                   gint x, gint y, gint length)
{
  if (length <= 0)
    return;

  if (horizontal)
    {
      _gtk_theme_line (window, dark, area, x, y, x + length - 1, y);
      _gtk_theme_line (window, light, area, x, y + 1, x + length - 1, y + 1);
    }
  else
    {
      _gtk_theme_line (window, dark, area, x, y, x, y + length - 1);
      _gtk_theme_line (window, light, area, x + 1, y, x + 1, y + length - 1);
    }
}


/* A diagonal mark, / across the middle of a stepper */
static void
gelidus_slash_one (GdkWindow *window, GdkRectangle *area,
                   GdkGC *gc1, GdkGC *gc2,
                   gint x, gint y, gint width, gint height)
{
  gint cx = x + width / 2;
  gint cy = y + height / 2;
  gint thick = (MIN (width, height) >> 1) - 1.5;

  if (thick <= 0)
    return;

  _gtk_theme_line (window, gc2, area, cx - thick, cy + thick, cx + thick, cy - thick);
  _gtk_theme_line (window, gc1, area, cx - thick, cy + thick - 1,
                   cx + thick - 1, cy - thick);
}

/* Two diagonal marks, // across the middle of a slider or handle,
 * each a dark line with a light one beside it */
static void
gelidus_slash_two (GdkWindow *window, GdkRectangle *area,
                   GdkGC *gc1, GdkGC *gc2,
                   gint x, gint y, gint width, gint height)
{
  gint cx = x + width / 2;
  gint cy = y + height / 2;
  gint thick = ((MIN (width, height) - 1) >> 1) - 1.5;
  gint ax1 = 0, ay1 = 0, ax2 = 0, ay2 = 0;
  gint pass;

  if (thick <= 0)
    return;

  if (width > height)
    ax2 = 1;
  else
    ay2 = 1;

  for (pass = 0; pass < 2; pass++)
    {
      gint off = pass ? 2 : -2;

      if (width > height)
        ax1 = off;
      else
        ay1 = off;

      _gtk_theme_line (window, gc2, area,
                       cx - thick + ax1, cy + thick + ay1,
                       cx + thick + ax1, cy - thick + ay1);
      _gtk_theme_line (window, gc1, area,
                       cx - thick + ax1 + ax2, cy + thick + ay1 + ay2,
                       cx + thick + ax1 + ax2, cy - thick + ay1 + ay2);
    }
}

/* A solid triangle with its corners on the pixels given, as solid as
 * GTK+ 1's ThinIce's: its outline, lines of whole pixels, and without
 * antialiasing everything between, taken through the middles of the
 * corner pixels. */
static void
gelidus_solid_triangle (GtkStyle *style, GdkWindow *window,
                        GdkRectangle *area, const GdkPoint *points,
                        const GdkColor *color)
{
  GdkPointF corners[3];
  GdkGC *gc;
  gint i;

  for (i = 0; i < 3; i++)
    {
      corners[i].x = points[i].x + 0.5;
      corners[i].y = points[i].y + 0.5;
    }

  gc = _gtk_theme_aa_gc (style, window, area, color);
  gdk_gc_set_antialias (gc, FALSE);
  gdk_draw_polygon_f (window, gc, TRUE, corners, 3);
  gdk_draw_polygon (window, gc, FALSE, (GdkPoint *) points, 3);
  gdk_gc_unref (gc);
}

/* GTK+ 1's ThinIce drew every arrow but a scrollbar's the same way,
 * matched here from how it looked: the corners of a triangle in the
 * space it is given, then, with the shadow out, an outline, each edge a
 * dark line with a light one a pixel to its right if it is steep or
 * below it if not; with the shadow in, a solid triangle in the dark
 * colour. */
static void
gelidus_arrow_corners (GtkArrowType arrow_type, gint x, gint y,
                       gint width, gint height, GdkPoint *points)
{
  switch (arrow_type)
    {
    case GTK_ARROW_UP:
      points[0].x = x + (width - 2) / 2; points[0].y = y;
      points[1].x = x;                   points[1].y = y + height - 2;
      points[2].x = x + width - 3;       points[2].y = y + height - 2;
      break;
    case GTK_ARROW_DOWN:
      points[0].x = x;                   points[0].y = y + 1;
      points[1].x = x + width - 3;       points[1].y = y + 1;
      points[2].x = x + (width - 2) / 2; points[2].y = y + height - 1;
      break;
    case GTK_ARROW_LEFT:
      points[0].x = x + width - 2; points[0].y = y;
      points[1].x = x;             points[1].y = y + (height - 2) / 2;
      points[2].x = x + width - 2; points[2].y = y + height - 3;
      break;
    case GTK_ARROW_RIGHT:
    default:
      points[0].x = x;             points[0].y = y;
      points[1].x = x + width - 1; points[1].y = y + (height - 2) / 2;
      points[2].x = x;             points[2].y = y + height - 2;
      break;
    }
}

static void
gelidus_arrow (GtkStyle *style, GdkWindow *window, GdkRectangle *area,
               GtkStateType state_type, GtkShadowType shadow_type,
               GtkArrowType arrow_type,
               gint x, gint y, gint width, gint height)
{
  GdkPoint points[3];
  gint pass, i;

  if (width < 4 || height < 4)
    return;

  gelidus_arrow_corners (arrow_type, x, y, width, height, points);

  if (shadow_type == GTK_SHADOW_IN || shadow_type == GTK_SHADOW_ETCHED_IN)
    {
      gelidus_solid_triangle (style, window, area, points,
                              &style->dark[state_type]);
      return;
    }

  /* the light lines, then the dark over them */
  for (pass = 0; pass < 2; pass++)
    for (i = 0; i < 3; i++)
      {
        GdkPoint p = points[i];
        GdkPoint q = points[(i + 1) % 3];
        const GdkPoint *r = &points[(i + 2) % 3];
        gint dx = 0, dy = 0;

        if (pass == 0)
          {
            if (ABS (q.y - p.y) > ABS (q.x - p.x))
              dx = 1;
            else
              dy = 1;

            /* A light line a row below a level edge, with the arrow
             * below too, is inside the arrow: its first pixel would
             * stand out to the left of the slanting edge there. */
            if (dy && p.y == q.y && r->y > p.y)
              {
                if (p.x < q.x)
                  p.x++;
                else
                  q.x++;
              }
          }

        _gtk_theme_line (window,
                         pass ? style->dark_gc[state_type] :
                         style->light_gc[state_type],
                         area, p.x + dx, p.y + dy, q.x + dx, q.y + dy);
      }
}

/* ---------------------------------------------------------------------
 * Drawing
 */

static void
gelidus_draw_hline (GtkStyle     *style,
                    GdkWindow    *window,
                    GtkStateType  state_type,
                    GdkRectangle *area,
                    GtkWidget    *widget,
                    gchar        *detail,
                    gint          x1,
                    gint          x2,
                    gint          y)
{
  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  gelidus_separator (window, area, style->dark_gc[state_type],
                     style->light_gc[state_type], TRUE, x1, y, x2 - x1);
}

static void
gelidus_draw_vline (GtkStyle     *style,
                    GdkWindow    *window,
                    GtkStateType  state_type,
                    GdkRectangle *area,
                    GtkWidget    *widget,
                    gchar        *detail,
                    gint          y1,
                    gint          y2,
                    gint          x)
{
  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  gelidus_separator (window, area, style->dark_gc[state_type],
                     style->light_gc[state_type], FALSE, x, y1, y2 - y1);
}

static void
gelidus_draw_shadow (GtkStyle      *style,
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
  GdkGC *gc1, *gc2;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  switch (shadow_type)
    {
    case GTK_SHADOW_IN:
      if (((x == 1) || (y == 1)) && (DETAIL ("entry") || DETAIL ("text")))
        {
          gc1 = gc2 = style->base_gc[state_type];
          break;
        }
      /* fall through */
    case GTK_SHADOW_ETCHED_IN:
      gc1 = style->light_gc[state_type];
      gc2 = style->dark_gc[state_type];
      break;
    case GTK_SHADOW_OUT:
    case GTK_SHADOW_ETCHED_OUT:
      gc1 = style->dark_gc[state_type];
      gc2 = style->light_gc[state_type];
      break;
    case GTK_SHADOW_NONE:
    default:
      return;
    }

  switch (shadow_type)
    {
    case GTK_SHADOW_ETCHED_IN:
    case GTK_SHADOW_ETCHED_OUT:
      _gtk_theme_rectangle (window, gc1, area, FALSE,
                            x + 1, y + 1, width - 3, height - 3);
      _gtk_theme_rectangle (window, gc2, area, FALSE,
                            x, y, width - 3, height - 3);
      break;
    default:
      _gtk_theme_simple_border (window, area, gc2, gc1, x, y, width, height);
      break;
    }
}


static void
gelidus_draw_shadow_gap (GtkStyle        *style,
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
  GdkGC *gc1, *gc2;
  gint start, end;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  shadow_type = gelidus_shadow_type (detail, shadow_type);

  switch (shadow_type)
    {
    case GTK_SHADOW_IN:
      gc1 = style->dark_gc[state_type];
      gc2 = style->light_gc[state_type];
      break;
    case GTK_SHADOW_OUT:
      gc1 = style->light_gc[state_type];
      gc2 = style->dark_gc[state_type];
      break;
    case GTK_SHADOW_ETCHED_IN:
    case GTK_SHADOW_ETCHED_OUT:
      gc1 = style->dark_gc[state_type];
      gc2 = style->dark_gc[state_type];
      break;
    case GTK_SHADOW_NONE:
    default:
      return;
    }

  /* The gap: pixels start to end - 1 along the side, leaving the
   * corners. (ThinIce measures the left and right sides against the
   * width; that is the height here.) */
  start = MAX (1, gap_x + 1);
  if (gap_side == GTK_POS_TOP || gap_side == GTK_POS_BOTTOM)
    end = MIN (width - 1, gap_x + gap_width - 1);
  else
    end = MIN (height - 1, gap_x + gap_width - 1);

  if (gap_side == GTK_POS_TOP && end > start)
    {
      _gtk_theme_line (window, gc1, area, x, y, x + start - 1, y);
      _gtk_theme_line (window, gc1, area, x + end, y, x + width - 1, y);
    }
  else
    _gtk_theme_line (window, gc1, area, x, y, x + width - 1, y);

  if (gap_side == GTK_POS_LEFT && end > start)
    {
      _gtk_theme_line (window, gc1, area, x, y, x, y + start - 1);
      _gtk_theme_line (window, gc1, area, x, y + end, x, y + height - 1);
    }
  else
    _gtk_theme_line (window, gc1, area, x, y, x, y + height - 1);

  if (gap_side == GTK_POS_BOTTOM && end > start)
    {
      _gtk_theme_line (window, gc2, area, x, y + height - 1,
                     x + start - 1, y + height - 1);
      _gtk_theme_line (window, gc2, area, x + end, y + height - 1,
                     x + width - 1, y + height - 1);
    }
  else
    _gtk_theme_line (window, gc2, area, x, y + height - 1,
                   x + width - 1, y + height - 1);

  if (gap_side == GTK_POS_RIGHT && end > start)
    {
      _gtk_theme_line (window, gc2, area, x + width - 1, y,
                     x + width - 1, y + start - 1);
      _gtk_theme_line (window, gc2, area, x + width - 1, y + end,
                     x + width - 1, y + height - 1);
    }
  else
    _gtk_theme_line (window, gc2, area, x + width - 1, y,
                   x + width - 1, y + height - 1);
}

static void
gelidus_draw_box_gap (GtkStyle        *style,
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
  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  gtk_style_apply_default_background (style, window,
                                      widget && !GTK_WIDGET_NO_WINDOW (widget),
                                      state_type, area,
                                      x, y, width, height);

  gelidus_draw_shadow_gap (style, window, state_type, shadow_type, area,
                        widget, detail, x, y, width, height,
                        gap_side, gap_x, gap_width);
}


/* ThinIce's slider: a box with two opposite corners cut off unless it
 * is small or rect_scrollbar is set, light above and left, dark below
 * and right, and marks across the middle. */
static void
gelidus_slider (GtkStyle       *style,
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
  GelidusRcData options;
  GdkPoint points[7];
  GdkGC *light, *dark;
  gboolean rect;
  gint midlines;
  gint modx, mody;
  gint i;

  gelidus_options (style, &options);

  midlines = options.mark_type1;
  rect = options.rect_scrollbar;

  /* too small: square and unmarked */
  if ((width <= SMALLEST_HANDLE) && (height <= SMALLEST_HANDLE))
    {
      midlines = MARKS_NOTHING;
      rect = TRUE;
    }

  light = style->light_gc[state_type];
  dark = style->dark_gc[state_type];

  if (rect)
    {
      _gtk_theme_rectangle (window, style->bg_gc[state_type], area, TRUE,
                            x, y, width, height);
      gelidus_draw_shadow (style, window, state_type, shadow_type, area,
                           widget, detail, x, y, width, height);
    }
  else
    {
      gint chopoff;

      if (orientation == GTK_ORIENTATION_HORIZONTAL)
        chopoff = CLAMP (width - SMALLEST_HANDLE, 0, 6);
      else
        chopoff = CLAMP (height - SMALLEST_HANDLE, 0, 6);

      points[0].x = x;                      points[0].y = y + height - 1;
      points[1].x = x;                      points[1].y = y + chopoff;
      points[2].x = x + chopoff;            points[2].y = y;
      points[3].x = x + width - 1;          points[3].y = y;
      points[4].x = x + width - 1;          points[4].y = y + height - 1 - chopoff;
      points[5].x = x + width - 1 - chopoff; points[5].y = y + height - 1;
      points[6].x = x;                      points[6].y = y + height - 1;

      /* GTK+ 1's slider is a window of its own: the corners cut off
       * are filled as the trough under it is */
      _gtk_theme_rectangle (window, style->bg_gc[GTK_STATE_ACTIVE], area, TRUE,
                            x, y, width, height);
      gelidus_polygon (window, style->bg_gc[state_type], area, TRUE,
                       points, 6);

      for (i = 0; i < 3; i++)
        _gtk_theme_line (window, light, area, points[i].x, points[i].y,
                         points[i + 1].x, points[i + 1].y);
      for (i = 3; i < 6; i++)
        _gtk_theme_line (window, dark, area, points[i].x, points[i].y,
                         points[i + 1].x, points[i + 1].y);
    }

  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      modx = 4;
      mody = 0;
    }
  else
    {
      modx = 0;
      mody = 4;
    }

  switch (midlines)
    {
    case MARKS_NOTHING:
      break;
    case MARKS_INVSLASH:
      gelidus_slash_two (window, area, dark, light, x, y, width, height);
      break;
    case MARKS_DOT:
      _gtk_theme_dot (window, area, light, dark,
                      x + width / 2 - modx, y + height / 2 - mody);
      _gtk_theme_dot (window, area, light, dark, x + width / 2, y + height / 2);
      _gtk_theme_dot (window, area, light, dark,
                      x + width / 2 + modx, y + height / 2 + mody);
      break;
    case MARKS_INVDOT:
      _gtk_theme_dot (window, area, dark, light,
                      x + width / 2 - modx, y + height / 2 - mody);
      _gtk_theme_dot (window, area, dark, light, x + width / 2, y + height / 2);
      _gtk_theme_dot (window, area, dark, light,
                      x + width / 2 + modx, y + height / 2 + mody);
      break;
    case MARKS_SLASH:
    default:
      gelidus_slash_two (window, area, light, dark, x, y, width, height);
      break;
    }
}

static void
gelidus_draw_slider (GtkStyle       *style,
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

  gelidus_slider (style, window, state_type, shadow_type, area, widget,
                  detail, x, y, width, height, orientation);
}

/* A row (or column) of dots along a paned widget's handle */
static void
gelidus_paned_dots (GtkStyle *style, GdkWindow *window,
                    GtkStateType state_type, GdkRectangle *area,
                    gint x, gint y, gint width, gint height,
                    GtkOrientation orientation)
{
  GelidusRcData options;
  GdkGC *light = style->light_gc[state_type];
  GdkGC *dark = style->dark_gc[state_type];
  gint i, w, start_i, end_i;

  gelidus_options (style, &options);

  w = (orientation == GTK_ORIENTATION_HORIZONTAL) ? width : height;

  switch (options.paned_dots)
    {
    case PANED_DOTSSOME:
      start_i = w / 2 - 16;
      end_i = w / 2 + 16;
      break;
    case PANED_DOTSNONE:
      start_i = w;
      end_i = 0;
      break;
    case PANED_DOTSFULL:
    default:
      start_i = 5;
      end_i = w - 5;
      break;
    }

  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    for (i = x + start_i; i <= x + end_i; i += 8)
      _gtk_theme_dot (window, area, light, dark, i, y + height / 2);
  else
    for (i = y + start_i; i <= y + end_i; i += 8)
      _gtk_theme_dot (window, area, light, dark, x + width / 2, i);
}

static void
gelidus_draw_arrow (GtkStyle      *style,
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
  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  /* GTK+ 1 draws a scrollbar's stepper as an arrow with a shadow;
   * ThinIce drew a box, then its mark in the middle. */
  if (DETAIL ("hscrollbar") || DETAIL ("vscrollbar"))
    {
      GelidusRcData options;
      GdkGC *gc1, *gc2;
      gint half_width = width / 2;
      gint half_height = height / 2;

      _gtk_theme_rectangle (window, style->bg_gc[state_type], area, TRUE,
                            x, y, width, height);
      gelidus_draw_shadow (style, window, state_type, shadow_type, area,
                           widget, detail, x, y, width, height);

      gelidus_options (style, &options);

      switch (shadow_type)
        {
        case GTK_SHADOW_IN:
          gc1 = style->bg_gc[state_type];
          gc2 = style->dark_gc[state_type];
          break;
        case GTK_SHADOW_OUT:
        case GTK_SHADOW_ETCHED_IN:
          gc1 = style->dark_gc[state_type];
          gc2 = style->light_gc[state_type];
          break;
        default:
          gc1 = gc2 = style->bg_gc[state_type];
          break;
        }

      switch (options.mark_type2)
        {
        case MARKS_NOTHING:
          return;
        case MARKS_INVSLASH:
          gelidus_slash_one (window, area, gc2, gc1, x, y, width, height);
          return;
        case MARKS_DOT:
          _gtk_theme_dot (window, area, gc2, gc1,
                          x + half_width, y + half_height);
          return;
        case MARKS_INVDOT:
          _gtk_theme_dot (window, area, gc1, gc2,
                          x + half_width, y + half_height);
          return;
        case MARKS_ARROW:
          /* an arrow, in the middle half of the stepper */
          gelidus_arrow (style, window, area, state_type, shadow_type,
                         arrow_type, x + width / 4, y + height / 4,
                         width / 2, height / 2);
          return;
        case MARKS_SLASH:
        default:
          gelidus_slash_one (window, area, gc1, gc2,
                             x, y, width - 1, height - 1);
          return;
        }
    }

  /* a submenu's arrow is in the middle of its item, however tall, a
   * row lower to be level with the label */
  if (DETAIL ("menuitem") && widget && GTK_IS_MENU_ITEM (widget))
    y = (widget->allocation.height - height) / 2 + 1;

  /* Every other arrow, a spin button's too, just the arrow, as ThinIce
   * drew it in GTK+ 1 */
  gelidus_arrow (style, window, area, state_type, shadow_type, arrow_type,
                 x, y, width, height);
}

static void
gelidus_draw_box (GtkStyle      *style,
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
  gboolean draw_border = TRUE;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  if (DETAIL ("buttondefault") ||
      (DETAIL ("bar") && height < 1 && width < 1))
    return;

  /* GTK+ 1 draws a scrollbar's slider as a box */
  if (DETAIL ("slider"))
    {
      gelidus_slider (style, window, state_type, shadow_type, area, widget,
                      detail, x, y, width, height,
                      (widget && GTK_IS_VSCROLLBAR (widget)) ?
                      GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
      return;
    }

  /* An option menu's indicator: the arrow alone, as ThinIce drew it in
   * GTK+ 1. Its point is in the middle only when the width is odd, and
   * the space GTK+ 1 gives is even, so it is a pixel narrower. */
  if (DETAIL ("optionmenutab"))
    {
      gint w = width - (width % 2 == 0);

      gelidus_arrow (style, window, area, state_type, shadow_type,
                     GTK_ARROW_DOWN, x + (width - w) / 2, y, w, height);
      return;
    }

  if (DETAIL ("bar"))
    {
      x += 1;
      y += 1;
      width -= 2;
      height -= 2;
      draw_border = FALSE;
    }
  else if (DETAIL ("handlebox_bin"))
    draw_border = FALSE;

  if (!style->bg_pixmap[state_type] ||
      gdk_window_get_type (window) == GDK_WINDOW_PIXMAP)
    _gtk_theme_rectangle (window, style->bg_gc[state_type], area, TRUE,
                          x, y, width, height);
  else
    gtk_style_apply_default_background (style, window,
                                        widget && !GTK_WIDGET_NO_WINDOW (widget),
                                        state_type, area,
                                        x, y, width, height);

  /* GTK+ 1 draws a paned widget's handle as a box: ThinIce draws dots */
  if (DETAIL ("hpaned") || DETAIL ("vpaned"))
    {
      gelidus_paned_dots (style, window, state_type, area,
                          x, y, width, height,
                          DETAIL ("hpaned") ? GTK_ORIENTATION_VERTICAL :
                          GTK_ORIENTATION_HORIZONTAL);
      return;
    }

  if (draw_border)
    gelidus_draw_shadow (style, window, state_type, shadow_type, area,
                         widget, detail, x, y, width, height);

  /* The default button has a triangle in its corner, in the accent */
  if (DETAIL ("button") && widget && GTK_WIDGET_HAS_DEFAULT (widget))
    {
      GdkPoint points[3];

      points[0].x = x + 2;  points[0].y = y + 2;
      points[1].x = x + 10; points[1].y = y + 2;
      points[2].x = x + 2;  points[2].y = y + 10;
      gelidus_polygon (window, style->bg_gc[GTK_STATE_SELECTED], area, TRUE,
                       points, 3);

      _gtk_theme_line (window, style->dark_gc[state_type], area,
                       x + 2, y + 10, x + 2, y + 2);
      _gtk_theme_line (window, style->dark_gc[state_type], area,
                       x + 2, y + 2, x + 10, y + 2);
      _gtk_theme_line (window, style->light_gc[state_type], area,
                       x + 10, y + 3, x + 3, y + 10);
    }
}

static void
gelidus_draw_check (GtkStyle      *style,
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

  /* GTK+ 1 puts a menu item's indicator a row higher than the middle
   * of its label; a row lower lines them up */
  if (widget && GTK_IS_CHECK_MENU_ITEM (widget))
    y += 1;

  /* GTK+ 1 draws an active indicator in the active state even when it
   * is insensitive; GTK+ 2, as ThinIce expects, in the insensitive one */
  if (widget && !GTK_WIDGET_IS_SENSITIVE (widget))
    state_type = GTK_STATE_INSENSITIVE;

  if (state_type == GTK_STATE_INSENSITIVE)
    {
      _gtk_theme_rectangle (window, style->dark_gc[state_type], area, FALSE,
                            x, y, width - 1, height - 1);
      return;
    }

  gelidus_draw_box (style, window, state_type, shadow_type, area, widget,
                    detail, x, y, width, height);

  /* On, it is sunk in and filled in the active colour */
  if (shadow_type == GTK_SHADOW_IN)
    _gtk_theme_rectangle (window, style->bg_gc[GTK_STATE_ACTIVE], area, TRUE,
                          x + 1, y + 1, width - 2, height - 2);
}

/* ThinIce's radio button, antialiased: a disc one pixel wider than
 * the button's middle, its upper left half in one colour and lower
 * right in another -- raised when off, sunk in when on -- then the
 * middle filled over it. */
static void
gelidus_draw_option (GtkStyle      *style,
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
  const GdkColor *color1, *color2, *color3;
  gint cx, cy, radius;
  GdkGC *gc;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  /* GTK+ 1 puts a menu item's indicator a row higher than the middle
   * of its label; a row lower lines them up */
  if (widget && GTK_IS_CHECK_MENU_ITEM (widget))
    y += 1;

  if (widget && !GTK_WIDGET_IS_SENSITIVE (widget))
    state_type = GTK_STATE_INSENSITIVE;

  if (width <= 0 || height <= 0)
    return;

  if (shadow_type == GTK_SHADOW_IN || shadow_type == GTK_SHADOW_ETCHED_IN)
    {
      color1 = &style->dark[state_type];
      color2 = &style->light[state_type];
      color3 = &style->bg[GTK_STATE_ACTIVE];
    }
  else
    {
      color1 = &style->light[state_type];
      color2 = &style->dark[state_type];
      color3 = &style->bg[state_type];
    }

  /* In ThinIce's terms: about (x + width / 2, y + height / 2), a disc
   * of radius MIN (width, height) / 2 - 1 inside a ring one pixel
   * wider. */
  cx = x + width / 2;
  cy = y + height / 2;
  radius = MIN (width, height) / 2 - 1;
  if (radius < 1)
    return;

  gc = _gtk_theme_aa_gc (style, window, area, color1);
  gdk_draw_arc (window, gc, TRUE, cx - radius - 1, cy - radius - 1,
                2 * radius + 2, 2 * radius + 2, 45 * 64, 180 * 64);
  gdk_gc_unref (gc);

  gc = _gtk_theme_aa_gc (style, window, area, color2);
  gdk_draw_arc (window, gc, TRUE, cx - radius - 1, cy - radius - 1,
                2 * radius + 2, 2 * radius + 2, 225 * 64, 180 * 64);
  gdk_gc_unref (gc);

  gc = _gtk_theme_aa_gc (style, window, area, color3);
  gdk_draw_arc (window, gc, TRUE, cx - radius, cy - radius,
                2 * radius, 2 * radius, 0, 360 * 64);
  gdk_gc_unref (gc);
}

static void
gelidus_draw_handle (GtkStyle       *style,
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
  GelidusRcData options;
  GdkGC *light, *dark;
  gint modx, mody;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  if (DETAIL ("paned"))
    {
      gtk_style_apply_default_background (style, window, FALSE,
                                          state_type, area,
                                          x, y, width, height);
      gelidus_paned_dots (style, window, state_type, area,
                          x, y, width, height, orientation);
      return;
    }

  gelidus_draw_box (style, window, state_type, shadow_type, area, widget,
                    detail, x, y, width, height);

  light = style->light_gc[state_type];
  dark = style->dark_gc[state_type];

  _gtk_theme_line (window, light, area, x + width, y, x + width, y + height - 2);

  gelidus_options (style, &options);

  if (height > width)
    {
      modx = 0;
      mody = 4;
    }
  else
    {
      modx = 4;
      mody = 0;
    }

  switch (options.mark_type1)
    {
    case MARKS_NOTHING:
      break;
    case MARKS_INVSLASH:
      gelidus_slash_two (window, area, dark, light, x, y, width, height);
      break;
    case MARKS_DOT:
      _gtk_theme_dot (window, area, light, dark,
                      x + width / 2 - modx, y + height / 2 - mody);
      _gtk_theme_dot (window, area, light, dark, x + width / 2, y + height / 2);
      _gtk_theme_dot (window, area, light, dark,
                      x + width / 2 + modx, y + height / 2 + mody);
      break;
    case MARKS_INVDOT:
      _gtk_theme_dot (window, area, dark, light,
                      x + width / 2 - modx, y + height / 2 - mody);
      _gtk_theme_dot (window, area, dark, light, x + width / 2, y + height / 2);
      _gtk_theme_dot (window, area, dark, light,
                      x + width / 2 + modx, y + height / 2 + mody);
      break;
    case MARKS_SLASH:
    default:
      gelidus_slash_two (window, area, light, dark, x, y, width, height);
      break;
    }
}

/* The focus rectangle of GTK+ 1's ThinIce: a solid line in black.
 * (The GTK+ 2 ThinIce left it to GTK+ 2, which dots it in the
 * foreground colour.) A list in add mode still dashes it, as GTK+
 * does, since that is how add mode is shown. */
static void
gelidus_draw_focus (GtkStyle     *style,
                    GdkWindow    *window,
                    GdkRectangle *area,
                    GtkWidget    *widget,
                    gchar        *detail,
                    gint          x,
                    gint          y,
                    gint          width,
                    gint          height)
{
  static gint8 dashed[2] = { 4, 4 };
  GdkGC *gc;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  /* GTK+ 1 passes the rectangle's size less one, as it is drawn */
  if (width == -1 || height == -1)
    {
      _gtk_theme_sanitize_size (window, &width, &height);
      width -= 1;
      height -= 1;
    }

  gc = style->black_gc;
  if (DETAIL ("add-mode"))
    {
      gdk_gc_set_line_attributes (gc, 1, GDK_LINE_ON_OFF_DASH, 0, 0);
      gdk_gc_set_dashes (gc, 0, dashed, 2);
    }
  _gtk_theme_rectangle (window, gc, area, FALSE, x, y, width, height);
  if (DETAIL ("add-mode"))
    gdk_gc_set_line_attributes (gc, 1, GDK_LINE_SOLID, 0, 0);
}

/* ---------------------------------------------------------------------
 * The engine
 */

/* The widget metrics ThinIce's theme sets, and GTK+ 2's defaults where
 * they differ from GTK+ 1's and ThinIce leaves them alone. */
static void
gelidus_set_props (GtkStyle *style)
{
  gtk_style_set_prop_experimental (style, "GtkButton::default_spacing", 0);
  gtk_style_set_prop_experimental (style, "GtkCheckButton::indicator_size", 10);
  gtk_style_set_prop_experimental (style, "GtkCheckMenuItem::indicator_size", 10);
  gtk_style_set_prop_experimental (style, "GtkRange::trough_border", 1);
  gtk_style_set_prop_experimental (style, "GtkRange::slider_width", 11);
  gtk_style_set_prop_experimental (style, "GtkRange::stepper_size", 11);
  gtk_style_set_prop_experimental (style, "GtkRange::stepper_spacing", 0);
  gtk_style_set_prop_experimental (style, "GtkPaned::handle_full_size", 1);
  gtk_style_set_prop_experimental (style, "GtkPaned::handle_width", 5);
}

static guint
gelidus_parse_enum (GScanner *scanner, gint *value)
{
  guint token = _gtk_theme_parse_equals (scanner);

  if (token != G_TOKEN_NONE)
    return token;
  switch ((guint) g_scanner_get_next_token (scanner))
    {
    case TOKEN_NOTHING:  *value = MARKS_NOTHING;  break;
    case TOKEN_SLASH:    *value = MARKS_SLASH;    break;
    case TOKEN_INVSLASH: *value = MARKS_INVSLASH; break;
    case TOKEN_DOT:      *value = MARKS_DOT;      break;
    case TOKEN_INVDOT:   *value = MARKS_INVDOT;   break;
    case TOKEN_ARROW:    *value = MARKS_ARROW;    break;
    case TOKEN_NONE:     *value = PANED_DOTSNONE; break;
    case TOKEN_SOME:     *value = PANED_DOTSSOME; break;
    case TOKEN_FULL:     *value = PANED_DOTSFULL; break;
    default:
      return TOKEN_NOTHING;
    }

  return G_TOKEN_NONE;
}

static guint
gelidus_parse_rc_style (GScanner   *scanner,
                        GtkRcStyle *rc_style)
{
  GelidusRcData *data;
  gboolean b = FALSE;
  guint old_scope;
  guint token;

  old_scope = _gtk_theme_scope_enter (scanner, "gelidus_theme_engine",
                                      gelidus_symbols,
                                      G_N_ELEMENTS (gelidus_symbols));

  data = g_new0 (GelidusRcData, 1);

  token = g_scanner_peek_next_token (scanner);
  while (token != G_TOKEN_RIGHT_CURLY)
    {
      g_scanner_get_next_token (scanner);

      switch (token)
        {
        case TOKEN_RECTSCROLLBAR:
          token = _gtk_theme_parse_boolean (scanner, TOKEN_TRUE, TOKEN_FALSE,
                                            &data->rect_scrollbar);
          data->flags |= GELIDUS_RECT_SCROLLBAR_SET;
          break;
        case TOKEN_SCROLLBARMARKS:
          token = _gtk_theme_parse_boolean (scanner, TOKEN_TRUE, TOKEN_FALSE,
                                            &b);
          data->mark_type1 = b ? MARKS_SLASH : MARKS_NOTHING;
          data->flags |= GELIDUS_MARK_TYPE1_SET;
          break;
        case TOKEN_SCROLLBUTTONMARKS:
          token = _gtk_theme_parse_boolean (scanner, TOKEN_TRUE, TOKEN_FALSE,
                                            &b);
          data->mark_type2 = b ? MARKS_SLASH : MARKS_NOTHING;
          data->flags |= GELIDUS_MARK_TYPE2_SET;
          break;
        case TOKEN_HANDLEBOXMARKS:
          /* read, as ThinIce read it, and put to no use, as ThinIce
           * put it */
          token = _gtk_theme_parse_boolean (scanner, TOKEN_TRUE, TOKEN_FALSE,
                                            &b);
          break;
        case TOKEN_MARKTYPE1:
          token = gelidus_parse_enum (scanner, &data->mark_type1);
          data->flags |= GELIDUS_MARK_TYPE1_SET;
          break;
        case TOKEN_MARKTYPE2:
          token = gelidus_parse_enum (scanner, &data->mark_type2);
          data->flags |= GELIDUS_MARK_TYPE2_SET;
          break;
        case TOKEN_PANEDDOTS:
          token = gelidus_parse_enum (scanner, &data->paned_dots);
          data->flags |= GELIDUS_PANED_DOTS_SET;
          break;
        default:
          token = G_TOKEN_RIGHT_CURLY;
          break;
        }

      if (token != G_TOKEN_NONE)
        {
          g_free (data);
          return token;
        }

      token = g_scanner_peek_next_token (scanner);
    }

  g_scanner_get_next_token (scanner);

  rc_style->engine_data = data;
  g_scanner_set_scope (scanner, old_scope);

  return G_TOKEN_NONE;
}

/* src is of lower priority than what dest already has */
static void
gelidus_merge_rc_style (GtkRcStyle *dest,
                        GtkRcStyle *src)
{
  GelidusRcData *src_data = src->engine_data;
  GelidusRcData *dest_data = dest->engine_data;
  guint flags;

  if (!dest_data)
    dest_data = dest->engine_data = g_new0 (GelidusRcData, 1);

  if (!src_data)
    return;

  flags = ~dest_data->flags & src_data->flags;

  if (flags & GELIDUS_RECT_SCROLLBAR_SET)
    dest_data->rect_scrollbar = src_data->rect_scrollbar;
  if (flags & GELIDUS_MARK_TYPE1_SET)
    dest_data->mark_type1 = src_data->mark_type1;
  if (flags & GELIDUS_MARK_TYPE2_SET)
    dest_data->mark_type2 = src_data->mark_type2;
  if (flags & GELIDUS_PANED_DOTS_SET)
    dest_data->paned_dots = src_data->paned_dots;

  dest_data->flags |= flags;
}

static void
gelidus_rc_style_to_style (GtkStyle   *style,
                           GtkRcStyle *rc_style)
{
  if (!gelidus_class)
    {
      /* Oval, cross, ramp and flat_box are the toolkit's own */
      gelidus_class = g_new (GtkStyleClass, 1);
      *gelidus_class = *style->klass;
      gelidus_class->xthickness = 2;
      gelidus_class->ythickness = 2;
      gelidus_class->draw_hline = gelidus_draw_hline;
      gelidus_class->draw_vline = gelidus_draw_vline;
      gelidus_class->draw_shadow = gelidus_draw_shadow;
      gelidus_class->draw_polygon = _gtk_theme_draw_polygon;
      gelidus_class->draw_arrow = gelidus_draw_arrow;
      gelidus_class->draw_diamond = _gtk_theme_draw_diamond;
      gelidus_class->draw_string = _gtk_theme_draw_string;
      gelidus_class->draw_box = gelidus_draw_box;
      gelidus_class->draw_tab = gelidus_draw_box;
      gelidus_class->draw_check = gelidus_draw_check;
      gelidus_class->draw_option = gelidus_draw_option;
      gelidus_class->draw_shadow_gap = gelidus_draw_shadow_gap;
      gelidus_class->draw_box_gap = gelidus_draw_box_gap;
      gelidus_class->draw_extension = _gtk_theme_draw_extension;
      gelidus_class->draw_focus = gelidus_draw_focus;
      gelidus_class->draw_slider = gelidus_draw_slider;
      gelidus_class->draw_handle = gelidus_draw_handle;
    }

  style->klass = gelidus_class;
  gelidus_set_props (style);
}

static void
gelidus_duplicate_style (GtkStyle *dest,
                         GtkStyle *src)
{
  dest->klass = src->klass;
  gelidus_set_props (dest);
}

void
gtk_theme_gelidus_init (GtkThemeEngine *engine)
{
  engine->parse_rc_style = gelidus_parse_rc_style;
  engine->merge_rc_style = gelidus_merge_rc_style;
  engine->rc_style_to_style = gelidus_rc_style_to_style;
  engine->duplicate_style = gelidus_duplicate_style;
  engine->destroy_rc_style = _gtk_theme_destroy_rc_style;
}
