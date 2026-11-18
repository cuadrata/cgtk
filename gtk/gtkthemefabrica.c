/* GTK - The GIMP Toolkit
 * Fabrica, a theme engine of soft edges and translucent lines
 *
 * Ported from the Industrial engine of gtk-engines 2.20.2:
 * Copyright (C) 2001 Red Hat, Inc.
 * Copyright (C) 2002 Ximian, Inc.
 * Copyright (C) 2006-2007 Benjamin Berg <benjamin@sipsolutions.net>
 * Written by Owen Taylor <otaylor@redhat.com>, modified by Alexander
 * Larsson <alexl@redhat.com>, Christopher James Lahey
 * <clahey@ximian.com> and Benjamin Berg.
 *
 * The arrows are GTK+ 2.24's, from its gtkstyle.c, which Industrial
 * used for them:
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

/* Industrial drew with cairo: rounded corners, lines of the foreground
 * colour made translucent, soft shadows. Fabrica draws it as GTK+ 1's
 * Industrial did, with GDK: each translucent line as the solid colour
 * it makes over what it lies on, a rounded corner as a few pixels of
 * fainter colour, a button's shadows as rows of fading colour. Only
 * the radio button and the tick, which do not look right otherwise,
 * are worked out antialiased as they are drawn.
 *
 * Where GTK+ 1 widgets draw differently from GTK+ 2 ones -- a
 * scrollbar's slider is a box, its steppers and a spin button's are
 * arrows with a shadow, an option menu has a tab of its own, a paned
 * widget's handle is a box -- the look is reproduced from what GTK+ 1
 * asks for.
 *
 * The engine takes Industrial's options, contrast and rounded_buttons,
 * and, for styles that want a frame thicker than one pixel, xthickness
 * and ythickness:
 *
 *   engine "fabrica" { contrast = 1.0  rounded_buttons = TRUE
 *                      xthickness = 2  ythickness = 2 }
 *
 * Every colour comes from the style.
 */

#include <math.h>
#include <string.h>

#include "gtkbutton.h"
#include "gtkclist.h"
#include "gtkgc.h"
#include "gtkcontainer.h"
#include "gtkmenuitem.h"
#include "gtknotebook.h"
#include "gtkrange.h"
#include "gtkrc.h"
#include "gtkscrollbar.h"
#include "gtkstyle.h"
#include "gtkthemes.h"
#include "gtkthemebuiltin.h"
#include "gtkthemecommon.h"
#include "gtkvscrollbar.h"
#include "gtkwidget.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DETAIL(xx) ((detail) && (!strcmp ((xx), (detail))))

#define XTHICKNESS(style) ((style)->klass->xthickness)
#define YTHICKNESS(style) ((style)->klass->ythickness)

#define LINE_OPACITY 0.4
#define HANDLE_OPACITY 0.38
#define DEFAULT_SHADOW_OPACITY 0.26
#define MENUITEM_BORDER_OPACITY 0.38
#define STANDARD_BORDER_OPACITY 0.38
#define BUTTON_BORDER_OPACITY 0.38
#define TROUGH_BG_OPACITY 0.15
#define TROUGH_BORDER_OPACITY 0.21
#define MENUBAR_BORDER_OPACITY 0.07

/* A handle's grid of dots, taken as upright */
#define HANDLE_HEIGHT (4 * 4 + 1 + 2)
#define HANDLE_WIDTH (4 + 1 + 2)

enum
{
  TOKEN_CONTRAST = G_TOKEN_LAST + 1,
  TOKEN_ROUNDED_BUTTONS,
  TOKEN_XTHICKNESS,
  TOKEN_YTHICKNESS,
  TOKEN_TRUE,
  TOKEN_FALSE
};

static const GtkThemeSymbol fabrica_symbols[] = {
  { "contrast",        TOKEN_CONTRAST },
  { "rounded_buttons", TOKEN_ROUNDED_BUTTONS },
  { "xthickness",      TOKEN_XTHICKNESS },
  { "ythickness",      TOKEN_YTHICKNESS },
  { "TRUE",            TOKEN_TRUE },
  { "FALSE",           TOKEN_FALSE }
};

#define FABRICA_CONTRAST_SET        (1 << 0)
#define FABRICA_ROUNDED_BUTTONS_SET (1 << 1)
#define FABRICA_XTHICKNESS_SET      (1 << 2)
#define FABRICA_YTHICKNESS_SET      (1 << 3)

typedef struct
{
  guint flags;
  gdouble contrast;
  gboolean rounded_buttons;
  gint xthickness;
  gint ythickness;
} FabricaRcData;

static GSList *fabrica_classes = NULL;

/* ---------------------------------------------------------------------
 * Options
 */

static FabricaRcData *
fabrica_rc_data (GtkStyle *style)
{
  return style->rc_style ? style->rc_style->engine_data : NULL;
}

static gdouble
fabrica_opacity (GtkStyle *style, gdouble opacity)
{
  FabricaRcData *data = fabrica_rc_data (style);
  gdouble contrast = 1.0;

  if (data && (data->flags & FABRICA_CONTRAST_SET))
    contrast = data->contrast;

  return CLAMP (opacity * contrast, 0.0, 1.0);
}

static gboolean
fabrica_rounded (GtkStyle *style)
{
  FabricaRcData *data = fabrica_rc_data (style);

  if (data && (data->flags & FABRICA_ROUNDED_BUTTONS_SET))
    return data->rounded_buttons;
  return TRUE;
}

/* ---------------------------------------------------------------------
 * Colours: Industrial's lines are the foreground colour made
 * translucent. Here each is worked out once, as a solid colour over
 * the colour it lies on, and drawn with GDK.
 */

static GdkGC *
fabrica_blend_gc (GtkStyle *style, const GdkColor *color,
                  const GdkColor *under, gdouble alpha)
{
  GdkGCValues values;
  GdkColor blend;

  blend.red = under->red + (color->red - under->red) * alpha;
  blend.green = under->green + (color->green - under->green) * alpha;
  blend.blue = under->blue + (color->blue - under->blue) * alpha;
  gdk_colormap_alloc_color (style->colormap, &blend, FALSE, TRUE);

  values.foreground = blend;
  return gtk_gc_get (style->depth, style->colormap, &values, GDK_GC_FOREGROUND);
}


static void
fabrica_point (GdkWindow *window, GdkGC *gc, GdkRectangle *area,
               gint x, gint y)
{
  _gtk_theme_clip (gc, area);
  gdk_draw_point (window, gc, x, y);
  _gtk_theme_unclip (gc, area);
}

static void
fabrica_fill (GdkWindow *window, GdkGC *gc, GdkRectangle *area,
              gint x, gint y, gint width, gint height)
{
  if (width <= 0 || height <= 0)
    return;
  _gtk_theme_clip (gc, area);
  gdk_draw_rectangle (window, gc, TRUE, x, y, width, height);
  _gtk_theme_unclip (gc, area);
}

/* How round a frame's corners are: square; or, as GTK+ 1's Industrial
 * had them, soft, as on most frames, cut, as on troughs and frames
 * round other widgets, or faint, as on a progress bar's bar; or round,
 * as on buttons. */
