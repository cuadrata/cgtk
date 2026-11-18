/* GTK - The GIMP Toolkit
 * Drawing helpers the built-in theme engines share
 *
 * These were the same in two or more of Altus, Gelidus, Fabrica and
 * Unicus; they come from the engines Altus and Gelidus were ported from,
 * gtk-engines 2.20.2's Mist and ThinIce:
 * Mist author: Dave Camp <dave@ximian.com>
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

#include <math.h>
#include <string.h>

#include "gtkbutton.h"
#include "gtkgc.h"
#include "gtkthemecommon.h"
#include "gtkwidget.h"

void
_gtk_theme_clip (GdkGC *gc, GdkRectangle *area)
{
  if (area)
    gdk_gc_set_clip_rectangle (gc, area);
}

void
_gtk_theme_unclip (GdkGC *gc, GdkRectangle *area)
{
  if (area)
    gdk_gc_set_clip_rectangle (gc, NULL);
}

void
_gtk_theme_line (GdkWindow *window, GdkGC *gc, GdkRectangle *area,
                 gint x1, gint y1, gint x2, gint y2)
{
  _gtk_theme_clip (gc, area);
  gdk_draw_line (window, gc, x1, y1, x2, y2);
  _gtk_theme_unclip (gc, area);
}

void
_gtk_theme_dot (GdkWindow *window, GdkRectangle *area,
                GdkGC *gc1, GdkGC *gc2, gint x, gint y)
{
  _gtk_theme_clip (gc1, area);
  gdk_draw_point (window, gc1, x + 1, y + 1);
  gdk_draw_point (window, gc1, x + 1, y);
  gdk_draw_point (window, gc1, x, y + 1);
  _gtk_theme_unclip (gc1, area);

  _gtk_theme_clip (gc2, area);
  gdk_draw_point (window, gc2, x - 1, y - 1);
  gdk_draw_point (window, gc2, x - 1, y);
  gdk_draw_point (window, gc2, x, y - 1);
  _gtk_theme_unclip (gc2, area);
}

void
_gtk_theme_rectangle (GdkWindow *window, GdkGC *gc, GdkRectangle *area,
                      gboolean filled, gint x, gint y, gint width, gint height)
{
  if (width < 0 || height < 0 || (filled && (width == 0 || height == 0)))
    return;

  _gtk_theme_clip (gc, area);
  gdk_draw_rectangle (window, gc, filled, x, y, width, height);
  _gtk_theme_unclip (gc, area);
}

void
_gtk_theme_simple_border (GdkWindow *window, GdkRectangle *area,
                          GdkGC *tl, GdkGC *br,
                          gint x, gint y, gint width, gint height)
{
  if (width <= 0 || height <= 0)
    return;

  _gtk_theme_line (window, tl, area, x, y + height - 1, x, y);
  _gtk_theme_line (window, tl, area, x, y, x + width - 1, y);
  _gtk_theme_line (window, br, area, x, y + height - 1, x + width - 1, y + height - 1);
  _gtk_theme_line (window, br, area, x + width - 1, y + height - 1, x + width - 1, y);
}

void
_gtk_theme_sanitize_size (GdkWindow *window, gint *width, gint *height)
{
  if (*width == -1 && *height == -1)
    gdk_window_get_size (window, width, height);
  else if (*width == -1)
    gdk_window_get_size (window, width, NULL);
  else if (*height == -1)
    gdk_window_get_size (window, NULL, height);
}

/*
 * A button's whole window, inside its border width. GTK+ 1 draws a
 * button that can be the default inside the space for the default's
 * frame, and shrinks a button with the focus; an engine that draws the
 * default and the focus on the button itself, as Windows and Clearlooks
 * did, draws it to these edges. Anything else is left as it is, and so
 * is a button with no window of its own, whose window is its parent's:
 * it is drawn where it was said to be.
 */
void
_gtk_theme_button_rect (GtkWidget *widget, GdkWindow *window, gint *x,
                        gint *y, gint *width, gint *height)
{
  gint border;

  if (!widget || !GTK_IS_BUTTON (widget) || GTK_WIDGET_NO_WINDOW (widget) ||
      window != widget->window)
    return;
  border = GTK_CONTAINER (widget)->border_width;
  *x = 0;
  *y = 0;
  *width = widget->allocation.width - 2 * border;
  *height = widget->allocation.height - 2 * border;
}

/* GTK+ 1 draws a focused entry's or text's frame a pixel in, to leave
 * room for a focus line round it; an engine that shows the focus in the
 * frame itself puts the frame back at the widget's edge */
