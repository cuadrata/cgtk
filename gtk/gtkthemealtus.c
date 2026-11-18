/* GTK - The GIMP Toolkit
 * Altus, a flat theme engine
 *
 * Ported from the Mist engine of gtk-engines 2.20.2. Mist is based on
 * ThinIce, based on raster's Motif theme, and on the Metal theme.
 *
 * Mist author: Dave Camp <dave@ximian.com>
 * ThinIce authors: Tim Gerla <timg@rrv.net>
 *                  Tomas Ogren <stric@ing.umu.se>
 *
 * The arrow geometry and the slider are from GTK+ 2.24's gtkstyle.c,
 * which Mist used for them:
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
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

/* Mist drew with cairo, one pixel wide and without antialiasing to
 * speak of, so here the same pixels are drawn with GDK; its round radio
 * button and GTK+ 2's arrows are drawn with GDK's antialiasing. Where
 * GTK+ 1 widgets draw differently from GTK+ 2 ones -- scrollbar and
 * spin button steppers are arrows with a shadow rather than a box and
 * an arrow, and there is no inconsistent state -- the GTK+ 2 look is
 * reproduced from what GTK+ 1 asks for.
 *
 * The engine takes two options, for styles that want a thicker frame
 * than the one pixel everything else has:
 *
 *   engine "altus" { xthickness = 2  ythickness = 2 }
 *
 * Every colour comes from the style, so a theme is recoloured by its
 * rc file alone.
 */

#include <math.h>
#include <string.h>

#include "gtkgc.h"
#include "gtkcheckmenuitem.h"
#include "gtkrc.h"
#include "gtkstyle.h"
#include "gtkthemes.h"
#include "gtkthemebuiltin.h"
#include "gtkthemecommon.h"
#include "gtkbutton.h"
#include "gtkclist.h"
#include "gtkmenuitem.h"
#include "gtkrange.h"
#include "gtkscrollbar.h"
#include "gtkstatusbar.h"
#include "gtkwidget.h"

#define DETAIL(xx) ((detail) && (!strcmp ((xx), (detail))))

#define XTHICKNESS(style) ((style)->klass->xthickness)
#define YTHICKNESS(style) ((style)->klass->ythickness)

enum
{
  TOKEN_XTHICKNESS = G_TOKEN_LAST + 1,
  TOKEN_YTHICKNESS
};

static const GtkThemeSymbol altus_symbols[] = {
  { "xthickness", TOKEN_XTHICKNESS },
  { "ythickness", TOKEN_YTHICKNESS }
};

#define ALTUS_XTHICKNESS_SET (1 << 0)
#define ALTUS_YTHICKNESS_SET (1 << 1)

typedef struct
{
  guint flags;
  gint xthickness;
  gint ythickness;
} AltusRcData;

static GSList *altus_classes = NULL;

/* Mist draws its borders in the dark colour, which is lost against a
 * dark background: on one, borders are drawn in a colour a quarter of
 * the way from the background to the foreground instead. Which kind
 * of theme it is comes from its colours alone, so a theme recoloured
 * from light to dark, or back, needs nothing else. */
#define DARK_BORDER_MIX 0.25

typedef struct
{
  GdkColor border[5];
  GdkGC *border_gc[5];
  GdkColor focus[2];
  GdkGC *focus_gc[2];           /* light and dark */
} AltusStyleData;

static GdkGC *
altus_border_gc (GtkStyle *style, GtkStateType state_type)
{
  AltusStyleData *data = style->engine_data;

  if (data && data->border_gc[state_type])
    return data->border_gc[state_type];
  return style->dark_gc[state_type];
}

static const GdkColor *
altus_border_color (GtkStyle *style, GtkStateType state_type)
{
  AltusStyleData *data = style->engine_data;

  if (data && data->border_gc[state_type])
    return &data->border[state_type];
  return &style->dark[state_type];
}

/* A focused button's edges. They take only the hue of the selection
 * colour, with its saturation held down, and are as bright as greys of
 * set lightnesses, so that a red or yellow selection gives edges as quiet
 * as a purple or blue one does. On a dark theme the lightnesses follow
 * the background's. */
#define FOCUS_SATURATION 0.40
#define FOCUS_LIGHT_LIFT 0.07   /* dark theme: lightness above the background */
#define FOCUS_DARK_SCALE 0.35   /* dark theme: fraction of the background's */
#define FOCUS_LIGHT_LIGHT 0.58  /* light theme: the light edge */
#define FOCUS_LIGHT_DARK 0.28   /* light theme: the dark edge */

static void
altus_rgb_to_hls (const GdkColor *color, gdouble *h, gdouble *l, gdouble *s)
{
  gdouble r = color->red / 65535.0;
  gdouble g = color->green / 65535.0;
  gdouble b = color->blue / 65535.0;
  gdouble max = MAX (r, MAX (g, b));
  gdouble min = MIN (r, MIN (g, b));
  gdouble delta = max - min;

  *l = (max + min) / 2;
  *h = 0;
  *s = 0;
  if (delta == 0)
    return;

  *s = *l <= 0.5 ? delta / (max + min) : delta / (2 - max - min);
  if (r == max)
    *h = (g - b) / delta;
  else if (g == max)
    *h = 2 + (b - r) / delta;
  else
    *h = 4 + (r - g) / delta;
  *h *= 60;
  if (*h < 0)
    *h += 360;
}

static gdouble
altus_hls_value (gdouble m1, gdouble m2, gdouble hue)
{
  if (hue >= 360)
    hue -= 360;
  else if (hue < 0)
    hue += 360;

  if (hue < 60)
    return m1 + (m2 - m1) * hue / 60;
  if (hue < 180)
    return m2;
  if (hue < 240)
    return m1 + (m2 - m1) * (240 - hue) / 60;
  return m1;
}

