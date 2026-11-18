/* GTK - The GIMP Toolkit
 * Clarus, a theme engine ported from Clearlooks
 *
 * Ported from the Clearlooks engine of gtk-engines 2.20.2, its
 * clearlooks_style.c, clearlooks_rc_style.c and support.c:
 * Copyright (C) 2005 Richard Stellingwerff <remenic@gmail.com>
 * Copyright (C) 2007 Benjamin Berg <benjamin@sipsolutions.net>
 * Copyright (C) 2007-2008 Andrea Cimitan <andrea.cimitan@gmail.com>
 * Originally written by Owen Taylor <otaylor@redhat.com>
 * and by Alexander Larsson <alexl@redhat.com>.
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

/* Clearlooks, the look of GNOME 2, drawn as Clearlooks drew it: its
 * drawing is in gtkthemeclarusclassic.c and the other styles' files,
 * kept as Clearlooks had it, drawn with GdkPen (gdk/gdkpen.c) in
 * place of cairo. This file is what GTK+ 1 asks for, turned into
 * what that drawing takes, as clearlooks_style.c turned GTK+ 2's.
 *
 * Where GTK+ 1 asks differently -- a scrollbar's steppers and a spin
 * button's are arrows drawn with a box under them, a scrollbar's
 * slider is a box, an option menu's arrow a tab of its own, a paned
 * widget's handle a box -- the drawing is what GTK+ 2 would have asked
 * Clearlooks for in their place. A button that can be the default is
 * drawn to its edges, and one with the focus keeps its size, as GTK+ 2
 * draws them. A scale's trough is filled to the slider, with the
 * focus round it in the trough border GTK+ 2 left for it. The arrow
 * keys can give the focus to a tab that is not the current one, and
 * Gummy shows it there as it does on the current tab.
 *
 * The engine takes Clearlooks' options, and xthickness and ythickness,
 * which a GTK+ 1 rc file cannot otherwise give a style:
 *
 *   engine "clarus"
 *   {
 *     style              = CLASSIC | GLOSSY | INVERTED | GUMMY
 *     contrast           = 1.0
 *     radius             = 3.0
 *     reliefstyle        = 0 | 1
 *     menubarstyle       = 0 | 1 | 2 | 3
 *     toolbarstyle       = 0 | 1 | 2
 *     colorize_scrollbar = TRUE
 *     scrollbar_color    = "#..."
 *     focus_color        = "#..."
 *     disable_focus      = FALSE
 *     xthickness         = 1
 *     ythickness         = 1
 *   }
 *
 * Clearlooks' animation and hint options are GTK+ 2's, and are taken
 * and left alone.
 */

#include "gtkthemeclarus.h"

#include "gtkbin.h"
#include "gtkbutton.h"
#include "gtkcheckmenuitem.h"
#include "gtkclist.h"
#include "gtkcombo.h"
#include "gtkentry.h"
#include "gtkframe.h"
#include "gtkhandlebox.h"
#include "gtkhscale.h"
#include "gtkhscrollbar.h"
#include "gtkmenu.h"
#include "gtkmenubar.h"
#include "gtkmenuitem.h"
#include "gtknotebook.h"
#include "gtkoptionmenu.h"
#include "gtkprogress.h"
#include "gtkprogressbar.h"
#include "gtkrange.h"
#include "gtkrc.h"
#include "gtkscale.h"
#include "gtkscrollbar.h"
#include "gtkspinbutton.h"
#include "gtkstatusbar.h"
#include "gtkstyle.h"
#include "gtktext.h"
#include "gtkthemes.h"
#include "gtkthemebuiltin.h"
#include "gtkthemecommon.h"
#include "gtktogglebutton.h"
#include "gtktoolbar.h"
#include "gtkvpaned.h"
#include "gtkvscale.h"
#include "gtkwidget.h"

#define DETAIL(xx) ((detail) && (!strcmp ((xx), (detail))))

/* ---------------------------------------------------------------------
 * Options
 */

enum
{
  CLARUS_STYLE_SET              = 1 << 0,
  CLARUS_FOCUS_COLOR_SET        = 1 << 1,
  CLARUS_SCROLLBAR_COLOR_SET    = 1 << 2,
  CLARUS_COLORIZE_SCROLLBAR_SET = 1 << 3,
  CLARUS_CONTRAST_SET           = 1 << 4,
  CLARUS_RELIEFSTYLE_SET        = 1 << 5,
  CLARUS_MENUBARSTYLE_SET       = 1 << 6,
  CLARUS_TOOLBARSTYLE_SET       = 1 << 7,
  CLARUS_RADIUS_SET             = 1 << 8,
  CLARUS_DISABLE_FOCUS_SET      = 1 << 9,
  CLARUS_XTHICKNESS_SET         = 1 << 10,
  CLARUS_YTHICKNESS_SET         = 1 << 11
};

typedef struct
{
  guint flags;
  ClarusStyles style;
  GdkColor focus_color;
  GdkColor scrollbar_color;
  gboolean colorize_scrollbar;
  gdouble contrast;
  gint reliefstyle;
  gint menubarstyle;
  gint toolbarstyle;
  gdouble radius;
  gboolean disable_focus;
  gint xthickness;
  gint ythickness;
} ClarusRcData;

enum
{
  TOKEN_FOCUSCOLOR = G_TOKEN_LAST + 1,
  TOKEN_SCROLLBARCOLOR,
  TOKEN_COLORIZESCROLLBAR,
  TOKEN_CONTRAST,
  TOKEN_RELIEFSTYLE,
  TOKEN_MENUBARSTYLE,
  TOKEN_TOOLBARSTYLE,
  TOKEN_ANIMATION,
  TOKEN_STYLE,
  TOKEN_RADIUS,
  TOKEN_HINT,
  TOKEN_DISABLE_FOCUS,
  TOKEN_XTHICKNESS,
  TOKEN_YTHICKNESS,
  TOKEN_CLASSIC,
  TOKEN_GLOSSY,
  TOKEN_INVERTED,
  TOKEN_GUMMY,
  TOKEN_TRUE,
  TOKEN_FALSE
};

static const GtkThemeSymbol clarus_symbols[] = {
  { "focus_color",        TOKEN_FOCUSCOLOR },
  { "scrollbar_color",    TOKEN_SCROLLBARCOLOR },
  { "colorize_scrollbar", TOKEN_COLORIZESCROLLBAR },
  { "contrast",           TOKEN_CONTRAST },
  { "reliefstyle",        TOKEN_RELIEFSTYLE },
  { "menubarstyle",       TOKEN_MENUBARSTYLE },
  { "toolbarstyle",       TOKEN_TOOLBARSTYLE },
  { "animation",          TOKEN_ANIMATION },
  { "style",              TOKEN_STYLE },
  { "radius",             TOKEN_RADIUS },
  { "hint",               TOKEN_HINT },
  { "disable_focus",      TOKEN_DISABLE_FOCUS },
  { "xthickness",         TOKEN_XTHICKNESS },
  { "ythickness",         TOKEN_YTHICKNESS },
  { "CLASSIC",            TOKEN_CLASSIC },
  { "GLOSSY",             TOKEN_GLOSSY },
  { "INVERTED",           TOKEN_INVERTED },
  { "GUMMY",              TOKEN_GUMMY },
  { "TRUE",               TOKEN_TRUE },
  { "FALSE",              TOKEN_FALSE }
};

/* What a style draws with: its options, as Clearlooks' style had them,
 * and the colours worked out from its own */
typedef struct
{
  ClarusColors colors;
  ClarusStyles style;
  guint8 reliefstyle;
  guint8 menubarstyle;
  guint8 toolbarstyle;
  GdkColor focus_color;
  gboolean has_focus_color;
  GdkColor scrollbar_color;
  gboolean colorize_scrollbar;
  gboolean has_scrollbar_color;
  gfloat radius;
  gboolean disable_focus;
  gboolean ready;               /* worked out from the style */
} ClarusStyle;

static ClarusStyleFunctions style_functions[CL_NUM_STYLES];
static ClarusStyleConstants style_constants[CL_NUM_STYLES];

#define STYLE_FUNCTION(function) \
  (style_functions[clarus_style (style)->style].function)

/* clearlooks_style_init_from_rc () and clearlooks_style_realize () */
static void
clarus_style_get (GtkStyle *style, ClarusStyle *ps)
{
  static const double shades[] =
    { 1.15, 0.95, 0.896, 0.82, 0.7, 0.665, 0.475, 0.45, 0.4 };
  ClarusRcData *rc = style->rc_style ? style->rc_style->engine_data : NULL;
  guint flags = rc ? rc->flags : 0;
  CairoColor spot_color, bg_normal;
  gdouble contrast;
  gint i;

  ps->style = (flags & CLARUS_STYLE_SET) ? rc->style : CL_STYLE_CLASSIC;
  ps->reliefstyle = (flags & CLARUS_RELIEFSTYLE_SET) ? rc->reliefstyle : 0;
  ps->menubarstyle = (flags & CLARUS_MENUBARSTYLE_SET) ? rc->menubarstyle : 0;
  ps->toolbarstyle = (flags & CLARUS_TOOLBARSTYLE_SET) ? rc->toolbarstyle : 0;
  ps->has_focus_color = (flags & CLARUS_FOCUS_COLOR_SET) != 0;
  if (ps->has_focus_color)
    ps->focus_color = rc->focus_color;
  ps->has_scrollbar_color = (flags & CLARUS_SCROLLBAR_COLOR_SET) != 0;
  if (ps->has_scrollbar_color)
    ps->scrollbar_color = rc->scrollbar_color;
  ps->colorize_scrollbar = (flags & CLARUS_COLORIZE_SCROLLBAR_SET) ?
                           rc->colorize_scrollbar : FALSE;
  ps->radius = CLAMP ((flags & CLARUS_RADIUS_SET)   ? rc->radius : 3.0,
                      0.0, 10.0);
  ps->disable_focus = (flags & CLARUS_DISABLE_FOCUS_SET) ?
                      rc->disable_focus : FALSE;
  contrast = (flags & CLARUS_CONTRAST_SET) ? rc->contrast : 1.0;
  if (contrast <= 0)
    contrast = 1.0;
  ps->ready = TRUE;

  /* Lighter to darker */
  ge_gdk_color_to_cairo (&style->bg[GTK_STATE_NORMAL], &bg_normal);
  for (i = 0; i < 9; i++)
    ge_shade_color (&bg_normal, (shades[i] < 1.0) ?
                    (shades[i] / contrast) : (shades[i] * contrast),
                    &ps->colors.shade[i]);

  ge_gdk_color_to_cairo (&style->bg[GTK_STATE_SELECTED], &spot_color);
  ge_shade_color (&spot_color, 1.25, &ps->colors.spot[0]);
  ge_shade_color (&spot_color, 1.05, &ps->colors.spot[1]);
  ge_shade_color (&spot_color, 0.65, &ps->colors.spot[2]);

  for (i = 0; i < 5; i++)
    {
      ge_gdk_color_to_cairo (&style->fg[i], &ps->colors.fg[i]);
      ge_gdk_color_to_cairo (&style->bg[i], &ps->colors.bg[i]);
      ge_gdk_color_to_cairo (&style->base[i], &ps->colors.base[i]);
      ge_gdk_color_to_cairo (&style->text[i], &ps->colors.text[i]);
    }
}