void
_gtk_theme_entry_frame (GtkWidget *widget, const gchar *detail,
                        gint *x, gint *y, gint *width, gint *height)
{
  if (widget && GTK_WIDGET_HAS_FOCUS (widget) && *x == 1 && *y == 1 &&
      detail && (!strcmp (detail, "entry") || !strcmp (detail, "text")))
    {
      *x -= 1;
      *y -= 1;
      *width += 2;
      *height += 2;
    }
}

/* A toggle button that is down, under the pointer: GTK+ 1 draws it in
 * PRELIGHT, its edges in, and an engine that shows the pointer by the
 * button's fill shows it up again. It is drawn ACTIVE, as when the
 * pointer is away, so that it stays down. */
GtkStateType
_gtk_theme_toggle_state (const gchar *detail, GtkStateType state_type,
                         GtkShadowType shadow_type)
{
  if (state_type == GTK_STATE_PRELIGHT && shadow_type == GTK_SHADOW_IN &&
      detail && !strcmp (detail, "togglebutton"))
    return GTK_STATE_ACTIVE;
  return state_type;
}

/* A shared GC for color scaled by r, g and b; give it back with
 * gtk_gc_release() */
GdkGC *
_gtk_theme_shade_gc (GtkStyle *style, const GdkColor *color,
                     gdouble r, gdouble g, gdouble b)
{
  GdkGCValues values;

  values.foreground.red = MIN (color->red * r, 65535);
  values.foreground.green = MIN (color->green * g, 65535);
  values.foreground.blue = MIN (color->blue * b, 65535);
  gdk_colormap_alloc_color (style->colormap, &values.foreground, FALSE, TRUE);

  return gtk_gc_get (style->depth, style->colormap, &values,
                     GDK_GC_FOREGROUND);
}

/* Whether the style is light text on a dark background */
gboolean
_gtk_theme_is_dark (GtkStyle *style)
{
  const GdkColor *bg = &style->bg[GTK_STATE_NORMAL];

  return (bg->red * 299 + bg->green * 587 + bg->blue * 114) / 1000 < 0x8000;
}

gboolean
_gtk_theme_parent_is (GtkWidget *widget, GtkType type)
{
  return widget && widget->parent &&
         gtk_type_is_a (GTK_OBJECT_TYPE (widget->parent), type);
}

/*
 * klass with xthickness and ythickness. An engine whose thickness a
 * gtkrc can set keeps one copy for each it is asked for in *classes,
 * since a style's class is shared, not freed.
 */
GtkStyleClass *
_gtk_theme_class (GSList **classes, const GtkStyleClass *klass,
                  gint xthickness, gint ythickness)
{
  GtkStyleClass *entry;
  GSList *tmp;

  for (tmp = *classes; tmp; tmp = tmp->next)
    {
      entry = tmp->data;
      if (entry->xthickness == xthickness && entry->ythickness == ythickness)
        return entry;
    }

  entry = g_new (GtkStyleClass, 1);
  *entry = *klass;
  entry->xthickness = xthickness;
  entry->ythickness = ythickness;
  *classes = g_slist_prepend (*classes, entry);

  return entry;
}

/*
 * The scanner in the engine's scope, its words added the first time:
 * returns the scope to go back to after the engine's block. On an
 * error the scope is left as it is, so that the error message can make
 * sense of the engine's words.
 */
guint
_gtk_theme_scope_enter (GScanner *scanner, const gchar *scope,
                        const GtkThemeSymbol *symbols, guint n_symbols)
{
  guint scope_id = g_quark_from_string (scope);
  guint old_scope = g_scanner_set_scope (scanner, scope_id);
  guint i;

  if (!g_scanner_lookup_symbol (scanner, symbols[0].name))
    {
      g_scanner_freeze_symbol_table (scanner);
      for (i = 0; i < n_symbols; i++)
        g_scanner_scope_add_symbol (scanner, scope_id, symbols[i].name,
                                    GINT_TO_POINTER (symbols[i].token));
      g_scanner_thaw_symbol_table (scanner);
    }

  return old_scope;
}

/* "= value" after an option's word: G_TOKEN_NONE, or the token that was
 * wanted instead */
guint
_gtk_theme_parse_equals (GScanner *scanner)
{
  return g_scanner_get_next_token (scanner) == G_TOKEN_EQUAL_SIGN ?
         G_TOKEN_NONE : G_TOKEN_EQUAL_SIGN;
}

guint
_gtk_theme_parse_number (GScanner *scanner, gdouble *value)
{
  guint token = _gtk_theme_parse_equals (scanner);

  if (token != G_TOKEN_NONE)
    return token;
  token = g_scanner_get_next_token (scanner);
  if (token == G_TOKEN_FLOAT)
    *value = scanner->value.v_float;
  else if (token == G_TOKEN_INT)
    *value = scanner->value.v_int;
  else
    return G_TOKEN_FLOAT;
  return G_TOKEN_NONE;
}