static gdouble
altus_luminance (const GdkColor *color)
{
  return (color->red * 0.299 + color->green * 0.587 +
          color->blue * 0.114) / 65535.0;
}

static void
altus_hls_to_rgb (gdouble h, gdouble l, gdouble s, GdkColor *color)
{
  gdouble m2 = l <= 0.5 ? l * (1 + s) : l + s - l * s;
  gdouble m1 = 2 * l - m2;

  color->red = 65535 * altus_hls_value (m1, m2, h + 120);
  color->green = 65535 * altus_hls_value (m1, m2, h);
  color->blue = 65535 * altus_hls_value (m1, m2, h - 120);
}

/* The colour of hue h and saturation s as bright as a grey of
 * lightness l: blues are dark and yellows bright at one lightness. */
static void
altus_hue_at (gdouble h, gdouble s, gdouble l, GdkColor *color)
{
  gdouble lo = 0, hi = 1;
  gint i;

  for (i = 0; i < 16; i++)
    {
      gdouble mid = (lo + hi) / 2;

      altus_hls_to_rgb (h, mid, s, color);
      if (altus_luminance (color) < l)
        lo = mid;
      else
        hi = mid;
    }
  altus_hls_to_rgb (h, lo, s, color);
}

static GdkGC *
altus_focus_gc (GtkStyle *style, gboolean dark)
{
  AltusStyleData *data = style->engine_data;

  if (data && data->focus_gc[dark])
    return data->focus_gc[dark];
  return dark ? style->dark_gc[GTK_STATE_SELECTED] :
    style->light_gc[GTK_STATE_SELECTED];
}

#define BORDER_GC(style, state) altus_border_gc ((style), (state))
#define BORDER_COLOR(style, state) altus_border_color ((style), (state))


static GtkShadowType
altus_get_shadow_type (const gchar *detail, GtkShadowType requested)
{
  GtkShadowType retval = GTK_SHADOW_NONE;

  if (requested != GTK_SHADOW_NONE)
    retval = GTK_SHADOW_ETCHED_IN;

  if (DETAIL ("dockitem") || DETAIL ("handlebox_bin"))
    retval = GTK_SHADOW_NONE;
  else if (DETAIL ("spinbutton_up") || DETAIL ("spinbutton_down"))
    retval = GTK_SHADOW_OUT;
  else if (DETAIL ("button") || DETAIL ("togglebutton") ||
           DETAIL ("notebook") || DETAIL ("optionmenu"))
    retval = requested;
  else if (DETAIL ("menu"))
    retval = GTK_SHADOW_ETCHED_IN;

  return retval;
}

static void
altus_draw_border (GtkStyle *style, GdkWindow *window, GdkRectangle *area,
                   GtkStateType state_type, GtkShadowType shadow_type,
                   gint x, gint y, gint width, gint height)
{
  GdkGC *gc1, *gc2;

  switch (shadow_type)
    {
    case GTK_SHADOW_ETCHED_IN:
    case GTK_SHADOW_ETCHED_OUT:
      gc1 = BORDER_GC (style, state_type);
      gc2 = BORDER_GC (style, state_type);
      break;
    case GTK_SHADOW_OUT:
      gc1 = style->light_gc[state_type];
      gc2 = style->dark_gc[state_type];
      break;
    case GTK_SHADOW_IN:
      gc1 = style->dark_gc[state_type];
      gc2 = style->light_gc[state_type];
      break;
    case GTK_SHADOW_NONE:
    default:
      return;
    }

  _gtk_theme_simple_border (window, area, gc1, gc2, x, y, width, height);
}


/* ---------------------------------------------------------------------
 * Drawing
 */

static void
altus_draw_hline (GtkStyle     *style,
                  GdkWindow    *window,
                  GtkStateType  state_type,
                  GdkRectangle *area,
                  GtkWidget    *widget,
                  gchar        *detail,
                  gint          x1,
                  gint          x2,
                  gint          y)
{
  gint thickness_light;
  gint thickness_dark;
  gint i;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  /* GTK+ 1 draws a menu separator along the top of the item; GTK+ 2
   * across the middle, inside the item's frame. */
  if (DETAIL ("menuitem") && widget && GTK_IS_MENU_ITEM (widget) &&
      !GTK_BIN (widget)->child)
    {
      x1 += XTHICKNESS (style);
      x2 -= XTHICKNESS (style);
      y = (widget->allocation.height - 1) / 2;
    }

  if (DETAIL ("hseparator") || DETAIL ("menuitem") ||
      DETAIL ("slider") || DETAIL ("vscale"))
    {
      _gtk_theme_line (window, BORDER_GC (style, state_type), area, x1, y, x2 - 1, y);
      return;
    }

  thickness_light = YTHICKNESS (style) / 2;
  thickness_dark = YTHICKNESS (style) - thickness_light;

  for (i = 0; i < thickness_dark; i++)
    {
      _gtk_theme_line (window, style->light_gc[state_type], area,
                       x2 - i - 1, y + i, x2, y + i);
      _gtk_theme_line (window, style->dark_gc[state_type], area,
                       x1, y + i, x2 - i - 1, y + i);
    }

  y += thickness_dark;
  for (i = 0; i < thickness_light; i++)
    {
      _gtk_theme_line (window, style->dark_gc[state_type], area,
                       x1, y + i, x1 + thickness_light - i - 1, y + i);
      _gtk_theme_line (window, style->light_gc[state_type], area,
                       x1 + thickness_light - i - 1, y + i, x2, y + i);
    }
}