/* The style's options and colours, kept with it until it changes */
static ClarusStyle *
clarus_style (GtkStyle *style)
{
  ClarusStyle *ps = style->engine_data;

  if (!ps)
    ps = style->engine_data = g_new0 (ClarusStyle, 1);
  if (!ps->ready)
    clarus_style_get (style, ps);
  return ps;
}

/* ---------------------------------------------------------------------
 * Where a widget is: support.c
 */

/* A combo's entry, button or arrow: GTK+ 2's comboboxentry hint */
static gboolean
clarus_in_combo (GtkWidget *widget)
{
  return _gtk_theme_parent_is (widget, gtk_combo_get_type ()) ||
         (widget &&
          _gtk_theme_parent_is (widget->parent, gtk_combo_get_type ()));
}

static void
clarus_clist_get_header_index (GtkCList *clist, GtkWidget *button,
                               gint *column_index, gint *columns)
{
  gint i;

  *columns = clist->columns;
  for (i = 0; i < *columns; i++)
    if (clist->column[i].button == button)
      {
        *column_index = i;
        break;
      }
}

/* The background a widget without a window of its own is drawn on */
static void
clarus_get_parent_bg (GtkWidget *widget, CairoColor *color)
{
  GtkWidget *parent;

  if (widget == NULL)
    return;

  parent = widget->parent;
  while (parent)
    {
      if (!GTK_WIDGET_NO_WINDOW (parent))
        break;
      if (GTK_IS_NOTEBOOK (parent) && GTK_NOTEBOOK (parent)->show_tabs &&
          GTK_NOTEBOOK (parent)->show_border)
        break;
      if (GTK_IS_TOOLBAR (parent))
        break;
      parent = parent->parent;
    }
  if (parent == NULL)
    return;

  ge_gdk_color_to_cairo (&parent->style->bg[GTK_WIDGET_STATE (parent)], color);
}

/* Which of GTK+ 1's two steppers a window is: the first, A, or the last,
 * D, as GTK+ 2 names its four */
static ClarusStepper
clarus_scrollbar_get_stepper (GtkWidget *widget, GdkWindow *window)
{
  if (!widget || !GTK_IS_RANGE (widget))
    return CL_STEPPER_UNKNOWN;
  if (window == GTK_RANGE (widget)->step_back)
    return CL_STEPPER_A;
  if (window == GTK_RANGE (widget)->step_forw)
    return CL_STEPPER_D;
  return CL_STEPPER_UNKNOWN;
}

/* Whether the slider is against a stepper */
static ClarusJunction
clarus_scrollbar_get_junction (GtkWidget *widget)
{
  GtkAdjustment *adj;
  ClarusJunction junction = CL_JUNCTION_NONE;

  if (!widget || !GTK_IS_RANGE (widget) || !GTK_RANGE (widget)->adjustment)
    return CL_JUNCTION_NONE;

  adj = GTK_RANGE (widget)->adjustment;
  if (adj->value <= adj->lower)
    junction |= CL_JUNCTION_BEGIN;
  if (adj->value >= adj->upper - adj->page_size)
    junction |= CL_JUNCTION_END;
  return junction;
}

static void
clarus_set_toolbar_parameters (ToolbarParameters *toolbar,
                               GtkWidget *widget, GdkWindow *window,
                               gint x, gint y)
{
  toolbar->topmost = FALSE;
  if (x == 0 && y == 0 && widget && widget->allocation.x == 0 &&
      widget->allocation.y == 0 && widget->window == window &&
      GTK_IS_TOOLBAR (widget))
    toolbar->topmost = TRUE;
}

/* Whether a notebook's tabs reach its start and its end, for which of
 * its page's corners are square */
static void
clarus_get_notebook_tab_position (GtkWidget *widget, gboolean *start,
                                  gboolean *end)
{
  GList *list;
  gboolean found_tabs = FALSE;

  *start = TRUE;
  *end = FALSE;
  if (!widget || !GTK_IS_NOTEBOOK (widget))
    return;

  for (list = GTK_NOTEBOOK (widget)->children; list; list = list->next)
    {
      GtkNotebookPage *page = list->data;

      if (!page->tab_label || !GTK_WIDGET_VISIBLE (page->tab_label))
        continue;
      if (!GTK_WIDGET_MAPPED (page->tab_label))
        {
          /* scrolled out of sight: the arrows are showing */
          *start = FALSE;
          *end = FALSE;
          return;
        }
      if (!found_tabs)
        {
          found_tabs = TRUE;
          *start = FALSE;
          *end = FALSE;
        }
      if (page->expand)
        {
          *start = TRUE;
          *end = TRUE;
        }
      else if (page->pack == GTK_PACK_START)
        *start = TRUE;
      else
        *end = TRUE;
    }
}

/* ---------------------------------------------------------------------
 * clearlooks_style.c, for GTK+ 1
 */

static void
clarus_set_widget_parameters (GtkWidget        *widget,
                              GtkStyle         *style,
                              GtkStateType      state_type,
                              WidgetParameters *params)
{
  ClarusStyle *ps = clarus_style (style);

  params->style_functions = &style_functions[ps->style];
  params->style_constants = &style_constants[ps->style];

  params->active        = (state_type == GTK_STATE_ACTIVE);
  params->prelight      = (state_type == GTK_STATE_PRELIGHT);
  params->disabled      = (state_type == GTK_STATE_INSENSITIVE);
  params->state_type    = (ClarusStateType) state_type;
  params->corners       = CR_CORNER_ALL;
  params->ltr           = TRUE;
  params->focus         = !ps->disable_focus && widget &&
                          GTK_WIDGET_HAS_FOCUS (widget);
  params->is_default    = widget && GTK_WIDGET_HAS_DEFAULT (widget);
  params->enable_shadow = FALSE;
  params->radius        = ps->radius;

  params->xthickness    = style->klass->xthickness;
  params->ythickness    = style->klass->ythickness;

  /* This is used in GtkEntry to fake transparency. */
  params->parentbg = ps->colors.bg[state_type];
  clarus_get_parent_bg (widget, &params->parentbg);
}

/* The drawing a call makes, and what it draws on: the area GTK+ asks
 * for, with a few pixels round it for what Clearlooks draws outside */
static GdkPen *
clarus_pen (GdkWindow *window, GdkRectangle *area, gint x, gint y,
            gint width, gint height)
{
  GdkPen *cr = _gdk_pen_new (window, area, x - 4, y - 4,
                             width + 8, height + 8);

  /* as ge_gdk_drawable_to_cairo () had it */
  _gdk_pen_set_line_width (cr, 1.0);
  _gdk_pen_set_line_cap (cr, GDK_CAP_PROJECTING);
  _gdk_pen_set_line_join (cr, GDK_JOIN_MITER);
  return cr;
}

static void
clarus_focus_color (GtkStyle *style, FocusParameters *focus,
                    const CairoColor *otherwise)
{
  ClarusStyle *ps = clarus_style (style);

  if (ps->has_focus_color)
    {
      ge_gdk_color_to_cairo (&ps->focus_color, &focus->color);
      focus->has_color = TRUE;
    }
  else
    {
      focus->color = *otherwise;
      focus->has_color = FALSE;
    }
}

static void
clarus_style_draw_flat_box (GtkStyle      *style,
                        GdkWindow   *window,
                        GtkStateType state_type,
                        GtkShadowType shadow_type,
                        GdkRectangle *area,
                        GtkWidget   *widget,
                        gchar       *detail,
                        gint         x,
                        gint         y,
                        gint         width,
                        gint         height)
{
  ClarusStyle *ps = clarus_style (style);

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);
  _gtk_theme_sanitize_size (window, &width, &height);

  if (DETAIL ("tooltip"))
    {
      WidgetParameters params;
      GdkPen *cr = clarus_pen (window, area, x, y, width, height);

      clarus_set_widget_parameters (widget, style, state_type, &params);
      STYLE_FUNCTION (draw_tooltip) (cr, &ps->colors, &params, x, y, width,
                                     height);
      _gdk_pen_free (cr);
    }
  else if ((ps->style == CL_STYLE_GLOSSY || ps->style == CL_STYLE_GUMMY) &&
           (DETAIL ("checkbutton") || DETAIL ("radiobutton")) &&
           state_type == GTK_STATE_PRELIGHT)
    {
      /* Don't draw any check/radiobutton bg in GLOSSY or GUMMY mode. */
    }
  else
    {
      GdkGC *gc = style->bg_gc[state_type];

      if (DETAIL ("selected") ||
          (DETAIL ("text") && state_type == GTK_STATE_SELECTED))
        gc = style->bg_gc[GTK_STATE_SELECTED];
      else if (DETAIL ("viewportbin"))
        gc = style->bg_gc[GTK_STATE_NORMAL];
      else if (DETAIL ("entry_bg"))
        gc = style->base_gc[state_type];

      if (gc != style->bg_gc[state_type] || !style->bg_pixmap[state_type] ||
          gdk_window_get_type (window) == GDK_WINDOW_PIXMAP)
        _gtk_theme_rectangle (window, gc, area, TRUE, x, y, width, height);
      else
        gtk_style_apply_default_background (style, window, TRUE, state_type,
                                            area, x, y, width, height);
    }
}