guint
_gtk_theme_parse_int (GScanner *scanner, gint *value)
{
  guint token = _gtk_theme_parse_equals (scanner);

  if (token != G_TOKEN_NONE)
    return token;
  if (g_scanner_get_next_token (scanner) != G_TOKEN_INT)
    return G_TOKEN_INT;
  *value = scanner->value.v_int;
  return G_TOKEN_NONE;
}

guint
_gtk_theme_parse_boolean (GScanner *scanner, guint token_true,
                          guint token_false, gboolean *value)
{
  guint token = _gtk_theme_parse_equals (scanner);

  if (token != G_TOKEN_NONE)
    return token;
  token = g_scanner_get_next_token (scanner);
  if (token == token_true)
    *value = TRUE;
  else if (token == token_false)
    *value = FALSE;
  else
    return token_true;
  return G_TOKEN_NONE;
}

guint
_gtk_theme_parse_color (GScanner *scanner, GdkColor *color)
{
  guint token = _gtk_theme_parse_equals (scanner);

  if (token != G_TOKEN_NONE)
    return token;
  return gtk_rc_parse_color (scanner, color);
}

GdkGC *
_gtk_theme_aa_gc (GtkStyle *style, GdkWindow *window, GdkRectangle *area,
                  const GdkColor *color)
{
  GdkGC *gc = gdk_gc_new (window);
  GdkColor c = *color;

  gdk_colormap_alloc_color (style->colormap, &c, FALSE, TRUE);
  gdk_gc_set_foreground (gc, &c);
  gdk_gc_set_antialias (gc, TRUE);
  if (area)
    gdk_gc_set_clip_rectangle (gc, area);

  return gc;
}

void
_gtk_theme_destroy_rc_style (GtkRcStyle *rc_style)
{
  g_free (rc_style->engine_data);
  rc_style->engine_data = NULL;
}

void
_gtk_theme_destroy_style (GtkStyle *style)
{
  g_free (style->engine_data);
  style->engine_data = NULL;
}

void
_gtk_theme_draw_string (GtkStyle     *style,
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

  if (state_type == GTK_STATE_INSENSITIVE)
    {
      _gtk_theme_clip (style->light_gc[state_type], area);
      gdk_draw_string (window, style->font, style->light_gc[state_type],
                       x + 1, y + 1, string);
      _gtk_theme_unclip (style->light_gc[state_type], area);
    }

  _gtk_theme_clip (style->fg_gc[state_type], area);
  gdk_draw_string (window, style->font, style->fg_gc[state_type],
                   x, y, string);
  _gtk_theme_unclip (style->fg_gc[state_type], area);
}

void
_gtk_theme_draw_polygon (GtkStyle      *style,
                         GdkWindow     *window,
                         GtkStateType   state_type,
                         GtkShadowType  shadow_type,
                         GdkRectangle  *area,
                         GtkWidget     *widget,
                         gchar         *detail,
                         GdkPoint      *points,
                         gint           npoints,
                         gint           fill)
{
  static const gdouble pi_over_4 = M_PI / 4;
  static const gdouble pi_3_over_4 = M_PI * 3 / 4;

  GdkGC *gc1, *gc2, *gc3, *gc4;
  gdouble angle;
  gint xadjust;
  gint yadjust;
  gint i;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);
  g_return_if_fail (points != NULL);

  switch (shadow_type)
    {
    case GTK_SHADOW_IN:
      gc1 = style->light_gc[state_type];
      gc2 = style->dark_gc[state_type];
      gc3 = style->light_gc[state_type];
      gc4 = style->dark_gc[state_type];
      break;
    case GTK_SHADOW_ETCHED_IN:
      gc1 = style->light_gc[state_type];
      gc2 = style->dark_gc[state_type];
      gc3 = style->dark_gc[state_type];
      gc4 = style->light_gc[state_type];
      break;
    case GTK_SHADOW_OUT:
      gc1 = style->dark_gc[state_type];
      gc2 = style->light_gc[state_type];
      gc3 = style->dark_gc[state_type];
      gc4 = style->light_gc[state_type];
      break;
    case GTK_SHADOW_ETCHED_OUT:
      gc1 = style->dark_gc[state_type];
      gc2 = style->light_gc[state_type];
      gc3 = style->light_gc[state_type];
      gc4 = style->dark_gc[state_type];
      break;
    default:
      return;
    }

  if (fill)
    {
      _gtk_theme_clip (style->bg_gc[state_type], area);
      gdk_draw_polygon (window, style->bg_gc[state_type], TRUE,
                        points, npoints);
      _gtk_theme_unclip (style->bg_gc[state_type], area);
    }

  npoints--;

  for (i = 0; i < npoints; i++)
    {
      if ((points[i].x == points[i + 1].x) &&
          (points[i].y == points[i + 1].y))
        angle = 0;
      else
        angle = atan2 (points[i + 1].y - points[i].y,
                       points[i + 1].x - points[i].x);

      if ((angle > -pi_3_over_4) && (angle < pi_over_4))
        {
          if (angle > -pi_over_4)
            {
              xadjust = 0;
              yadjust = 1;
            }
          else
            {
              xadjust = 1;
              yadjust = 0;
            }

          _gtk_theme_line (window, gc1, area,
                     points[i].x - xadjust, points[i].y - yadjust,
                     points[i + 1].x - xadjust, points[i + 1].y - yadjust);
          _gtk_theme_line (window, gc3, area,
                     points[i].x, points[i].y,
                     points[i + 1].x, points[i + 1].y);
        }
      else
        {
          if ((angle < -pi_3_over_4) || (angle > pi_3_over_4))
            {
              xadjust = 0;
              yadjust = 1;
            }
          else
            {
              xadjust = 1;
              yadjust = 0;
            }

          _gtk_theme_line (window, gc4, area,
                     points[i].x + xadjust, points[i].y + yadjust,
                     points[i + 1].x + xadjust, points[i + 1].y + yadjust);
          _gtk_theme_line (window, gc2, area,
                     points[i].x, points[i].y,
                     points[i + 1].x, points[i + 1].y);
        }
    }
}