static void
altus_draw_vline (GtkStyle     *style,
                  GdkWindow    *window,
                  GtkStateType  state_type,
                  GdkRectangle *area,
                  GtkWidget    *widget,
                  gchar        *detail,
                  gint          y1,
                  gint          y2,
                  gint          x)
{
  gint thickness_light;
  gint thickness_dark;
  gint i;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  if (DETAIL ("vseparator") || DETAIL ("toolbar") ||
      DETAIL ("slider") || DETAIL ("hscale"))
    {
      _gtk_theme_line (window, BORDER_GC (style, state_type), area, x, y1, x, y2 - 1);
      return;
    }

  thickness_light = XTHICKNESS (style) / 2;
  thickness_dark = XTHICKNESS (style) - thickness_light;

  for (i = 0; i < thickness_dark; i++)
    {
      _gtk_theme_line (window, style->light_gc[state_type], area,
                       x + i, y2 - i - 1, x + i, y2);
      _gtk_theme_line (window, style->dark_gc[state_type], area,
                       x + i, y1, x + i, y2 - i - 1);
    }

  x += thickness_dark;
  for (i = 0; i < thickness_light; i++)
    {
      _gtk_theme_line (window, style->dark_gc[state_type], area,
                       x + i, y1, x + i, y1 + thickness_light - i - 1);
      _gtk_theme_line (window, style->light_gc[state_type], area,
                       x + i, y1 + thickness_light - i - 1, x + i, y2 - 1);
    }
}

static void
altus_draw_shadow (GtkStyle      *style,
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

  shadow_type = altus_get_shadow_type (detail, shadow_type);
  if (shadow_type == GTK_SHADOW_NONE)
    return;

  if (state_type == GTK_STATE_INSENSITIVE)
    shadow_type = GTK_SHADOW_ETCHED_IN;

  if (DETAIL ("frame") && widget && widget->parent &&
      GTK_IS_STATUSBAR (widget->parent))
    _gtk_theme_line (window, BORDER_GC (style, GTK_STATE_NORMAL), area,
                     x, y, x + width - 1, y);
  else
    altus_draw_border (style, window, area, state_type, shadow_type,
                       x, y, width, height);
}


/* From GTK+ 2: the size and place of an arrow in the area given. */
static void
altus_arrow_geometry (GtkArrowType  arrow_type,
                      gint         *x,
                      gint         *y,
                      gint         *width,
                      gint         *height)
{
  gint w = *width;
  gint h = *height;

  switch (arrow_type)
    {
    case GTK_ARROW_UP:
    case GTK_ARROW_DOWN:
      w += (w % 2) - 1;
      h = (w / 2 + 1);

      if (h > *height)
        {
          h = *height;
          w = 2 * h - 1;
        }

      if (arrow_type == GTK_ARROW_DOWN)
        {
          if (*height % 2 == 1 || h % 2 == 0)
            *height += 1;
        }
      else
        {
          if (*height % 2 == 0 || h % 2 == 0)
            *height -= 1;
        }
      break;

    case GTK_ARROW_RIGHT:
    case GTK_ARROW_LEFT:
      h += (h % 2) - 1;
      w = (h / 2 + 1);

      if (w > *width)
        {
          w = *width;
          h = 2 * w - 1;
        }

      if (arrow_type == GTK_ARROW_RIGHT)
        {
          if (*width % 2 == 1 || w % 2 == 0)
            *width += 1;
        }
      else
        {
          if (*width % 2 == 0 || w % 2 == 0)
            *width -= 1;
        }
      break;
    }

  *x += (*width - w) / 2;
  *y += (*height - h) / 2;
  *height = h;
  *width = w;
}

/* The corners of GTK+ 2's arrow in the space altus_arrow_geometry()
 * gives it, offset by dx, dy */
static void
altus_arrow_triangle (GtkArrowType arrow_type, gint x, gint y,
                      gint width, gint height, gdouble d, gdouble *t)
{
  switch (arrow_type)
    {
    case GTK_ARROW_DOWN:
      t[0] = x;             t[1] = y;
      t[2] = x + width;     t[3] = y;
      t[4] = x + width / 2.; t[5] = y + height;
      break;
    case GTK_ARROW_UP:
      t[0] = x;             t[1] = y + height;
      t[2] = x + width / 2.; t[3] = y;
      t[4] = x + width;     t[5] = y + height;
      break;
    case GTK_ARROW_LEFT:
      t[0] = x + width;     t[1] = y;
      t[2] = x + width;     t[3] = y + height;
      t[4] = x;             t[5] = y + height / 2.;
      break;
    case GTK_ARROW_RIGHT:
    default:
      t[0] = x;             t[1] = y;
      t[2] = x + width;     t[3] = y + height / 2.;
      t[4] = x;             t[5] = y + height;
      break;
    }

  t[0] += d; t[2] += d; t[4] += d;
  t[1] += d; t[3] += d; t[5] += d;
}

/* Antialiased triangles, for arrows: each device pixel's share of each
 * triangle, composited over what is already there, drawn at the
 * device's resolution. A triangle is three corners in the toolkit's
 * pixels, as cairo would take them: a corner at (x, y) is the top left
 * of pixel x, y. */
/* A GC drawing color antialiased, clipped to area */

/* An antialiased triangle, t its three corners */
static void
altus_aa_triangle (GtkStyle *style, GdkWindow *window, GdkRectangle *area,
                   const GdkColor *color, const gdouble *t)
{
  GdkPointF points[3];
  GdkGC *gc = _gtk_theme_aa_gc (style, window, area, color);
  gint i;

  for (i = 0; i < 3; i++)
    {
      points[i].x = t[2 * i];
      points[i].y = t[2 * i + 1];
    }
  gdk_draw_polygon_f (window, gc, TRUE, points, 3);
  gdk_gc_unref (gc);
}