enum
{
  FABRICA_CORNER_SQUARE,
  FABRICA_CORNER_SOFT,
  FABRICA_CORNER_CUT,
  FABRICA_CORNER_FAINT,
  FABRICA_CORNER_ROUND
};

/* The radius of the outside of the corner, drawn antialiased: Industrial's
 * radii of 1.5 and 2.5 for its lines' middles */
static gdouble
fabrica_corner_radius (gint corners)
{
  switch (corners)
    {
    case FABRICA_CORNER_ROUND: return 3.0;
    case FABRICA_CORNER_CUT:   return 1.5;
    case FABRICA_CORNER_SOFT:
    case FABRICA_CORNER_FAINT: return 2.0;
    default:                   return 0.0;
    }
}

/* A GC that draws color antialiased, clipped to area */

/* Industrial's frame: a line of color at alpha round x, y, width,
 * height, over fill (NULL for none), made solid over under, the
 * background of the state it is drawn in. Rounded corners are drawn
 * antialiased, over whatever the frame lies on, at the device's
 * resolution. A raised frame has a faint shade inside its bottom and
 * right. */
static void
fabrica_frame (GtkStyle *style, GdkWindow *window, GdkRectangle *area,
               const GdkColor *color, gdouble alpha,
               const GdkColor *fill, const GdkColor *under,
               gint corners, gboolean shade,
               gint x, gint y, gint width, gint height)
{
  const GdkColor *inside = fill ? fill : under;
  gdouble radius = fabrica_corner_radius (corners);
  GdkColor solid;
  GdkGC *gc;

  if (width < 2 || height < 2)
    return;

  solid.red = under->red + (color->red - under->red) * alpha;
  solid.green = under->green + (color->green - under->green) * alpha;
  solid.blue = under->blue + (color->blue - under->blue) * alpha;

  if (radius > 0)
    {
      if (fill)
        {
          gc = _gtk_theme_aa_gc (style, window, area, fill);
          gdk_draw_rounded_rectangle (window, gc, TRUE, x + 1, y + 1,
                                      width - 2, height - 2, radius - 1);
          gdk_gc_unref (gc);
        }

      gc = _gtk_theme_aa_gc (style, window, area, &solid);
      gdk_draw_rounded_rectangle (window, gc, FALSE, x, y, width, height,
                                  radius);
      gdk_gc_unref (gc);
    }
  else
    {
      GdkGC *line = fabrica_blend_gc (style, &solid, &solid, 0);

      if (fill)
        {
          gc = fabrica_blend_gc (style, fill, fill, 0);
          fabrica_fill (window, gc, area, x + 1, y + 1, width - 2, height - 2);
          gtk_gc_release (gc);
        }

      _gtk_theme_line (window, line, area, x, y, x + width - 1, y);
      _gtk_theme_line (window, line, area, x, y + height - 1,
                       x + width - 1, y + height - 1);
      _gtk_theme_line (window, line, area, x, y, x, y + height - 1);
      _gtk_theme_line (window, line, area, x + width - 1, y,
                       x + width - 1, y + height - 1);
      gtk_gc_release (line);
    }

  if (shade && width > 4 && height > 4)
    {
      gc = fabrica_blend_gc (style, color, inside, alpha * 0.1);
      _gtk_theme_line (window, gc, area, x + 2, y + height - 2,
                       x + width - 3, y + height - 2);
      _gtk_theme_line (window, gc, area, x + width - 2, y + 2,
                       x + width - 2, y + height - 3);
      gtk_gc_release (gc);
    }
}

/* The soft shadow inside a button's lower right, as GTK+ 1's
 * Industrial had it: rows and columns fading up and in from the bottom
 * and right, over the part of the button below the line from its lower
 * left corner to its upper right. On a raised button it starts a
 * pixel in from the frame; on a pressed one, against it. */
#define SHADE_DEPTH 7

static void
fabrica_button_shade (GtkStyle *style, GdkWindow *window, GdkRectangle *area,
                      const GdkColor *color, const GdkColor *fill,
                      gboolean pressed, gint x, gint y, gint width, gint height)
{
  static const gdouble alphas[SHADE_DEPTH] = {
    0.37, 0.33, 0.24, 0.16, 0.08, 0.04, 0.04
  };
  GdkGC *gcs[SHADE_DEPTH];
  gint slope = MIN (width / 2, height / 2);
  gint inset = pressed ? 1 : 2;
  gint px, py, k;

  for (k = 0; k < SHADE_DEPTH; k++)
    gcs[k] = fabrica_blend_gc (style, color, fill,
                               fabrica_opacity (style, alphas[k] * 0.1));

  for (py = y + 1; py < y + height - 1; py++)
    for (px = x + 1; px < x + width - 1; px++)
      {
        gint dx = px - x, dy = py - y, d;

        /* below the line from the lower left to the upper right */
        if (dx < slope)
          {
            if (dx + dy < height)
              continue;
          }
        else if (dx > width - slope)
          {
            if (dy - slope < width - slope - dx)
              continue;
          }
        else if (dy < height - slope)
          continue;

        d = MIN (y + height - inset - py, x + width - inset - px);
        if (d < 1 || d > SHADE_DEPTH)
          continue;
        fabrica_point (window, gcs[d - 1], area, px, py);
      }

  for (k = 0; k < SHADE_DEPTH; k++)
    gtk_gc_release (gcs[k]);
}

/* The shadow inside a pressed button's upper left, as GTK+ 1's
 * Industrial had it: two or three pixels deep, above the line from its
 * lower left corner to its upper right */
static void
fabrica_button_inner_shade (GtkStyle *style, GdkWindow *window,
                            GdkRectangle *area, const GdkColor *color,
                            const GdkColor *fill,
                            gint x, gint y, gint width, gint height)
{
  static const gdouble alphas[3] = { 1.38, 0.645, 0.23 };
  GdkGC *gcs[3];
  gint slope = MIN (width / 2, height / 2);
  gint px, py, k;

  for (k = 0; k < 3; k++)
    gcs[k] = fabrica_blend_gc (style, color, fill,
                               fabrica_opacity (style, alphas[k] * 0.1));

  for (py = y + 1; py < y + height - 1; py++)
    for (px = x + 1; px < x + width - 1; px++)
      {
        gint dx = px - x, dy = py - y, d;

        /* above the line from the lower left to the upper right, and
         * not on the frame's corners */
        if (dx < slope)
          {
            if (dx + dy > height)
              continue;
          }
        else if (dx > width - slope)
          {
            if (dy - slope > width - slope - dx)
              continue;
          }
        else if (dy > height - slope)
          continue;
        if ((dx == 1 && dy == 1) || (dx == width - 2 && dy == 1) ||
            (dx == 1 && dy == height - 2))
          continue;

        /* two deep along the top, three down the left */
        d = MIN (dy, dx);
        if (d < 1 || d > 3 || (d == 3 && dx != 3))
          continue;
        fabrica_point (window, gcs[d - 1], area, px, py);
      }

  for (k = 0; k < 3; k++)
    gtk_gc_release (gcs[k]);
}

