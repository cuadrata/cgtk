/* GTK - The GIMP Toolkit
 * Unicus, a flat theme engine with soft, rounded controls
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

/* Unicus takes after the look of GNOME's Adwaita: flat surfaces,
 * borderless rounded buttons, controls filled with the accent colour
 * when on, thin pill-shaped scrollbars, sliders and progress bars, an
 * accent line under the current tab, and a ring of the accent round
 * whatever has the focus. It is drawn from scratch, with GDK's
 * antialiased shapes, so it is as fine as the display at any scale.
 * Menus are square with a border, as GTK+ 3's Adwaita had them where
 * windows could not be translucent.
 *
 * Every colour comes from the style: the accent is bg[SELECTED], a
 * button's colours are its own style's bg, and lines are made from the
 * background and foreground. The engine takes two options, for styles
 * that want more room inside their frames than one pixel:
 *
 *   engine "unicus" { xthickness = 6  ythickness = 4 }
 *
 * and, for a scale, which wants a larger slider than a scrollbar, how
 * wide its slider is:
 *
 *   engine "unicus" { slider_width = 20 }
 */

#include <math.h>
#include <string.h>

#include "gtkcheckmenuitem.h"
#include "gtkclist.h"
#include "gtkcombo.h"
#include "gtkcontainer.h"
#include "gtklistitem.h"
#include "gtkmenuitem.h"
#include "gtkrange.h"
#include "gtkrc.h"
#include "gtkscrollbar.h"
#include "gtkspinbutton.h"
#include "gtkstatusbar.h"
#include "gtkstyle.h"
#include "gtkthemes.h"
#include "gtkthemebuiltin.h"
#include "gtkthemecommon.h"
#include "gtkvscale.h"
#include "gtkvscrollbar.h"
#include "gtkwidget.h"

#define DETAIL(xx) ((detail) && (!strcmp ((xx), (detail))))

#define XTHICKNESS(style) ((style)->klass->xthickness)

/* Corner radii, in the toolkit's pixels */
#define BUTTON_RADIUS 6.0
#define CHECK_RADIUS 4.0
#define ITEM_RADIUS 5.0
#define FRAME_RADIUS 6.0

/* How much of the foreground there is in a line, against the
 * background: a faint one, and a firmer one for the edges of controls
 * that are off */
#define LINE_MIX 0.15
#define EDGE_MIX 0.30

enum
{
  TOKEN_XTHICKNESS = G_TOKEN_LAST + 1,
  TOKEN_YTHICKNESS,
  TOKEN_SLIDER_WIDTH
};

static const GtkThemeSymbol unicus_symbols[] = {
  { "xthickness",   TOKEN_XTHICKNESS },
  { "ythickness",   TOKEN_YTHICKNESS },
  { "slider_width", TOKEN_SLIDER_WIDTH }
};

#define UNICUS_XTHICKNESS_SET (1 << 0)
#define UNICUS_YTHICKNESS_SET (1 << 1)
#define UNICUS_SLIDER_WIDTH_SET (1 << 2)

typedef struct
{
  guint flags;
  gint xthickness;
  gint ythickness;
  gint slider_width;
} UnicusRcData;

static GSList *unicus_classes = NULL;

/* ---------------------------------------------------------------------
 * Colours and drawing
 */

static GdkColor
unicus_mix (const GdkColor *a, const GdkColor *b, gdouble t)
{
  GdkColor c;

  c.pixel = 0;
  c.red = a->red + (b->red - a->red) * t;
  c.green = a->green + (b->green - a->green) * t;
  c.blue = a->blue + (b->blue - a->blue) * t;

  return c;
}

/* A line of the style's, faint or firmer */
static GdkColor
unicus_line (GtkStyle *style, GtkStateType state_type, gdouble mix)
{
  return unicus_mix (&style->bg[state_type], &style->fg[state_type], mix);
}

static void
unicus_fill (GtkStyle *style, GdkWindow *window, GdkRectangle *area,
             const GdkColor *color, gint x, gint y, gint width, gint height)
{
  GdkGC *gc;

  if (width <= 0 || height <= 0)
    return;
  gc = _gtk_theme_aa_gc (style, window, area, color);
  gdk_draw_rectangle (window, gc, TRUE, x, y, width, height);
  gdk_gc_unref (gc);
}

static void
unicus_rounded (GtkStyle *style, GdkWindow *window, GdkRectangle *area,
                const GdkColor *color, gboolean filled, gint line_width,
                gint x, gint y, gint width, gint height, gdouble radius)
{
  GdkGC *gc;

  if (width <= 0 || height <= 0)
    return;
  gc = _gtk_theme_aa_gc (style, window, area, color);
  if (!filled)
    gdk_gc_set_line_attributes (gc, line_width, GDK_LINE_SOLID,
                                GDK_CAP_BUTT, GDK_JOIN_MITER);
  gdk_draw_rounded_rectangle (window, gc, filled, x, y, width, height, radius);
  gdk_gc_unref (gc);
}

static void
unicus_disc (GtkStyle *style, GdkWindow *window, GdkRectangle *area,
             const GdkColor *color, gboolean filled, gint line_width,
             gint x, gint y, gint size)
{
  GdkGC *gc;

  if (size <= 0)
    return;
  gc = _gtk_theme_aa_gc (style, window, area, color);
  if (!filled)
    gdk_gc_set_line_attributes (gc, line_width, GDK_LINE_SOLID,
                                GDK_CAP_BUTT, GDK_JOIN_MITER);
  gdk_draw_arc (window, gc, filled, x, y, size, size, 0, 360 * 64);
  gdk_gc_unref (gc);
}