/* An antialiased disc of radius r about cx, cy, anywhere */
static void
altus_disc (GdkWindow *window, GdkGC *gc, gdouble cx, gdouble cy, gdouble r)
{
  GdkPointF points[64];
  gint i;

  for (i = 0; i < 64; i++)
    {
      points[i].x = cx + r * cos (i * 2 * M_PI / 64);
      points[i].y = cy - r * sin (i * 2 * M_PI / 64);
    }
  gdk_draw_polygon_f (window, gc, TRUE, points, 64);
}


static void altus_draw_box (GtkStyle *style, GdkWindow *window,
                            GtkStateType state_type, GtkShadowType shadow_type,
                            GdkRectangle *area, GtkWidget *widget,
                            gchar *detail, gint x, gint y,
                            gint width, gint height);

static void
altus_draw_arrow (GtkStyle      *style,
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

  /* GTK+ 1 draws a scrollbar's or a spin button's stepper as an arrow
   * with a shadow; GTK+ 2 draws a box, and a smaller arrow in it. */
  if (DETAIL ("hscrollbar") || DETAIL ("vscrollbar") || DETAIL ("spinbutton"))
    {
      gchar *box_detail = DETAIL ("spinbutton") ? "spinbutton_up" : detail;
      gint arrow_width = width / 2;
      gint arrow_height = height / 2;

      altus_draw_box (style, window, state_type, shadow_type, area, widget,
                      box_detail, x, y, width, height);

      x += (width - arrow_width) / 2;
      y += (height - arrow_height) / 2;
      width = arrow_width;
      height = arrow_height;
    }
  else if (DETAIL ("arrow"))
    {
      /* GtkArrow's arrow_scaling */
      gint extent = MIN (width, height) * 0.7;

      x += (width - extent) / 2;
      y += (height - extent) / 2;
      width = height = extent;
    }

  if (width <= 0 || height <= 0)
    return;

  altus_arrow_geometry (arrow_type, &x, &y, &width, &height);

  /* a submenu's arrow is in the middle of its item, however tall */
  if (DETAIL ("menuitem") && widget && GTK_IS_MENU_ITEM (widget))
    y = (widget->allocation.height - height) / 2;

  /* GTK+ 2 embosses an insensitive arrow in white; the light colour,
   * which is white on a light theme, suits a dark one too. */
  /* GTK+ 2 draws its arrow antialiased; so does Altus */
  {
    gdouble t[6];

    if (state_type == GTK_STATE_INSENSITIVE)
      {
        altus_arrow_triangle (arrow_type, x, y, width, height, 1, t);
        altus_aa_triangle (style, window, area, &style->light[state_type], t);
      }
    altus_arrow_triangle (arrow_type, x, y, width, height, 0, t);
    altus_aa_triangle (style, window, area, &style->fg[state_type], t);
  }
}


static void
altus_draw_string (GtkStyle     *style,
                   GdkWindow    *window,
                   GtkStateType  state_type,
                   GdkRectangle *area,
                   GtkWidget    *widget,
                   gchar        *detail,
                   gint          x,
                   gint          y,
                   const gchar  *string)
{
  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_clip (style->fg_gc[state_type], area);
  gdk_draw_string (window, style->font, style->fg_gc[state_type],
                   x, y, string);
  _gtk_theme_unclip (style->fg_gc[state_type], area);
}

/* A button or toggle button, whose focus is shown in its edges rather
 * than drawn round it. A list's column titles are buttons too, but drawn
 * flat: they are left as they are. */
static gboolean
altus_focus_inside (GtkWidget *widget, const gchar *detail)
{
  return (DETAIL ("button") || DETAIL ("togglebutton")) &&
    widget && GTK_IS_BUTTON (widget) &&
    !(widget->parent && GTK_IS_CLIST (widget->parent));
}