/* An arrow as GTK+ 1's Industrial drew it: its base the width less two
 * pixels, made odd, and half that high, in the middle of the space it
 * is given, its sides at forty-five degrees -- drawn antialiased, so as
 * fine as the display. */
static void
fabrica_arrow (GtkStyle *style, GdkWindow *window, GdkRectangle *area,
               const GdkColor *color, GtkArrowType arrow_type,
               gint x, gint y, gint width, gint height)
{
  GdkPointF points[3];
  gdouble cx, cy;
  gint base, depth;
  GdkGC *gc;

  if (arrow_type == GTK_ARROW_UP || arrow_type == GTK_ARROW_DOWN)
    base = MIN (width - 2, 2 * height - 3);
  else
    base = MIN (height - 2, 2 * width - 3);
  base -= (base % 2 == 0);
  if (base < 1)
    return;
  depth = base / 2 + 1;

  if (arrow_type == GTK_ARROW_UP || arrow_type == GTK_ARROW_DOWN)
    {
      cx = x + (width - base) / 2 + base / 2.0;
      y += (height - depth) / 2;
      points[0].x = cx;         points[0].y = arrow_type == GTK_ARROW_UP ? y : y + depth;
      points[1].x = cx - depth; points[1].y = arrow_type == GTK_ARROW_UP ? y + depth : y;
      points[2].x = cx + depth; points[2].y = points[1].y;
    }
  else
    {
      cy = y + (height - base) / 2 + base / 2.0;
      x += (width - depth) / 2;
      points[0].y = cy;         points[0].x = arrow_type == GTK_ARROW_LEFT ? x : x + depth;
      points[1].y = cy - depth; points[1].x = arrow_type == GTK_ARROW_LEFT ? x + depth : x;
      points[2].y = cy + depth; points[2].x = points[1].x;
    }

  gc = _gtk_theme_aa_gc (style, window, area, color);
  gdk_draw_polygon_f (window, gc, TRUE, points, 3);
  gdk_gc_unref (gc);
}

/* The grid of dots on a handle, in the middle of x, y, width, height */
static void
fabrica_grid (GtkStyle *style, GdkWindow *window, GdkRectangle *area,
              GtkStateType state_type, gint x, gint y, gint width, gint height,
              gint maxwidth, gint maxheight, GtkOrientation orientation)
{
  gdouble alpha = fabrica_opacity (style, HANDLE_OPACITY);
  gint handle_width, handle_height, xpos, ypos;
  gboolean indented = FALSE;
  GdkGC *centre, *edge;

  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      handle_width = MIN (maxwidth, HANDLE_HEIGHT);
      handle_height = MIN (maxheight, HANDLE_WIDTH);
    }
  else
    {
      handle_width = MIN (maxwidth, HANDLE_WIDTH);
      handle_height = MIN (maxheight, HANDLE_HEIGHT);
    }

  if (handle_width <= 0 || handle_height <= 0)
    return;

  x += (width - handle_width) / 2;
  y += (height - handle_height) / 2;

  centre = fabrica_blend_gc (style, &style->fg[state_type],
                             &style->bg[state_type], alpha);
  edge = fabrica_blend_gc (style, &style->fg[state_type],
                           &style->bg[state_type], alpha * 0.5);

  for (ypos = y + 1; ypos < y + handle_height; ypos += 2)
    {
      for (xpos = x + (indented ? 3 : 1); xpos < x + handle_width; xpos += 4)
        {
          fabrica_point (window, edge, area, xpos - 1, ypos);
          fabrica_point (window, edge, area, xpos + 1, ypos);
          fabrica_point (window, edge, area, xpos, ypos - 1);
          fabrica_point (window, edge, area, xpos, ypos + 1);
          fabrica_point (window, centre, area, xpos, ypos);
        }
      indented = !indented;
    }

  gtk_gc_release (centre);
  gtk_gc_release (edge);
}

/* The grid on a scrollbar's slider: Industrial's, but kept as far from
 * the slider's ends as from its sides, and on a short slider cut down
 * to the dots that fit whole -- rows two pixels apart down a vertical
 * one, the staggered columns four apart along a horizontal one */
static void
fabrica_slider_grid (GtkStyle *style, GdkWindow *window, GdkRectangle *area,
                     GtkStateType state_type, gint x, gint y, gint width,
                     gint height, gboolean vertical)
{
  gint across = vertical ? width : height;
  gint along = vertical ? height : width;
  gint room = along - 2 * MAX ((across - HANDLE_WIDTH) / 2, 1);

  room = MIN (room, HANDLE_HEIGHT);
  if (room < 3)
    return;
  if (vertical)
    room -= 1 - room % 2;
  else
    room = 4 * ((room - 3) / 4) + 3;

  if (vertical)
    fabrica_grid (style, window, area, state_type, x, y, width, height,
                  width - 2, room, GTK_ORIENTATION_VERTICAL);
  else
    fabrica_grid (style, window, area, state_type, x, y, width, height,
                  room, height - 2, GTK_ORIENTATION_HORIZONTAL);
}

/* ---------------------------------------------------------------------
 * Drawing
 */

/* Text, embossed when insensitive as GTK+ draws it, but in the light
 * colour rather than white, so that it does not glare on a dark theme */

static void
fabrica_draw_hline (GtkStyle     *style,
                    GdkWindow    *window,
                    GtkStateType  state_type,
                    GdkRectangle *area,
                    GtkWidget    *widget,
                    gchar        *detail,
                    gint          x1,
                    gint          x2,
                    gint          y)
{
  GdkGC *gc;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  /* GTK+ 1 draws a menu separator along the top of the item; across
   * the middle here, inside the item's frame */
  if (DETAIL ("menuitem") && widget && GTK_IS_MENU_ITEM (widget) &&
      !GTK_BIN (widget)->child)
    {
      x1 += XTHICKNESS (style);
      x2 -= XTHICKNESS (style) + 1;
      y = (widget->allocation.height - 1) / 2;
    }

  gc = fabrica_blend_gc (style, &style->fg[state_type], &style->bg[state_type],
                         fabrica_opacity (style, LINE_OPACITY));
  _gtk_theme_line (window, gc, area, x1, y, x2, y);
  gtk_gc_release (gc);
}

static void
fabrica_draw_vline (GtkStyle     *style,
                    GdkWindow    *window,
                    GtkStateType  state_type,
                    GdkRectangle *area,
                    GtkWidget    *widget,
                    gchar        *detail,
                    gint          y1,
                    gint          y2,
                    gint          x)
{
  GdkGC *gc;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  gc = fabrica_blend_gc (style, &style->fg[state_type], &style->bg[state_type],
                         fabrica_opacity (style, LINE_OPACITY));
  _gtk_theme_line (window, gc, area, x, y1, x, y2);
  gtk_gc_release (gc);
}

/* A button or toggle button, whose focus is shown in its frame rather
 * than drawn round it. A list's column titles are buttons too, but
 * drawn flat: they are left as they are. */
