/* GTK - The GIMP Toolkit
 * Clarus, a theme engine ported from Clearlooks: its types
 *
 * Ported from the Clearlooks engine of gtk-engines 2.20.2, its
 * clearlooks_types.h, and the gtk-engines support library's
 * cairo-support.h:
 * Copyright (C) 2006 Andrew Johnson <acjgenius@earthlink.net>
 * Copyright (C) 2006 Thomas Wood <thos@gnome.org>
 * Copyright (C) 2006-2007 Benjamin Berg <benjamin@sipsolutions.net>
 * Copyright (C) 2007-2008 Andrea Cimitan <andrea.cimitan@gmail.com>
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

/* The parameters Clearlooks' drawing takes, and the table of drawing
 * functions each of its styles fills in, kept as Clearlooks had them
 * but for their names. Private to the engine. */

#ifndef __GTK_THEME_CLARUS_H__
#define __GTK_THEME_CLARUS_H__

#include <string.h>

#include <gtk/gtk.h>

#include "gdk/gdkpen.h"
#include "gtkthemecommon.h"

#ifndef G_PI
#define G_PI    3.14159265358979323846
#endif
#ifndef G_PI_2
#define G_PI_2  1.57079632679489661923
#endif
#ifndef G_PI_4
#define G_PI_4  0.78539816339744830962
#endif

typedef unsigned char boolean;
typedef unsigned char uint8;

/* cairo-support.h */

typedef struct
{
  gdouble r;
  gdouble g;
  gdouble b;
  gdouble a;
} CairoColor;

typedef enum
{
  CR_CORNER_NONE        = 0,
  CR_CORNER_TOPLEFT     = 1,
  CR_CORNER_TOPRIGHT    = 2,
  CR_CORNER_BOTTOMLEFT  = 4,
  CR_CORNER_BOTTOMRIGHT = 8,
  CR_CORNER_ALL         = 15
} CairoCorners;

typedef enum
{
  CR_MIRROR_NONE       = 0,
  CR_MIRROR_HORIZONTAL = 1 << 0,
  CR_MIRROR_VERTICAL   = 1 << 1
} CairoMirror;

/* clearlooks_types.h */

typedef struct _ClarusStyleFunctions ClarusStyleFunctions;
typedef struct _ClarusStyleConstants ClarusStyleConstants;

typedef enum
{
        CL_STYLE_CLASSIC = 0,
        CL_STYLE_GLOSSY = 1,
        CL_STYLE_INVERTED = 2,
        CL_STYLE_GUMMY = 3,
        CL_NUM_STYLES = 4
} ClarusStyles;

typedef enum
{
        CL_STATE_NORMAL,
        CL_STATE_ACTIVE,
        CL_STATE_SELECTED,
        CL_STATE_INSENSITIVE
} ClarusStateType;

typedef enum
{
        CL_JUNCTION_NONE      = 0,
        CL_JUNCTION_BEGIN     = 1,
        CL_JUNCTION_END       = 2
} ClarusJunction;

typedef enum
{
        CL_STEPPER_UNKNOWN    = 0,
        CL_STEPPER_A          = 1,
        CL_STEPPER_B          = 2,
        CL_STEPPER_C          = 4,
        CL_STEPPER_D          = 8
} ClarusStepper;

typedef enum
{
        CL_ORDER_FIRST    = 1 << 0,
        CL_ORDER_LAST     = 1 << 1,
} ClarusOrder;

typedef enum
{
        CL_CONT_NONE       = 0,
        CL_CONT_LEFT       = 1 << 0,
        CL_CONT_RIGHT      = 1 << 1
} ClarusContinue;

typedef enum
{
        CL_ORIENTATION_LEFT_TO_RIGHT,
        CL_ORIENTATION_RIGHT_TO_LEFT,
        CL_ORIENTATION_BOTTOM_TO_TOP,
        CL_ORIENTATION_TOP_TO_BOTTOM
} ClarusOrientation;

typedef enum
{
        CL_GAP_LEFT,
        CL_GAP_RIGHT,
        CL_GAP_TOP,
        CL_GAP_BOTTOM
} ClarusGapSide;

typedef enum
{
        CL_SHADOW_NONE,
        CL_SHADOW_IN,
        CL_SHADOW_OUT,
        CL_SHADOW_ETCHED_IN,
        CL_SHADOW_ETCHED_OUT
} ClarusShadowType;