static void
altus_draw_box (GtkStyle      *style,
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
  GdkGC *border;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);
  state_type = _gtk_theme_toggle_state (detail, state_type, shadow_type);

  if (DETAIL ("menuitem") && state_type == GTK_STATE_PRELIGHT)
    state_type = GTK_STATE_SELECTED;

  border = BORDER_GC (style, state_type);

  /* A paned widget's divider (GtkPaned::handle_full_size): the window's
   * background with the handle's three dots at its middle */
  if (DETAIL ("hpaned") || DETAIL ("vpaned"))
    {
      gint dx = DETAIL ("hpaned") ? 0 : 4;
      gint dy = DETAIL ("hpaned") ? 4 : 0;
      gint i;

      _gtk_theme_rectangle (window, style->bg_gc[state_type], area, TRUE,
                            x, y, width, height);
      for (i = -1; i <= 1; i++)
        _gtk_theme_dot (window, area, style->light_gc[state_type],
                        style->dark_gc[state_type],
                        x + width / 2 + i * dx, y + height / 2 + i * dy);
      return;
    }

  if (DETAIL ("optionmenutab"))
    {
      _gtk_theme_line (window, border, area, x - 5, y, x - 5, y + height);

      gtk_paint_arrow (style, window, state_type, shadow_type, area,
                       widget, detail, GTK_ARROW_DOWN, TRUE,
                       x + 1, y + 1, width - 2, height - 2);
    }
  else if (DETAIL ("trough") && widget && GTK_IS_SCROLLBAR (widget))
    {
      /* A scrollbar's trough has no edges: its steppers and slider have
       * their own, and GTK+ 1 clears the trough behind a moving slider
       * without drawing the trough again, so edges under the slider's
       * path would be lost as it moves. */
      _gtk_theme_rectangle (window, style->bg_gc[state_type], area, TRUE,
                            x, y, width, height);
    }
  else if (DETAIL ("trough"))
    {
      _gtk_theme_rectangle (window, style->bg_gc[state_type], area, TRUE,
                            x, y, width - 1, height - 1);
      _gtk_theme_rectangle (window, border, area, FALSE,
                            x, y, width - 1, height - 1);
    }
  else if (DETAIL ("handlebox"))
    {
      /* A handle box's grip: only its dots, on the background */
      _gtk_theme_rectangle (window, style->bg_gc[state_type], area, TRUE,
                            x, y, width, height);
    }
  else if (DETAIL ("menubar") || DETAIL ("dockitem_bin") ||
           DETAIL ("dockitem") || DETAIL ("toolbar"))
    {
      if (shadow_type != GTK_SHADOW_NONE)
        _gtk_theme_line (window, BORDER_GC (style, GTK_STATE_NORMAL), area,
                         x, y + height - 1, x + width - 1, y + height - 1);
    }
  else if (DETAIL ("bar"))
    {
      if (width > 1 && height > 1)
        _gtk_theme_rectangle (window, style->base_gc[GTK_STATE_SELECTED], area,
                              TRUE, x + 1, y + 1, width - 2, height - 2);

      _gtk_theme_simple_border (window, area,
                                style->dark_gc[GTK_STATE_SELECTED],
                                style->dark_gc[GTK_STATE_SELECTED],
                                x, y, width, height);
    }
  else if (DETAIL ("button") && widget && widget->parent &&
           GTK_IS_CLIST (widget->parent))
    {
      /* A list's column title: flat, with a line below it and one
       * between it and the next. The list's frame is round the titles
       * already, so there is no line where a title meets it, which
       * would double the frame. */
      GtkCList *clist = GTK_CLIST (widget->parent);

      gtk_style_apply_default_background (style, window, TRUE,
                                          state_type, area,
                                          x, y, width, height);

      _gtk_theme_line (window, BORDER_GC (style, GTK_STATE_NORMAL), area,
                       x, y + height - 1, x + width - 1, y + height - 1);
      if (widget->allocation.x + widget->allocation.width <
          clist->column_title_area.width)
        _gtk_theme_line (window, BORDER_GC (style, GTK_STATE_NORMAL), area,
                         x + width - 1, y, x + width - 1, y + height - 1);
    }
  else if (DETAIL ("buttondefault") || DETAIL ("togglebuttondefault"))
    {
      /* Nothing: a second outline round the default button, as GTK+ 1
       * asks for, was heavier than any other line in the theme. */
    }
  else
    {
      /* GTK+ 1 shrinks a focused button by a pixel all round, to draw
       * the focus outside it: here it keeps its size, and the focus is
       * shown in the colours of its edges (below). */
      if (altus_focus_inside (widget, detail) && GTK_WIDGET_HAS_FOCUS (widget))
        {
          x -= 1;
          y -= 1;
          width += 2;
          height += 2;
        }

      gtk_style_apply_default_background (style, window,
                                          widget && !GTK_WIDGET_NO_WINDOW (widget),
                                          state_type, area,
                                          x, y, width, height);

      shadow_type = altus_get_shadow_type (detail, shadow_type);
      if (state_type == GTK_STATE_INSENSITIVE &&
          shadow_type != GTK_SHADOW_NONE)
        shadow_type = GTK_SHADOW_ETCHED_IN;

      /* A focused button's edges are in the light and dark of the
       * selection colour rather than the greys: no line is added, so
       * however small the button nothing crowds its label. */
      if (altus_focus_inside (widget, detail) &&
          GTK_WIDGET_HAS_FOCUS (widget) &&
          (shadow_type == GTK_SHADOW_OUT || shadow_type == GTK_SHADOW_IN))
        {
          GdkGC *light = altus_focus_gc (style, FALSE);
          GdkGC *dark = altus_focus_gc (style, TRUE);

          if (shadow_type == GTK_SHADOW_OUT)
            _gtk_theme_simple_border (window, area, light, dark,
                                      x, y, width, height);
          else
            _gtk_theme_simple_border (window, area, dark, light,
                                      x, y, width, height);
        }
      else
        altus_draw_border (style, window, area, state_type, shadow_type,
                           x, y, width, height);
    }
}

static void
altus_draw_check (GtkStyle      *style,
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
  GtkStateType base_state;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  /* GTK+ 1 puts a menu item's indicator a row higher than the middle
   * of its label; a row lower lines them up */
  if (widget && GTK_IS_CHECK_MENU_ITEM (widget))
    y += 1;

  /* GTK+ 1 draws an active indicator in the active state even when it
   * is insensitive; GTK+ 2, as Mist expects, in the insensitive one. */
  if (widget && !GTK_WIDGET_IS_SENSITIVE (widget))
    state_type = GTK_STATE_INSENSITIVE;

  base_state = (state_type == GTK_STATE_INSENSITIVE) ?
    GTK_STATE_INSENSITIVE : GTK_STATE_NORMAL;

  _gtk_theme_rectangle (window, style->base_gc[base_state], area, TRUE,
                        x + 1, y + 1, width - 3, height - 3);
  _gtk_theme_rectangle (window, BORDER_GC (style, state_type), area, FALSE,
                        x, y, width - 2, height - 2);

  if (shadow_type == GTK_SHADOW_IN)
    _gtk_theme_rectangle (window,
                          (state_type == GTK_STATE_INSENSITIVE) ?
                          BORDER_GC (style, state_type) :
                          style->base_gc[GTK_STATE_SELECTED],
                          area, TRUE, x + 2, y + 2, width - 5, height - 5);
}

/* The share of the square side by side across at (px, py) that lies
 * between radii r_in and r_out of (cx, cy). */
/* Mist's radio button, antialiased: a ring, a disc inside it, and a
 * dot when on */
