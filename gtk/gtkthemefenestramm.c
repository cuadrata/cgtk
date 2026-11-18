/* GTK - The GIMP Toolkit
 * FenestraMM, a theme engine after Windows 2000 and Office 2000
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

/* FenestraMM draws the classic look of Windows 2000 as Office 2000 had it:
 * two-pixel bevels on push buttons, scrollbars, tabs and fields, and
 * Office's flat toolbars -- buttons that show a one-pixel edge only
 * under the pointer, grips and thin column headers.
 *
 * A bevel has four colours besides the face, which Windows calls the
 * highlight, the light, the shadow and the dark shadow. The engine
 * takes them as options:
 *
 *   engine "fenestramm"
 *   {
 *     highlight   = "#ffffff"
 *     light       = "#d4d0c8"
 *     shadow      = "#808080"
 *     dark_shadow = "#404040"
 *   }
 *
 * Without them it makes them from bg[NORMAL]. Everything else comes
 * from the style: bg[SELECTED] is the selection and a progress bar's
 * blocks, bg[PRELIGHT] and fg[PRELIGHT] a menu's highlighted item.
 */

#include <string.h>

#include "gtkbutton.h"
#include "gtkclist.h"
#include "gtkcombo.h"
#include "gtkentry.h"
#include "gtkgc.h"
#include "gtkhscale.h"
#include "gtkhscrollbar.h"
#include "gtkmenubar.h"
#include "gtkmenuitem.h"
#include "gtknotebook.h"
#include "gtkoptionmenu.h"
#include "gtkprogressbar.h"
#include "gtkrc.h"
#include "gtkscale.h"
#include "gtkscrollbar.h"
#include "gtkspinbutton.h"
#include "gtkstatusbar.h"
#include "gtkstyle.h"
#include "gtkthemes.h"
#include "gtkthemebuiltin.h"
#include "gtkthemecommon.h"
#include "gtktogglebutton.h"
#include "gtktoolbar.h"
#include "gtkwidget.h"

#define DETAIL(xx) ((detail) && (!strcmp ((xx), (detail))))

/* ---------------------------------------------------------------------
 * Options
 */

enum
{
  FENESTRAMM_HIGHLIGHT_SET   = 1 << 0,
  FENESTRAMM_LIGHT_SET       = 1 << 1,
  FENESTRAMM_SHADOW_SET      = 1 << 2,
  FENESTRAMM_DARK_SHADOW_SET = 1 << 3
};

typedef struct
{
  guint flags;
  GdkColor highlight;
  GdkColor light;
  GdkColor shadow;
  GdkColor dark_shadow;
} FenestraMMRcData;

enum
{
  TOKEN_HIGHLIGHT = G_TOKEN_LAST + 1,
  TOKEN_LIGHT,
  TOKEN_SHADOW,
  TOKEN_DARK_SHADOW
};

static const GtkThemeSymbol fenestramm_symbols[] = {
  { "highlight",   TOKEN_HIGHLIGHT },
  { "light",       TOKEN_LIGHT },
  { "shadow",      TOKEN_SHADOW },
  { "dark_shadow", TOKEN_DARK_SHADOW }
};

static FenestraMMRcData *
fenestramm_rc_data (GtkStyle *style)
{
  return style->rc_style ? style->rc_style->engine_data : NULL;
}

/* ---------------------------------------------------------------------
 * Colours
 */

/* The colours of a bevel; release them with fenestramm_colors_release() */
typedef struct
{
  GdkGC *highlight;     /* the light edge of a raised bevel */
  GdkGC *light;         /* inside the highlight, or the outer light edge */
  GdkGC *shadow;        /* the dark edge of a raised bevel */
  GdkGC *dark;          /* outside the shadow */
} FenestraMMColors;

static void
fenestramm_colors_get (GtkStyle *style, FenestraMMColors *c)
{
  FenestraMMRcData *data = fenestramm_rc_data (style);
  guint flags = data ? data->flags : 0;
  GdkColor *bg = &style->bg[GTK_STATE_NORMAL];

  c->highlight = (flags & FENESTRAMM_HIGHLIGHT_SET) ?
    _gtk_theme_shade_gc (style, &data->highlight, 1, 1, 1) :
    _gtk_theme_shade_gc (style, &style->light[GTK_STATE_NORMAL], 1, 1, 1);
  c->light = (flags & FENESTRAMM_LIGHT_SET) ?
    _gtk_theme_shade_gc (style, &data->light, 1, 1, 1) :
    _gtk_theme_shade_gc (style, bg, 1, 1, 1);
  c->shadow = (flags & FENESTRAMM_SHADOW_SET) ?
    _gtk_theme_shade_gc (style, &data->shadow, 1, 1, 1) :
    _gtk_theme_shade_gc (style, bg, 0.6, 0.6, 0.6);
  c->dark = (flags & FENESTRAMM_DARK_SHADOW_SET) ?
    _gtk_theme_shade_gc (style, &data->dark_shadow, 1, 1, 1) :
    _gtk_theme_shade_gc (style, bg, 0.3, 0.3, 0.3);
}

static void
fenestramm_colors_release (FenestraMMColors *c)
{
  gtk_gc_release (c->highlight);
  gtk_gc_release (c->light);
  gtk_gc_release (c->shadow);
  gtk_gc_release (c->dark);
}

/* ---------------------------------------------------------------------
 * Drawing
 */

/* A one-pixel ring: TOP_LEFT along the top and left, BOTTOM_RIGHT along
 * the bottom and right, which has the corners at the top right and the
 * bottom left, as Windows draws an edge */
static void
fenestramm_ring (GdkWindow *window, GdkRectangle *area, GdkGC *top_left,
                 GdkGC *bottom_right, gint x, gint y, gint width, gint height)
{
  gint r = x + width - 1, b = y + height - 1;

  if (width <= 0 || height <= 0)
    return;
  if (top_left)
    {
      _gtk_theme_line (window, top_left, area, x, y, r - 1, y);
      _gtk_theme_line (window, top_left, area, x, y, x, b - 1);
    }
  if (bottom_right)
    {
      _gtk_theme_line (window, bottom_right, area, x, b, r, b);
      _gtk_theme_line (window, bottom_right, area, r, y, r, b);
    }
}

/* The edges Windows draws, from the outside in */
typedef enum
{
  FENESTRAMM_RAISED,         /* a scrollbar's thumb or stepper, a menu */
  FENESTRAMM_RAISED_SOFT,    /* a push button, a notebook's page */
  FENESTRAMM_SUNKEN,         /* a field */
  FENESTRAMM_SUNKEN_SOFT,    /* a push button pressed in */
  FENESTRAMM_ETCHED,         /* a frame */
  FENESTRAMM_BUMP,
  FENESTRAMM_THIN_RAISED,    /* Office's: a toolbar's button, a header */
  FENESTRAMM_THIN_SUNKEN     /* a status bar's panel */
} FenestraMMEdge;

static void
fenestramm_edge (GdkWindow *window, GdkRectangle *area, FenestraMMColors *c,
                 FenestraMMEdge edge, gint x, gint y, gint width, gint height)
{
  GdkGC *outer_tl = NULL, *outer_br = NULL, *inner_tl = NULL, *inner_br = NULL;

  switch (edge)
    {
    case FENESTRAMM_RAISED:
      outer_tl = c->light;      outer_br = c->dark;
      inner_tl = c->highlight;  inner_br = c->shadow;
      break;
    case FENESTRAMM_RAISED_SOFT:
      outer_tl = c->highlight;  outer_br = c->dark;
      inner_tl = c->light;      inner_br = c->shadow;
      break;
    case FENESTRAMM_SUNKEN:
      outer_tl = c->shadow;     outer_br = c->highlight;
      inner_tl = c->dark;       inner_br = c->light;
      break;
    case FENESTRAMM_SUNKEN_SOFT:
      outer_tl = c->dark;       outer_br = c->highlight;
      inner_tl = c->shadow;     inner_br = c->light;
      break;
    case FENESTRAMM_ETCHED:
      outer_tl = c->shadow;     outer_br = c->highlight;
      inner_tl = c->highlight;  inner_br = c->shadow;
      break;
    case FENESTRAMM_BUMP:
      outer_tl = c->highlight;  outer_br = c->shadow;
      inner_tl = c->shadow;     inner_br = c->highlight;
      break;
    case FENESTRAMM_THIN_RAISED:
      outer_tl = c->highlight;  outer_br = c->shadow;
      break;
    case FENESTRAMM_THIN_SUNKEN:
      outer_tl = c->shadow;     outer_br = c->highlight;
      break;
    }

  fenestramm_ring (window, area, outer_tl, outer_br, x, y, width, height);
  if (inner_tl || inner_br)
    fenestramm_ring (window, area, inner_tl, inner_br, x + 1, y + 1, width - 2,
                     height - 2);
}