static gboolean
fabrica_focus_inside (GtkWidget *widget, const gchar *detail)
{
  return (DETAIL ("button") || DETAIL ("togglebutton")) &&
    widget && GTK_IS_BUTTON (widget) &&
    !(widget->parent && GTK_IS_CLIST (widget->parent));
}

static void
fabrica_real_draw_box (GtkStyle      *style,
                       GdkWindow     *window,
                       GtkStateType   state_type,
                       GtkShadowType  shadow_type,
                       GdkRectangle  *area,
                       GtkWidget     *widget,
                       gchar         *detail,
                       gint           x,
                       gint           y,
                       gint           width,
                       gint           height,
                       gboolean       fill)
{
  const GdkColor *fg = &style->fg[state_type];
  const GdkColor *bg = &style->bg[state_type];
  const GdkColor *under = &style->bg[state_type];
  gint rounded = fabrica_rounded (style) ? FABRICA_CORNER_SOFT :
    FABRICA_CORNER_SQUARE;

  if (DETAIL ("button") && widget && widget->parent &&
      GTK_IS_CLIST (widget->parent))
    {
      /* A list's column title: flat, a line below it, a short line at
       * its end */
      GdkGC *gc = fabrica_blend_gc (style, fg, bg,
                                    fabrica_opacity (style, BUTTON_BORDER_OPACITY));

      fabrica_fill (window, style->bg_gc[state_type], area, x, y, width, height);
      _gtk_theme_line (window, gc, area, x, y + height - 1,
                       x + width - 1, y + height - 1);
      /* the short line at a title's end, but none where the title
       * reaches the list's frame, which is a line already */
      if (height > 11 && widget->allocation.x + widget->allocation.width <
          GTK_CLIST (widget->parent)->column_title_area.width)
        _gtk_theme_line (window, gc, area, x + width - 2, y + 5,
                         x + width - 2, y + height - 6);
      gtk_gc_release (gc);
    }
  else if (DETAIL ("button") || DETAIL ("togglebutton") ||
           DETAIL ("optionmenu"))
    {
      gdouble alpha = (widget && GTK_WIDGET_HAS_DEFAULT (widget)) ?
        1.0 : BUTTON_BORDER_OPACITY;
      const GdkColor *frame_color = fg;

      alpha = fabrica_opacity (style, alpha);

      /* a focused button's frame is in the selection colour, solid
       * whatever the contrast: no line is added, so however small the
       * button nothing crowds its label */
      if (fabrica_focus_inside (widget, detail) && GTK_WIDGET_HAS_FOCUS (widget))
        {
          frame_color = &style->bg[GTK_STATE_SELECTED];
          alpha = 1.0;
        }

      fabrica_frame (style, window, area, frame_color, alpha,
                     bg, under,
                     rounded ? FABRICA_CORNER_ROUND : FABRICA_CORNER_SQUARE,
                     FALSE, x, y, width, height);

      /* shadows are black, not the foreground colour, which on a dark
       * theme is light */
      if (shadow_type != GTK_SHADOW_NONE && width > 18 && height > 18)
        fabrica_button_shade (style, window, area, &style->black, bg,
                              shadow_type == GTK_SHADOW_IN,
                              x, y, width, height);
      if (shadow_type == GTK_SHADOW_IN)
        fabrica_button_inner_shade (style, window, area, &style->black, bg,
                                    x, y, width, height);


      /* a line inside an option menu, before its indicator */
      if (DETAIL ("optionmenu"))
        {
          gint ind_width, left, right, vline_x;

          ind_width = gtk_style_get_prop_experimental (style,
                                                       "GtkOptionMenu::indicator_width", 7);
          left = gtk_style_get_prop_experimental (style,
                                                  "GtkOptionMenu::indicator_left_spacing", 7);
          right = gtk_style_get_prop_experimental (style,
                                                   "GtkOptionMenu::indicator_right_spacing", 5);

          vline_x = x + width - (ind_width + left + right + XTHICKNESS (style) - 2);
          fabrica_draw_vline (style, window, state_type, area, widget, detail,
                              y + YTHICKNESS (style) + 4,
                              y + height - YTHICKNESS (style) - 5, vline_x);
        }
    }
  else if (DETAIL ("buttondefault") || DETAIL ("togglebuttondefault"))
    {
      /* the default button has its dark frame; nothing round it */
    }
  else if (DETAIL ("menuitem"))
    {
      fabrica_frame (style, window, area, &style->fg[GTK_STATE_NORMAL],
                     fabrica_opacity (style, MENUITEM_BORDER_OPACITY),
                     fill ? bg : NULL, under, rounded, FALSE,
                     x, y, width, height);
    }
  else if (DETAIL ("trough"))
    {
      /* a trough is drawn active: as normal here, darkened a little */
      GdkColor trough;
      gdouble a = fabrica_opacity (style, TROUGH_BG_OPACITY);

      if (state_type == GTK_STATE_ACTIVE)
        state_type = GTK_STATE_NORMAL;
      fg = &style->fg[state_type];
      under = &style->bg[state_type];

      trough.red = under->red + (fg->red - under->red) * a;
      trough.green = under->green + (fg->green - under->green) * a;
      trough.blue = under->blue + (fg->blue - under->blue) * a;

      /* What lies outside the rounded corners is the background: a
       * progress bar draws into a pixmap of its own, with nothing in it
       * yet. */
      fabrica_fill (window, style->bg_gc[state_type], area,
                    x, y, width, height);

      fabrica_frame (style, window, area, fg,
                     fabrica_opacity (style, TROUGH_BORDER_OPACITY),
                     &trough, under,
                     rounded ? FABRICA_CORNER_CUT : FABRICA_CORNER_SQUARE,
                     FALSE, x, y, width, height);
    }
  else if (DETAIL ("menu") || DETAIL ("handlebox_bin") ||
           DETAIL ("toolbar") || DETAIL ("dockitem") || DETAIL ("dockitem_bin"))
    {
      /* A handle box lays out what it holds right up to its edges,
       * whatever its frame: so it has none, only its background. */
      if (DETAIL ("handlebox_bin"))
        {
          if (fill)
            fabrica_fill (window, style->bg_gc[state_type], area,
                          x, y, width, height);
          return;
        }

      /* A menu is a window of its own, with nothing to see past its
       * corners if they were round: square, then. */
      if (fill)
        fabrica_frame (style, window, area, fg,
                       fabrica_opacity (style, TROUGH_BORDER_OPACITY),
                       bg, under,
                       DETAIL ("menu") ? FABRICA_CORNER_SQUARE : rounded,
                       FALSE, x, y, width, height);
    }
  else if (DETAIL ("menubar"))
    {
      if (shadow_type == GTK_SHADOW_NONE)
        return;
      fabrica_frame (style, window, area, fg,
                     fabrica_opacity (style, MENUBAR_BORDER_OPACITY),
                     fill ? bg : NULL, under, rounded, FALSE,
                     x, y, width, height);
    }
  else if (DETAIL ("slider") || DETAIL ("vscrollbar") ||
           DETAIL ("hscrollbar"))
    {
      fabrica_frame (style, window, area, fg, fabrica_opacity (style, 0.5),
                     fill ? bg : NULL, under, rounded, TRUE,
                     x, y, width, height);
    }
  else if (DETAIL ("entry"))
    {
      fabrica_frame (style, window, area, fg,
                     fabrica_opacity (style, STANDARD_BORDER_OPACITY),
                     &style->base[state_type], under, rounded, FALSE,
                     x, y, width, height);
    }
  else if (DETAIL ("bar"))
    {
      /* a progress bar's bar: a pixel in from the trough's frame, in
       * the selection colour, its line made over that colour */
      const GdkColor *blue = &style->bg[GTK_STATE_SELECTED];

      fabrica_frame (style, window, area, fg, fabrica_opacity (style, 0.33),
                     blue, blue,
                     rounded ? FABRICA_CORNER_FAINT : FABRICA_CORNER_SQUARE,
                     FALSE, x + 1, y + 1, width - 2, height - 2);
    }
  else
    {
      if (fill && shadow_type == GTK_SHADOW_NONE)
        fabrica_fill (window, style->bg_gc[state_type], area,
                      x, y, width, height);
      else if (shadow_type != GTK_SHADOW_NONE)
        /* A frame round other widgets -- a list, a scrolled window, a
         * frame -- has cut corners: the pixel inside a soft corner is
         * the child's, which would draw over it. */
        fabrica_frame (style, window, area, fg,
                       fabrica_opacity (style, STANDARD_BORDER_OPACITY),
                       fill ? bg : NULL, under,
                       fill ? rounded :
                       (rounded ? FABRICA_CORNER_CUT : FABRICA_CORNER_SQUARE),
                       FALSE, x, y, width, height);
    }
}