void
_gtk_theme_draw_diamond (GtkStyle      *style,
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
  GdkGC *outer, *inner;
  gint half_width;
  gint half_height;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  half_width = width / 2;
  half_height = height / 2;

  switch (shadow_type)
    {
    case GTK_SHADOW_IN:
      outer = style->light_gc[state_type];
      inner = style->dark_gc[state_type];
      break;
    case GTK_SHADOW_OUT:
      outer = style->dark_gc[state_type];
      inner = style->light_gc[state_type];
      break;
    default:
      return;
    }

  /* the lower half, then the upper */
  _gtk_theme_line (window, outer, area, x + 2, y + half_height,
             x + half_width, y + height - 2);
  _gtk_theme_line (window, outer, area, x + half_width, y + height - 2,
             x + width - 2, y + half_height);
  _gtk_theme_line (window, outer, area, x + 1, y + half_height,
             x + half_width, y + height - 1);
  _gtk_theme_line (window, outer, area, x + half_width, y + height - 1,
             x + width - 1, y + half_height);
  _gtk_theme_line (window, outer, area, x, y + half_height,
             x + half_width, y + height);
  _gtk_theme_line (window, outer, area, x + half_width, y + height,
             x + width, y + half_height);

  _gtk_theme_line (window, inner, area, x + 2, y + half_height,
             x + half_width, y + 2);
  _gtk_theme_line (window, inner, area, x + half_width, y + 2,
             x + width - 2, y + half_height);
  _gtk_theme_line (window, inner, area, x + 1, y + half_height,
             x + half_width, y + 1);
  _gtk_theme_line (window, inner, area, x + half_width, y + 1,
             x + width - 1, y + half_height);
  _gtk_theme_line (window, inner, area, x, y + half_height,
             x + half_width, y);
  _gtk_theme_line (window, inner, area, x + half_width, y,
             x + width, y + half_height);
}

void
_gtk_theme_draw_extension (GtkStyle        *style,
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
  GdkRectangle rect, clip;
  GdkGC *light, *dark;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  light = style->light_gc[state_type];
  dark = style->dark_gc[state_type];

  if (!style->bg_pixmap[state_type] ||
      gdk_window_get_type (window) == GDK_WINDOW_PIXMAP)
    _gtk_theme_rectangle (window, style->bg_gc[state_type], area, TRUE,
                    x, y, width, height);
  else
    gtk_style_apply_default_background (style, window,
                                        widget && !GTK_WIDGET_NO_WINDOW (widget),
                                        state_type, area,
                                        x, y, width, height);

  /* The border goes round the tab and one pixel past it on the side
   * it joins, where it is clipped off: the tab is open on that side. */
  rect.x = x;
  rect.y = y;
  rect.width = width;
  rect.height = height;
  if (area)
    {
      if (!gdk_rectangle_intersect (&rect, area, &clip))
        return;
    }
  else
    clip = rect;

  switch (gap_side)
    {
    case GTK_POS_BOTTOM:
      height++;
      break;
    case GTK_POS_TOP:
      y--;
      height++;
      break;
    case GTK_POS_RIGHT:
      width++;
      break;
    case GTK_POS_LEFT:
      x--;
      width++;
      break;
    }

  _gtk_theme_simple_border (window, &clip, light, dark, x, y, width, height);
}