static void
clarus_style_draw_shadow (GtkStyle      *style,
                      GdkWindow   *window,
                      GtkStateType state_type,
                      GtkShadowType shadow_type,
                      GdkRectangle *area,
                      GtkWidget   *widget,
                      gchar       *detail,
                      gint         x,
                      gint         y,
                      gint         width,
                      gint         height)
{
  ClarusStyle *ps = clarus_style (style);
  ClarusColors *colors = &ps->colors;
  GdkPen *cr;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);
  _gtk_theme_sanitize_size (window, &width, &height);
  /* GTK+ 2's entry draws its focus in its frame */
  _gtk_theme_entry_frame (widget, detail, &x, &y, &width, &height);

  cr = clarus_pen (window, area, x, y, width, height);

  if (DETAIL ("entry") || DETAIL ("text"))
    {
      WidgetParameters params;
      FocusParameters  focus;

      /* GtkEntry doesn't set the INSENSITIVE state */
      if (state_type == GTK_STATE_NORMAL && widget)
        state_type = GTK_WIDGET_STATE (widget);

      clarus_set_widget_parameters (widget, style, state_type, &params);

      if (clarus_in_combo (widget) ||
          (widget && GTK_IS_SPIN_BUTTON (widget)))
        {
          width += style->klass->xthickness;
          params.corners = CR_CORNER_TOPLEFT | CR_CORNER_BOTTOMLEFT;
        }

      /* Fill the background as it is initialised to base[NORMAL] */
      _gdk_pen_rectangle (cr, 0, 0, width, height);
      ge_cairo_set_color (cr, &params.parentbg);
      _gdk_pen_fill (cr);

      clarus_focus_color (style, &focus, &colors->spot[2]);
      STYLE_FUNCTION (draw_entry) (cr, colors, &params, &focus,
                                   x, y, width, height);
    }
  else if (DETAIL ("frame") &&
           _gtk_theme_parent_is (widget, gtk_statusbar_get_type ()))
    {
      WidgetParameters params;

      clarus_set_widget_parameters (widget, style, state_type, &params);
      gtk_style_apply_default_background (style, window, TRUE, state_type,
                                          area, x, y, width, height);
      if (shadow_type != GTK_SHADOW_NONE)
        STYLE_FUNCTION (draw_statusbar) (cr, colors, &params,
                                         x, y, width, height);
    }
  else if (DETAIL ("frame"))
    {
      WidgetParameters params;
      FrameParameters  frame;

      frame.shadow  = (ClarusShadowType) shadow_type;
      frame.gap_x   = -1;  /* No gap will be drawn */
      frame.border  = &colors->shade[4];

      clarus_set_widget_parameters (widget, style, state_type, &params);
      params.corners = CR_CORNER_NONE;

      STYLE_FUNCTION (draw_frame) (cr, colors, &params, &frame,
                                   x, y, width, height);
    }
  else if (DETAIL ("scrolled_window") || DETAIL ("viewport") || detail == NULL)
    {
      CairoColor border;

      if (ps->style == CL_STYLE_CLASSIC)
        ge_shade_color (&colors->bg[0], 0.78, &border);
      else
        border = colors->shade[5];

      _gdk_pen_rectangle (cr, x + 0.5, y + 0.5, width - 1, height - 1);
      ge_cairo_set_color (cr, &border);
      _gdk_pen_set_line_width (cr, 1);
      _gdk_pen_stroke (cr);
    }
  else
    {
      WidgetParameters params;
      FrameParameters frame;

      frame.shadow = (ClarusShadowType) shadow_type;
      frame.gap_x  = -1;
      frame.border = &colors->shade[5];
      clarus_set_widget_parameters (widget, style, state_type, &params);
      params.corners = CR_CORNER_ALL;

      STYLE_FUNCTION (draw_frame) (cr, colors, &params, &frame,
                                   x, y, width, height);
    }

  _gdk_pen_free (cr);
}

static void
clarus_style_draw_box_gap (GtkStyle        *style,
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
                       GtkPositionType gap_side,
                       gint           gap_x,
                       gint           gap_width)
{
  ClarusColors *colors = &clarus_style (style)->colors;
  WidgetParameters params;
  FrameParameters frame;
  gboolean start, end;
  GdkPen *cr;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);
  _gtk_theme_sanitize_size (window, &width, &height);

  cr = clarus_pen (window, area, x, y, width, height);

  frame.shadow    = (ClarusShadowType) shadow_type;
  frame.gap_side  = (ClarusGapSide) gap_side;
  frame.gap_x     = gap_x;
  frame.gap_width = gap_width;
  frame.border    = &colors->shade[5];

  clarus_set_widget_parameters (widget, style, state_type, &params);
  clarus_get_notebook_tab_position (widget, &start, &end);

  params.corners = CR_CORNER_ALL;
  switch (gap_side)
    {
    case GTK_POS_TOP:
      if (start)
        params.corners ^= CR_CORNER_TOPLEFT;
      if (end)
        params.corners ^= CR_CORNER_TOPRIGHT;
      break;
    case GTK_POS_BOTTOM:
      if (start)
        params.corners ^= CR_CORNER_BOTTOMLEFT;
      if (end)
        params.corners ^= CR_CORNER_BOTTOMRIGHT;
      break;
    case GTK_POS_LEFT:
      if (start)
        params.corners ^= CR_CORNER_TOPLEFT;
      if (end)
        params.corners ^= CR_CORNER_BOTTOMLEFT;
      break;
    case GTK_POS_RIGHT:
      if (start)
        params.corners ^= CR_CORNER_TOPRIGHT;
      if (end)
        params.corners ^= CR_CORNER_BOTTOMRIGHT;
      break;
    }

  /* Fill the background with bg[NORMAL] */
  ge_cairo_rounded_rectangle (cr, x, y, width, height, params.radius,
                              params.corners);
  ge_cairo_set_color (cr, &colors->bg[GTK_STATE_NORMAL]);
  _gdk_pen_fill (cr);

  STYLE_FUNCTION (draw_frame) (cr, colors, &params, &frame,
                               x, y, width, height);
  _gdk_pen_free (cr);
}

/* Whether the notebook's tab at (x, y) has the focus. In GTK+ 2 only
 * the current tab takes it, and Clearlooks shows it on the current tab
 * whenever the notebook has the focus; GTK+ 1's arrow keys move it to
 * the others. */
static gboolean
clarus_tab_has_focus (GtkWidget *widget, gint x, gint y)
{
  GtkNotebook *notebook = GTK_NOTEBOOK (widget);
  GtkNotebookPage *page;

  if (!GTK_WIDGET_HAS_FOCUS (widget) || !notebook->focus_tab)
    return FALSE;

  page = notebook->focus_tab->data;
  return page->allocation.x == x && page->allocation.y == y;
}

/* The focus on a tab that is not the current one, as Gummy's draw_tab
 * draws it on the current tab */
static void
clarus_gummy_tab_focus (GdkPen                 *cr,
                        const WidgetParameters *params,
                        const TabParameters    *tab,
                        int x, int y, int width, int height)
{
  CairoColor focus_fill = tab->focus.color;
  CairoColor fill_shade1, fill_shade2, fill_shade3;
  CairoColor focus_border;
  GdkPenPattern *pattern;
  double radius;
  double stripe_size = 2.0;
  double focus_inset_x, focus_inset_y;
  double border_alpha = 0.54;
  double fill_alpha = 0.17;

  radius = MIN (params->radius,
                MIN ((width - 2.0) / 2.0, (height - 2.0) / 2.0));

  _gdk_pen_save (cr);
  _gdk_pen_rectangle (cr, x, y, width, height);
  _gdk_pen_clip (cr);
  _gdk_pen_new_path (cr);

  _gdk_pen_set_line_width (cr, 1.0);
  _gdk_pen_translate (cr, x + 0.5, y + 0.5);

  if (tab->gap_side == CL_GAP_TOP || tab->gap_side == CL_GAP_BOTTOM)
    {
      if (params->ythickness == 3)
        stripe_size = 3.0;
      height += 3;
      if (tab->gap_side == CL_GAP_TOP)
        _gdk_pen_translate (cr, 0.0, -3.0);
      focus_inset_x = 4;
      focus_inset_y = stripe_size + 3;
    }
  else
    {
      if (params->xthickness == 3)
        stripe_size = 3.0;
      width += 3;
      if (tab->gap_side == CL_GAP_LEFT)
        _gdk_pen_translate (cr, -3.0, 0.0);
      focus_inset_x = stripe_size + 3;
      focus_inset_y = 4;
    }

  ge_shade_color (&focus_fill, 0.65, &focus_border);
  ge_shade_color (&focus_fill, 1.18, &fill_shade1);
  ge_shade_color (&focus_fill, 1.02, &fill_shade2);
  ge_shade_color (&focus_fill, 0.84, &fill_shade3);

  ge_cairo_rounded_rectangle (cr, focus_inset_x, focus_inset_y,
                              width - focus_inset_x * 2 - 1,
                              height - focus_inset_y * 2 - 1,
                              radius - 1, CR_CORNER_ALL);
  pattern = _gdk_pen_pattern_new_linear (0, 0, 0, height);
  _gdk_pen_pattern_add_stop_rgba (pattern, 0.0, fill_shade1.r,
                                  fill_shade1.g, fill_shade1.b,
                                  fill_alpha);
  _gdk_pen_pattern_add_stop_rgba (pattern, 0.5, fill_shade2.r,
                                  fill_shade2.g, fill_shade2.b,
                                  fill_alpha);
  _gdk_pen_pattern_add_stop_rgba (pattern, 0.5, focus_fill.r,
                                  focus_fill.g, focus_fill.b, fill_alpha);
  _gdk_pen_pattern_add_stop_rgba (pattern, 1.0, fill_shade3.r,
                                  fill_shade3.g, fill_shade3.b,
                                  fill_alpha);
  _gdk_pen_set_pattern (cr, pattern);
  _gdk_pen_fill_preserve (cr);
  _gdk_pen_pattern_unref (pattern);

  clarus_set_mixed_color (cr, &params->parentbg, &focus_border,
                          border_alpha);
  _gdk_pen_stroke (cr);
  _gdk_pen_restore (cr);
}