static void
fabrica_draw_box (GtkStyle      *style,
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
  state_type = _gtk_theme_toggle_state (detail, state_type, shadow_type);

  /* GTK+ 1 draws a paned widget's handle as a box; Industrial drew no
   * box, only the grid on the window's background */
  if (DETAIL ("hpaned") || DETAIL ("vpaned"))
    {
      _gtk_theme_rectangle (window, style->bg_gc[state_type], area, TRUE,
                            x, y, width, height);
      fabrica_grid (style, window, area, state_type, x, y, width, height,
                    width, height,
                    DETAIL ("hpaned") ? GTK_ORIENTATION_VERTICAL :
                    GTK_ORIENTATION_HORIZONTAL);
      return;
    }

  /* GTK+ 1 shrinks a focused button by a pixel all round, to draw the
   * focus outside it: here it keeps its size, and the focus is shown in
   * the colour of its frame (fabrica_real_draw_box). */
  if (fabrica_focus_inside (widget, detail) && GTK_WIDGET_HAS_FOCUS (widget))
    {
      x -= 1;
      y -= 1;
      width += 2;
      height += 2;
    }

  fabrica_real_draw_box (style, window, state_type, shadow_type, area,
                         widget, detail, x, y, width, height, TRUE);

  /* GTK+ 1 draws a scrollbar's slider as a box: Industrial's slider
   * has a grid on it */
  if (DETAIL ("slider") && widget && GTK_IS_SCROLLBAR (widget))
    fabrica_slider_grid (style, window, area, state_type, x, y, width, height,
                         GTK_IS_VSCROLLBAR (widget));
}

static void
fabrica_draw_shadow (GtkStyle      *style,
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

  fabrica_real_draw_box (style, window, state_type, shadow_type, area,
                         widget, detail, x, y, width, height, FALSE);
}

static void
fabrica_real_draw_box_gap (GtkStyle        *style,
                           GdkWindow       *window,
                           GtkStateType     state_type,
                           GtkShadowType    shadow_type,
                           GdkRectangle    *area,
                           GtkWidget       *widget,
                           gint             x,
                           gint             y,
                           gint             width,
                           gint             height,
                           GtkPositionType  gap_side,
                           gint             gap_x,
                           gint             gap_width,
                           gboolean         fill)
{
  const GdkColor *bg = &style->bg[state_type];
  GdkGC *gc;
  gint gap_maxwidth;

  if (shadow_type == GTK_SHADOW_NONE)
    return;

  gap_maxwidth = (gap_side == GTK_POS_TOP || gap_side == GTK_POS_BOTTOM) ?
    width : height;

  gap_x -= 1;
  gap_width += 2;
  if (gap_x < 0)
    {
      gap_width += gap_x;
      gap_x = 0;
    }
  if (gap_x + gap_width > gap_maxwidth)
    gap_width = gap_maxwidth - gap_x;

  /* the page, filled out to its corners, so that its rounded corners
   * are made over it */
  if (fill)
    fabrica_fill (window, style->bg_gc[state_type], area, x, y, width, height);

  fabrica_frame (style, window, area, &style->fg[state_type],
                 fabrica_opacity (style, STANDARD_BORDER_OPACITY),
                 fill ? bg : NULL, &style->bg[state_type],
                 fabrica_rounded (style), FALSE, x, y, width, height);

  /* Where the gap, or any tab, reaches a corner, that corner is square,
   * so that the tab's side runs straight on into the page's. */
  if (fabrica_rounded (style))
    {
      GdkGC *line = fabrica_blend_gc (style, &style->fg[state_type], bg,
                                      fabrica_opacity (style,
                                                       STANDARD_BORDER_OPACITY));
      GdkGC *inside = fabrica_blend_gc (style, bg, bg, 0);
      gboolean start = gap_x <= 0;
      gboolean end = gap_x + gap_width >= gap_maxwidth;
      gboolean along = gap_side == GTK_POS_TOP || gap_side == GTK_POS_BOTTOM;
      gint first = along ? x : y, last = first + gap_maxwidth;
      gint x2 = x + width - 1, y2 = y + height - 1;
      gint cx[2], cy[2], dx[2], dy[2], n = 0, k;

      /* any tab reaching a corner squares it too */
      if (widget && GTK_IS_NOTEBOOK (widget))
        {
          GList *list;

          for (list = GTK_NOTEBOOK (widget)->children; list; list = list->next)
            {
              GtkNotebookPage *page = list->data;
              gint a, b;

              if (!page->tab_label || !GTK_WIDGET_MAPPED (page->tab_label))
                continue;
              a = along ? page->allocation.x : page->allocation.y;
              b = a + (along ? page->allocation.width : page->allocation.height);
              if (a <= first + 1)
                start = TRUE;
              if (b >= last - 1)
                end = TRUE;
            }
        }

      /* the corners to square: the corner pixel and the way in from it */

      switch (gap_side)
        {
        case GTK_POS_TOP:
          if (start) { cx[n] = x;  cy[n] = y;  dx[n] = 1;  dy[n] = 1;  n++; }
          if (end)   { cx[n] = x2; cy[n] = y;  dx[n] = -1; dy[n] = 1;  n++; }
          break;
        case GTK_POS_BOTTOM:
          if (start) { cx[n] = x;  cy[n] = y2; dx[n] = 1;  dy[n] = -1; n++; }
          if (end)   { cx[n] = x2; cy[n] = y2; dx[n] = -1; dy[n] = -1; n++; }
          break;
        case GTK_POS_LEFT:
          if (start) { cx[n] = x;  cy[n] = y;  dx[n] = 1;  dy[n] = 1;  n++; }
          if (end)   { cx[n] = x;  cy[n] = y2; dx[n] = 1;  dy[n] = -1; n++; }
          break;
        case GTK_POS_RIGHT:
          if (start) { cx[n] = x2; cy[n] = y;  dx[n] = -1; dy[n] = 1;  n++; }
          if (end)   { cx[n] = x2; cy[n] = y2; dx[n] = -1; dy[n] = -1; n++; }
          break;
        }

      /* over the rounded corner, the two pixels of each side nearest
       * it and the fill inside them: all the rounding touched */
      for (k = 0; k < n; k++)
        {
          _gtk_theme_line (window, line, area, cx[k], cy[k],
                           cx[k] + dx[k], cy[k]);
          _gtk_theme_line (window, line, area, cx[k], cy[k],
                           cx[k], cy[k] + dy[k]);
          if (fill)
            fabrica_point (window, inside, area, cx[k] + dx[k], cy[k] + dy[k]);
        }

      gtk_gc_release (line);
      gtk_gc_release (inside);
    }

  /* the gap, filled over, with a dot of the line left at each end */
  gc = fabrica_blend_gc (style, bg, bg, 0);
  switch (gap_side)
    {
    case GTK_POS_TOP:
      fabrica_fill (window, gc, area, x + gap_x + 1, y, gap_width - 2, 1);
      break;
    case GTK_POS_BOTTOM:
      fabrica_fill (window, gc, area, x + gap_x + 1, y + height - 1,
                    gap_width - 2, 1);
      break;
    case GTK_POS_LEFT:
      fabrica_fill (window, gc, area, x, y + gap_x + 1, 1, gap_width - 2);
      break;
    case GTK_POS_RIGHT:
      fabrica_fill (window, gc, area, x + width - 1, y + gap_x + 1,
                    1, gap_width - 2);
      break;
    }
  gtk_gc_release (gc);
}