typedef enum
{
        CL_HANDLE_TOOLBAR,
        CL_HANDLE_SPLITTER
} ClarusHandleType;

typedef enum
{
        CL_ARROW_NORMAL,
        CL_ARROW_COMBO
} ClarusArrowType;

typedef enum
{
        CL_FOCUS_BUTTON,
        CL_FOCUS_BUTTON_FLAT,
        CL_FOCUS_LABEL,
        CL_FOCUS_TREEVIEW,
        CL_FOCUS_TREEVIEW_HEADER,
        CL_FOCUS_TREEVIEW_ROW,
        CL_FOCUS_TREEVIEW_DND,
        CL_FOCUS_SCALE,
        CL_FOCUS_TAB,
        CL_FOCUS_COLOR_WHEEL_DARK,
        CL_FOCUS_COLOR_WHEEL_LIGHT,
        CL_FOCUS_ICONVIEW,
        CL_FOCUS_UNKNOWN
} ClarusFocusType;


typedef enum
{
        CL_DIRECTION_UP,
        CL_DIRECTION_DOWN,
        CL_DIRECTION_LEFT,
        CL_DIRECTION_RIGHT
} ClarusDirection;

typedef struct
{
        double x;
        double y;
        double width;
        double height;
} ClarusRectangle;

typedef struct
{
        CairoColor fg[5];
        CairoColor bg[5];
        CairoColor base[5];
        CairoColor text[5];

        CairoColor shade[9];
        CairoColor spot[3];
} ClarusColors;

typedef struct
{
        boolean active;
        boolean prelight;
        boolean disabled;
        boolean focus;
        boolean is_default;
        boolean ltr;
        boolean enable_shadow;

        gfloat  radius;

        ClarusStateType state_type;

        uint8 corners;
        uint8 xthickness;
        uint8 ythickness;

        CairoColor parentbg;

        ClarusStyleFunctions *style_functions;
        ClarusStyleConstants *style_constants;
} WidgetParameters;

typedef struct
{
        ClarusFocusType type;
        ClarusContinue  continue_side;
        CairoColor          color;
        boolean             has_color;
        gint                line_width;
        gint                padding;
        guint8*             dash_list;
        boolean             interior;
} FocusParameters;

typedef struct
{
        boolean lower;
        boolean horizontal;
        boolean fill_level;
} SliderParameters;

typedef struct
{
        ClarusOrientation orientation;
        boolean pulsing;
        float value;
} ProgressBarParameters;

typedef struct
{
        int linepos;
} OptionMenuParameters;

typedef struct
{
        ClarusShadowType shadow;
        ClarusGapSide gap_side;
        int gap_x;
        int gap_width;
        const CairoColor *border; /* maybe changes this to some other hint ... */
} FrameParameters;

typedef struct
{
        ClarusGapSide gap_side;
        FocusParameters   focus;
} TabParameters;

typedef struct
{
        CairoCorners    corners;
        ClarusShadowType shadow;
} ShadowParameters;

typedef struct
{
        boolean horizontal;
} SeparatorParameters;

typedef struct
{
        ClarusOrder order; /* XXX: rename to position */
        boolean         resizable;
} ListViewHeaderParameters;

typedef struct
{
        CairoColor         color;
        ClarusJunction junction; /* On which sides the slider junctions */
        boolean            horizontal;
        boolean            has_color;
} ScrollBarParameters;

typedef struct
{
        ClarusHandleType type;
        boolean              horizontal;
} HandleParameters;

typedef struct
{
        ClarusStepper stepper;  /* Which stepper to draw */
} ScrollBarStepperParameters;

typedef struct
{
        int style;
} MenuBarParameters;

typedef struct
{
        ClarusShadowType shadow_type;
        boolean              in_cell;
        boolean              in_menu;
} CheckboxParameters;

typedef struct
{
        ClarusArrowType type;
        ClarusDirection direction;
} ArrowParameters;

typedef struct
{
        int      style;
        boolean  topmost;
} ToolbarParameters;

struct _ClarusStyleConstants
{
        gdouble topleft_highlight_shade;
        gdouble topleft_highlight_alpha;
};