/* Fill with GC every other pixel, in a checkerboard set to the window,
 * as Windows fills a scrollbar's trough or a button that is down */
static void
fenestramm_dither (GdkWindow *window, GdkRectangle *area, GdkGC *gc,
                   gint x, gint y, gint width, gint height)
{
  GdkRectangle rect = { x, y, width, height }, clip;
  GdkPoint *points;
  gint i, j, n = 0;

  if (width <= 0 || height <= 0)
    return;
  if (area)
    {
      if (!gdk_rectangle_intersect (&rect, area, &clip))
        return;
    }
  else
    clip = rect;

  points = g_new (GdkPoint, (clip.width / 2 + 1) * clip.height);
  for (j = clip.y; j < clip.y + clip.height; j++)
    for (i = clip.x + (clip.x + j + 1) % 2; i < clip.x + clip.width; i += 2)
      {
        points[n].x = i;
        points[n].y = j;
        n++;
      }
  gdk_draw_points (window, gc, points, n);
  g_free (points);
}

/* A rectangle of dots, every other pixel round it from its top left
 * corner, as Windows draws the focus */
static void
fenestramm_dotted_rect (GdkWindow *window, GdkRectangle *area, GdkGC *gc,
                        gint x, gint y, gint width, gint height)
{
  GdkPoint *points;
  gint i, n = 0;

  if (width <= 0 || height <= 0)
    return;
  points = g_new (GdkPoint, 2 * (width + height));

#define DOT(u, v) G_STMT_START {                        \
    if (((u) + (v)) % 2 == 0)                           \
      {                                                 \
        points[n].x = x + (u);                          \
        points[n].y = y + (v);                          \
        n++;                                            \
      }                                                 \
  } G_STMT_END

  for (i = 0; i < width; i++)
    {
      DOT (i, 0);
      if (height > 1)
        DOT (i, height - 1);
    }
  for (i = 1; i < height - 1; i++)
    {
      DOT (0, i);
      if (width > 1)
        DOT (width - 1, i);
    }
#undef DOT

  _gtk_theme_clip (gc, area);
  gdk_draw_points (window, gc, points, n);
  _gtk_theme_unclip (gc, area);
  g_free (points);
}

/* The triangle at a scale over one: as Windows draws its arrows larger,
 * stepping a device pixel a row rather than a toolkit pixel, in the
 * space the triangle takes at a scale of one, at (x0, y0). It is a
 * device pixel short of that space along its base, at an even scale.
 * The pixel left over goes before it, not after: at a scale of one the
 * button already has a pixel more after the arrow than before it (as
 * Windows has), and putting the spare one there too set the arrow two
 * device pixels off the button's middle at a scale of two. */
static void
fenestramm_triangle_scaled (GdkWindow *window, GdkGC *gc, gint scale,
                            GtkArrowType arrow_type, gint base, gint x0,
                            gint y0)
{
  gboolean upright = arrow_type == GTK_ARROW_UP ||
                     arrow_type == GTK_ARROW_DOWN;
  gint span = base / 2 + 1;
  gint width = upright ? base : span, height = upright ? span : base;
  gint stride = width * scale * 4;
  gint dbase = scale * base - (scale - 1), dspan = dbase / 2 + 1;
  gint along = (scale * base - dbase + 1) / 2;
  gint out = (scale * span - dspan) / 2;
  GdkGCValues values;
  guchar *buf, *p;
  gint i, j;

  gdk_gc_get_values (gc, &values);
  buf = g_new0 (guchar, stride * height * scale);

  for (i = 0; i < dspan; i++)
    {
      /* the row (or column) i from the point, and its run of pixels */
      gint row = out + (arrow_type == GTK_ARROW_UP ||
                        arrow_type == GTK_ARROW_LEFT ? i : dspan - 1 - i);

      for (j = along + dspan - 1 - i; j <= along + dspan - 1 + i; j++)
        {
          if (upright)
            p = buf + row * stride + j * 4;
          else
            p = buf + j * stride + row * 4;
          p[0] = values.foreground.red >> 8;
          p[1] = values.foreground.green >> 8;
          p[2] = values.foreground.blue >> 8;
          p[3] = 255;
        }
    }

  gdk_draw_rgba_image_scaled (window, gc, x0, y0, width, height, scale,
                              buf, stride);
  g_free (buf);
}

/* A solid triangle pointing ARROW_TYPE, BASE pixels across, centred in
 * the area */
static void
fenestramm_triangle (GdkWindow *window, GdkGC *gc, GdkRectangle *area,
                     GtkArrowType arrow_type, gint base, gint x, gint y,
                     gint width, gint height)
{
  gint span, i, x0, y0, scale;

  if (base % 2 == 0)
    base--;
  if (base < 1)
    return;
  span = base / 2 + 1;

  scale = gdk_window_get_scale_factor (window);
  if (arrow_type == GTK_ARROW_UP || arrow_type == GTK_ARROW_DOWN)
    {
      x0 = x + (width - base) / 2;
      y0 = y + (height - span) / 2;
    }
  else
    {
      x0 = x + (width - span) / 2;
      y0 = y + (height - base) / 2;
    }

  _gtk_theme_clip (gc, area);
  if (scale > 1)
    {
      fenestramm_triangle_scaled (window, gc, scale, arrow_type, base, x0, y0);
      _gtk_theme_unclip (gc, area);
      return;
    }

  switch (arrow_type)
    {
    case GTK_ARROW_UP:
    case GTK_ARROW_DOWN:
      for (i = 0; i < span; i++)
        {
          gint row = arrow_type == GTK_ARROW_UP ? y0 + i : y0 + span - 1 - i;

          gdk_draw_line (window, gc, x0 + span - 1 - i, row,
                         x0 + span - 1 + i, row);
        }
      break;
    case GTK_ARROW_LEFT:
    case GTK_ARROW_RIGHT:
      for (i = 0; i < span; i++)
        {
          gint col = arrow_type == GTK_ARROW_LEFT ? x0 + i : x0 + span - 1 - i;

          gdk_draw_line (window, gc, col, y0 + span - 1 - i, col,
                         y0 + span - 1 + i);
        }
      break;
    }
  _gtk_theme_unclip (gc, area);
}

/* Pixels from a bitmap of rows, '#' for a pixel */
static void
fenestramm_glyph (GdkWindow *window, GdkGC *gc, GdkRectangle *area,
                  const gchar *const *rows, gint n_rows, gint x, gint y)
{
  gint i, j;

  _gtk_theme_clip (gc, area);
  for (j = 0; j < n_rows; j++)
    for (i = 0; rows[j][i]; i++)
      if (rows[j][i] == '#')
        gdk_draw_point (window, gc, x + i, y + j);
  _gtk_theme_unclip (gc, area);
}

static const gchar *const check_mark[] = {
  "......#",
  ".....##",
  "#...###",
  "##.###.",
  "#####..",
  ".###...",
  "..#...."
};

/* A radio button's ring, twelve pixels across: 'o' the outer ring, 'i'
 * the inner, '.' inside it. The upper left half of a ring is dark, the
 * lower right light. */
static const gchar *const radio_rings[] = {
  "    oooo    ",
  "  ooiiiioo  ",
  " oii....iio ",
  " oi......io ",
  "oi........io",
  "oi........io",
  "oi........io",
  "oi........io",
  " oi......io ",
  " oii....iio ",
  "  ooiiiioo  ",
  "    oooo    "
};

static const gchar *const radio_dot[] = {
  ".##.",
  "####",
  "####",
  ".##."
};

static const gchar *const menu_radio_dot[] = {
  ".####.",
  "######",
  "######",
  "######",
  "######",
  ".####."
};

/* ---------------------------------------------------------------------
 * Where a widget is
 */