static void
clarus_style_draw_extension (GtkStyle        *style,
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
                         GtkPositionType gap_side)
{
  ClarusColors *colors = &clarus_style (style)->colors;
  WidgetParameters params;
  TabParameters    tab;
  FocusParameters  focus;
  GdkPen *cr;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);
  _gtk_theme_sanitize_size (window, &width, &height);

  cr = clarus_pen (window, area, x, y, width, height);
  clarus_set_widget_parameters (widget, style, state_type, &params);

  tab.gap_side = (ClarusGapSide) gap_side;
  switch (gap_side)
    {
    case GTK_POS_TOP:
      params.corners = CR_CORNER_BOTTOMLEFT | CR_CORNER_BOTTOMRIGHT;
      break;
    case GTK_POS_BOTTOM:
      params.corners = CR_CORNER_TOPLEFT | CR_CORNER_TOPRIGHT;
      break;
    case GTK_POS_LEFT:
      params.corners = CR_CORNER_TOPRIGHT | CR_CORNER_BOTTOMRIGHT;
      break;
    case GTK_POS_RIGHT:
      params.corners = CR_CORNER_TOPLEFT | CR_CORNER_BOTTOMLEFT;
      break;
    }

  clarus_focus_color (style, &focus, &colors->bg[GTK_STATE_SELECTED]);
  tab.focus = focus;

  if (widget && GTK_IS_NOTEBOOK (widget))
    params.focus = params.focus && clarus_tab_has_focus (widget, x, y);

  /* Clearlooks leaves its clip and translation in place */
  _gdk_pen_save (cr);
  STYLE_FUNCTION (draw_tab) (cr, colors, &params, &tab, x, y, width, height);
  _gdk_pen_restore (cr);
  if (params.focus && params.active &&
      clarus_style (style)->style == CL_STYLE_GUMMY)
    clarus_gummy_tab_focus (cr, &params, &tab, x, y, width, height);
  _gdk_pen_free (cr);
}

static void
clarus_handle (GtkStyle *style, GdkWindow *window, GtkStateType state_type,
               GdkRectangle *area, GtkWidget *widget,
               ClarusHandleType type, gboolean horizontal,
               gint x, gint y, gint width, gint height)
{
  WidgetParameters params;
  HandleParameters handle;
  GdkPen *cr = clarus_pen (window, area, x, y, width, height);

  clarus_set_widget_parameters (widget, style, state_type, &params);
  handle.type = type;
  handle.horizontal = horizontal;
  STYLE_FUNCTION (draw_handle) (cr, &clarus_style (style)->colors, &params,
                                &handle, x, y, width, height);
  _gdk_pen_free (cr);
}

static void
clarus_style_draw_handle (GtkStyle       *style,
                      GdkWindow    *window,
                      GtkStateType  state_type,
                      GtkShadowType shadow_type,
                      GdkRectangle *area,
                      GtkWidget    *widget,
                      gchar        *detail,
                      gint          x,
                      gint          y,
                      gint          width,
                      gint          height,
                      GtkOrientation orientation)
{
  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);
  _gtk_theme_sanitize_size (window, &width, &height);

  /* GTK+ 1's handle box draws its handle as tall as it is: the grip
   * runs along it, as GTK+ 2 draws it */
  if (DETAIL ("handlebox"))
    orientation = width < height ? GTK_ORIENTATION_VERTICAL :
                                   GTK_ORIENTATION_HORIZONTAL;
  clarus_handle (style, window, state_type, area, widget,
                 DETAIL ("paned") ? CL_HANDLE_SPLITTER : CL_HANDLE_TOOLBAR,
                 orientation == GTK_ORIENTATION_HORIZONTAL,
                 x, y, width, height);
}

/* A scrollbar's stepper, or a spin button's, with its arrow: GTK+ 1
 * draws them as arrows to be filled; GTK+ 2 asks for the box and then
 * the arrow, in a smaller box */
static void
clarus_stepper (GtkStyle *style, GdkWindow *window, GtkStateType state_type,
                GtkShadowType shadow_type, GdkRectangle *area,
                GtkWidget *widget, gchar *detail, GtkArrowType arrow_type,
                gint x, gint y, gint width, gint height)
{
  ClarusStyle *ps = clarus_style (style);
  ClarusColors *colors = &ps->colors;
  WidgetParameters params;
  ArrowParameters arrow;
  GdkPen *cr = clarus_pen (window, area, x, y, width, height);
  gint aw, ah, ax, ay;

  /* GTK+ 1 etches a spin button's arrow that can go no further, where
   * GTK+ 2 makes it insensitive */
  if (DETAIL ("spinbutton") && shadow_type == GTK_SHADOW_ETCHED_IN)
    state_type = GTK_STATE_INSENSITIVE;

  clarus_set_widget_parameters (widget, style, state_type, &params);

  if (DETAIL ("spinbutton"))
    {
      /* only pressed is it drawn; GTK+ 2's "spinbutton_up" and
       * "spinbutton_down" */
      if (state_type == GTK_STATE_ACTIVE)
        {
          gint bx = x, by = y, bw = width, bh = height;

          if (style->klass->xthickness == 3)
            {
              bw++;
              bx--;
            }
          if (arrow_type == GTK_ARROW_UP)
            {
              bh += 2;
              params.corners = CR_CORNER_TOPRIGHT;
            }
          else
            params.corners = CR_CORNER_BOTTOMRIGHT;
          STYLE_FUNCTION (draw_spinbutton_down) (cr, colors, &params,
                                                 bx, by, bw, bh);
        }

      /* gtk_spin_button_draw_arrow () */
      aw = width / 2;
      aw -= aw % 2 - 1;         /* force odd */
      ah = (aw + 1) / 2;
      ax = x + (width - aw) / 2;
      ay = y + (height - ah) / 2;
      if (arrow_type == GTK_ARROW_UP)
        ay -= 1;
      else
        ay += 1;
    }
  else
    {
      ScrollBarParameters scrollbar;
      ScrollBarStepperParameters stepper;

      params.corners = CR_CORNER_NONE;
      scrollbar.has_color  = ps->colorize_scrollbar || ps->has_scrollbar_color;
      scrollbar.horizontal = DETAIL ("hscrollbar");
      scrollbar.junction   = clarus_scrollbar_get_junction (widget);
      stepper.stepper = clarus_scrollbar_get_stepper (widget, window);

      STYLE_FUNCTION (draw_scrollbar_stepper) (cr, colors, &params, &scrollbar,
                                               &stepper, x, y, width, height);

      /* gtk_range_draw_stepper () */
      aw = width / 2;
      ah = height / 2;
      ax = x + (width - aw) / 2;
      ay = y + (height - ah) / 2;
    }

  arrow.type = CL_ARROW_NORMAL;
  arrow.direction = (ClarusDirection) arrow_type;
  STYLE_FUNCTION (draw_arrow) (cr, colors, &params, &arrow, ax, ay, aw, ah);
  _gdk_pen_free (cr);
}

/* A scale's trough window, all of it, as it is drawn again without
 * being cleared: the parent's colour, and the trough inside the trough
 * border as GTK+ 2 draws it with trough-side-details, filled to the
 * middle of the slider. The slider's window draws its part of it too,
 * with the trough window at (x, y). */
static void
clarus_scale_trough (GdkPen                 *cr,
                     const ClarusColors     *colors,
                     const WidgetParameters *widget_params,
                     GtkRange               *range,
                     gint                    x,
                     gint                    y,
                     gint                    width,
                     gint                    height)
{
  WidgetParameters params = *widget_params;
  SliderParameters slider;
  gint border, tx, ty, tw, th, sx, sy, sw, sh, length, change;

  ge_cairo_set_color (cr, &params.parentbg);
  _gdk_pen_rectangle (cr, x, y, width, height);
  _gdk_pen_fill (cr);

  _gtk_range_get_props (range, NULL, &border, NULL, NULL);
  tx = x + border;
  ty = y + border;
  tw = width - 2 * border;
  th = height - 2 * border;

  params.corners = CR_CORNER_NONE;
  slider.fill_level = FALSE;
  slider.horizontal = GTK_IS_HSCALE (range);

  /* Clearlooks draws the slider's outline a pixel inside its window,
   * with only its shadow outside it: the trough ends a pixel in too, so
   * that it does not show past the slider at either end */
  if (slider.horizontal)
    {
      tx += 1;
      tw -= 2;
    }
  else
    {
      ty += 1;
      th -= 2;
    }
  if (tw <= 0 || th <= 0)
    return;
  length = slider.horizontal ? tw : th;

  gdk_window_get_position (range->slider, &sx, &sy);
  gdk_window_get_size (range->slider, &sw, &sh);
  change = (slider.horizontal ? sx + sw / 2 : sy + sh / 2) - border - 1;
  change = CLAMP (change, 0, length);

  if (change > 0)
    {
      slider.lower = TRUE;
      if (slider.horizontal)
        params.style_functions->draw_scale_trough (cr, colors, &params,
                                                   &slider, tx, ty,
                                                   change, th);
      else
        params.style_functions->draw_scale_trough (cr, colors, &params,
                                                   &slider, tx, ty,
                                                   tw, change);
    }
  if (change < length)
    {
      slider.lower = FALSE;
      if (slider.horizontal)
        params.style_functions->draw_scale_trough (cr, colors, &params,
                                                   &slider, tx + change, ty,
                                                   tw - change, th);
      else
        params.style_functions->draw_scale_trough (cr, colors, &params,
                                                   &slider, tx, ty + change,
                                                   tw, th - change);
    }
}