struct _ClarusStyleFunctions
{
        void (*draw_top_left_highlight) (GdkPen *cr,
                                       const CairoColor *color,
                                       const WidgetParameters *params,
                                       int x, int y, int width, int height,
                                       gdouble radius,
                                       CairoCorners corners);

        void (*draw_button)           (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       int x, int y, int width, int height);

        void (*draw_scale_trough)     (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       const SliderParameters *slider,
                                       int x, int y, int width, int height);

        void (*draw_progressbar_trough) (GdkPen         *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       int x, int y, int width, int height);

        void (*draw_progressbar_fill) (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       const ProgressBarParameters      *progressbar,
                                       int x, int y, int width, int height, gint offset);

        void (*draw_slider_button)    (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       const SliderParameters *slider,
                                       int x, int y, int width, int height);

        void (*draw_entry)            (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       const FocusParameters  *focus,
                                       int x, int y, int width, int height);

        void (*draw_spinbutton)       (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       int x, int y, int width, int height);

        void (*draw_spinbutton_down)  (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       int x, int y, int width, int height);

        void (*draw_optionmenu)       (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       const OptionMenuParameters *optionmenu,
                                       int x, int y, int width, int height);

        void (*draw_inset)            (GdkPen *cr,
                                        const CairoColor *bg_color,
                                        double x, double y, double w, double h,
                                        double radius, uint8 corners);

        void (*draw_menubar)          (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       const MenuBarParameters *menubar,
                                       int x, int y, int width, int height);

        void (*draw_tab)              (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       const TabParameters *tab,
                                       int x, int y, int width, int height);

        void (*draw_frame)            (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       const FrameParameters *frame,
                                       int x, int y, int width, int height);

        void (*draw_separator)        (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       const SeparatorParameters *separator,
                                       int x, int y, int width, int height);

        void (*draw_menu_item_separator) (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       const SeparatorParameters *separator,
                                       int x, int y, int width, int height);

        void (*draw_list_view_header) (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       const ListViewHeaderParameters *header,
                                       int x, int y, int width, int height);

        void (*draw_toolbar)          (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       const ToolbarParameters *toolbar,
                                       int x, int y, int width, int height);

        void (*draw_menuitem)         (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       int x, int y, int width, int height);

        void (*draw_menubaritem)      (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       int x, int y, int width, int height);

        void (*draw_scrollbar_stepper) (GdkPen *cr,
                                        const ClarusColors      *colors,
                                        const WidgetParameters  *widget,
                                        const ScrollBarParameters *scrollbar,
                                        const ScrollBarStepperParameters *stepper,
                                        int x, int y, int width, int height);

        void (*draw_scrollbar_slider) (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       const ScrollBarParameters        *scrollbar,
                                       int x, int y, int width, int height);

        void (*draw_scrollbar_trough) (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       const ScrollBarParameters        *scrollbar,
                                       int x, int y, int width, int height);

        void (*draw_statusbar)        (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       int x, int y, int width, int height);

        void (*draw_menu_frame)       (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       int x, int y, int width, int height);

        void (*draw_tooltip)          (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       int x, int y, int width, int height);

        void (*draw_handle)           (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       const HandleParameters *handle,
                                       int x, int y, int width, int height);

        void (*draw_arrow)            (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       const ArrowParameters *arrow,
                                       int x, int y, int width, int height);

        void (*draw_focus)            (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       const FocusParameters *focus,
                                       int x, int y, int width, int height);

        void (*draw_checkbox)         (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       const CheckboxParameters *checkbox,
                                       int x, int y, int width, int height);

        void (*draw_radiobutton)      (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       const CheckboxParameters *checkbox,
                                       int x, int y, int width, int height);

        /* Style internal functions */
        /* XXX: Only used by slider_button, inline it? */
        void (*draw_shadow)           (GdkPen *cr,
                                       const ClarusColors *colors,
                                       gfloat radius, int width, int height);

        void (*draw_slider)           (GdkPen *cr,
                                       const ClarusColors *colors,
                                       const WidgetParameters *widget,
                                       int x, int y, int width, int height);

        void (*draw_gripdots)         (GdkPen *cr,
                                       const ClarusColors *colors, int x, int y,
                                       int width, int height, int xr, int yr,
                                       float contrast);
};


#define CLARUS_RECTANGLE_SET(rect, _x, _y, _w, _h) (rect).x      = (_x); \
                                                       (rect).y      = (_y); \
                                                       (rect).width  = (_w); \
                                                       (rect).height = (_h);