static void
altus_draw_option (GtkStyle      *style,
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
  static const GdkColor black = { 0, 0, 0, 0 };
  GtkStateType base_state;
  const GdkColor *dot;
  gdouble cx, cy;
  gint radius, dot_radius;
  GdkGC *ring, *fill;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  /* GTK+ 1 puts a menu item's indicator a row higher than the middle
   * of its label; a row lower lines them up */
  if (widget && GTK_IS_CHECK_MENU_ITEM (widget))
    y += 1;

  /* GTK+ 1 draws an active indicator in the active state even when it
   * is insensitive; GTK+ 2, as Mist expects, in the insensitive one. */
  if (widget && !GTK_WIDGET_IS_SENSITIVE (widget))
    state_type = GTK_STATE_INSENSITIVE;

  if (width <= 0 || height <= 0)
    return;

  base_state = (state_type == GTK_STATE_INSENSITIVE) ?
    GTK_STATE_INSENSITIVE : GTK_STATE_NORMAL;

  dot = (state_type == GTK_STATE_INSENSITIVE) ?
    BORDER_COLOR (style, state_type) : &style->base[GTK_STATE_SELECTED];

  /* In Mist's terms, in the toolkit's pixels: centred on
   * (x + width / 2, y + height / 2), a one pixel line of radius
   * width / 2 - 0.5, and a dot of radius (width - 7) / 2 + 1. */
  radius = width / 2;
  dot_radius = (width - 7) / 2 + 1;
  cx = x + radius;
  cy = y + height / 2;

  ring = _gtk_theme_aa_gc (style, window, area, BORDER_COLOR (style, base_state));
  fill = _gtk_theme_aa_gc (style, window, area, &style->base[base_state]);

  if (_gtk_theme_is_dark (style))
    {
      /* the ring over the fill */
      altus_disc (window, fill, cx, cy, radius - 0.5);
      gdk_draw_arc (window, ring, FALSE, cx - radius, cy - radius,
                    2 * radius, 2 * radius, 0, 360 * 64);
    }
  else
    {
      /* the fill over the ring, and the ring darkened to show through */
      gdk_draw_arc (window, ring, FALSE, cx - radius, cy - radius,
                    2 * radius, 2 * radius, 0, 360 * 64);
      altus_disc (window, fill, cx, cy, radius - 0.5);
      gdk_gc_unref (ring);
      ring = _gtk_theme_aa_gc (style, window, area, &black);
      gdk_gc_set_alpha (ring, 0.35);
      gdk_draw_arc (window, ring, FALSE, cx - radius, cy - radius,
                    2 * radius, 2 * radius, 0, 360 * 64);
    }

  if (shadow_type == GTK_SHADOW_IN)
    {
      GdkGC *gc = _gtk_theme_aa_gc (style, window, area, dot);

      gdk_draw_arc (window, gc, TRUE, cx - dot_radius, cy - dot_radius,
                    2 * dot_radius, 2 * dot_radius, 0, 360 * 64);
      gdk_gc_unref (gc);
    }

  gdk_gc_unref (ring);
  gdk_gc_unref (fill);
}

static void
altus_draw_shadow_gap (GtkStyle        *style,
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

  shadow_type = altus_get_shadow_type (detail, shadow_type);

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
      gc1 = BORDER_GC (style, state_type);
      gc2 = BORDER_GC (style, state_type);
      break;
    case GTK_SHADOW_NONE:
    default:
      return;
    }

  /* The gap: pixels start to end - 1 along the side, leaving the
   * corners. (Mist measures the left and right sides against the
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
altus_draw_box_gap (GtkStyle        *style,
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

  altus_draw_shadow_gap (style, window, state_type, shadow_type, area,
                         widget, detail, x, y, width, height,
                         gap_side, gap_x, gap_width);
}


static void
altus_draw_focus (GtkStyle     *style,
                  GdkWindow    *window,
                  GdkRectangle *area,
                  GtkWidget    *widget,
                  gchar        *detail,
                  gint          x,
                  gint          y,
                  gint          width,
                  gint          height)
{
  GtkStateType state_type;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  /* GTK+ 1 passes the rectangle's size less one, as it is drawn */
  if (width == -1 || height == -1)
    {
      _gtk_theme_sanitize_size (window, &width, &height);
      width -= 1;
      height -= 1;
    }

  state_type = widget ? GTK_WIDGET_STATE (widget) : GTK_STATE_NORMAL;

  /* A list's focus row spans the list, whose frame is at either end:
   * only its top and bottom are drawn, so that the frame is not doubled. */
  if (DETAIL ("clist"))
    {
      _gtk_theme_line (window, BORDER_GC (style, state_type), area,
                       x, y, x + width, y);
      _gtk_theme_line (window, BORDER_GC (style, state_type), area,
                       x, y + height, x + width, y + height);
      return;
    }

  /* A button's focus is in the colours of its edges (altus_draw_box) */
  if (altus_focus_inside (widget, detail))
    return;

  /* Elsewhere, a dotted rectangle, as GTK+ 2 draws focus for Mist.
   * The dots are on every other pixel, counted from the corner, so
   * that the corners are always drawn. */
  {
    GdkGC *gc = BORDER_GC (style, state_type);
    GdkPoint *points;
    gint n = 0, i;

    if (width < 0 || height < 0)
      return;

    points = g_new (GdkPoint, width + height + 4);
    for (i = 0; i <= width; i += 2)
      {
        points[n].x = x + i;
        points[n++].y = y;
        points[n].x = x + width - i;
        points[n++].y = y + height;
      }
    for (i = 2; i < height; i += 2)
      {
        points[n].x = x;
        points[n++].y = y + height - i;
        points[n].x = x + width;
        points[n++].y = y + i;
      }

    _gtk_theme_clip (gc, area);
    gdk_draw_points (window, gc, points, n);
    _gtk_theme_unclip (gc, area);
    g_free (points);
  }
}