static void
clarus_style_draw_box (GtkStyle      *style,
                   GdkWindow   *window,
                   GtkStateType state_type,
                   GtkShadowType shadow_type,
                   GdkRectangle *area,
                   GtkWidget   *widget,
                   gchar       *detail,
                   gint         x,
                   gint         y,
                   gint         width,
                   gint         height)
{
  ClarusStyle *ps = clarus_style (style);
  ClarusColors *colors = &ps->colors;
  GdkPen *cr;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);
  _gtk_theme_sanitize_size (window, &width, &height);
  state_type = _gtk_theme_toggle_state (detail, state_type, shadow_type);

  if (DETAIL ("buttondefault") || DETAIL ("togglebuttondefault"))
    return;   /* drawn with the button */

  if ((DETAIL ("button") || DETAIL ("togglebutton")) && widget &&
      _gtk_theme_parent_is (widget, gtk_clist_get_type ()))
    {
      WidgetParameters params;
      ListViewHeaderParameters header;
      gint columns = 3, column_index = 1;

      cr = clarus_pen (window, area, x, y, width, height);
      clarus_set_widget_parameters (widget, style, state_type, &params);
      params.corners = CR_CORNER_NONE;
      _gtk_theme_button_rect (widget, window, &x, &y, &width, &height);
      clarus_clist_get_header_index (GTK_CLIST (widget->parent), widget,
                                     &column_index, &columns);

      header.resizable = TRUE;
      header.order = 0;
      if (column_index == 0)
        header.order |= CL_ORDER_FIRST;
      if (column_index == columns - 1)
        header.order |= CL_ORDER_LAST;

      gtk_style_apply_default_background (style, window, FALSE, state_type,
                                          area, x, y, width, height);
      STYLE_FUNCTION (draw_list_view_header) (cr, colors, &params, &header,
                                              x, y, width, height);
      _gdk_pen_free (cr);
      return;
    }

  if (DETAIL ("button") || DETAIL ("togglebutton"))
    {
      WidgetParameters params;

      _gtk_theme_button_rect (widget, window, &x, &y, &width, &height);
      cr = clarus_pen (window, area, x, y, width, height);
      clarus_set_widget_parameters (widget, style, state_type, &params);
      params.active = shadow_type == GTK_SHADOW_IN;

      if (clarus_in_combo (widget))
        {
          params.corners = CR_CORNER_TOPRIGHT | CR_CORNER_BOTTOMRIGHT;
          if (params.xthickness > 2)
            {
              x--;
              width++;
            }
        }
      else
        {
          params.corners = CR_CORNER_ALL;
          if (ps->reliefstyle != 0)
            params.enable_shadow = TRUE;
        }

      if (shadow_type != GTK_SHADOW_ETCHED_IN)
        STYLE_FUNCTION (draw_button) (cr, colors, &params,
                                      x, y, width, height);
      else
        {
          _gdk_pen_save (cr);
          _gdk_pen_move_to (cr, x, y);
          _gdk_pen_line_to (cr, x + width, y);
          _gdk_pen_line_to (cr, x, y + height);
          _gdk_pen_close_path (cr);
          _gdk_pen_clip (cr);
          params.active = TRUE;
          params.state_type = CL_STATE_ACTIVE;
          STYLE_FUNCTION (draw_button) (cr, colors, &params,
                                        x, y, width, height);
          _gdk_pen_restore (cr);

          _gdk_pen_save (cr);
          _gdk_pen_move_to (cr, x + width, y);
          _gdk_pen_line_to (cr, x + width, y + height);
          _gdk_pen_line_to (cr, x, y + height);
          _gdk_pen_close_path (cr);
          _gdk_pen_clip (cr);
          params.active = FALSE;
          params.state_type = CL_STATE_NORMAL;
          STYLE_FUNCTION (draw_button) (cr, colors, &params,
                                        x, y, width, height);
          _gdk_pen_restore (cr);
        }
      _gdk_pen_free (cr);
      return;
    }

  cr = clarus_pen (window, area, x, y, width, height);

  if (DETAIL ("menubar"))
    {
      WidgetParameters params;
      MenuBarParameters menubar;
      gboolean horizontal;

      clarus_set_widget_parameters (widget, style, state_type, &params);
      menubar.style = ps->menubarstyle;

      horizontal = height < 2 * width;
      /* This is not that great. Ideally we would have a nice vertical
       * menubar. */
      if (shadow_type != GTK_SHADOW_NONE && horizontal)
        STYLE_FUNCTION (draw_menubar) (cr, colors, &params, &menubar,
                                       x, y, width, height);
    }
  else if (DETAIL ("spinbutton"))
    {
      WidgetParameters params;

      /* The "spinbutton" box is always drawn with state NORMAL */
      if (state_type == GTK_STATE_NORMAL && widget)
        state_type = GTK_WIDGET_STATE (widget);

      clarus_set_widget_parameters (widget, style, state_type, &params);
      params.corners = CR_CORNER_TOPRIGHT | CR_CORNER_BOTTOMRIGHT;
      if (style->klass->xthickness == 3)
        {
          x--;
          width++;
        }
      STYLE_FUNCTION (draw_spinbutton) (cr, colors, &params,
                                        x, y, width, height);
    }
  else if (DETAIL ("trough") && widget && GTK_IS_SCALE (widget) &&
           GTK_RANGE (widget)->slider)
    {
      WidgetParameters params;

      clarus_set_widget_parameters (widget, style, state_type, &params);
      clarus_scale_trough (cr, colors, &params, GTK_RANGE (widget),
                           x, y, width, height);
    }
  else if (DETAIL ("trough") && widget && GTK_IS_PROGRESS_BAR (widget))
    {
      WidgetParameters params;

      clarus_set_widget_parameters (widget, style, state_type, &params);

      /* Fill the background as it is initialised to base[NORMAL] */
      _gdk_pen_rectangle (cr, 0, 0, width, height);
      ge_cairo_set_color (cr, &params.parentbg);
      _gdk_pen_fill (cr);
      STYLE_FUNCTION (draw_progressbar_trough) (cr, colors, &params,
                                                x, y, width, height);
    }
  else if (DETAIL ("trough") && widget && GTK_IS_SCROLLBAR (widget))
    {
      WidgetParameters params;
      ScrollBarParameters scrollbar;

      clarus_set_widget_parameters (widget, style, state_type, &params);
      params.corners = CR_CORNER_ALL;
      scrollbar.horizontal = GTK_IS_HSCROLLBAR (widget);
      scrollbar.junction   = clarus_scrollbar_get_junction (widget);

      /* The trough is a little shorter than the space under the
       * steppers, as with GTK+ 2's trough-under-steppers */
      if (scrollbar.horizontal)
        {
          x += 2;
          width -= 4;
        }
      else
        {
          y += 2;
          height -= 4;
        }
      STYLE_FUNCTION (draw_scrollbar_trough) (cr, colors, &params, &scrollbar,
                                              x, y, width, height);
    }
  else if (DETAIL ("bar"))
    {
      WidgetParameters      params;
      ProgressBarParameters progressbar;

      clarus_set_widget_parameters (widget, style, state_type, &params);

      if (widget && GTK_IS_PROGRESS_BAR (widget))
        {
          progressbar.orientation = (ClarusOrientation)
            GTK_PROGRESS_BAR (widget)->orientation;
          progressbar.value =
            gtk_progress_get_current_percentage (GTK_PROGRESS (widget));
          progressbar.pulsing = GTK_PROGRESS (widget)->activity_mode;
        }
      else
        {
          progressbar.orientation = CL_ORIENTATION_LEFT_TO_RIGHT;
          progressbar.value = 0;
          progressbar.pulsing = FALSE;
        }

      /* a clip a little larger than the area, for the shadow */
      if (area)
        {
          GdkRectangle tmp = *area;

          if (!progressbar.pulsing)
            {
              switch (progressbar.orientation)
                {
                case CL_ORIENTATION_RIGHT_TO_LEFT:
                  tmp.x -= 1;
                case CL_ORIENTATION_LEFT_TO_RIGHT:
                  tmp.width += 1;
                  break;
                case CL_ORIENTATION_BOTTOM_TO_TOP:
                  tmp.y -= 1;
                case CL_ORIENTATION_TOP_TO_BOTTOM:
                  tmp.height += 1;
                  break;
                }
            }
          else if (progressbar.orientation == CL_ORIENTATION_RIGHT_TO_LEFT ||
                   progressbar.orientation == CL_ORIENTATION_LEFT_TO_RIGHT)
            {
              tmp.x -= 1;
              tmp.width += 2;
            }
          else
            {
              tmp.y -= 1;
              tmp.height += 2;
            }
          _gdk_pen_free (cr);
          cr = clarus_pen (window, &tmp, x, y, width, height);
        }

      STYLE_FUNCTION (draw_progressbar_fill) (cr, colors, &params, &progressbar,
                                              x, y, width, height, 10);
    }
  else if (DETAIL ("optionmenu"))
    {
      WidgetParameters params;
      OptionMenuParameters optionmenu;
      gint indicator_width, left, right;

      clarus_set_widget_parameters (widget, style, state_type, &params);
      if (ps->reliefstyle != 0)
        params.enable_shadow = TRUE;

      indicator_width = gtk_style_get_prop_experimental (style,
                          "GtkOptionMenu::indicator_width", 12);
      left = gtk_style_get_prop_experimental (style,
               "GtkOptionMenu::indicator_left_spacing", 3);
      right = gtk_style_get_prop_experimental (style,
                "GtkOptionMenu::indicator_right_spacing", 7);
      optionmenu.linepos = width - (indicator_width + left + right) - 1;

      STYLE_FUNCTION (draw_optionmenu) (cr, colors, &params, &optionmenu,
                                        x, y, width, height);
    }
  else if (DETAIL ("menuitem"))
    {
      WidgetParameters params;

      clarus_set_widget_parameters (widget, style, state_type, &params);
      if (_gtk_theme_parent_is (widget, gtk_menu_bar_get_type ()))
        {
          params.corners = CR_CORNER_TOPLEFT | CR_CORNER_TOPRIGHT;
          height += 1;
          STYLE_FUNCTION (draw_menubaritem) (cr, colors, &params,
                                             x, y, width, height);
        }
      else
        {
          params.corners = CR_CORNER_ALL;
          STYLE_FUNCTION (draw_menuitem) (cr, colors, &params,
                                          x, y, width, height);
        }
    }
  else if (DETAIL ("slider") && widget && GTK_IS_SCROLLBAR (widget))
    {
      WidgetParameters    params;
      ScrollBarParameters scrollbar;

      clarus_set_widget_parameters (widget, style, state_type, &params);
      params.corners = CR_CORNER_NONE;

      scrollbar.has_color  = FALSE;
      scrollbar.horizontal = GTK_IS_HSCROLLBAR (widget);
      scrollbar.junction   = clarus_scrollbar_get_junction (widget);

      if (ps->colorize_scrollbar)
        {
          scrollbar.color = colors->spot[1];
          scrollbar.has_color = TRUE;
        }
      if (ps->has_scrollbar_color)
        {
          ge_gdk_color_to_cairo (&ps->scrollbar_color, &scrollbar.color);
          scrollbar.has_color = TRUE;
        }
      if ((ps->style == CL_STYLE_GLOSSY || ps->style == CL_STYLE_GUMMY) &&
          !scrollbar.has_color)
        scrollbar.color = colors->bg[0];

      STYLE_FUNCTION (draw_scrollbar_slider) (cr, colors, &params, &scrollbar,
                                              x, y, width, height);
    }
  else if (DETAIL ("handlebox_bin") || DETAIL ("toolbar"))
    {
      WidgetParameters  params;
      ToolbarParameters toolbar;
      gboolean horizontal;

      clarus_set_widget_parameters (widget, style, state_type, &params);
      clarus_set_toolbar_parameters (&toolbar, widget, window, x, y);
      toolbar.style = ps->toolbarstyle;

      horizontal = height < 2 * width;
      if (shadow_type != GTK_SHADOW_NONE && horizontal)
        STYLE_FUNCTION (draw_toolbar) (cr, colors, &params, &toolbar,
                                       x, y, width, height);
      else
        gtk_style_apply_default_background (style, window, TRUE, state_type,
                                            area, x, y, width, height);
    }
  else if (DETAIL ("trough"))
    {
      /* Nothing? Why benjamin? */
    }
  else if (DETAIL ("menu"))
    {
      WidgetParameters params;

      clarus_set_widget_parameters (widget, style, state_type, &params);
      STYLE_FUNCTION (draw_menu_frame) (cr, colors, &params,
                                        x, y, width, height);
    }
  else if (DETAIL ("paned") || DETAIL ("hpaned") || DETAIL ("vpaned"))
    {
      /* GTK+ 1's paned handle is a box: GTK+ 2's a handle */
      gtk_style_apply_default_background (style, window, TRUE, state_type,
                                          area, x, y, width, height);
      clarus_handle (style, window, state_type, area, widget,
                     CL_HANDLE_SPLITTER,
                     DETAIL ("vpaned") ||
                     (widget && GTK_IS_VPANED (widget)),
                     x, y, width, height);
    }
  else
    {
      if (!style->bg_pixmap[state_type] ||
          gdk_window_get_type (window) == GDK_WINDOW_PIXMAP)
        _gtk_theme_rectangle (window, style->bg_gc[state_type], area, TRUE,
                              x, y, width, height);
      else
        gtk_style_apply_default_background (style, window, TRUE, state_type,
                                            area, x, y, width, height);
      gtk_paint_shadow (style, window, state_type, shadow_type, area, widget,
                        detail, x, y, width, height);
    }

  _gdk_pen_free (cr);
}