/* A straight stroke from one point to another, t pixels wide */
static void
unicus_stroke (GdkWindow *window, GdkGC *gc,
               gdouble x1, gdouble y1, gdouble x2, gdouble y2, gdouble t)
{
  gdouble len = sqrt ((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
  gdouble nx, ny, ex, ey;
  GdkPointF p[4];

  if (len == 0)
    return;
  ex = (x2 - x1) / len * t / 2;
  ey = (y2 - y1) / len * t / 2;
  nx = -ey;
  ny = ex;

  p[0].x = x1 - ex + nx; p[0].y = y1 - ey + ny;
  p[1].x = x2 + ex + nx; p[1].y = y2 + ey + ny;
  p[2].x = x2 + ex - nx; p[2].y = y2 + ey - ny;
  p[3].x = x1 - ex - nx; p[3].y = y1 - ey - ny;
  gdk_draw_polygon_f (window, gc, TRUE, p, 4);
}

/* A chevron pointing in arrow_type's direction, in the middle of the
 * space it is given */
static void
unicus_chevron (GtkStyle *style, GdkWindow *window, GdkRectangle *area,
                const GdkColor *color, GtkArrowType arrow_type,
                gint x, gint y, gint width, gint height)
{
  gdouble size = MIN (width, height);
  gdouble cx = x + width / 2.0, cy = y + height / 2.0;
  gdouble a = size * 0.30, b = size * 0.15, t = MAX (1.4, size * 0.13);
  GdkGC *gc = _gtk_theme_aa_gc (style, window, area, color);

  switch (arrow_type)
    {
    case GTK_ARROW_UP:
      unicus_stroke (window, gc, cx - a, cy + b, cx, cy - b, t);
      unicus_stroke (window, gc, cx, cy - b, cx + a, cy + b, t);
      break;
    case GTK_ARROW_DOWN:
      unicus_stroke (window, gc, cx - a, cy - b, cx, cy + b, t);
      unicus_stroke (window, gc, cx, cy + b, cx + a, cy - b, t);
      break;
    case GTK_ARROW_LEFT:
      unicus_stroke (window, gc, cx + b, cy - a, cx - b, cy, t);
      unicus_stroke (window, gc, cx - b, cy, cx + b, cy + a, t);
      break;
    case GTK_ARROW_RIGHT:
    default:
      unicus_stroke (window, gc, cx - b, cy - a, cx + b, cy, t);
      unicus_stroke (window, gc, cx + b, cy, cx - b, cy + a, t);
      break;
    }

  gdk_gc_unref (gc);
}


/* ---------------------------------------------------------------------
 * Drawing
 */

static void
unicus_draw_hline (GtkStyle     *style,
                   GdkWindow    *window,
                   GtkStateType  state_type,
                   GdkRectangle *area,
                   GtkWidget    *widget,
                   gchar        *detail,
                   gint          x1,
                   gint          x2,
                   gint          y)
{
  GdkColor line;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  /* GTK+ 1 draws a menu separator along the top of the item: across
   * the middle here, clear of the item's sides */
  if (DETAIL ("menuitem") && widget && GTK_IS_MENU_ITEM (widget) &&
      !GTK_BIN (widget)->child)
    {
      x1 += XTHICKNESS (style) + 4;
      x2 -= XTHICKNESS (style) + 5;
      y = (widget->allocation.height - 1) / 2;
    }

  line = unicus_line (style, state_type, LINE_MIX);
  unicus_fill (style, window, area, &line, x1, y, x2 - x1 + 1, 1);
}

static void
unicus_draw_vline (GtkStyle     *style,
                   GdkWindow    *window,
                   GtkStateType  state_type,
                   GdkRectangle *area,
                   GtkWidget    *widget,
                   gchar        *detail,
                   gint          y1,
                   gint          y2,
                   gint          x)
{
  GdkColor line;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  line = unicus_line (style, state_type, LINE_MIX);
  unicus_fill (style, window, area, &line, x, y1, 1, y2 - y1 + 1);
}

/* A combo's entry or button */
static gboolean
unicus_in_combo (GtkWidget *widget)
{
  return widget && widget->parent && GTK_IS_COMBO (widget->parent);
}

/* An entry that runs on into buttons at its right: a spin button's, or
 * a combo's */
static gboolean
unicus_joined (GtkWidget *widget)
{
  return widget && (GTK_IS_SPIN_BUTTON (widget) || unicus_in_combo (widget));
}

/* An entry's field, borderless and rounded; a spin button's or a
 * combo's is square on the right, where its buttons are */
static void
unicus_field (GtkStyle *style, GdkWindow *window, GdkRectangle *area,
              GtkWidget *widget, GtkStateType state_type,
              gint x, gint y, gint width, gint height)
{
  GdkRectangle rect, clip;
  gint extra = 0;

  rect.x = x;
  rect.y = y;
  rect.width = width;
  rect.height = height;
  if (area && !gdk_rectangle_intersect (&rect, area, &clip))
    return;
  if (!area)
    clip = rect;

  if (unicus_joined (widget))
    extra = BUTTON_RADIUS;

  unicus_fill (style, window, &clip, &style->bg[state_type],
               x, y, width, height);
  unicus_rounded (style, window, &clip, &style->base[state_type], TRUE, 0,
                  x, y, width + extra, height, BUTTON_RADIUS);
}

static void
unicus_draw_shadow (GtkStyle      *style,
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
  GdkColor line;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  if (shadow_type == GTK_SHADOW_NONE)
    return;

  if (DETAIL ("entry"))
    {
      unicus_field (style, window, area, widget, state_type,
                    x, y, width, height);
      return;
    }

  line = unicus_line (style, GTK_STATE_NORMAL, LINE_MIX);

  /* A status bar's frame: a line along its top */
  if (DETAIL ("frame") && widget && widget->parent &&
      GTK_IS_STATUSBAR (widget->parent))
    {
      unicus_fill (style, window, area, &line, x, y, width, 1);
      return;
    }

  /* A frame is rounded, like a card; a frame round other widgets --
   * a list, a scrolled window -- is square, since they fill it to its
   * edges */
  if (DETAIL ("frame"))
    unicus_rounded (style, window, area, &line, FALSE, 1,
                    x, y, width, height, FRAME_RADIUS);
  else
    {
      GdkGC *gc = _gtk_theme_aa_gc (style, window, area, &line);

      gdk_draw_rectangle (window, gc, FALSE, x, y, width - 1, height - 1);
      gdk_gc_unref (gc);
    }
}

/* A button: borderless and rounded, in its own style's colours; the
 * default one in the accent. A toggle button that is down is a shade
 * deeper than a pressed one, so that it is not taken for one under the
 * pointer. */
static void
unicus_button (GtkStyle *style, GdkWindow *window, GdkRectangle *area,
               GtkWidget *widget, GtkStateType state_type, gboolean down,
               gint x, gint y, gint width, gint height)
{
  const GdkColor *fill = &style->bg[state_type];
  GdkColor accent, deeper;

  if (down && state_type != GTK_STATE_INSENSITIVE)
    {
      deeper = unicus_mix (&style->bg[GTK_STATE_ACTIVE],
                           &style->fg[GTK_STATE_NORMAL], 0.1);
      fill = &deeper;
    }

  if (widget && GTK_WIDGET_HAS_DEFAULT (widget) &&
      state_type != GTK_STATE_INSENSITIVE)
    {
      accent = style->bg[GTK_STATE_SELECTED];
      if (state_type == GTK_STATE_PRELIGHT)
        accent = unicus_mix (&accent, &style->fg[GTK_STATE_SELECTED], 0.1);
      else if (state_type == GTK_STATE_ACTIVE)
        accent = unicus_mix (&accent, &style->fg[GTK_STATE_NORMAL], 0.15);
      fill = &accent;
    }

  unicus_rounded (style, window, area, fill, TRUE, 0,
                  x, y, width, height, BUTTON_RADIUS);
}

static void
unicus_draw_box (GtkStyle      *style,
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
  GdkColor line;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);
  state_type = _gtk_theme_toggle_state (detail, state_type, shadow_type);

  if (DETAIL ("button") && widget && widget->parent &&
      GTK_IS_CLIST (widget->parent))
    {
      /* a list's column title: on the list, a line below it and one
       * between it and the next */
      GtkCList *clist = GTK_CLIST (widget->parent);

      line = unicus_line (style, GTK_STATE_NORMAL, LINE_MIX);
      unicus_fill (style, window, area, &style->base[GTK_STATE_NORMAL],
                   x, y, width, height);
      unicus_fill (style, window, area, &line, x, y + height - 1, width, 1);
      if (widget->allocation.x + widget->allocation.width <
          clist->column_title_area.width)
        unicus_fill (style, window, area, &line,
                     x + width - 1, y + 4, 1, height - 8);
    }
  else if (DETAIL ("button") && unicus_in_combo (widget))
    {
      /* a combo's button: the end of its entry's field, square on the
       * left where the field runs on, and a shade darker under the
       * pointer or pressed */
      GtkStyle *field = GTK_COMBO (widget->parent)->entry->style;
      GdkColor fill = field->base[GTK_STATE_NORMAL];
      GdkRectangle rect, clip;

      if (state_type == GTK_STATE_PRELIGHT)
        fill = unicus_mix (&fill, &field->text[GTK_STATE_NORMAL], 0.06);
      else if (state_type == GTK_STATE_ACTIVE)
        fill = unicus_mix (&fill, &field->text[GTK_STATE_NORMAL], 0.12);

      rect.x = x;
      rect.y = y;
      rect.width = width;
      rect.height = height;
      if (area && !gdk_rectangle_intersect (&rect, area, &clip))
        return;
      if (!area)
        clip = rect;
      unicus_fill (style, window, &clip,
                   &widget->parent->style->bg[GTK_STATE_NORMAL],
                   x, y, width, height);
      unicus_rounded (style, window, &clip, &fill, TRUE, 0,
                      x - BUTTON_RADIUS, y, width + BUTTON_RADIUS, height,
                      BUTTON_RADIUS);
    }
  else if (DETAIL ("button") || DETAIL ("togglebutton") ||
           DETAIL ("optionmenu"))
    {
      /* what lies round the rounded corners: the background of the
       * widget the button is in */
      GtkStyle *outer = (widget && widget->parent) ? widget->parent->style : style;

      unicus_fill (style, window, area, &outer->bg[GTK_STATE_NORMAL],
                   x, y, width, height);
      unicus_button (style, window, area, widget, state_type,
                     DETAIL ("togglebutton") && shadow_type == GTK_SHADOW_IN,
                     x, y, width, height);
    }
  else if (DETAIL ("buttondefault") || DETAIL ("togglebuttondefault") ||
           DETAIL ("handlebox_bin"))
    {
      if (DETAIL ("handlebox_bin"))
        unicus_fill (style, window, area, &style->bg[state_type],
                     x, y, width, height);
    }
  else if (DETAIL ("menu"))
    {
      /* square, with a border: a menu is a window of its own */
      GdkGC *gc;

      line = unicus_line (style, GTK_STATE_NORMAL, EDGE_MIX);
      unicus_fill (style, window, area, &style->bg[GTK_STATE_NORMAL],
                   x, y, width, height);
      gc = _gtk_theme_aa_gc (style, window, area, &line);
      gdk_draw_rectangle (window, gc, FALSE, x, y, width - 1, height - 1);
      gdk_gc_unref (gc);
    }
  else if (DETAIL ("menuitem"))
    {
      /* a highlighted item: a rounded patch, clear of the menu's sides */
      unicus_rounded (style, window, area, &style->bg[state_type], TRUE, 0,
                      x + 3, y, width - 6, height, ITEM_RADIUS);
    }
  else if (DETAIL ("menubar"))
    {
      line = unicus_line (style, GTK_STATE_NORMAL, LINE_MIX);
      unicus_fill (style, window, area, &style->bg[GTK_STATE_NORMAL],
                   x, y, width, height);
      unicus_fill (style, window, area, &line, x, y + height - 1, width, 1);
    }
  else if (DETAIL ("trough"))
    {
      GdkColor trough = unicus_line (style, GTK_STATE_NORMAL, 0.12);
      gboolean vertical = widget && (GTK_IS_VSCROLLBAR (widget) ||
                                     GTK_IS_VSCALE (widget));

      unicus_fill (style, window, area, &style->bg[GTK_STATE_NORMAL],
                   x, y, width, height);

      if (widget && GTK_IS_SCROLLBAR (widget))
        return;

      /* a scale's trough is a thin pill across its middle; a progress
       * bar's is the whole of it */
      if (widget && GTK_IS_RANGE (widget))
        {
          if (vertical)
            unicus_rounded (style, window, area, &trough, TRUE, 0,
                            x + width / 2 - 2, y + 2, 4, height - 4, 2);
          else
            unicus_rounded (style, window, area, &trough, TRUE, 0,
                            x + 2, y + height / 2 - 2, width - 4, 4, 2);
        }
      else
        unicus_rounded (style, window, area, &trough, TRUE, 0,
                        x, y, width, height, MIN (width, height) / 2.0);
    }
  else if (DETAIL ("bar"))
    {
      unicus_rounded (style, window, area, &style->bg[GTK_STATE_SELECTED],
                      TRUE, 0, x, y, width, height,
                      MIN (width, height) / 2.0);
    }
  else if (DETAIL ("slider") && widget && GTK_IS_SCROLLBAR (widget))
    {
      /* a scrollbar's slider: a pill down its middle, darker under the
       * pointer */
      GdkColor pill = unicus_line (style, GTK_STATE_NORMAL,
                                   state_type == GTK_STATE_PRELIGHT ? 0.55 : 0.38);
      gint w = MIN (width, height) - 4;

      unicus_fill (style, window, area, &style->bg[GTK_STATE_NORMAL],
                   x, y, width, height);
      if (GTK_IS_VSCROLLBAR (widget))
        unicus_rounded (style, window, area, &pill, TRUE, 0,
                        x + (width - w) / 2, y + 1, w, height - 2, w / 2.0);
      else
        unicus_rounded (style, window, area, &pill, TRUE, 0,
                        x + 1, y + (height - w) / 2, width - 2, w, w / 2.0);
    }
  else if (DETAIL ("spinbutton"))
    {
      /* the spin button's buttons: part of its field, square on the
       * left, where the field runs on */
      GdkRectangle rect, clip;

      rect.x = x;
      rect.y = y;
      rect.width = width;
      rect.height = height;
      if (area && !gdk_rectangle_intersect (&rect, area, &clip))
        return;
      if (!area)
        clip = rect;
      unicus_fill (style, window, &clip, &style->bg[GTK_STATE_NORMAL],
                   x, y, width, height);
      unicus_rounded (style, window, &clip, &style->base[GTK_STATE_NORMAL],
                      TRUE, 0, x - BUTTON_RADIUS, y, width + BUTTON_RADIUS,
                      height, BUTTON_RADIUS);
    }
  else if (DETAIL ("hpaned") || DETAIL ("vpaned"))
    {
      GdkColor dot = unicus_line (style, state_type, EDGE_MIX);
      gint i;

      unicus_fill (style, window, area, &style->bg[state_type],
                   x, y, width, height);
      for (i = -1; i <= 1; i++)
        {
          if (DETAIL ("hpaned"))
            unicus_disc (style, window, area, &dot, TRUE, 0,
                         x + width / 2 - 1, y + height / 2 + i * 5 - 1, 3);
          else
            unicus_disc (style, window, area, &dot, TRUE, 0,
                         x + width / 2 + i * 5 - 1, y + height / 2 - 1, 3);
        }
    }
  else
    {
      if (shadow_type == GTK_SHADOW_NONE)
        unicus_fill (style, window, area, &style->bg[state_type],
                     x, y, width, height);
      else
        {
          GdkGC *gc;

          line = unicus_line (style, GTK_STATE_NORMAL, LINE_MIX);
          unicus_fill (style, window, area, &style->bg[state_type],
                       x, y, width, height);
          gc = _gtk_theme_aa_gc (style, window, area, &line);
          gdk_draw_rectangle (window, gc, FALSE, x, y, width - 1, height - 1);
          gdk_gc_unref (gc);
        }
    }
}

/* An option menu's indicator: a chevron */
static void
unicus_draw_tab (GtkStyle      *style,
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
  unicus_chevron (style, window, area, &style->fg[state_type],
                  GTK_ARROW_DOWN, x, y, width, height);
}

static void
unicus_draw_arrow (GtkStyle      *style,
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
  GdkColor color;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  /* A scrollbar's steppers are flat, a faint chevron on the background;
   * a spin button's are its "-" and "+" */
  if (DETAIL ("hscrollbar") || DETAIL ("vscrollbar"))
    {
      unicus_fill (style, window, area, &style->bg[GTK_STATE_NORMAL],
                   x, y, width, height);
      color = unicus_line (style, state_type,
                           state_type == GTK_STATE_PRELIGHT ? 0.8 : 0.5);
      unicus_chevron (style, window, area, &color, arrow_type,
                      x + 1, y + 1, width - 2, height - 2);
      return;
    }

  if (DETAIL ("spinbutton"))
    {
      GdkRectangle half, clip;
      gint pw, ph;
      GdkGC *gc;
      gdouble cx, cy, a;

      /* GTK+ 1 may leave the panel bare: each button draws its half of
       * it as part of the field, rounded on the right */
      gdk_window_get_size (window, &pw, &ph);
      half.x = 0;
      half.y = arrow_type == GTK_ARROW_UP ? 0 : ph / 2;
      half.width = pw;
      half.height = arrow_type == GTK_ARROW_UP ? ph / 2 : ph - ph / 2;
      clip = half;
      if (!area || gdk_rectangle_intersect (&half, area, &clip))
        {
          unicus_fill (style, window, &clip, &style->bg[GTK_STATE_NORMAL],
                       0, 0, pw, ph);
          unicus_rounded (style, window, &clip, &style->base[GTK_STATE_NORMAL],
                          TRUE, 0, -BUTTON_RADIUS, 0, pw + BUTTON_RADIUS, ph,
                          BUTTON_RADIUS);
        }

      cx = half.x + half.width / 2.0 - 1;
      cy = half.y + half.height / 2.0 + (arrow_type == GTK_ARROW_UP ? 1 : -1);
      a = MIN (half.width, half.height) * 0.28;

      color = state_type == GTK_STATE_INSENSITIVE ?
        style->fg[GTK_STATE_INSENSITIVE] : style->text[GTK_STATE_NORMAL];
      gc = _gtk_theme_aa_gc (style, window, area, &color);
      unicus_stroke (window, gc, cx - a, cy, cx + a, cy, 1.5);
      if (arrow_type == GTK_ARROW_UP)
        unicus_stroke (window, gc, cx, cy - a, cx, cy + a, 1.5);
      gdk_gc_unref (gc);
      return;
    }

  color = style->fg[state_type];
  unicus_chevron (style, window, area, &color, arrow_type,
                  x, y, width, height);
}

/* A label on the default button is in the accent's own foreground */
static void
unicus_draw_string (GtkStyle     *style,
                    GdkWindow    *window,
                    GtkStateType  state_type,
                    GdkRectangle *area,
                    GtkWidget    *widget,
                    gchar        *detail,
                    gint          x,
                    gint          y,
                    const gchar  *string)
{
  GdkGC *gc = style->fg_gc[state_type];

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  if (widget && widget->parent && GTK_IS_BUTTON (widget->parent) &&
      GTK_WIDGET_HAS_DEFAULT (widget->parent) &&
      state_type != GTK_STATE_INSENSITIVE)
    gc = style->fg_gc[GTK_STATE_SELECTED];

  if (area)
    gdk_gc_set_clip_rectangle (gc, area);
  gdk_draw_string (window, style->font, gc, x, y, string);
  if (area)
    gdk_gc_set_clip_rectangle (gc, NULL);
}

/* Whether an indicator is drawn as insensitive */
static gboolean
unicus_insensitive (GtkWidget *widget, GtkStateType state_type)
{
  return state_type == GTK_STATE_INSENSITIVE ||
    (widget && !GTK_WIDGET_IS_SENSITIVE (widget));
}

static void
unicus_draw_check (GtkStyle      *style,
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
  gboolean insensitive = unicus_insensitive (widget, state_type);
  gboolean on = shadow_type == GTK_SHADOW_IN;
  gint size;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  /* GTK+ 1 puts a menu item's indicator a row higher than the middle
   * of its label; a row lower lines them up */
  if (widget && GTK_IS_CHECK_MENU_ITEM (widget))
    y += 1;

  size = MIN (width, height);
  x += (width - size) / 2;
  y += (height - size) / 2;

  if (on)
    {
      GdkColor accent = style->bg[GTK_STATE_SELECTED];
      GdkColor mark = style->fg[GTK_STATE_SELECTED];
      GdkGC *gc;

      if (insensitive)
        accent = unicus_mix (&style->bg[GTK_STATE_NORMAL], &accent, 0.45);
      unicus_rounded (style, window, area, &accent, TRUE, 0,
                      x, y, size, size, CHECK_RADIUS);

      gc = _gtk_theme_aa_gc (style, window, area, &mark);
      unicus_stroke (window, gc, x + size * 0.24, y + size * 0.52,
                     x + size * 0.43, y + size * 0.71, size * 0.14);
      unicus_stroke (window, gc, x + size * 0.43, y + size * 0.71,
                     x + size * 0.77, y + size * 0.30, size * 0.14);
      gdk_gc_unref (gc);
    }
  else
    {
      GdkColor edge = unicus_line (style, GTK_STATE_NORMAL,
                                   insensitive ? LINE_MIX : EDGE_MIX);

      unicus_rounded (style, window, area, &style->base[GTK_STATE_NORMAL],
                      TRUE, 0, x, y, size, size, CHECK_RADIUS);
      unicus_rounded (style, window, area, &edge, FALSE, 2,
                      x, y, size, size, CHECK_RADIUS);
    }
}

static void
unicus_draw_option (GtkStyle      *style,
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
  gboolean insensitive = unicus_insensitive (widget, state_type);
  gboolean on = shadow_type == GTK_SHADOW_IN;
  gint size, dot;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  if (widget && GTK_IS_CHECK_MENU_ITEM (widget))
    y += 1;

  size = MIN (width, height);
  x += (width - size) / 2;
  y += (height - size) / 2;

  if (on)
    {
      GdkColor accent = style->bg[GTK_STATE_SELECTED];

      if (insensitive)
        accent = unicus_mix (&style->bg[GTK_STATE_NORMAL], &accent, 0.45);
      unicus_disc (style, window, area, &accent, TRUE, 0, x, y, size);
      dot = size * 0.36;
      dot -= (size - dot) % 2;
      unicus_disc (style, window, area, &style->fg[GTK_STATE_SELECTED],
                   TRUE, 0, x + (size - dot) / 2, y + (size - dot) / 2, dot);
    }
  else
    {
      GdkColor edge = unicus_line (style, GTK_STATE_NORMAL,
                                   insensitive ? LINE_MIX : EDGE_MIX);

      unicus_disc (style, window, area, &style->base[GTK_STATE_NORMAL],
                   TRUE, 0, x, y, size);
      unicus_disc (style, window, area, &edge, FALSE, 2, x, y, size);
    }
}

/* A notebook's page: a square frame, whole round its tabs as well */
static void
unicus_draw_box_gap (GtkStyle        *style,
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
  GdkColor line;
  GdkGC *gc;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  line = unicus_line (style, GTK_STATE_NORMAL, LINE_MIX);
  unicus_fill (style, window, area, &style->bg[state_type],
               x, y, width, height);
  gc = _gtk_theme_aa_gc (style, window, area, &line);
  gdk_draw_rectangle (window, gc, FALSE, x, y, width - 1, height - 1);
  gdk_gc_unref (gc);
}

/* A frame with a gap in its top for its label: rounded, like any other
 * frame, and the gap cleared */
static void
unicus_draw_shadow_gap (GtkStyle        *style,
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
  GdkColor line;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  line = unicus_line (style, GTK_STATE_NORMAL, LINE_MIX);
  unicus_rounded (style, window, area, &line, FALSE, 1,
                  x, y, width, height, FRAME_RADIUS);

  switch (gap_side)
    {
    case GTK_POS_TOP:
      unicus_fill (style, window, area, &style->bg[state_type],
                   x + gap_x, y, gap_width, 1);
      break;
    case GTK_POS_BOTTOM:
      unicus_fill (style, window, area, &style->bg[state_type],
                   x + gap_x, y + height - 1, gap_width, 1);
      break;
    case GTK_POS_LEFT:
      unicus_fill (style, window, area, &style->bg[state_type],
                   x, y + gap_x, 1, gap_width);
      break;
    case GTK_POS_RIGHT:
      unicus_fill (style, window, area, &style->bg[state_type],
                   x + width - 1, y + gap_x, 1, gap_width);
      break;
    }
}

/* A notebook's tab: flat, the current one underlined in the accent */
static void
unicus_draw_extension (GtkStyle        *style,
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
  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  unicus_fill (style, window, area, &style->bg[GTK_STATE_NORMAL],
               x, y, width, height);

  /* the current tab is the one GTK+ 1 draws in the normal state */
  if (state_type != GTK_STATE_NORMAL)
    return;

  switch (gap_side)
    {
    case GTK_POS_TOP:
      unicus_rounded (style, window, area, &style->bg[GTK_STATE_SELECTED],
                      TRUE, 0, x + 6, y + 1, width - 12, 3, 1.5);
      break;
    case GTK_POS_BOTTOM:
      unicus_rounded (style, window, area, &style->bg[GTK_STATE_SELECTED],
                      TRUE, 0, x + 6, y + height - 4, width - 12, 3, 1.5);
      break;
    case GTK_POS_LEFT:
      unicus_rounded (style, window, area, &style->bg[GTK_STATE_SELECTED],
                      TRUE, 0, x + 1, y + 4, 3, height - 8, 1.5);
      break;
    case GTK_POS_RIGHT:
      unicus_rounded (style, window, area, &style->bg[GTK_STATE_SELECTED],
                      TRUE, 0, x + width - 4, y + 4, 3, height - 8, 1.5);
      break;
    }
}

/* The focus: a ring of the accent, round an entry or a button just
 * inside its edge; a square line round a list's focus row */
static void
unicus_draw_focus (GtkStyle     *style,
                   GdkWindow    *window,
                   GdkRectangle *area,
                   GtkWidget    *widget,
                   gchar        *detail,
                   gint          x,
                   gint          y,
                   gint          width,
                   gint          height)
{
  GdkColor ring;
  GdkGC *gc;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  /* The current tab has its underline already: no ring round it too */
  if (DETAIL ("tab"))
    return;

  /* GTK+ 1 passes the rectangle's size less one, as it is drawn */
  if (width == -1 || height == -1)
    _gtk_theme_sanitize_size (window, &width, &height);
  else
    {
      width += 1;
      height += 1;
    }

  /* A list's focus row, square like its selection: nothing when it is
   * selected, since the selection shows where it is; otherwise a line
   * round it in the accent */
  if (DETAIL ("clist") || (widget && GTK_IS_LIST_ITEM (widget)))
    {
      gboolean selected = FALSE;

      if (widget && GTK_IS_LIST_ITEM (widget))
        selected = GTK_WIDGET_STATE (widget) == GTK_STATE_SELECTED;
      else if (widget && GTK_IS_CLIST (widget))
        {
          GList *row = g_list_nth (GTK_CLIST (widget)->row_list,
                                   GTK_CLIST (widget)->focus_row);

          selected = row && GTK_CLIST_ROW (row)->state == GTK_STATE_SELECTED;
        }
      if (selected)
        return;

      ring = unicus_mix (&style->base[GTK_STATE_NORMAL],
                         &style->bg[GTK_STATE_SELECTED], 0.6);
      gc = _gtk_theme_aa_gc (style, window, area, &ring);
      gdk_draw_rectangle (window, gc, FALSE, x, y, width - 1, height - 1);
      gdk_gc_unref (gc);
      return;
    }

  ring = unicus_mix (&style->bg[GTK_STATE_NORMAL],
                     &style->bg[GTK_STATE_SELECTED], 0.6);

  /* A spin button's or a combo's entry: square on the right, where its
   * buttons carry the field on */
  if (DETAIL ("entry") && unicus_joined (widget))
    {
      GdkRectangle rect, clip;

      rect.x = x;
      rect.y = y;
      rect.width = width;
      rect.height = height;
      if (area && !gdk_rectangle_intersect (&rect, area, &clip))
        return;
      if (!area)
        clip = rect;
      unicus_rounded (style, window, &clip, &ring, FALSE, 2,
                      x, y, width + 2 * BUTTON_RADIUS, height, BUTTON_RADIUS);
      unicus_fill (style, window, &clip, &ring,
                   x + width - 1, y, 1, height);
      return;
    }

  gc = _gtk_theme_aa_gc (style, window, area, &ring);
  gdk_gc_set_line_attributes (gc, 2, GDK_LINE_SOLID, GDK_CAP_BUTT,
                              GDK_JOIN_MITER);
  gdk_draw_rounded_rectangle (window, gc, FALSE, x, y, width, height,
                              DETAIL ("entry") ? BUTTON_RADIUS : CHECK_RADIUS + 1);
  gdk_gc_unref (gc);
}

/* A scale's slider: a round knob, with a faint edge */
static void
unicus_draw_slider (GtkStyle       *style,
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
  GdkColor knob, edge;
  gint size;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  knob = _gtk_theme_is_dark (style) ?
    unicus_mix (&style->fg[GTK_STATE_NORMAL], &style->bg[GTK_STATE_NORMAL], 0.15) :
    style->base[GTK_STATE_NORMAL];
  if (state_type == GTK_STATE_PRELIGHT)
    knob = unicus_mix (&knob, &style->bg[GTK_STATE_SELECTED], 0.12);
  edge = unicus_line (style, GTK_STATE_NORMAL, EDGE_MIX);

  size = MIN (width, height) - 2;
  unicus_disc (style, window, area, &edge, TRUE, 0,
               x + (width - size) / 2, y + (height - size) / 2, size);
  unicus_disc (style, window, area, &knob, TRUE, 0,
               x + (width - size) / 2 + 1, y + (height - size) / 2 + 1,
               size - 2);
}

/* A handle: a few dots, and no frame */
static void
unicus_draw_handle (GtkStyle       *style,
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
  GdkColor dot;
  gint i, j;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  dot = unicus_line (style, state_type, EDGE_MIX);
  for (j = -1; j <= 1; j++)
    for (i = 0; i < 2; i++)
      {
        gint dx = orientation == GTK_ORIENTATION_VERTICAL ? i * 4 - 2 : j * 5;
        gint dy = orientation == GTK_ORIENTATION_VERTICAL ? j * 5 : i * 4 - 2;

        unicus_disc (style, window, area, &dot, TRUE, 0,
                     x + width / 2 + dx - 1, y + height / 2 + dy - 1, 3);
      }
}

/* ---------------------------------------------------------------------
 * The engine
 */

static GtkStyleClass *
unicus_get_class (GtkStyleClass *parent, gint xthickness, gint ythickness)
{
  static GtkStyleClass klass;

  if (!klass.draw_hline)
    {
      /* Polygon, diamond, oval, cross, ramp and flat_box are the toolkit's
       * own */
      klass = *parent;
      klass.draw_hline = unicus_draw_hline;
      klass.draw_vline = unicus_draw_vline;
      klass.draw_shadow = unicus_draw_shadow;
      klass.draw_arrow = unicus_draw_arrow;
      klass.draw_string = unicus_draw_string;
      klass.draw_box = unicus_draw_box;
      klass.draw_check = unicus_draw_check;
      klass.draw_option = unicus_draw_option;
      klass.draw_tab = unicus_draw_tab;
      klass.draw_shadow_gap = unicus_draw_shadow_gap;
      klass.draw_box_gap = unicus_draw_box_gap;
      klass.draw_extension = unicus_draw_extension;
      klass.draw_focus = unicus_draw_focus;
      klass.draw_slider = unicus_draw_slider;
      klass.draw_handle = unicus_draw_handle;
    }

  return _gtk_theme_class (&unicus_classes, &klass, xthickness, ythickness);
}

/* The widgets' sizes: indicators of fourteen pixels, slim scrollbars,
 * a slim progress bar, and a scale's slider as the style says */
static void
unicus_set_props (GtkStyle *style)
{
  UnicusRcData *data = style->rc_style ? style->rc_style->engine_data : NULL;
  gint slider_width = 12;

  if (data && (data->flags & UNICUS_SLIDER_WIDTH_SET))
    slider_width = CLAMP (data->slider_width, 4, 64);

  gtk_style_set_prop_experimental (style, "GtkButton::default_spacing", 0);
  gtk_style_set_prop_experimental (style, "GtkCheckButton::indicator_size", 14);
  gtk_style_set_prop_experimental (style, "GtkRadioButton::indicator_size", 14);
  gtk_style_set_prop_experimental (style, "GtkCheckMenuItem::indicator_size", 14);
  gtk_style_set_prop_experimental (style, "GtkOptionMenu::indicator_width", 12);
  gtk_style_set_prop_experimental (style, "GtkOptionMenu::indicator_height", 12);
  gtk_style_set_prop_experimental (style, "GtkOptionMenu::indicator_left_spacing", 6);
  gtk_style_set_prop_experimental (style, "GtkOptionMenu::indicator_right_spacing", 6);
  /* GTK+ 1 draws a range's trough again only when what is exposed
   * reaches past its border: with a border of nothing it never would,
   * and a scale's thin trough would not be drawn at all */
  gtk_style_set_prop_experimental (style, "GtkRange::trough_border", 1);
  gtk_style_set_prop_experimental (style, "GtkRange::slider_width", slider_width);
  /* a scale's slider is its round knob, and takes no more room */
  gtk_style_set_prop_experimental (style, "GtkScale::slider_length", slider_width);
  gtk_style_set_prop_experimental (style, "GtkRange::stepper_size", 12);
  gtk_style_set_prop_experimental (style, "GtkRange::stepper_spacing", 0);
  gtk_style_set_prop_experimental (style, "GtkProgressBar::min_horizontal_bar_height", 6);
  gtk_style_set_prop_experimental (style, "GtkProgressBar::min_vertical_bar_width", 6);
  gtk_style_set_prop_experimental (style, "GtkPaned::handle_full_size", 1);
  gtk_style_set_prop_experimental (style, "GtkPaned::handle_width", 6);
}

static guint
unicus_parse_rc_style (GScanner   *scanner,
                       GtkRcStyle *rc_style)
{
  UnicusRcData *data;
  guint old_scope;
  guint token;

  old_scope = _gtk_theme_scope_enter (scanner, "unicus_theme_engine",
                                      unicus_symbols,
                                      G_N_ELEMENTS (unicus_symbols));

  data = g_new0 (UnicusRcData, 1);

  token = g_scanner_peek_next_token (scanner);
  while (token != G_TOKEN_RIGHT_CURLY)
    {
      g_scanner_get_next_token (scanner);

      switch (token)
        {
        case TOKEN_XTHICKNESS:
          token = _gtk_theme_parse_int (scanner, &data->xthickness);
          data->flags |= UNICUS_XTHICKNESS_SET;
          break;
        case TOKEN_YTHICKNESS:
          token = _gtk_theme_parse_int (scanner, &data->ythickness);
          data->flags |= UNICUS_YTHICKNESS_SET;
          break;
        case TOKEN_SLIDER_WIDTH:
          token = _gtk_theme_parse_int (scanner, &data->slider_width);
          data->flags |= UNICUS_SLIDER_WIDTH_SET;
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
unicus_merge_rc_style (GtkRcStyle *dest,
                       GtkRcStyle *src)
{
  UnicusRcData *src_data = src->engine_data;
  UnicusRcData *dest_data = dest->engine_data;
  guint flags;

  if (!dest_data)
    dest_data = dest->engine_data = g_new0 (UnicusRcData, 1);

  if (!src_data)
    return;

  flags = ~dest_data->flags & src_data->flags;

  if (flags & UNICUS_XTHICKNESS_SET)
    dest_data->xthickness = src_data->xthickness;
  if (flags & UNICUS_YTHICKNESS_SET)
    dest_data->ythickness = src_data->ythickness;
  if (flags & UNICUS_SLIDER_WIDTH_SET)
    dest_data->slider_width = src_data->slider_width;

  dest_data->flags |= flags;
}

static void
unicus_rc_style_to_style (GtkStyle   *style,
                          GtkRcStyle *rc_style)
{
  UnicusRcData *data = rc_style->engine_data;
  gint xthickness = 1;
  gint ythickness = 1;

  if (data && (data->flags & UNICUS_XTHICKNESS_SET))
    xthickness = CLAMP (data->xthickness, 0, 12);
  if (data && (data->flags & UNICUS_YTHICKNESS_SET))
    ythickness = CLAMP (data->ythickness, 0, 12);

  style->klass = unicus_get_class (style->klass, xthickness, ythickness);
  unicus_set_props (style);
}

static void
unicus_duplicate_style (GtkStyle *dest,
                        GtkStyle *src)
{
  dest->klass = src->klass;
  unicus_set_props (dest);
}

void
gtk_theme_unicus_init (GtkThemeEngine *engine)
{
  engine->parse_rc_style = unicus_parse_rc_style;
  engine->merge_rc_style = unicus_merge_rc_style;
  engine->rc_style_to_style = unicus_rc_style_to_style;
  engine->duplicate_style = unicus_duplicate_style;
  engine->destroy_rc_style = _gtk_theme_destroy_rc_style;
}