static void
altus_draw_slider (GtkStyle       *style,
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

  gtk_paint_box (style, window, state_type, shadow_type,
                 area, widget, detail, x, y, width, height);

  if (DETAIL ("hscale") || DETAIL ("vscale"))
    {
      if (orientation == GTK_ORIENTATION_HORIZONTAL)
        gtk_paint_vline (style, window, state_type, area, widget, detail,
                         y + YTHICKNESS (style),
                         y + height - YTHICKNESS (style) - 1, x + width / 2);
      else
        gtk_paint_hline (style, window, state_type, area, widget, detail,
                         x + XTHICKNESS (style),
                         x + width - XTHICKNESS (style) - 1, y + height / 2);
    }
}

static void
altus_draw_handle (GtkStyle       *style,
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
  GdkRectangle dest, clip;
  GdkGC *light, *dark;
  gint modx, mody;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  gtk_paint_box (style, window, state_type, shadow_type, area, widget,
                 detail, x, y, width, height);

  light = style->light_gc[state_type];
  dark = style->dark_gc[state_type];

  x += XTHICKNESS (style);
  y += YTHICKNESS (style);
  dest.x = x;
  dest.y = y;
  dest.width = width - (XTHICKNESS (style) * 2);
  dest.height = height - (YTHICKNESS (style) * 2);

  if (dest.width <= 0 || dest.height <= 0)
    return;
  if (area)
    {
      if (!gdk_rectangle_intersect (&dest, area, &clip))
        return;
    }
  else
    clip = dest;

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

  _gtk_theme_dot (window, &clip, light, dark,
                  x + width / 2 - modx, y + height / 2 - mody);
  _gtk_theme_dot (window, &clip, light, dark,
                  x + width / 2, y + height / 2);
  _gtk_theme_dot (window, &clip, light, dark,
                  x + width / 2 + modx, y + height / 2 + mody);
}

/* ---------------------------------------------------------------------
 * The engine
 */

static GtkStyleClass *
altus_get_class (GtkStyleClass *parent, gint xthickness, gint ythickness)
{
  static GtkStyleClass klass;

  if (!klass.draw_hline)
    {
      /* Oval, cross, ramp and flat_box are the toolkit's own */
      klass = *parent;
      klass.draw_hline = altus_draw_hline;
      klass.draw_vline = altus_draw_vline;
      klass.draw_shadow = altus_draw_shadow;
      klass.draw_polygon = _gtk_theme_draw_polygon;
      klass.draw_arrow = altus_draw_arrow;
      klass.draw_diamond = _gtk_theme_draw_diamond;
      klass.draw_string = altus_draw_string;
      klass.draw_box = altus_draw_box;
      klass.draw_check = altus_draw_check;
      klass.draw_option = altus_draw_option;
      klass.draw_tab = altus_draw_box;
      klass.draw_shadow_gap = altus_draw_shadow_gap;
      klass.draw_box_gap = altus_draw_box_gap;
      klass.draw_extension = _gtk_theme_draw_extension;
      klass.draw_focus = altus_draw_focus;
      klass.draw_slider = altus_draw_slider;
      klass.draw_handle = altus_draw_handle;
    }

  return _gtk_theme_class (&altus_classes, &klass, xthickness, ythickness);
}

/* The widget metrics Mist's theme sets, and GTK+ 2's defaults where
 * they differ from GTK+ 1's and Mist leaves them alone. */
static void
altus_set_props (GtkStyle *style)
{
  gtk_style_set_prop_experimental (style, "GtkButton::default_spacing", 0);
  gtk_style_set_prop_experimental (style, "GtkPaned::handle_full_size", 1);
  gtk_style_set_prop_experimental (style, "GtkCheckButton::indicator_size", 10);
  gtk_style_set_prop_experimental (style, "GtkCheckMenuItem::indicator_size", 10);
  gtk_style_set_prop_experimental (style, "GtkRadioButton::indicator_size", 12);
  gtk_style_set_prop_experimental (style, "GtkOptionMenu::indicator_width", 7);
  gtk_style_set_prop_experimental (style, "GtkOptionMenu::indicator_height", 13);
  gtk_style_set_prop_experimental (style, "GtkOptionMenu::indicator_left_spacing", 7);
  gtk_style_set_prop_experimental (style, "GtkOptionMenu::indicator_right_spacing", 5);
  gtk_style_set_prop_experimental (style, "GtkRange::trough_border", 0);
  gtk_style_set_prop_experimental (style, "GtkRange::slider_width", 15);
  gtk_style_set_prop_experimental (style, "GtkRange::stepper_size", 15);
  /* A scrollbar's slider slides one pixel into each stepper, so that at
   * either end its edge lies on the stepper's instead of beside it:
   * Mist overlaps them the same way. */
  gtk_style_set_prop_experimental (style, "GtkRange::stepper_spacing", -1);
}