static void
clarus_style_draw_slider (GtkStyle       *style,
                      GdkWindow    *window,
                      GtkStateType  state_type,
                      GtkShadowType shadow_type,
                      GdkRectangle *area,
                      GtkWidget    *widget,
                      gchar        *detail,
                      gint          x,
                      gint          y,
                      gint          width,
                      gint          height,
                      GtkOrientation orientation)
{
  ClarusStyle *ps = clarus_style (style);
  WidgetParameters params;
  SliderParameters slider;
  GdkPen *cr;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);
  _gtk_theme_sanitize_size (window, &width, &height);

  cr = clarus_pen (window, area, x, y, width, height);
  clarus_set_widget_parameters (widget, style, state_type, &params);

  slider.horizontal = widget ? GTK_IS_HSCALE (widget) :
                               orientation == GTK_ORIENTATION_HORIZONTAL;
  slider.lower = FALSE;
  slider.fill_level = FALSE;

  if (ps->style == CL_STYLE_GLOSSY) /* XXX! */
    params.corners = CR_CORNER_ALL;

  STYLE_FUNCTION (draw_slider_button) (cr, &ps->colors, &params, &slider,
                                       x, y, width, height);
  _gdk_pen_free (cr);
}

static void
clarus_style_draw_option (GtkStyle      *style,
                      GdkWindow   *window,
                      GtkStateType state_type,
                      GtkShadowType shadow_type,
                      GdkRectangle *area,
                      GtkWidget   *widget,
                      gchar       *detail,
                      gint         x,
                      gint         y,
                      gint         width,
                      gint         height)
{
  WidgetParameters params;
  CheckboxParameters checkbox;
  GdkPen *cr;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  /* GTK+ 1 draws an insensitive radio button in its own state */
  if (widget && !GTK_WIDGET_IS_SENSITIVE (widget))
    state_type = GTK_STATE_INSENSITIVE;

  cr = clarus_pen (window, area, x, y, width, height);
  checkbox.shadow_type = (ClarusShadowType) shadow_type;
  checkbox.in_cell = FALSE;
  checkbox.in_menu = widget && GTK_IS_MENU_ITEM (widget);

  clarus_set_widget_parameters (widget, style, state_type, &params);
  STYLE_FUNCTION (draw_radiobutton) (cr, &clarus_style   (style)->colors,
                                     &params, &checkbox, x, y, width, height);
  _gdk_pen_free (cr);
}

static void
clarus_style_draw_check (GtkStyle      *style,
                     GdkWindow   *window,
                     GtkStateType state_type,
                     GtkShadowType shadow_type,
                     GdkRectangle *area,
                     GtkWidget   *widget,
                     gchar       *detail,
                     gint         x,
                     gint         y,
                     gint         width,
                     gint         height)
{
  WidgetParameters params;
  CheckboxParameters checkbox;
  GdkPen *cr;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  if (widget && !GTK_WIDGET_IS_SENSITIVE (widget))
    state_type = GTK_STATE_INSENSITIVE;

  cr = clarus_pen (window, area, x, y, width, height);
  clarus_set_widget_parameters (widget, style, state_type, &params);
  params.corners = CR_CORNER_ALL;

  checkbox.shadow_type = (ClarusShadowType) shadow_type;
  checkbox.in_cell = FALSE;
  checkbox.in_menu = widget && GTK_IS_MENU_ITEM (widget);

  STYLE_FUNCTION (draw_checkbox) (cr, &clarus_style (style)->colors, &params,
                                  &checkbox, x, y, width, height);
  _gdk_pen_free (cr);
}

static void
clarus_style_draw_vline (GtkStyle     *style,
                     GdkWindow  *window,
                     GtkStateType state_type,
                     GdkRectangle *area,
                     GtkWidget  *widget,
                     gchar      *detail,
                     gint        y1,
                     gint        y2,
                     gint        x)
{
  SeparatorParameters separator = { FALSE };
  GdkPen *cr;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  /* GTK+ 2's paned widget has no line along its gutter */
  if (DETAIL ("paned") || DETAIL ("hpaned") || DETAIL ("vpaned"))
    return;

  cr = clarus_pen (window, area, x, y1, 2, y2 - y1 + 1);
  STYLE_FUNCTION (draw_separator) (cr, &clarus_style   (style)->colors, NULL,
                                   &separator, x, y1, 2, y2 - y1 + 1);
  _gdk_pen_free (cr);
}

static void
clarus_style_draw_hline (GtkStyle     *style,
                     GdkWindow  *window,
                     GtkStateType state_type,
                     GdkRectangle *area,
                     GtkWidget  *widget,
                     gchar      *detail,
                     gint        x1,
                     gint        x2,
                     gint        y)
{
  ClarusColors *colors = &clarus_style (style)->colors;
  SeparatorParameters separator;
  GdkPen *cr;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  if (DETAIL ("paned") || DETAIL ("hpaned") || DETAIL ("vpaned"))
    return;

  cr = clarus_pen (window, area, x1, y, x2 - x1 + 1, 2);
  separator.horizontal = TRUE;
  if (!DETAIL ("menuitem"))
    STYLE_FUNCTION (draw_separator) (cr, colors, NULL, &separator,
                                     x1, y, x2 - x1 + 1, 2);
  else
    STYLE_FUNCTION (draw_menu_item_separator) (cr, colors, NULL, &separator,
                                               x1, y, x2 - x1 + 1, 2);
  _gdk_pen_free (cr);
}

static void
clarus_style_draw_shadow_gap (GtkStyle        *style,
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
                          GtkPositionType gap_side,
                          gint           gap_x,
                          gint           gap_width)
{
  ClarusColors *colors = &clarus_style (style)->colors;
  WidgetParameters params;
  FrameParameters  frame;
  GdkPen *cr;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);
  _gtk_theme_sanitize_size (window, &width, &height);

  cr = clarus_pen (window, area, x, y, width, height);
  frame.shadow    = (ClarusShadowType) shadow_type;
  frame.gap_side  = (ClarusGapSide) gap_side;
  frame.gap_x     = gap_x;
  frame.gap_width = gap_width;
  frame.border    = &colors->shade[5];

  clarus_set_widget_parameters (widget, style, state_type, &params);
  params.corners = CR_CORNER_ALL;

  STYLE_FUNCTION (draw_frame) (cr, colors, &params, &frame,
                               x, y, width, height);
  _gdk_pen_free (cr);
}

/* An option menu's arrow: a combo box's */
static void
clarus_style_draw_tab (GtkStyle      *style,
                   GdkWindow   *window,
                   GtkStateType state_type,
                   GtkShadowType shadow_type,
                   GdkRectangle *area,
                   GtkWidget   *widget,
                   gchar       *detail,
                   gint         x,
                   gint         y,
                   gint         width,
                   gint         height)
{
  WidgetParameters params;
  ArrowParameters  arrow;
  GdkPen *cr;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);
  _gtk_theme_sanitize_size (window, &width, &height);

  cr = clarus_pen (window, area, x, y, width, height);
  clarus_set_widget_parameters (widget, style, state_type, &params);
  arrow.type      = CL_ARROW_COMBO;
  arrow.direction = CL_DIRECTION_DOWN;

  STYLE_FUNCTION (draw_arrow) (cr, &clarus_style (style)->colors, &params,
                               &arrow, x, y, width, height);
  _gdk_pen_free (cr);
}