/* ---------------------------------------------------------------------
 * The support library's helpers, gtkthemeclarussupport.c
 */

#define ge_hsb_from_color                _gtk_clarus_hsb_from_color
#define ge_color_from_hsb                _gtk_clarus_color_from_hsb
#define ge_gdk_color_to_cairo            _gtk_clarus_gdk_color_to_cairo
#define ge_shade_color                   _gtk_clarus_shade_color
#define ge_mix_color                     _gtk_clarus_mix_color
#define ge_cairo_set_color               _gtk_clarus_cairo_set_color
#define ge_cairo_rounded_corner          _gtk_clarus_cairo_rounded_corner
#define ge_cairo_rounded_rectangle       _gtk_clarus_cairo_rounded_rectangle
#define ge_cairo_stroke_rectangle        _gtk_clarus_cairo_stroke_rectangle
#define ge_cairo_inner_rectangle         _gtk_clarus_cairo_inner_rectangle
#define ge_cairo_inner_rounded_rectangle _gtk_clarus_cairo_inner_rounded_rectangle
#define ge_cairo_mirror                  _gtk_clarus_cairo_mirror
#define ge_cairo_exchange_axis           _gtk_clarus_cairo_exchange_axis

void ge_hsb_from_color (const CairoColor *color, gdouble *hue,
                        gdouble *saturation, gdouble *brightness);
void ge_color_from_hsb (gdouble hue, gdouble saturation, gdouble brightness,
                        CairoColor *color);
void ge_gdk_color_to_cairo (const GdkColor *c, CairoColor *cc);
void ge_shade_color (const CairoColor *base, gdouble shade_ratio,
                     CairoColor *composite);
void ge_mix_color (const CairoColor *color1, const CairoColor *color2,
                   gdouble mix_factor, CairoColor *composite);
void ge_cairo_set_color (GdkPen *cr, const CairoColor *color);
void ge_cairo_rounded_corner (GdkPen *cr, double x, double y, double radius,
                              CairoCorners corner);
void ge_cairo_rounded_rectangle (GdkPen *cr, double x, double y, double w,
                                 double h, double radius, CairoCorners corners);
void ge_cairo_stroke_rectangle (GdkPen *cr, double x, double y, double w,
                                double h);
void ge_cairo_inner_rectangle (GdkPen *cr, double x, double y, double width,
                               double height);
void ge_cairo_inner_rounded_rectangle (GdkPen *cr, double x, double y,
                                       double width, double height,
                                       double radius, CairoCorners corners);
void ge_cairo_mirror (GdkPen *cr, CairoMirror mirror, gint *x, gint *y,
                      gint *width, gint *height);
void ge_cairo_exchange_axis (GdkPen *cr, gint *x, gint *y, gint *width,
                             gint *height);

/* ---------------------------------------------------------------------
 * The styles' drawing: clearlooks_draw.h
 */

#define clarus_register_style_classic  _gtk_clarus_register_style_classic
#define clarus_register_style_glossy   _gtk_clarus_register_style_glossy
#define clarus_register_style_gummy    _gtk_clarus_register_style_gummy
#define clarus_register_style_inverted _gtk_clarus_register_style_inverted
#define clarus_draw_focus              _gtk_clarus_draw_focus
#define clarus_set_mixed_color         _gtk_clarus_set_mixed_color

void clarus_register_style_classic (ClarusStyleFunctions   *functions,
                                    ClarusStyleConstants   *constants);
void clarus_register_style_glossy (ClarusStyleFunctions   *functions,
                                   ClarusStyleConstants   *constants);
void clarus_register_style_gummy (ClarusStyleFunctions   *functions,
                                  ClarusStyleConstants   *constants);
void clarus_register_style_inverted (ClarusStyleFunctions   *functions,
                                     ClarusStyleConstants   *constants);

/* Fallback focus function */
void clarus_draw_focus (GdkPen *cr, const ClarusColors *colors,
                        const WidgetParameters *widget,
                        const FocusParameters *focus,
                        int x, int y, int width, int height);

void clarus_set_mixed_color (GdkPen *cr, const CairoColor *color1,
                             const CairoColor *color2, gdouble mix_factor);

#endif /* __GTK_THEME_CLARUS_H__ */