static void
fabrica_draw_box_gap (GtkStyle        *style,
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
  fabrica_real_draw_box_gap (style, window, state_type, shadow_type, area,
                             widget, x, y, width, height, gap_side, gap_x, gap_width,
                             TRUE);
}

static void
fabrica_draw_shadow_gap (GtkStyle        *style,
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
  fabrica_real_draw_box_gap (style, window, state_type, shadow_type, area,
                             widget, x, y, width, height, gap_side, gap_x, gap_width,
                             FALSE);
}

static void
fabrica_draw_extension (GtkStyle        *style,
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

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  if (shadow_type == GTK_SHADOW_NONE)
    return;

  _gtk_theme_sanitize_size (window, &width, &height);

  /* the tab, running on past its open side, cut off there */
  rect.x = x;
  rect.y = y;
  rect.width = width;
  rect.height = height;
  if (area && !gdk_rectangle_intersect (&rect, area, &clip))
    return;
  if (!area)
    clip = rect;

  switch (gap_side)
    {
    case GTK_POS_TOP:    y -= 3; height += 3; break;
    case GTK_POS_BOTTOM: height += 3;         break;
    case GTK_POS_LEFT:   x -= 3; width += 3;  break;
    case GTK_POS_RIGHT:  width += 3;          break;
    }

  fabrica_frame (style, window, &clip, &style->fg[state_type],
                 fabrica_opacity (style, STANDARD_BORDER_OPACITY),
                 &style->bg[state_type], &style->bg[state_type],
                 fabrica_rounded (style), FALSE, x, y, width, height);
}

static void
fabrica_draw_focus (GtkStyle     *style,
                    GdkWindow    *window,
                    GdkRectangle *area,
                    GtkWidget    *widget,
                    gchar        *detail,
                    gint          x,
                    gint          y,
                    gint          width,
                    gint          height)
{
  static gint8 dotted[2] = { 1, 1 };
  GtkStateType state_type;
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

  /* an entry's focus: a line two pixels wide inside its frame, in the
   * selection colour */
  if (DETAIL ("entry"))
    {
      gc = fabrica_blend_gc (style, &style->bg[GTK_STATE_SELECTED],
                             &style->bg[GTK_STATE_SELECTED], 0);
      _gtk_theme_clip (gc, area);
      gdk_draw_rectangle (window, gc, FALSE, x + 1, y + 1, width - 2, height - 2);
      gdk_draw_rectangle (window, gc, FALSE, x + 2, y + 2, width - 4, height - 4);
      _gtk_theme_unclip (gc, area);
      gtk_gc_release (gc);
      return;
    }

  /* a button's focus is in the colour of its frame */
  if (fabrica_focus_inside (widget, detail))
    return;

  /* GTK+ 2's focus rectangle: dotted, in the foreground colour */
  state_type = widget ? GTK_WIDGET_STATE (widget) : GTK_STATE_NORMAL;
  gc = style->fg_gc[state_type];

  _gtk_theme_clip (gc, area);
  gdk_gc_set_line_attributes (gc, 1, GDK_LINE_ON_OFF_DASH, 0, 0);
  gdk_gc_set_dashes (gc, 0, dotted, 2);
  gdk_draw_rectangle (window, gc, FALSE, x, y, width, height);
  gdk_gc_set_line_attributes (gc, 1, GDK_LINE_SOLID, 0, 0);
  _gtk_theme_unclip (gc, area);
}

static void
fabrica_draw_slider (GtkStyle       *style,
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

  gtk_paint_box (style, window, state_type, shadow_type, area, widget,
                 detail, x, y, width, height);
  fabrica_grid (style, window, area, state_type, x, y, width, height,
                width - 2, height - 2, orientation);
}

static void
fabrica_draw_handle (GtkStyle       *style,
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

  /* A handle box's handle has no frame of its own, inside the handle
   * box's: just the grid. */
  if (DETAIL ("handlebox"))
    {
      fabrica_grid (style, window, area, state_type, x, y, width, height,
                    width - 2, height - 2, orientation);
      return;
    }

  gtk_paint_box (style, window, state_type, shadow_type, area, widget,
                 detail, x, y, width, height);

  if (!DETAIL ("paned"))
    {
      x += 2;
      y += 2;
      width -= 4;
      height -= 4;
    }

  fabrica_grid (style, window, area, state_type, x, y, width, height,
                shadow_type == GTK_SHADOW_NONE ? width : width - 2,
                shadow_type == GTK_SHADOW_NONE ? height : height - 2,
                orientation);
}

/* Industrial's tick, in a seven by seven square, as a polygon: its
 * curves divided into straight lines */