/* A button is flat, as Office draws one, in a toolbar */
static gboolean
fenestramm_is_flat_button (GtkWidget *widget)
{
  return widget && GTK_IS_BUTTON (widget) &&
         (GTK_BUTTON (widget)->relief == GTK_RELIEF_NONE ||
          _gtk_theme_parent_is (widget, gtk_toolbar_get_type ()));
}

/* ---------------------------------------------------------------------
 * Pieces
 */

/* A push button, as Windows draws one: a soft bevel, a black frame
 * round it when it is the default, and when it is pressed in a black
 * frame with the shadow inside it. A toggle button that is down is
 * sunken and dithered. */
static void
fenestramm_button (GtkStyle *style, GdkWindow *window, GtkStateType state_type,
                   GtkShadowType shadow_type, GdkRectangle *area,
                   GtkWidget *widget, gint x, gint y, gint width, gint height)
{
  gboolean is_default = widget && GTK_WIDGET_HAS_DEFAULT (widget);
  gboolean held = widget && GTK_IS_BUTTON (widget) &&
                  GTK_BUTTON (widget)->button_down &&
                  GTK_BUTTON (widget)->in_button;
  gboolean toggled = widget && GTK_IS_TOGGLE_BUTTON (widget) &&
                     GTK_TOGGLE_BUTTON (widget)->active;
  FenestraMMColors c;

  _gtk_theme_button_rect (widget, window, &x, &y, &width, &height);
  fenestramm_colors_get (style, &c);

  _gtk_theme_rectangle (window, style->bg_gc[state_type], area, TRUE, x, y,
                        width, height);

  if (shadow_type == GTK_SHADOW_IN && toggled && !held)
    {
      fenestramm_dither (window, area, c.highlight, x + 2, y + 2, width - 4,
                         height - 4);
      fenestramm_edge (window, area, &c, FENESTRAMM_SUNKEN_SOFT, x, y, width,
                       height);
    }
  else if (shadow_type == GTK_SHADOW_IN &&
           (is_default || (widget && GTK_WIDGET_HAS_FOCUS (widget))))
    {
      _gtk_theme_rectangle (window, style->black_gc, area, FALSE, x, y,
                            width - 1, height - 1);
      _gtk_theme_rectangle (window, c.shadow, area, FALSE, x + 1, y + 1,
                            width - 3, height - 3);
    }
  else if (shadow_type == GTK_SHADOW_IN)
    fenestramm_edge (window, area, &c, FENESTRAMM_SUNKEN_SOFT, x, y, width,
                     height);
  else
    {
      if (is_default)
        {
          _gtk_theme_rectangle (window, style->black_gc, area, FALSE, x, y,
                                width - 1, height - 1);
          x += 1;
          y += 1;
          width -= 2;
          height -= 2;
        }
      fenestramm_edge (window, area, &c, FENESTRAMM_RAISED_SOFT, x, y, width,
                       height);
    }

  fenestramm_colors_release (&c);
}

/* A button in a toolbar, as Office draws one: nothing until the pointer
 * is over it, then a thin raised edge; pressed in, or down, a thin
 * sunken one, dithered when it is down and the pointer is elsewhere */
static void
fenestramm_flat_button (GtkStyle *style, GdkWindow *window,
                        GtkStateType state_type, GtkShadowType shadow_type,
                        GdkRectangle *area, GtkWidget *widget,
                        gint x, gint y, gint width, gint height)
{
  gboolean toggled = widget && GTK_IS_TOGGLE_BUTTON (widget) &&
                     GTK_TOGGLE_BUTTON (widget)->active;
  FenestraMMColors c;

  if (state_type == GTK_STATE_INSENSITIVE ||
      (state_type == GTK_STATE_NORMAL && shadow_type != GTK_SHADOW_IN))
    return;

  _gtk_theme_button_rect (widget, window, &x, &y, &width, &height);
  fenestramm_colors_get (style, &c);

  _gtk_theme_rectangle (window, style->bg_gc[GTK_STATE_NORMAL], area, TRUE,
                        x, y, width, height);
  if (shadow_type == GTK_SHADOW_IN)
    {
      if (toggled && state_type != GTK_STATE_PRELIGHT)
        fenestramm_dither (window, area, c.highlight, x + 1, y + 1, width - 2,
                           height - 2);
      fenestramm_edge (window, area, &c, FENESTRAMM_THIN_SUNKEN, x, y, width,
                       height);
    }
  else
    fenestramm_edge (window, area, &c, FENESTRAMM_THIN_RAISED, x, y, width,
                     height);

  fenestramm_colors_release (&c);
}

/* A list's column title, as Office's headers: a thin raised edge, and
 * the shadow round it while it is pressed */
static void
fenestramm_header (GtkStyle *style, GdkWindow *window, GtkStateType state_type,
                   GtkShadowType shadow_type, GdkRectangle *area,
                   GtkWidget *widget, gint x, gint y, gint width, gint height)
{
  FenestraMMColors c;

  _gtk_theme_button_rect (widget, window, &x, &y, &width, &height);
  fenestramm_colors_get (style, &c);
  _gtk_theme_rectangle (window, style->bg_gc[GTK_STATE_NORMAL], area, TRUE,
                        x, y, width, height);
  if (shadow_type == GTK_SHADOW_IN)
    _gtk_theme_rectangle (window, c.shadow, area, FALSE, x, y, width - 1,
                          height - 1);
  else
    fenestramm_edge (window, area, &c, FENESTRAMM_THIN_RAISED, x, y, width,
                     height);
  fenestramm_colors_release (&c);
}

/* A drop-down's arrow button, inside its white field, as Windows draws
 * one: raised like a scrollbar's stepper, and pressed in, flat with the
 * shadow round it */
static void
fenestramm_dropdown_button (GtkStyle *style, GdkWindow *window,
                            gboolean pressed, GdkRectangle *area, gint x,
                            gint y, gint width, gint height)
{
  FenestraMMColors c;

  fenestramm_colors_get (style, &c);
  _gtk_theme_rectangle (window, style->bg_gc[GTK_STATE_NORMAL], area, TRUE,
                        x, y, width, height);
  if (pressed)
    _gtk_theme_rectangle (window, c.shadow, area, FALSE, x, y, width - 1,
                          height - 1);
  else
    fenestramm_edge (window, area, &c, FENESTRAMM_RAISED, x, y, width, height);
  fenestramm_colors_release (&c);
}

/* An option menu, as Windows draws a drop-down list: a sunken white
 * field with the arrow's button inside its edge, at the right */
static void
fenestramm_option_menu (GtkStyle *style, GdkWindow *window,
                        GtkStateType state_type, GdkRectangle *area,
                        GtkWidget *widget, gint x, gint y, gint width,
                        gint height)
{
  GtkStateType fill = state_type == GTK_STATE_INSENSITIVE ?
                      GTK_STATE_INSENSITIVE : GTK_STATE_NORMAL;
  FenestraMMColors c;

  fenestramm_colors_get (style, &c);
  _gtk_theme_rectangle (window, style->base_gc[fill], area, TRUE, x, y,
                        width, height);
  fenestramm_edge (window, area, &c, FENESTRAMM_SUNKEN, x, y, width, height);
  fenestramm_colors_release (&c);
}

/* The arrow's button inside an option menu: sixteen pixels wide, as a
 * scrollbar is, against the inside of the field's edge */
static void
fenestramm_option_menu_tab (GtkStyle *style, GdkWindow *window,
                            GtkStateType state_type, GdkRectangle *area,
                            GtkWidget *widget, gint x, gint y, gint width,
                            gint height)
{
  gboolean pressed = state_type == GTK_STATE_ACTIVE;
  gint bx = x - 4, by = y, bw = 16, bh = height;

  if (widget)
    {
      gint border = GTK_CONTAINER (widget)->border_width + 1;

      by = border + 2;
      bh = widget->allocation.height - 2 * border - 4;
    }
  fenestramm_dropdown_button (style, window, pressed, area, bx, by, bw, bh);
  gtk_paint_arrow (style, window, state_type, GTK_SHADOW_OUT, area, widget,
                   "optionmenutab", GTK_ARROW_DOWN, FALSE,
                   pressed ? bx + 1 : bx, pressed ? by + 1 : by, bw, bh);
}

/* A scrollbar's thumb or stepper, or a spin button's: raised, and
 * pressed in, flat with the shadow round it */