static void
clarus_style_draw_arrow (GtkStyle      *style,
                     GdkWindow   *window,
                     GtkStateType state_type,
                     GtkShadowType shadow_type,
                     GdkRectangle *area,
                     GtkWidget   *widget,
                     gchar       *detail,
                     GtkArrowType arrow_type,
                     gint         fill,
                     gint         x,
                     gint         y,
                     gint         width,
                     gint         height)
{
  WidgetParameters params;
  ArrowParameters  arrow;
  GdkPen *cr;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);
  _gtk_theme_sanitize_size (window, &width, &height);

  if (fill && (DETAIL ("hscrollbar") || DETAIL ("vscrollbar") ||
               DETAIL ("spinbutton")))
    {
      clarus_stepper (style, window, state_type, shadow_type, area, widget,
                      detail, arrow_type, x, y, width, height);
      return;
    }

  cr = clarus_pen (window, area, x, y, width, height);
  clarus_set_widget_parameters (widget, style, state_type, &params);
  arrow.type = CL_ARROW_NORMAL;
  arrow.direction = (ClarusDirection) arrow_type;

  /* I have no idea why, but the arrow of GtkCombo is larger than in
   * other places. Subtracting 3 seems to fix this. */
  if (widget && widget->parent &&
      _gtk_theme_parent_is (widget->parent, gtk_combo_get_type ()))
    {
      x += 1;
      width -= 3;
    }

  STYLE_FUNCTION (draw_arrow) (cr, &clarus_style (style)->colors, &params,
                               &arrow, x, y, width, height);
  _gdk_pen_free (cr);
}

static void
clarus_style_draw_focus (GtkStyle     *style,
                     GdkWindow  *window,
                     GdkRectangle *area,
                     GtkWidget  *widget,
                     gchar      *detail,
                     gint        x,
                     gint        y,
                     gint        width,
                     gint        height)
{
  ClarusStyle *ps = clarus_style (style);
  WidgetParameters params;
  FocusParameters focus;
  GdkPen *cr;

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  /* GTK+ 1 gives the focus a rectangle one less than it means, except
   * when it asks for the whole window */
  if (width != -1)
    width += 1;
  if (height != -1)
    height += 1;
  _gtk_theme_sanitize_size (window, &width, &height);

  /* Just return if focus drawing is disabled. */
  if (ps->disable_focus)
    return;
  /* An entry shows its focus in its frame, as GTK+ 2's interior focus
   * has it; a scrollbar takes none in GTK+ 2 */
  if (DETAIL ("entry") || DETAIL ("text") ||
      (widget && GTK_IS_SCROLLBAR (widget)))
    return;

  clarus_set_widget_parameters (widget, style, GTK_WIDGET_STATE (widget),
                                &params);
  params.corners = CR_CORNER_ALL;

  focus.has_color = FALSE;
  focus.interior = TRUE;
  focus.line_width = 1;
  focus.padding = 1;
  focus.continue_side = CL_CONT_NONE;
  focus.dash_list = (guint8 *) "\1\1";

  if ((DETAIL ("button") || DETAIL ("togglebutton")) && widget &&
      GTK_IS_BUTTON (widget) && !GTK_IS_OPTION_MENU (widget))
    {
      if (_gtk_theme_parent_is (widget, gtk_clist_get_type ()))
        focus.type = CL_FOCUS_TREEVIEW_HEADER;
      else if (GTK_BUTTON (widget)->relief == GTK_RELIEF_NORMAL)
        focus.type = CL_FOCUS_BUTTON;
      else
        focus.type = CL_FOCUS_BUTTON_FLAT;

      /* GTK+ 2 draws it inside the button's frame, a pixel in */
      _gtk_theme_button_rect (widget, window, &x, &y, &width, &height);
      x += style->klass->xthickness + focus.padding;
      y += style->klass->ythickness + focus.padding;
      width -= 2 * (style->klass->xthickness + focus.padding);
      height -= 2 * (style->klass->ythickness + focus.padding);
      if (clarus_in_combo (widget))
        {
          params.corners = CR_CORNER_TOPRIGHT | CR_CORNER_BOTTOMRIGHT;
          if (params.xthickness > 2)
            {
              x--;
              width++;
            }
        }
    }
  else if (DETAIL ("button") && widget && GTK_IS_OPTION_MENU (widget))
    {
      focus.type = CL_FOCUS_BUTTON;
      x += style->klass->xthickness + 1 + focus.padding;
      y += style->klass->ythickness + 1 + focus.padding;
      width -= 2 * (style->klass->xthickness + 1 + focus.padding);
      height -= 2 * (style->klass->ythickness + 1 + focus.padding);
    }
  else if (DETAIL ("trough") && widget && GTK_IS_SCALE (widget))
    focus.type = CL_FOCUS_SCALE;
  else if (DETAIL ("tab"))
    focus.type = CL_FOCUS_TAB;
  else if (DETAIL ("checkbutton") || DETAIL ("radiobutton"))
    {
      /* round the whole button, its check box with its label: GTK+ 2
       * drew it round the label and gave it room, but GTK+ 1 gives none,
       * and clears only the button when the focus goes */
      focus.type = CL_FOCUS_LABEL;
    }
  else if (widget && GTK_IS_CLIST (widget))
    focus.type = CL_FOCUS_TREEVIEW_ROW;
  else
    focus.type = CL_FOCUS_UNKNOWN;

  clarus_focus_color (style, &focus, &ps->colors.bg[GTK_STATE_SELECTED]);

  cr = clarus_pen (window, area, x, y, width, height);
  STYLE_FUNCTION (draw_focus) (cr, &ps->colors, &params, &focus,
                               x, y, width, height);
  _gdk_pen_free (cr);
}

/* Text: as Clearlooks drew a layout, an insensitive one etched */
static void
clarus_style_draw_string (GtkStyle     *style,
                      GdkWindow  *window,
                      GtkStateType state_type,
                      GdkRectangle *area,
                      GtkWidget  *widget,
                      gchar      *detail,
                      gint        x,
                      gint        y,
                      const gchar *string)
{
  GdkGC *gc = style->fg_gc[state_type];

  g_return_if_fail (style != NULL);
  g_return_if_fail (window != NULL);

  if (state_type == GTK_STATE_INSENSITIVE)
    {
      ClarusStyle *ps = clarus_style (style);
      WidgetParameters params;
      CairoColor temp;
      GdkGCValues values;
      GdkGC *etched;

      clarus_set_widget_parameters (widget, style, state_type, &params);
      if (widget && GTK_WIDGET_NO_WINDOW (widget))
        ge_shade_color (&params.parentbg, 1.2, &temp);
      else
        ge_shade_color (&ps->colors.bg[widget ? GTK_WIDGET_STATE (widget) :
                                       GTK_STATE_NORMAL], 1.2, &temp);

      values.foreground.red = temp.r * 65535;
      values.foreground.green = temp.g * 65535;
      values.foreground.blue = temp.b * 65535;
      gdk_colormap_alloc_color (style->colormap, &values.foreground, FALSE,
                                TRUE);
      etched = gtk_gc_get (style->depth, style->colormap, &values,
                           GDK_GC_FOREGROUND);
      _gtk_theme_clip (etched, area);
      gdk_draw_string (window, style->font, etched, x + 1, y + 1, string);
      _gtk_theme_unclip (etched, area);
      gtk_gc_release (etched);
    }

  _gtk_theme_clip (gc, area);
  gdk_draw_string (window, style->font, gc, x, y, string);
  _gtk_theme_unclip (gc, area);
}

/* ---------------------------------------------------------------------
 * The engine
 */

static GSList *clarus_classes = NULL;

/* A class for each thickness a style asks for */
static GtkStyleClass *
clarus_get_class (GtkStyleClass *parent, gint xthickness, gint ythickness)
{
  static GtkStyleClass klass;

  if (!klass.draw_hline)
    {
      klass = *parent;
      klass.draw_hline = clarus_style_draw_hline;
      klass.draw_vline = clarus_style_draw_vline;
      klass.draw_shadow = clarus_style_draw_shadow;
      klass.draw_polygon = _gtk_theme_draw_polygon;
      klass.draw_arrow = clarus_style_draw_arrow;
      klass.draw_diamond = _gtk_theme_draw_diamond;
      klass.draw_string = clarus_style_draw_string;
      klass.draw_box = clarus_style_draw_box;
      klass.draw_flat_box = clarus_style_draw_flat_box;
      klass.draw_check = clarus_style_draw_check;
      klass.draw_option = clarus_style_draw_option;
      klass.draw_tab = clarus_style_draw_tab;
      klass.draw_shadow_gap = clarus_style_draw_shadow_gap;
      klass.draw_box_gap = clarus_style_draw_box_gap;
      klass.draw_extension = clarus_style_draw_extension;
      klass.draw_focus = clarus_style_draw_focus;
      klass.draw_slider = clarus_style_draw_slider;
      klass.draw_handle = clarus_style_draw_handle;
    }

  return _gtk_theme_class (&clarus_classes, &klass, xthickness, ythickness);
}

/* The metrics of Clearlooks' theme, and GTK+ 2's where they differ from
 * GTK+ 1's and the theme leaves them alone */
static void
clarus_set_props (GtkStyle *style)
{
  gtk_style_set_prop_experimental (style, "GtkButton::default_spacing", 0);
  gtk_style_set_prop_experimental (style, "GtkCheckButton::indicator_size", 14);
  gtk_style_set_prop_experimental (style, "GtkRadioButton::indicator_size", 14);
  gtk_style_set_prop_experimental (style, "GtkRange::slider_width", 15);
  gtk_style_set_prop_experimental (style, "GtkRange::stepper_size", 15);
  gtk_style_set_prop_experimental (style, "GtkRange::stepper_spacing", 0);
  gtk_style_set_prop_experimental (style, "GtkRange::trough_border", 0);
  gtk_style_set_prop_experimental (style, "GtkRange::min_slider_size", 30);
  gtk_style_set_prop_experimental (style, "GtkScale::slider_length", 23);
  /* GTK+ 2 leaves room for the focus round a scale's trough */
  gtk_style_set_prop_experimental (style, "GtkScale::trough_border", 2);
  gtk_style_set_prop_experimental (style, "GtkScale::trough_side_details", 1);
  gtk_style_set_prop_experimental (style, "GtkPaned::handle_full_size", 1);
  gtk_style_set_prop_experimental (style, "GtkPaned::handle_width", 6);
  gtk_style_set_prop_experimental (style, "GtkSpinButton::shadow_type",
                                   GTK_SHADOW_IN);
  gtk_style_set_prop_experimental (style, "GtkOptionMenu::indicator_width", 7);
  gtk_style_set_prop_experimental (style,
                                   "GtkOptionMenu::indicator_height", 13);
  gtk_style_set_prop_experimental (style,
                                   "GtkOptionMenu::indicator_left_spacing", 7);
  gtk_style_set_prop_experimental (style,
                                   "GtkOptionMenu::indicator_right_spacing", 5);
  gtk_style_set_prop_experimental (style,
                                   "GtkOptionMenu::indicator_top_spacing", 2);
  gtk_style_set_prop_experimental (style,
                                   "GtkOptionMenu::indicator_bottom_spacing",
                                   2);
}