static gint
fabrica_tick (gdouble x, gdouble y, gdouble scale, gdouble *points)
{
  static const gdouble path[][6] = {
    { 0.0, 4.1 }, { 2.8, 6.65 },
    { 3.7, 5.2, 5.0, 2.65, 6.9, 1.4 },
    { 6.4, 0.6 },
    { 4.5, 1.9, 3.55, 3.4, 2.6, 4.7 },
    { 0.8, 3.0 }
  };
  gdouble px, py;
  gint n = 0, i, k;

  points[2 * n] = x + path[0][0] * scale;
  points[2 * n + 1] = y + path[0][1] * scale;
  n++;
  points[2 * n] = x + path[1][0] * scale;
  points[2 * n + 1] = y + path[1][1] * scale;
  n++;
  px = path[1][0];
  py = path[1][1];

  for (k = 2; k <= 4; k += 2)
    {
      const gdouble *c = path[k];

      for (i = 1; i <= 8; i++)
        {
          gdouble t = i / 8.0, u = 1 - t;

          points[2 * n] = x + scale *
            (u * u * u * px + 3 * u * u * t * c[0] + 3 * u * t * t * c[2] +
             t * t * t * c[4]);
          points[2 * n + 1] = y + scale *
            (u * u * u * py + 3 * u * u * t * c[1] + 3 * u * t * t * c[3] +
             t * t * t * c[5]);
          n++;
        }
      points[2 * n] = x + path[k + 1][0] * scale;
      points[2 * n + 1] = y + path[k + 1][1] * scale;
      n++;
      px = path[k + 1][0];
      py = path[k + 1][1];
    }

  return n;
}

/* The colours of an indicator: base and text when it is normal, the
 * state's bg and fg otherwise */
static void
fabrica_indicator_colors (GtkStyle *style, GtkWidget *widget,
                          GtkStateType *state_type,
                          const GdkColor **fg, const GdkColor **bg)
{
  /* GTK+ 1 draws an active indicator in the active state even when it
   * is insensitive; GTK+ 2 in the insensitive one */
  if (widget && !GTK_WIDGET_IS_SENSITIVE (widget))
    *state_type = GTK_STATE_INSENSITIVE;
  else if (*state_type == GTK_STATE_ACTIVE)
    *state_type = GTK_STATE_NORMAL;
  /* in a highlighted menu item, as in any other: its highlight's
   * colours are for the label, and an indicator in them is lost */
  else if (*state_type == GTK_STATE_PRELIGHT && widget &&
           GTK_IS_MENU_ITEM (widget))
    *state_type = GTK_STATE_NORMAL;

  if (*state_type == GTK_STATE_NORMAL)
    {
      *fg = &style->text[*state_type];
      *bg = &style->base[*state_type];
    }
  else
    {
      *fg = &style->fg[*state_type];
      *bg = &style->bg[*state_type];
    }
}

static void
fabrica_draw_check (GtkStyle      *style,
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
  const GdkColor *fg, *bg;
  gdouble tick[2 * 24];
  gint size;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);


  fabrica_indicator_colors (style, widget, &state_type, &fg, &bg);

  size = MIN (width, height);
  x += (width - size) / 2;
  y += (height - size) / 2;

  fabrica_frame (style, window, area, fg,
                 fabrica_opacity (style, DEFAULT_SHADOW_OPACITY),
                 bg, &style->bg[state_type], fabrica_rounded (style),
                 FALSE, x, y, size, size);


  if (shadow_type == GTK_SHADOW_IN && size > 4)
    {
      GdkPointF points[24];
      GdkGC *gc = _gtk_theme_aa_gc (style, window, area, fg);
      gint n, i;

      n = fabrica_tick (x + 2.0, y + 2.0, (size - 4) / 7.0, tick);
      for (i = 0; i < n; i++)
        {
          points[i].x = tick[2 * i];
          points[i].y = tick[2 * i + 1];
        }
      /* solid whatever the contrast, which is for lines and edges: a
       * faded tick reads as an insensitive one */
      gdk_draw_polygon_f (window, gc, TRUE, points, n);
      gdk_gc_unref (gc);
    }
}

static void
fabrica_draw_option (GtkStyle      *style,
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
  const GdkColor *fg, *bg;
  GdkGC *gc;
  gint size;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);
  fabrica_indicator_colors (style, widget, &state_type, &fg, &bg);

  size = MIN (width, height);
  x += (width - size) / 2;
  y += (height - size) / 2;

  /* the disc, and a ring round it */
  gc = _gtk_theme_aa_gc (style, window, area, bg);
  gdk_draw_arc (window, gc, TRUE, x, y, size, size, 0, 360 * 64);
  gdk_gc_unref (gc);

  gc = _gtk_theme_aa_gc (style, window, area, fg);
  gdk_gc_set_alpha (gc, fabrica_opacity (style, 0.5));
  gdk_draw_arc (window, gc, FALSE, x, y, size, size, 0, 360 * 64);

  /* on: a dot, lit on its upper left; solid whatever the contrast, as
   * the check box's tick is */
  if (shadow_type == GTK_SHADOW_IN && size > 6)
    {
      gdk_gc_set_alpha (gc, 1.0);
      gdk_draw_arc (window, gc, TRUE, x + 3, y + 3, size - 6, size - 6,
                    0, 360 * 64);

      if (size > 8)
        {
          GdkGC *light = _gtk_theme_aa_gc (style, window, area, bg);

          gdk_gc_set_alpha (light, fabrica_opacity (style, 0.35));
          gdk_draw_arc (window, light, TRUE, x + 4, y + 4, size - 8, size - 8,
                        45 * 64, 180 * 64);
          gdk_gc_unref (light);
        }
    }
  gdk_gc_unref (gc);
}

static void
fabrica_draw_arrow (GtkStyle      *style,
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
   * with a shadow: a box, and the arrow in it, as GTK+ 1's Industrial
   * drew them */
  if (DETAIL ("hscrollbar") || DETAIL ("vscrollbar") || DETAIL ("spinbutton"))
    {
      fabrica_real_draw_box (style, window, state_type, shadow_type, area,
                             widget, detail, x, y, width, height, TRUE);
      x += 2;
      y += 2;
      width -= 4;
      height -= 4;
    }

  /* a submenu's arrow is in the middle of its item, however tall, a
   * row lower to be level with the label */
  if (DETAIL ("menuitem") && widget && GTK_IS_MENU_ITEM (widget))
    y = (widget->allocation.height - height) / 2 + 1;

  if (state_type == GTK_STATE_INSENSITIVE)
    fabrica_arrow (style, window, area, &style->light[state_type],
                   arrow_type, x + 1, y + 1, width, height);
  fabrica_arrow (style, window, area, &style->fg[state_type], arrow_type,
                 x, y, width, height);
}

/* GTK+ 2's option menu tab: an arrow up above an arrow down */
static void
fabrica_draw_tab (GtkStyle      *style,
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
  gint ind_width, arrow_height;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  ind_width = gtk_style_get_prop_experimental (style,
                                               "GtkOptionMenu::indicator_width",
                                               7);
  ind_width += (ind_width % 2) - 1;
  arrow_height = ind_width / 2 + 1;
  x += (width - ind_width) / 2;
  y += (height - (2 * arrow_height + 4)) / 2;

  fabrica_arrow (style, window, area, &style->fg[state_type], GTK_ARROW_UP,
                 x - 1, y, ind_width + 2, arrow_height);
  fabrica_arrow (style, window, area, &style->fg[state_type],
                 GTK_ARROW_DOWN, x - 1, y + arrow_height + 4,
                 ind_width + 2, arrow_height);
}