static void
fenestramm_scroll_box (GtkStyle *style, GdkWindow *window,
                       GtkShadowType shadow_type, GdkRectangle *area,
                       gint x, gint y, gint width, gint height)
{
  FenestraMMColors c;

  fenestramm_colors_get (style, &c);
  _gtk_theme_rectangle (window, style->bg_gc[GTK_STATE_NORMAL], area, TRUE,
                        x, y, width, height);
  if (shadow_type == GTK_SHADOW_IN)
    _gtk_theme_rectangle (window, c.shadow, area, FALSE, x, y, width - 1,
                          height - 1);
  else
    fenestramm_edge (window, area, &c, FENESTRAMM_RAISED, x, y, width, height);
  fenestramm_colors_release (&c);
}

/* Where a scale's channel runs across or down it: from a pixel before
 * the thumb at one end to a pixel past it at the other */
static void
fenestramm_scale_channel (GtkWidget *widget, gboolean horizontal, gint x,
                          gint y, gint width, gint height,
                          GdkRectangle *channel)
{
  gint border = 0, inset;

  if (widget && GTK_IS_SCALE (widget))
    _gtk_range_get_props (GTK_RANGE (widget), NULL, &border, NULL, NULL);
  inset = MAX (border - 1, 0);

  if (horizontal)
    {
      channel->x = x + inset;
      channel->width = width - 2 * inset;
      channel->y = y + (height - 4) / 2;
      channel->height = 4;
    }
  else
    {
      channel->y = y + inset;
      channel->height = height - 2 * inset;
      channel->x = x + (width - 4) / 2;
      channel->width = 4;
    }
}

/* A scale's trough: the face, with a sunken channel along it */
static void
fenestramm_scale_trough (GtkStyle *style, GdkWindow *window, GdkRectangle *area,
                         GtkWidget *widget, gint x, gint y, gint width,
                         gint height)
{
  GdkRectangle channel;
  FenestraMMColors c;

  fenestramm_scale_channel (widget, GTK_IS_HSCALE (widget), x, y, width, height,
                            &channel);
  fenestramm_colors_get (style, &c);
  _gtk_theme_rectangle (window, style->bg_gc[GTK_STATE_NORMAL], area, TRUE,
                        x, y, width, height);
  fenestramm_edge (window, area, &c, FENESTRAMM_SUNKEN, channel.x, channel.y,
                   channel.width, channel.height);
  fenestramm_colors_release (&c);
}

/* A scale's slider: the thumb, raised, filling the window GTK+ gives
 * it, which is eleven pixels long */
static void
fenestramm_scale_slider (GtkStyle *style, GdkWindow *window,
                         GtkStateType state_type, GdkRectangle *area,
                         gint x, gint y, gint width, gint height)
{
  FenestraMMColors c;

  fenestramm_colors_get (style, &c);
  _gtk_theme_rectangle (window, style->bg_gc[state_type], area, TRUE, x, y,
                        width, height);
  fenestramm_edge (window, area, &c, FENESTRAMM_RAISED_SOFT, x, y, width,
                   height);
  fenestramm_colors_release (&c);
}

/* A progress bar's blocks, in the selection colour, two pixels apart */
static void
fenestramm_progress_blocks (GtkStyle *style, GdkWindow *window,
                            GdkRectangle *area, GtkWidget *widget, gint x,
                            gint y, gint width, gint height)
{
  GtkProgressBarOrientation orientation = GTK_PROGRESS_LEFT_TO_RIGHT;
  GdkGC *gc = style->bg_gc[GTK_STATE_SELECTED];
  GdkRectangle clip = { x, y, width, height };
  gboolean horizontal;
  gint size, pos, end;

  if (widget && GTK_IS_PROGRESS_BAR (widget))
    orientation = GTK_PROGRESS_BAR (widget)->orientation;
  horizontal = orientation == GTK_PROGRESS_LEFT_TO_RIGHT ||
               orientation == GTK_PROGRESS_RIGHT_TO_LEFT;
  size = MAX (2, ((horizontal ? height : width) * 2) / 3);

  if (area && !gdk_rectangle_intersect (&clip, area, &clip))
    return;

  end = horizontal ? width : height;
  for (pos = 0; pos < end; pos += size + 2)
    {
      gint n = MIN (size, end - pos);

      switch (orientation)
        {
        case GTK_PROGRESS_LEFT_TO_RIGHT:
          _gtk_theme_rectangle (window, gc, &clip, TRUE, x + pos, y, n, height);
          break;
        case GTK_PROGRESS_RIGHT_TO_LEFT:
          _gtk_theme_rectangle (window, gc, &clip, TRUE, x + width - pos - n, y,
                                n, height);
          break;
        case GTK_PROGRESS_TOP_TO_BOTTOM:
          _gtk_theme_rectangle (window, gc, &clip, TRUE, x, y + pos, width, n);
          break;
        case GTK_PROGRESS_BOTTOM_TO_TOP:
          _gtk_theme_rectangle (window, gc, &clip, TRUE, x,
                                y + height - pos - n, width, n);
          break;
        }
    }
}

/* Lines in a tab's own terms: U along the row of tabs, V from the tab's
 * free end towards the page */
typedef struct
{
  GdkWindow *window;
  GdkRectangle *area;
  GtkPositionType pos;
  gint x, y, r, b;
} FenestraMMTab;

static void
fenestramm_tab_point (FenestraMMTab *t, gint u, gint v, gint *px, gint *py)
{
  switch (t->pos)
    {
    case GTK_POS_BOTTOM:  *px = t->x + u;  *py = t->b - v;  break;
    case GTK_POS_LEFT:    *px = t->x + v;  *py = t->y + u;  break;
    case GTK_POS_RIGHT:   *px = t->r - v;  *py = t->y + u;  break;
    default:              *px = t->x + u;  *py = t->y + v;  break;
    }
}

static void
fenestramm_tab_line (FenestraMMTab *t, GdkGC *gc, gint u1, gint v1, gint u2,
                     gint v2)
{
  gint x1, y1, x2, y2;

  if (u2 < u1 || v2 < v1)
    return;
  fenestramm_tab_point (t, u1, v1, &x1, &y1);
  fenestramm_tab_point (t, u2, v2, &x2, &y2);
  _gtk_theme_line (t->window, gc, t->area, x1, y1, x2, y2);
}

/* The sides of a tab, from V1 to V2: the highlight on the side towards
 * the start of the row, the shadows on the other */
static void
fenestramm_tab_sides (FenestraMMTab *t, FenestraMMColors *c, gint length,
                      gint v1, gint v2)
{
  fenestramm_tab_line (t, c->highlight, 0, v1, 0, v2);
  fenestramm_tab_line (t, c->dark, length - 1, v1, length - 1, v2);
  fenestramm_tab_line (t, c->shadow, length - 2, v1, length - 2, v2);
}

/* A notebook's tab, as Windows draws one: square, its corners cut by a
 * pixel, the highlight at the top and left and the shadows at the
 * bottom and right */
static void
fenestramm_tab (GtkStyle *style, GdkWindow *window, GtkStateType state_type,
                GdkRectangle *area, GtkWidget *widget, GtkPositionType gap_side,
                gint x, gint y, gint width, gint height)
{
  FenestraMMTab t;
  FenestraMMColors c;
  gboolean horizontal;
  gint length, depth;
  GdkGC *end_outer, *end_inner, *corner_start, *corner_end;

  switch (gap_side)
    {
    case GTK_POS_BOTTOM:  t.pos = GTK_POS_TOP;     break;
    case GTK_POS_TOP:     t.pos = GTK_POS_BOTTOM;  break;
    case GTK_POS_RIGHT:   t.pos = GTK_POS_LEFT;    break;
    default:              t.pos = GTK_POS_RIGHT;   break;
    }
  t.window = window;
  t.area = area;
  t.x = x;
  t.y = y;
  t.r = x + width - 1;
  t.b = y + height - 1;
  horizontal = t.pos == GTK_POS_TOP || t.pos == GTK_POS_BOTTOM;
  length = horizontal ? width : height;
  depth = horizontal ? height : width;

  fenestramm_colors_get (style, &c);
  gtk_style_apply_default_background (style, window, TRUE, GTK_STATE_NORMAL,
                                      area, x, y, width, height);
  if (horizontal)
    _gtk_theme_rectangle (window, style->bg_gc[GTK_STATE_NORMAL], area, TRUE,
                          x + 1, t.pos == GTK_POS_TOP ? y + 1 : y, width - 2,
                          height - 1);
  else
    _gtk_theme_rectangle (window, style->bg_gc[GTK_STATE_NORMAL], area, TRUE,
                          t.pos == GTK_POS_LEFT ? x + 1 : x, y + 1, width - 1,
                          height - 2);

  /* The free end is light at the top or left, dark at the bottom or
   * right */
  if (t.pos == GTK_POS_TOP || t.pos == GTK_POS_LEFT)
    {
      end_outer = c.highlight;
      end_inner = NULL;
    }
  else
    {
      end_outer = c.dark;
      end_inner = c.shadow;
    }
  corner_start = c.highlight;
  corner_end = c.dark;

  fenestramm_tab_line (&t, end_outer, 2, 0, length - 3, 0);
  if (end_inner)
    fenestramm_tab_line (&t, end_inner, 2, 1, length - 3, 1);
  fenestramm_tab_line (&t, corner_start, 1, 1, 1, 1);
  fenestramm_tab_line (&t, corner_end, length - 2, 1, length - 2, 1);
  fenestramm_tab_sides (&t, &c, length, 2, depth - 1);

  fenestramm_colors_release (&c);
}