/* Clearlooks reads its styles' numbers as floats */
static guint
clarus_parse_int (GScanner *scanner, gint *value)
{
  gdouble number = 0;
  guint token = _gtk_theme_parse_number (scanner, &number);

  *value = (gint) number;
  return token;
}

static guint
clarus_parse_style (GScanner *scanner, ClarusStyles *style)
{
  guint token = _gtk_theme_parse_equals (scanner);

  if (token != G_TOKEN_NONE)
    return token;
  switch ((guint) g_scanner_get_next_token (scanner))
    {
    case TOKEN_CLASSIC:   *style = CL_STYLE_CLASSIC;   break;
    case TOKEN_GLOSSY:    *style = CL_STYLE_GLOSSY;    break;
    case TOKEN_INVERTED:  *style = CL_STYLE_INVERTED;  break;
    case TOKEN_GUMMY:     *style = CL_STYLE_GUMMY;     break;
    default:              return TOKEN_CLASSIC;
    }
  return G_TOKEN_NONE;
}

/* GTK+ 2's options, taken and left alone */
static guint
clarus_parse_ignored (GScanner *scanner)
{
  guint token = _gtk_theme_parse_equals (scanner);

  if (token != G_TOKEN_NONE)
    return token;
  g_scanner_get_next_token (scanner);
  return G_TOKEN_NONE;
}

static guint
clarus_parse_rc_style (GScanner   *scanner,
                       GtkRcStyle *rc_style)
{
  ClarusRcData *data;
  gdouble number = 0;
  guint old_scope;
  guint token;

  old_scope = _gtk_theme_scope_enter (scanner, "clarus_theme_engine",
                                      clarus_symbols,
                                      G_N_ELEMENTS (clarus_symbols));

  data = g_new0 (ClarusRcData, 1);

  token = g_scanner_peek_next_token (scanner);
  while (token != G_TOKEN_RIGHT_CURLY)
    {
      g_scanner_get_next_token (scanner);

      switch (token)
        {
        case TOKEN_FOCUSCOLOR:
          token = _gtk_theme_parse_color (scanner, &data->focus_color);
          data->flags |= CLARUS_FOCUS_COLOR_SET;
          break;
        case TOKEN_SCROLLBARCOLOR:
          token = _gtk_theme_parse_color (scanner, &data->scrollbar_color);
          data->flags |= CLARUS_SCROLLBAR_COLOR_SET;
          break;
        case TOKEN_COLORIZESCROLLBAR:
          token = _gtk_theme_parse_boolean (scanner, TOKEN_TRUE, TOKEN_FALSE,
                                            &data->colorize_scrollbar);
          data->flags |= CLARUS_COLORIZE_SCROLLBAR_SET;
          break;
        case TOKEN_CONTRAST:
          token = _gtk_theme_parse_number (scanner, &data->contrast);
          data->flags |= CLARUS_CONTRAST_SET;
          break;
        case TOKEN_RELIEFSTYLE:
          token = clarus_parse_int (scanner, &data->reliefstyle);
          data->flags |= CLARUS_RELIEFSTYLE_SET;
          break;
        case TOKEN_MENUBARSTYLE:
          token = clarus_parse_int (scanner, &data->menubarstyle);
          data->flags |= CLARUS_MENUBARSTYLE_SET;
          break;
        case TOKEN_TOOLBARSTYLE:
          token = clarus_parse_int (scanner, &data->toolbarstyle);
          data->flags |= CLARUS_TOOLBARSTYLE_SET;
          break;
        case TOKEN_STYLE:
          token = clarus_parse_style (scanner, &data->style);
          data->flags |= CLARUS_STYLE_SET;
          break;
        case TOKEN_RADIUS:
          token = _gtk_theme_parse_number (scanner, &data->radius);
          data->flags |= CLARUS_RADIUS_SET;
          break;
        case TOKEN_DISABLE_FOCUS:
          token = _gtk_theme_parse_boolean (scanner, TOKEN_TRUE, TOKEN_FALSE,
                                            &data->disable_focus);
          data->flags |= CLARUS_DISABLE_FOCUS_SET;
          break;
        case TOKEN_XTHICKNESS:
          token = _gtk_theme_parse_number (scanner, &number);
          data->xthickness = number;
          data->flags |= CLARUS_XTHICKNESS_SET;
          break;
        case TOKEN_YTHICKNESS:
          token = _gtk_theme_parse_number (scanner, &number);
          data->ythickness = number;
          data->flags |= CLARUS_YTHICKNESS_SET;
          break;
        case TOKEN_ANIMATION:
        case TOKEN_HINT:
          token = clarus_parse_ignored (scanner);
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
clarus_merge_rc_style (GtkRcStyle *dest,
                       GtkRcStyle *src)
{
  ClarusRcData *s = src->engine_data;
  ClarusRcData *d = dest->engine_data;
  guint flags;

  if (!d)
    d = dest->engine_data = g_new0 (ClarusRcData, 1);
  if (!s)
    return;

  flags = ~d->flags & s->flags;

  if (flags & CLARUS_STYLE_SET)
    d->style = s->style;
  if (flags & CLARUS_FOCUS_COLOR_SET)
    d->focus_color = s->focus_color;
  if (flags & CLARUS_SCROLLBAR_COLOR_SET)
    d->scrollbar_color = s->scrollbar_color;
  if (flags & CLARUS_COLORIZE_SCROLLBAR_SET)
    d->colorize_scrollbar = s->colorize_scrollbar;
  if (flags & CLARUS_CONTRAST_SET)
    d->contrast = s->contrast;
  if (flags & CLARUS_RELIEFSTYLE_SET)
    d->reliefstyle = s->reliefstyle;
  if (flags & CLARUS_MENUBARSTYLE_SET)
    d->menubarstyle = s->menubarstyle;
  if (flags & CLARUS_TOOLBARSTYLE_SET)
    d->toolbarstyle = s->toolbarstyle;
  if (flags & CLARUS_RADIUS_SET)
    d->radius = s->radius;
  if (flags & CLARUS_DISABLE_FOCUS_SET)
    d->disable_focus = s->disable_focus;
  if (flags & CLARUS_XTHICKNESS_SET)
    d->xthickness = s->xthickness;
  if (flags & CLARUS_YTHICKNESS_SET)
    d->ythickness = s->ythickness;

  d->flags |= flags;
}

static void
clarus_register_styles (void)
{
  static gboolean done = FALSE;

  if (done)
    return;
  done = TRUE;

  clarus_register_style_classic (&style_functions[CL_STYLE_CLASSIC],
                                 &style_constants[CL_STYLE_CLASSIC]);

  style_functions[CL_STYLE_GLOSSY] = style_functions[CL_STYLE_CLASSIC];
  style_constants[CL_STYLE_GLOSSY] = style_constants[CL_STYLE_CLASSIC];
  clarus_register_style_glossy (&style_functions[CL_STYLE_GLOSSY],
                                &style_constants[CL_STYLE_GLOSSY]);

  style_functions[CL_STYLE_INVERTED] = style_functions[CL_STYLE_CLASSIC];
  style_constants[CL_STYLE_INVERTED] = style_constants[CL_STYLE_CLASSIC];
  clarus_register_style_inverted (&style_functions[CL_STYLE_INVERTED],
                                  &style_constants[CL_STYLE_INVERTED]);

  style_functions[CL_STYLE_GUMMY] = style_functions[CL_STYLE_CLASSIC];
  style_constants[CL_STYLE_GUMMY] = style_constants[CL_STYLE_CLASSIC];
  clarus_register_style_gummy (&style_functions[CL_STYLE_GUMMY],
                               &style_constants[CL_STYLE_GUMMY]);
}

static void
clarus_rc_style_to_style (GtkStyle   *style,
                          GtkRcStyle *rc_style)
{
  ClarusRcData *data = rc_style->engine_data;
  gint xthickness = 1, ythickness = 1;

  clarus_register_styles ();

  if (data && (data->flags & CLARUS_XTHICKNESS_SET))
    xthickness = CLAMP (data->xthickness, 0, 8);
  if (data && (data->flags & CLARUS_YTHICKNESS_SET))
    ythickness = CLAMP (data->ythickness, 0, 8);

  style->klass = clarus_get_class (style->klass, xthickness, ythickness);
  style->engine_data = g_new0 (ClarusStyle, 1);
  clarus_set_props (style);
}

static void
clarus_duplicate_style (GtkStyle *dest,
                        GtkStyle *src)
{
  dest->klass = src->klass;
  dest->engine_data = g_new0 (ClarusStyle, 1);
  if (src->engine_data)
    *(ClarusStyle *) dest->engine_data = *(ClarusStyle *) src->engine_data;
  clarus_set_props (dest);
}

/* The colours are worked out when the style's are known */
static void
clarus_realize_style (GtkStyle *style)
{
  if (!style->engine_data)
    style->engine_data = g_new0 (ClarusStyle, 1);
  clarus_style_get (style, style->engine_data);
}

void
gtk_theme_clarus_init (GtkThemeEngine *engine)
{
  engine->parse_rc_style = clarus_parse_rc_style;
  engine->merge_rc_style = clarus_merge_rc_style;
  engine->rc_style_to_style = clarus_rc_style_to_style;
  engine->duplicate_style = clarus_duplicate_style;
  engine->realize_style = clarus_realize_style;
  engine->destroy_rc_style = _gtk_theme_destroy_rc_style;
  engine->destroy_style = _gtk_theme_destroy_style;
}