/* ---------------------------------------------------------------------
 * The engine
 */

static GtkStyleClass *
fabrica_get_class (GtkStyleClass *parent, gint xthickness, gint ythickness)
{
  static GtkStyleClass klass;

  if (!klass.draw_hline)
    {
      /* Polygon, diamond, oval, cross, ramp and flat_box are the toolkit's
       * own */
      klass = *parent;
      klass.draw_hline = fabrica_draw_hline;
      klass.draw_vline = fabrica_draw_vline;
      klass.draw_string = _gtk_theme_draw_string;
      klass.draw_shadow = fabrica_draw_shadow;
      klass.draw_arrow = fabrica_draw_arrow;
      klass.draw_box = fabrica_draw_box;
      klass.draw_check = fabrica_draw_check;
      klass.draw_option = fabrica_draw_option;
      klass.draw_tab = fabrica_draw_tab;
      klass.draw_shadow_gap = fabrica_draw_shadow_gap;
      klass.draw_box_gap = fabrica_draw_box_gap;
      klass.draw_extension = fabrica_draw_extension;
      klass.draw_focus = fabrica_draw_focus;
      klass.draw_slider = fabrica_draw_slider;
      klass.draw_handle = fabrica_draw_handle;
    }

  return _gtk_theme_class (&fabrica_classes, &klass, xthickness, ythickness);
}

/* The widget metrics Industrial's theme sets, and GTK+ 2's defaults
 * where they differ from GTK+ 1's and Industrial leaves them alone. */
static void
fabrica_set_props (GtkStyle *style)
{
  gtk_style_set_prop_experimental (style, "GtkButton::default_spacing", 4);
  gtk_style_set_prop_experimental (style, "GtkCheckButton::indicator_size", 13);
  gtk_style_set_prop_experimental (style, "GtkRadioButton::indicator_size", 13);
  gtk_style_set_prop_experimental (style, "GtkCheckMenuItem::indicator_size", 13);
  gtk_style_set_prop_experimental (style, "GtkOptionMenu::indicator_width", 7);
  gtk_style_set_prop_experimental (style, "GtkOptionMenu::indicator_height", 13);
  gtk_style_set_prop_experimental (style, "GtkOptionMenu::indicator_left_spacing", 7);
  gtk_style_set_prop_experimental (style, "GtkOptionMenu::indicator_right_spacing", 5);
  gtk_style_set_prop_experimental (style, "GtkRange::trough_border", 0);
  gtk_style_set_prop_experimental (style, "GtkRange::slider_width", 15);
  gtk_style_set_prop_experimental (style, "GtkRange::stepper_size", 15);
  /* the slider runs a pixel into each stepper, so that at either end
   * its edge lies on the stepper's, as Industrial made them overlap */
  gtk_style_set_prop_experimental (style, "GtkRange::stepper_spacing", -1);
  gtk_style_set_prop_experimental (style, "GtkProgressBar::min_horizontal_bar_height", 16);
  gtk_style_set_prop_experimental (style, "GtkProgressBar::min_vertical_bar_width", 16);
  gtk_style_set_prop_experimental (style, "GtkPaned::handle_full_size", 1);
  gtk_style_set_prop_experimental (style, "GtkPaned::handle_width", 7);
}

static guint
fabrica_parse_rc_style (GScanner   *scanner,
                        GtkRcStyle *rc_style)
{
  FabricaRcData *data;
  gdouble number = 0;
  guint old_scope;
  guint token;

  old_scope = _gtk_theme_scope_enter (scanner, "fabrica_theme_engine",
                                      fabrica_symbols,
                                      G_N_ELEMENTS (fabrica_symbols));

  data = g_new0 (FabricaRcData, 1);

  token = g_scanner_peek_next_token (scanner);
  while (token != G_TOKEN_RIGHT_CURLY)
    {
      g_scanner_get_next_token (scanner);

      switch (token)
        {
        case TOKEN_CONTRAST:
          token = _gtk_theme_parse_number (scanner, &data->contrast);
          data->flags |= FABRICA_CONTRAST_SET;
          break;
        case TOKEN_ROUNDED_BUTTONS:
          token = _gtk_theme_parse_boolean (scanner, TOKEN_TRUE, TOKEN_FALSE,
                                            &data->rounded_buttons);
          data->flags |= FABRICA_ROUNDED_BUTTONS_SET;
          break;
        case TOKEN_XTHICKNESS:
          token = _gtk_theme_parse_number (scanner, &number);
          data->xthickness = number;
          data->flags |= FABRICA_XTHICKNESS_SET;
          break;
        case TOKEN_YTHICKNESS:
          token = _gtk_theme_parse_number (scanner, &number);
          data->ythickness = number;
          data->flags |= FABRICA_YTHICKNESS_SET;
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
fabrica_merge_rc_style (GtkRcStyle *dest,
                        GtkRcStyle *src)
{
  FabricaRcData *src_data = src->engine_data;
  FabricaRcData *dest_data = dest->engine_data;
  guint flags;

  if (!dest_data)
    dest_data = dest->engine_data = g_new0 (FabricaRcData, 1);

  if (!src_data)
    return;

  flags = ~dest_data->flags & src_data->flags;

  if (flags & FABRICA_CONTRAST_SET)
    dest_data->contrast = src_data->contrast;
  if (flags & FABRICA_ROUNDED_BUTTONS_SET)
    dest_data->rounded_buttons = src_data->rounded_buttons;
  if (flags & FABRICA_XTHICKNESS_SET)
    dest_data->xthickness = src_data->xthickness;
  if (flags & FABRICA_YTHICKNESS_SET)
    dest_data->ythickness = src_data->ythickness;

  dest_data->flags |= flags;
}

static void
fabrica_rc_style_to_style (GtkStyle   *style,
                           GtkRcStyle *rc_style)
{
  FabricaRcData *data = rc_style->engine_data;
  gint xthickness = 1;
  gint ythickness = 1;

  if (data && (data->flags & FABRICA_XTHICKNESS_SET))
    xthickness = CLAMP (data->xthickness, 0, 8);
  if (data && (data->flags & FABRICA_YTHICKNESS_SET))
    ythickness = CLAMP (data->ythickness, 0, 8);

  style->klass = fabrica_get_class (style->klass, xthickness, ythickness);
  fabrica_set_props (style);
}

static void
fabrica_duplicate_style (GtkStyle *dest,
                         GtkStyle *src)
{
  dest->klass = src->klass;
  fabrica_set_props (dest);
}

void
gtk_theme_fabrica_init (GtkThemeEngine *engine)
{
  engine->parse_rc_style = fabrica_parse_rc_style;
  engine->merge_rc_style = fabrica_merge_rc_style;
  engine->rc_style_to_style = fabrica_rc_style_to_style;
  engine->duplicate_style = fabrica_duplicate_style;
  engine->destroy_rc_style = _gtk_theme_destroy_rc_style;
}