/* A notebook's page: a soft raised edge, with a gap where the current
 * tab joins it, through which the tab's sides run on */
static void
fenestramm_page (GtkStyle *style, GdkWindow *window, GdkRectangle *area,
                 gint x, gint y, gint width, gint height, gboolean gap,
                 GtkPositionType gap_side, gint gap_x, gint gap_width)
{
  FenestraMMColors c;
  FenestraMMTab t;

  fenestramm_colors_get (style, &c);
  _gtk_theme_rectangle (window, style->bg_gc[GTK_STATE_NORMAL], area, TRUE,
                        x, y, width, height);
  fenestramm_edge (window, area, &c, FENESTRAMM_RAISED_SOFT, x, y, width,
                   height);

  if (gap && gap_width > 0)
    {
      /* The page's edge, two pixels deep, where the tab is: in the tab's
       * terms it is two more pixels below its open end */
      t.window = window;
      t.area = area;
      t.pos = gap_side;
      switch (gap_side)
        {
        case GTK_POS_TOP:
        default:
          t.x = x + gap_x;  t.y = y;
          t.r = t.x + gap_width - 1;  t.b = y + 1;
          _gtk_theme_rectangle (window, style->bg_gc[GTK_STATE_NORMAL], area,
                                TRUE, t.x, y, gap_width, 2);
          break;
        case GTK_POS_BOTTOM:
          t.x = x + gap_x;  t.y = y + height - 2;
          t.r = t.x + gap_width - 1;  t.b = y + height - 1;
          _gtk_theme_rectangle (window, style->bg_gc[GTK_STATE_NORMAL], area,
                                TRUE, t.x, t.y, gap_width, 2);
          break;
        case GTK_POS_LEFT:
          t.x = x;  t.y = y + gap_x;
          t.r = x + 1;  t.b = t.y + gap_width - 1;
          _gtk_theme_rectangle (window, style->bg_gc[GTK_STATE_NORMAL], area,
                                TRUE, x, t.y, 2, gap_width);
          break;
        case GTK_POS_RIGHT:
          t.x = x + width - 2;  t.y = y + gap_x;
          t.r = x + width - 1;  t.b = t.y + gap_width - 1;
          _gtk_theme_rectangle (window, style->bg_gc[GTK_STATE_NORMAL], area,
                                TRUE, t.x, t.y, 2, gap_width);
          break;
        }
      fenestramm_tab_sides (&t, &c, gap_width, 0, 1);
    }

  fenestramm_colors_release (&c);
}

/* ---------------------------------------------------------------------
 * The style class
 */

static void
fenestramm_draw_shadow (GtkStyle      *style,
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
  FenestraMMColors c;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  if (shadow_type == GTK_SHADOW_NONE)
    return;
  _gtk_theme_sanitize_size (window, &width, &height);
  /* Windows shows no focus round a field: its edge stays put */
  _gtk_theme_entry_frame (widget, detail, &x, &y, &width, &height);

  /* A spin button's or a combo's field is open at the right, where its
   * arrows' panel or its button carries the edge on round them */
  if (DETAIL ("entry") && widget &&
      (GTK_IS_SPIN_BUTTON (widget) ||
       _gtk_theme_parent_is (widget, gtk_combo_get_type ())))
    width += 2;

  fenestramm_colors_get (style, &c);
  if (DETAIL ("frame") &&
      _gtk_theme_parent_is (widget, gtk_statusbar_get_type ()))
    fenestramm_edge (window, area, &c, FENESTRAMM_THIN_SUNKEN, x, y, width,
                     height);
  else
    switch (shadow_type)
      {
      case GTK_SHADOW_IN:
        fenestramm_edge (window, area, &c, FENESTRAMM_SUNKEN, x, y, width,
                         height);
        break;
      case GTK_SHADOW_OUT:
        fenestramm_edge (window, area, &c, FENESTRAMM_RAISED, x, y, width,
                         height);
        break;
      case GTK_SHADOW_ETCHED_IN:
        fenestramm_edge (window, area, &c, FENESTRAMM_ETCHED, x, y, width,
                         height);
        break;
      case GTK_SHADOW_ETCHED_OUT:
        fenestramm_edge (window, area, &c, FENESTRAMM_BUMP, x, y, width,
                         height);
        break;
      default:
        break;
      }
  fenestramm_colors_release (&c);
}