static guint
altus_parse_rc_style (GScanner   *scanner,
                      GtkRcStyle *rc_style)
{
  AltusRcData *data;
  guint old_scope;
  guint token;

  old_scope = _gtk_theme_scope_enter (scanner, "altus_theme_engine",
                                      altus_symbols,
                                      G_N_ELEMENTS (altus_symbols));

  data = g_new0 (AltusRcData, 1);

  token = g_scanner_peek_next_token (scanner);
  while (token != G_TOKEN_RIGHT_CURLY)
    {
      g_scanner_get_next_token (scanner);

      switch (token)
        {
        case TOKEN_XTHICKNESS:
          token = _gtk_theme_parse_int (scanner, &data->xthickness);
          data->flags |= ALTUS_XTHICKNESS_SET;
          break;
        case TOKEN_YTHICKNESS:
          token = _gtk_theme_parse_int (scanner, &data->ythickness);
          data->flags |= ALTUS_YTHICKNESS_SET;
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
altus_merge_rc_style (GtkRcStyle *dest,
                      GtkRcStyle *src)
{
  AltusRcData *src_data = src->engine_data;
  AltusRcData *dest_data = dest->engine_data;

  if (!dest_data)
    dest_data = dest->engine_data = g_new0 (AltusRcData, 1);

  if (!src_data)
    return;

  if (!(dest_data->flags & ALTUS_XTHICKNESS_SET) &&
      (src_data->flags & ALTUS_XTHICKNESS_SET))
    {
      dest_data->xthickness = src_data->xthickness;
      dest_data->flags |= ALTUS_XTHICKNESS_SET;
    }
  if (!(dest_data->flags & ALTUS_YTHICKNESS_SET) &&
      (src_data->flags & ALTUS_YTHICKNESS_SET))
    {
      dest_data->ythickness = src_data->ythickness;
      dest_data->flags |= ALTUS_YTHICKNESS_SET;
    }
}

static void
altus_rc_style_to_style (GtkStyle   *style,
                         GtkRcStyle *rc_style)
{
  AltusRcData *rc_data = rc_style->engine_data;
  gint xthickness = 1;
  gint ythickness = 1;

  if (rc_data && (rc_data->flags & ALTUS_XTHICKNESS_SET))
    xthickness = CLAMP (rc_data->xthickness, 0, 8);
  if (rc_data && (rc_data->flags & ALTUS_YTHICKNESS_SET))
    ythickness = CLAMP (rc_data->ythickness, 0, 8);

  style->klass = altus_get_class (style->klass, xthickness, ythickness);

  /* the border colours of a dark theme, made when the style is attached */
  style->engine_data = g_new0 (AltusStyleData, 1);

  altus_set_props (style);
}

static void
altus_duplicate_style (GtkStyle *dest,
                       GtkStyle *src)
{
  dest->klass = src->klass;
  dest->engine_data = g_new0 (AltusStyleData, 1);

  altus_set_props (dest);
}

static void
altus_realize_style (GtkStyle *style)
{
  AltusStyleData *data = style->engine_data;
  GdkGCValues gc_values;
  gint i;

  if (!data)
    return;

  {
    gdouble hue, sat, light, bg_hue, bg_light, bg_sat;

    altus_rgb_to_hls (&style->bg[GTK_STATE_SELECTED], &hue, &light, &sat);
    altus_rgb_to_hls (&style->bg[GTK_STATE_NORMAL],
                      &bg_hue, &bg_light, &bg_sat);
    sat = MIN (sat, FOCUS_SATURATION);
    if (_gtk_theme_is_dark (style))
      {
        altus_hue_at (hue, sat, MIN (bg_light + FOCUS_LIGHT_LIFT, 1.0),
                      &data->focus[0]);
        altus_hue_at (hue, sat, bg_light * FOCUS_DARK_SCALE, &data->focus[1]);
      }
    else
      {
        altus_hue_at (hue, sat, FOCUS_LIGHT_LIGHT, &data->focus[0]);
        altus_hue_at (hue, sat, FOCUS_LIGHT_DARK, &data->focus[1]);
      }
  }

  for (i = 0; i < 2; i++)
    {
      gdk_colormap_alloc_color (style->colormap, &data->focus[i], FALSE, TRUE);
      gc_values.foreground = data->focus[i];
      data->focus_gc[i] = gtk_gc_get (style->depth, style->colormap,
                                      &gc_values, GDK_GC_FOREGROUND);
    }

  if (!_gtk_theme_is_dark (style))
    return;

  for (i = 0; i < 5; i++)
    {
      data->border[i].red = style->bg[i].red +
        (style->fg[i].red - style->bg[i].red) * DARK_BORDER_MIX;
      data->border[i].green = style->bg[i].green +
        (style->fg[i].green - style->bg[i].green) * DARK_BORDER_MIX;
      data->border[i].blue = style->bg[i].blue +
        (style->fg[i].blue - style->bg[i].blue) * DARK_BORDER_MIX;

      gdk_colormap_alloc_color (style->colormap, &data->border[i],
                                FALSE, TRUE);
      gc_values.foreground = data->border[i];
      data->border_gc[i] = gtk_gc_get (style->depth, style->colormap,
                                       &gc_values, GDK_GC_FOREGROUND);
    }

}

static void
altus_unrealize_style (GtkStyle *style)
{
  AltusStyleData *data = style->engine_data;
  gint i;

  if (!data)
    return;

  for (i = 0; i < 5; i++)
    if (data->border_gc[i])
      {
        gtk_gc_release (data->border_gc[i]);
        data->border_gc[i] = NULL;
      }
  for (i = 0; i < 2; i++)
    if (data->focus_gc[i])
      {
        gtk_gc_release (data->focus_gc[i]);
        data->focus_gc[i] = NULL;
      }
}

void
gtk_theme_altus_init (GtkThemeEngine *engine)
{
  engine->parse_rc_style = altus_parse_rc_style;
  engine->merge_rc_style = altus_merge_rc_style;
  engine->rc_style_to_style = altus_rc_style_to_style;
  engine->duplicate_style = altus_duplicate_style;
  engine->realize_style = altus_realize_style;
  engine->unrealize_style = altus_unrealize_style;
  engine->destroy_rc_style = _gtk_theme_destroy_rc_style;
  engine->destroy_style = _gtk_theme_destroy_style;
}