static void
fenestramm_draw_box (GtkStyle      *style,
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
  FenestraMMColors c;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  /* A splitter, as in Explorer: only the face, between the panes' own
   * edges */
  if (DETAIL ("hpaned") || DETAIL ("vpaned"))
    {
      _gtk_theme_rectangle (window, style->bg_gc[state_type], area, TRUE,
                            x, y, width, height);
      return;
    }

  if (DETAIL ("button") || DETAIL ("togglebutton"))
    {
      if (_gtk_theme_parent_is (widget, gtk_clist_get_type ()))
        fenestramm_header (style, window, state_type, shadow_type, area, widget,
                           x, y, width, height);
      else if (_gtk_theme_parent_is (widget, gtk_combo_get_type ()))
        {
          /* the end of the entry's sunken edge, round the arrow's
           * button, which is against the inside of it */
          _gtk_theme_button_rect (widget, window, &x, &y, &width, &height);
          fenestramm_colors_get (style, &c);
          fenestramm_edge (window, area, &c, FENESTRAMM_SUNKEN, x - 2, y,
                           width + 2, height);
          fenestramm_colors_release (&c);
          fenestramm_dropdown_button (style, window,
                                      shadow_type == GTK_SHADOW_IN, area, x,
                                      y + 2, width - 2, height - 4);
        }
      else if (fenestramm_is_flat_button (widget))
        fenestramm_flat_button (style, window, state_type, shadow_type, area,
                                widget, x, y, width, height);
      else
        fenestramm_button (style, window, state_type, shadow_type, area, widget,
                           x, y, width, height);
    }
  else if (DETAIL ("buttondefault") || DETAIL ("togglebuttondefault"))
    ;                   /* drawn with the button */
  else if (DETAIL ("optionmenu"))
    fenestramm_option_menu (style, window, state_type, area, widget,
                            x, y, width, height);
  else if (DETAIL ("trough"))
    {
      if (widget && GTK_IS_PROGRESS_BAR (widget))
        {
          fenestramm_colors_get (style, &c);
          _gtk_theme_rectangle (window, style->bg_gc[GTK_STATE_NORMAL], area,
                                TRUE, x, y, width, height);
          fenestramm_edge (window, area, &c, FENESTRAMM_THIN_SUNKEN, x, y,
                           width, height);
          fenestramm_colors_release (&c);
        }
      else if (widget && GTK_IS_SCALE (widget))
        fenestramm_scale_trough (style, window, area, widget, x, y, width,
                                 height);
      else
        {
          fenestramm_colors_get (style, &c);
          _gtk_theme_rectangle (window, style->bg_gc[GTK_STATE_NORMAL], area,
                                TRUE, x, y, width, height);
          fenestramm_dither (window, area, c.highlight, x, y, width, height);
          fenestramm_colors_release (&c);
        }
    }
  else if (DETAIL ("slider"))
    fenestramm_scroll_box (style, window, GTK_SHADOW_OUT, area, x, y, width,
                           height);
  else if (DETAIL ("bar"))
    fenestramm_progress_blocks (style, window, area, widget, x, y, width,
                                height);
  else if (DETAIL ("spinbutton"))
    {
      /* the arrows' panel, inside the entry's sunken edge */
      GtkStateType field = !widget || GTK_WIDGET_IS_SENSITIVE (widget) ?
                           GTK_STATE_NORMAL : GTK_STATE_INSENSITIVE;

      fenestramm_colors_get (style, &c);
      _gtk_theme_rectangle (window, style->base_gc[field], area, TRUE, x, y,
                            width, height);
      _gtk_theme_line (window, c.shadow, area, x, y, x + width - 2, y);
      _gtk_theme_line (window, c.dark, area, x, y + 1, x + width - 3, y + 1);
      _gtk_theme_line (window, c.highlight, area, x, y + height - 1,
                       x + width - 1, y + height - 1);
      _gtk_theme_line (window, c.light, area, x, y + height - 2,
                       x + width - 2, y + height - 2);
      _gtk_theme_line (window, c.highlight, area, x + width - 1, y,
                       x + width - 1, y + height - 1);
      _gtk_theme_line (window, c.light, area, x + width - 2, y + 1,
                       x + width - 2, y + height - 2);
      fenestramm_colors_release (&c);
    }
  else if (DETAIL ("menu"))
    {
      fenestramm_colors_get (style, &c);
      fenestramm_edge (window, area, &c, FENESTRAMM_RAISED, x, y, width,
                       height);
      fenestramm_colors_release (&c);
    }
  else if (DETAIL ("menuitem"))
    {
      if (_gtk_theme_parent_is (widget, gtk_menu_bar_get_type ()))
        {
          /* Office's: a menu bar's item that is open is thinly sunken */
          fenestramm_colors_get (style, &c);
          _gtk_theme_rectangle (window, style->bg_gc[GTK_STATE_NORMAL], area,
                                TRUE, x, y, width, height);
          fenestramm_edge (window, area, &c, FENESTRAMM_THIN_SUNKEN, x, y,
                           width, height);
          fenestramm_colors_release (&c);
        }
      else
        _gtk_theme_rectangle (window, style->bg_gc[state_type], area, TRUE,
                              x, y, width, height);
    }
  else if (DETAIL ("handlebox_bin"))
    gtk_style_apply_default_background (style, window, TRUE, state_type, area,
                                        x, y, width, height);
  else if (DETAIL ("menubar"))
    _gtk_theme_rectangle (window, style->bg_gc[state_type], area, TRUE, x, y,
                          width, height);
  else if (DETAIL ("notebook"))
    fenestramm_page (style, window, area, x, y, width, height, FALSE, 0, 0, 0);
  else
    {
      if (!style->bg_pixmap[state_type] ||
          gdk_window_get_type (window) == GDK_WINDOW_PIXMAP)
        _gtk_theme_rectangle (window, style->bg_gc[state_type], area, TRUE, x,
                              y, width, height);
      else
        gtk_style_apply_default_background (style, window, TRUE, state_type,
                                            area, x, y, width, height);
      gtk_paint_shadow (style, window, state_type, shadow_type, area, widget,
                        detail, x, y, width, height);
    }
}

static void
fenestramm_draw_flat_box (GtkStyle      *style,
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

  if (DETAIL ("selected") ||
      (DETAIL ("text") && state_type == GTK_STATE_SELECTED))
    gc = style->bg_gc[GTK_STATE_SELECTED];
  else if (DETAIL ("viewportbin") || DETAIL ("checkbutton"))
    gc = style->bg_gc[GTK_STATE_NORMAL];  /* Windows lights neither up */
  else if (DETAIL ("entry_bg"))
    gc = style->base_gc[state_type];

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

/* A menu bar's open item keeps black text on the face */
static gboolean
fenestramm_in_menu_bar (GtkWidget *widget)
{
  return widget && widget->parent && GTK_IS_MENU_ITEM (widget->parent) &&
         _gtk_theme_parent_is (widget->parent, gtk_menu_bar_get_type ());
}

static void
fenestramm_draw_string (GtkStyle     *style,
                        GdkWindow    *window,
                        GtkStateType  state_type,
                        GdkRectangle *area,
                        GtkWidget    *widget,
                        gchar        *detail,
                        gint          x,
                        gint          y,
                        const gchar  *string)
{
  GdkGC *gc;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  /* engraved, the highlight below and to the right; on a dark face
   * only the grey */
  if (state_type == GTK_STATE_INSENSITIVE && !_gtk_theme_is_dark (style))
    {
      FenestraMMColors c;

      fenestramm_colors_get (style, &c);
      _gtk_theme_clip (c.highlight, area);
      gdk_draw_string (window, style->font, c.highlight, x + 1, y + 1, string);
      _gtk_theme_unclip (c.highlight, area);
      fenestramm_colors_release (&c);
    }

  if (state_type == GTK_STATE_PRELIGHT && fenestramm_in_menu_bar (widget))
    gc = style->fg_gc[GTK_STATE_NORMAL];
  else
    gc = style->fg_gc[state_type];
  _gtk_theme_clip (gc, area);
  gdk_draw_string (window, style->font, gc, x, y, string);
  _gtk_theme_unclip (gc, area);
}

static void
fenestramm_draw_hline (GtkStyle     *style,
                       GdkWindow    *window,
                       GtkStateType  state_type,
                       GdkRectangle *area,
                       GtkWidget    *widget,
                       gchar        *detail,
                       gint          x1,
                       gint          x2,
                       gint          y)
{
  FenestraMMColors c;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  fenestramm_colors_get (style, &c);
  _gtk_theme_line (window, c.shadow, area, x1, y, x2, y);
  _gtk_theme_line (window, c.highlight, area, x1, y + 1, x2, y + 1);
  fenestramm_colors_release (&c);
}

static void
fenestramm_draw_vline (GtkStyle     *style,
                       GdkWindow    *window,
                       GtkStateType  state_type,
                       GdkRectangle *area,
                       GtkWidget    *widget,
                       gchar        *detail,
                       gint          y1,
                       gint          y2,
                       gint          x)
{
  FenestraMMColors c;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  fenestramm_colors_get (style, &c);
  _gtk_theme_line (window, c.shadow, area, x, y1, x, y2);
  _gtk_theme_line (window, c.highlight, area, x + 1, y1, x + 1, y2);
  fenestramm_colors_release (&c);
}

static void
fenestramm_draw_arrow (GtkStyle      *style,
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
  gboolean stepper = fill && (DETAIL ("hscrollbar") || DETAIL ("vscrollbar") ||
                              DETAIL ("spinbutton"));
  gboolean insensitive = state_type == GTK_STATE_INSENSITIVE;
  GdkGC *ink;
  gint base;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  if (stepper)
    {
      fenestramm_scroll_box (style, window, shadow_type, area, x, y, width,
                             height);
      /* pressed in, the arrow moves down and to the right */
      if (shadow_type == GTK_SHADOW_IN)
        {
          x += 1;
          y += 1;
        }
    }

  if (DETAIL ("menuitem") || DETAIL ("optionmenutab"))
    base = 7;
  else if (widget &&
           _gtk_theme_parent_is (widget->parent, gtk_combo_get_type ()))
    {
      /* a combo's: a scrollbar's arrow, centred in the button drawn
       * inside the combo's field, and moving with it */
      GtkWidget *button = widget->parent;

      base = 7;
      if (window == button->window)
        {
          _gtk_theme_button_rect (button, window, &x, &y, &width, &height);
          y += 2;
          width -= 2;
          height -= 4;
        }
      if (GTK_WIDGET_STATE (button) == GTK_STATE_ACTIVE)
        {
          x += 1;
          y += 1;
        }
    }
  else if (DETAIL ("spinbutton"))
    base = 5;
  else
    base = ((MIN (width, height) - 2) / 2) | 1;

  if (DETAIL ("menuitem") && state_type == GTK_STATE_PRELIGHT &&
      !_gtk_theme_parent_is (widget, gtk_menu_bar_get_type ()))
    ink = style->fg_gc[GTK_STATE_PRELIGHT];
  else
    ink = style->fg_gc[insensitive ? GTK_STATE_INSENSITIVE : GTK_STATE_NORMAL];

  if (insensitive && !_gtk_theme_is_dark (style))
    {
      FenestraMMColors c;

      fenestramm_colors_get (style, &c);
      fenestramm_triangle (window, c.highlight, area, arrow_type, base, x + 1,
                           y + 1, width, height);
      fenestramm_colors_release (&c);
    }
  fenestramm_triangle (window, ink, area, arrow_type, base, x, y, width,
                       height);
}

static void
fenestramm_draw_check (GtkStyle      *style,
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
  gboolean insensitive = state_type == GTK_STATE_INSENSITIVE ||
                         (widget && !GTK_WIDGET_IS_SENSITIVE (widget));
  FenestraMMColors c;
  GdkGC *ink;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  ink = style->text_gc[insensitive ? GTK_STATE_INSENSITIVE : GTK_STATE_NORMAL];
  fenestramm_colors_get (style, &c);
  if (widget && GTK_IS_MENU_ITEM (widget))
    {
      /* a menu's: only the tick */
      if (shadow_type == GTK_SHADOW_IN)
        {
          if (insensitive && !_gtk_theme_is_dark (style))
            fenestramm_glyph (window, c.highlight, area, check_mark,
                              G_N_ELEMENTS (check_mark),
                              x + 1 + (width - 7) / 2,
                              y + 1 + (height - 7) / 2);
          ink = style->fg_gc[insensitive ? GTK_STATE_INSENSITIVE : state_type];
          fenestramm_glyph (window, ink, area, check_mark,
                            G_N_ELEMENTS (check_mark),
                            x + (width - 7) / 2, y + (height - 7) / 2);
        }
    }
  else
    {
      /* thirteen pixels: a sunken box, white inside, and the tick in it */
      x += (width - 13) / 2;
      y += (height - 13) / 2;
      _gtk_theme_rectangle (window, insensitive ?
                            style->bg_gc[GTK_STATE_NORMAL] :
                            style->base_gc[GTK_STATE_NORMAL],
                            area, TRUE, x + 2, y + 2, 9, 9);
      fenestramm_edge (window, area, &c, FENESTRAMM_SUNKEN, x, y, 13, 13);
      if (shadow_type == GTK_SHADOW_IN)
        fenestramm_glyph (window, ink, area, check_mark,
                          G_N_ELEMENTS (check_mark), x + 3, y + 3);
    }
  fenestramm_colors_release (&c);
}

static void
fenestramm_draw_option (GtkStyle      *style,
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
  gboolean insensitive = state_type == GTK_STATE_INSENSITIVE ||
                         (widget && !GTK_WIDGET_IS_SENSITIVE (widget));
  FenestraMMColors c;
  gint i, j;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  fenestramm_colors_get (style, &c);
  if (widget && GTK_IS_MENU_ITEM (widget))
    {
      if (shadow_type == GTK_SHADOW_IN)
        {
          gint dx = x + (width - 6) / 2, dy = y + (height - 6) / 2;

          if (insensitive && !_gtk_theme_is_dark (style))
            fenestramm_glyph (window, c.highlight, area, menu_radio_dot,
                              G_N_ELEMENTS (menu_radio_dot), dx + 1, dy + 1);
          fenestramm_glyph (window,
                            style->fg_gc[insensitive ?
                                         GTK_STATE_INSENSITIVE : state_type],
                            area, menu_radio_dot, G_N_ELEMENTS (menu_radio_dot),
                            dx, dy);
        }
    }
  else
    {
      GdkGC *inside = insensitive ? style->bg_gc[GTK_STATE_NORMAL] :
                                    style->base_gc[GTK_STATE_NORMAL];
      GdkGC *gcs[4];

      x += (width - 12) / 2;
      y += (height - 12) / 2;
      /* the upper left half of each ring dark, the lower right light */
      gcs[0] = c.shadow;
      gcs[1] = c.highlight;
      gcs[2] = c.dark;
      gcs[3] = c.light;

      for (j = 0; j < 12; j++)
        for (i = 0; i < 12; i++)
          {
            gchar p = radio_rings[j][i];
            GdkGC *gc;

            if (p == ' ')
              continue;
            if (p == '.')
              gc = inside;
            else
              gc = gcs[(p == 'i' ? 2 : 0) + (i + j > 10 ? 1 : 0)];
            _gtk_theme_clip (gc, area);
            gdk_draw_point (window, gc, x + i, y + j);
            _gtk_theme_unclip (gc, area);
          }
      if (shadow_type == GTK_SHADOW_IN)
        fenestramm_glyph (window,
                          style->text_gc[insensitive ?
                                         GTK_STATE_INSENSITIVE :
                                         GTK_STATE_NORMAL],
                          area, radio_dot, G_N_ELEMENTS (radio_dot), x + 4,
                          y + 4);
    }
  fenestramm_colors_release (&c);
}

static void
fenestramm_draw_tab (GtkStyle      *style,
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

  if (DETAIL ("optionmenutab"))
    fenestramm_option_menu_tab (style, window, state_type, area, widget,
                                x, y, width, height);
  else
    gtk_paint_box (style, window, state_type, shadow_type, area, widget,
                   detail, x, y, width, height);
}

static void
fenestramm_draw_shadow_gap (GtkStyle        *style,
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
  GdkRectangle gap = { 0, 0, 0, 0 };

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);
  gtk_paint_shadow (style, window, state_type, shadow_type, area, widget,
                    detail, x, y, width, height);

  /* a frame's label sits in the gap */
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
fenestramm_draw_box_gap (GtkStyle        *style,
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
  fenestramm_page (style, window, area, x, y, width, height, TRUE, gap_side,
                   gap_x, gap_width);
}

static void
fenestramm_draw_extension (GtkStyle        *style,
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
  fenestramm_tab (style, window, state_type, area, widget, gap_side, x, y,
                  width, height);
}

static void
fenestramm_draw_focus (GtkStyle     *style,
                       GdkWindow    *window,
                       GdkRectangle *area,
                       GtkWidget    *widget,
                       gchar        *detail,
                       gint          x,
                       gint          y,
                       gint          width,
                       gint          height)
{
  GdkGC *ink = style->fg_gc[GTK_STATE_NORMAL];

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  /* GTK+ 1 passes the rectangle's size less one, as it is drawn */
  if (width == -1 || height == -1)
    {
      _gtk_theme_sanitize_size (window, &width, &height);
      width -= 1;
      height -= 1;
    }

  /* Windows shows no focus in a field, or on a scrollbar */
  if (DETAIL ("entry") || DETAIL ("text") ||
      (DETAIL ("trough") && widget && GTK_IS_SCROLLBAR (widget)))
    return;

  if (widget && GTK_IS_OPTION_MENU (widget))
    {
      /* round the text, inside the field's edge and left of the button */
      gint border = GTK_CONTAINER (widget)->border_width + 1;
      gint fw = widget->allocation.width - 2 * border;
      gint fh = widget->allocation.height - 2 * border;

      if (window == widget->window)
        fenestramm_dotted_rect (window, area, ink, border + 3, border + 3,
                                fw - 22, fh - 6);
      return;
    }

  if ((DETAIL ("button") || DETAIL ("togglebutton")) && widget &&
      GTK_IS_BUTTON (widget) && window == widget->window)
    {
      /* four pixels in from the button's edges */
      _gtk_theme_button_rect (widget, window, &x, &y, &width, &height);
      if (_gtk_theme_parent_is (widget, gtk_combo_get_type ()) ||
          _gtk_theme_parent_is (widget, gtk_clist_get_type ()) ||
          fenestramm_is_flat_button (widget))
        return;
      fenestramm_dotted_rect (window, area, ink, x + 4, y + 4, width - 8,
                              height - 8);
    }
  else if ((DETAIL ("checkbutton") || DETAIL ("radiobutton")) && widget &&
           GTK_IS_BIN (widget) && GTK_BIN (widget)->child)
    {
      /* round the label's text, a pixel out; the label, at the left of
       * the space it has, is drawn in the same window */
      GtkWidget *child = GTK_BIN (widget)->child;
      GtkAllocation *a = &child->allocation;
      GtkRequisition *r = &child->requisition;
      gint w = MIN (r->width, a->width), h = MIN (r->height, a->height);

      fenestramm_dotted_rect (window, area, ink, a->x - 1,
                              a->y + (a->height - h) / 2 - 1, w + 2, h + 2);
    }
  else
    fenestramm_dotted_rect (window, area, ink, x, y, width + 1, height + 1);
}

static void
fenestramm_draw_slider (GtkStyle       *style,
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
  fenestramm_scale_slider (style, window, state_type, area, x, y, width,
                           height);
}

/* A handle: Office's grip, a bar three pixels wide, thinly raised. A
 * handle box's runs along its edge, three pixels in; any other down its
 * middle. */
static void
fenestramm_draw_handle (GtkStyle       *style,
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
  FenestraMMColors c;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  _gtk_theme_sanitize_size (window, &width, &height);

  fenestramm_colors_get (style, &c);
  gtk_style_apply_default_background (style, window, TRUE, state_type, area,
                                      x, y, width, height);
  if (DETAIL ("handlebox"))
    {
      if (width <= height)
        fenestramm_edge (window, area, &c, FENESTRAMM_THIN_RAISED, x + 3, y + 3,
                         3, height - 6);
      else
        fenestramm_edge (window, area, &c, FENESTRAMM_THIN_RAISED, x + 3, y + 3,
                         width - 6, 3);
    }
  else if (orientation == GTK_ORIENTATION_VERTICAL)
    fenestramm_edge (window, area, &c, FENESTRAMM_THIN_RAISED,
                     x + (width - 3) / 2, y + 2, 3, height - 4);
  else
    fenestramm_edge (window, area, &c, FENESTRAMM_THIN_RAISED, x + 2,
                     y + (height - 3) / 2, width - 4, 3);
  fenestramm_colors_release (&c);
}

/* ---------------------------------------------------------------------
 * The engine
 */

static GtkStyleClass *fenestramm_class = NULL;

/* Windows' metrics: scrollbars sixteen pixels wide, with square
 * steppers and no trough round the thumb, which is never shorter than
 * eight; a scale's thumb eleven pixels long, with room round its
 * channel for the focus */
static void
fenestramm_set_props (GtkStyle *style)
{
  gtk_style_set_prop_experimental (style, "GtkRange::slider_width", 16);
  gtk_style_set_prop_experimental (style, "GtkRange::stepper_size", 16);
  gtk_style_set_prop_experimental (style, "GtkRange::stepper_spacing", 0);
  gtk_style_set_prop_experimental (style, "GtkRange::min_slider_size", 8);
  gtk_style_set_prop_experimental (style, "GtkRange::trough_border", 0);
  gtk_style_set_prop_experimental (style, "GtkScale::slider_length", 11);
  gtk_style_set_prop_experimental (style, "GtkScale::trough_border", 2);
  gtk_style_set_prop_experimental (style, "GtkButton::default_spacing", 0);
  gtk_style_set_prop_experimental (style, "GtkPaned::handle_full_size", 1);
  gtk_style_set_prop_experimental (style, "GtkPaned::handle_width", 4);
  gtk_style_set_prop_experimental (style, "GtkSpinButton::shadow_type",
                                   GTK_SHADOW_IN);
  gtk_style_set_prop_experimental (style, "GtkCheckButton::indicator_size", 13);
  gtk_style_set_prop_experimental (style, "GtkRadioButton::indicator_size", 13);
  gtk_style_set_prop_experimental (style, "GtkOptionMenu::indicator_width", 7);
  gtk_style_set_prop_experimental (style,
                                   "GtkOptionMenu::indicator_left_spacing", 3);
  gtk_style_set_prop_experimental (style,
                                   "GtkOptionMenu::indicator_right_spacing", 5);
}

static guint
fenestramm_parse_rc_style (GScanner   *scanner,
                           GtkRcStyle *rc_style)
{
  FenestraMMRcData *data;
  guint old_scope;
  guint token;

  old_scope = _gtk_theme_scope_enter (scanner, "fenestramm_theme_engine",
                                      fenestramm_symbols,
                                      G_N_ELEMENTS (fenestramm_symbols));

  data = g_new0 (FenestraMMRcData, 1);

  token = g_scanner_peek_next_token (scanner);
  while (token != G_TOKEN_RIGHT_CURLY)
    {
      g_scanner_get_next_token (scanner);

      switch (token)
        {
        case TOKEN_HIGHLIGHT:
          token = _gtk_theme_parse_color (scanner, &data->highlight);
          data->flags |= FENESTRAMM_HIGHLIGHT_SET;
          break;
        case TOKEN_LIGHT:
          token = _gtk_theme_parse_color (scanner, &data->light);
          data->flags |= FENESTRAMM_LIGHT_SET;
          break;
        case TOKEN_SHADOW:
          token = _gtk_theme_parse_color (scanner, &data->shadow);
          data->flags |= FENESTRAMM_SHADOW_SET;
          break;
        case TOKEN_DARK_SHADOW:
          token = _gtk_theme_parse_color (scanner, &data->dark_shadow);
          data->flags |= FENESTRAMM_DARK_SHADOW_SET;
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
fenestramm_merge_rc_style (GtkRcStyle *dest,
                           GtkRcStyle *src)
{
  FenestraMMRcData *src_data = src->engine_data;
  FenestraMMRcData *dest_data = dest->engine_data;
  guint flags;

  if (!dest_data)
    dest_data = dest->engine_data = g_new0 (FenestraMMRcData, 1);

  if (!src_data)
    return;

  flags = ~dest_data->flags & src_data->flags;

  if (flags & FENESTRAMM_HIGHLIGHT_SET)
    dest_data->highlight = src_data->highlight;
  if (flags & FENESTRAMM_LIGHT_SET)
    dest_data->light = src_data->light;
  if (flags & FENESTRAMM_SHADOW_SET)
    dest_data->shadow = src_data->shadow;
  if (flags & FENESTRAMM_DARK_SHADOW_SET)
    dest_data->dark_shadow = src_data->dark_shadow;

  dest_data->flags |= flags;
}

static void
fenestramm_rc_style_to_style (GtkStyle   *style,
                              GtkRcStyle *rc_style)
{
  if (!fenestramm_class)
    {
      fenestramm_class = g_new (GtkStyleClass, 1);
      *fenestramm_class = *style->klass;
      fenestramm_class->xthickness = 2;
      fenestramm_class->ythickness = 2;
      fenestramm_class->draw_hline = fenestramm_draw_hline;
      fenestramm_class->draw_vline = fenestramm_draw_vline;
      fenestramm_class->draw_shadow = fenestramm_draw_shadow;
      fenestramm_class->draw_polygon = _gtk_theme_draw_polygon;
      fenestramm_class->draw_arrow = fenestramm_draw_arrow;
      fenestramm_class->draw_diamond = _gtk_theme_draw_diamond;
      fenestramm_class->draw_string = fenestramm_draw_string;
      fenestramm_class->draw_box = fenestramm_draw_box;
      fenestramm_class->draw_flat_box = fenestramm_draw_flat_box;
      fenestramm_class->draw_check = fenestramm_draw_check;
      fenestramm_class->draw_option = fenestramm_draw_option;
      fenestramm_class->draw_tab = fenestramm_draw_tab;
      fenestramm_class->draw_shadow_gap = fenestramm_draw_shadow_gap;
      fenestramm_class->draw_box_gap = fenestramm_draw_box_gap;
      fenestramm_class->draw_extension = fenestramm_draw_extension;
      fenestramm_class->draw_focus = fenestramm_draw_focus;
      fenestramm_class->draw_slider = fenestramm_draw_slider;
      fenestramm_class->draw_handle = fenestramm_draw_handle;
    }

  style->klass = fenestramm_class;
  fenestramm_set_props (style);
}

static void
fenestramm_duplicate_style (GtkStyle *dest,
                            GtkStyle *src)
{
  dest->klass = src->klass;
  fenestramm_set_props (dest);
}

void
gtk_theme_fenestramm_init (GtkThemeEngine *engine)
{
  engine->parse_rc_style = fenestramm_parse_rc_style;
  engine->merge_rc_style = fenestramm_merge_rc_style;
  engine->rc_style_to_style = fenestramm_rc_style_to_style;
  engine->duplicate_style = fenestramm_duplicate_style;
  engine->destroy_rc_style = _gtk_theme_destroy_rc_style;
}
