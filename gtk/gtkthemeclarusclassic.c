/* GTK - The GIMP Toolkit
 * Clarus: drawing for its classic style, which the others start from
 *
 * Ported from the Clearlooks engine of gtk-engines 2.20.2:
 * Copyright (C) 2006 Richard Stellingwerff <remenic@gmail.com>
 * Copyright (C) 2006 Daniel Borgman <daniel.borgmann@gmail.com>
 * Copyright (C) 2007 Benjamin Berg <benjamin@sipsolutions.net>
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

/* As Clearlooks drew it, with GdkPen (gdk/gdkpen.c) in place of cairo.
 * The code is Clearlooks' own, but for its names and a few variables
 * it set and never used, and its trailing spaces, and is kept in its
 * own layout, so that it can be compared with it. */

#include <math.h>

#include "gtkthemeclarus.h"

/* Normal shadings */
#define SHADE_TOP 1.055
#define SHADE_CENTER_TOP 1.01
#define SHADE_CENTER_BOTTOM 0.98
#define SHADE_BOTTOM 0.90

typedef void (*menubar_draw_proto) (GdkPen *cr,
                                    const ClarusColors     *colors,
                                    const WidgetParameters *params,
                                    const MenuBarParameters *menubar,
                                    int x, int y, int width, int height);

static void
clarus_draw_inset (GdkPen           *cr,
                   const CairoColor *bg_color,
                   double x, double y, double width, double height,
                   double radius, uint8 corners)
{
        CairoColor shadow;
        CairoColor highlight;
        double line_width;
        double min = MIN (width, height);

        line_width = _gdk_pen_get_line_width (cr);

        /* not really sure of shading ratios... we will think */
        ge_shade_color (bg_color, 0.94, &shadow);
        ge_shade_color (bg_color, 1.06, &highlight);

        /* highlight */
        _gdk_pen_save (cr);

        _gdk_pen_move_to (cr, x, y + height);
        _gdk_pen_line_to (cr, x + min / 2.0, y + height - min / 2.0);
        _gdk_pen_line_to (cr, x + width - min / 2.0, y + min / 2.0);
        _gdk_pen_line_to (cr, x + width, y);
        _gdk_pen_line_to (cr, x, y);
        _gdk_pen_close_path (cr);

        _gdk_pen_clip (cr);

        ge_cairo_rounded_rectangle (cr, x + line_width / 2.0, y + line_width / 2.0,
                                    width - line_width, height - line_width,
                                    radius, corners);

        ge_cairo_set_color (cr, &shadow);
        _gdk_pen_stroke (cr);

        _gdk_pen_restore (cr);

        /* shadow */
        _gdk_pen_save (cr);

        _gdk_pen_move_to (cr, x, y + height);
        _gdk_pen_line_to (cr, x + min / 2.0, y + height - min / 2.0);
        _gdk_pen_line_to (cr, x + width - min / 2.0, y + min / 2.0);
        _gdk_pen_line_to (cr, x + width, y);
        _gdk_pen_line_to (cr, x + width, y + height);
        _gdk_pen_close_path (cr);

        _gdk_pen_clip (cr);

        ge_cairo_rounded_rectangle (cr, x + line_width / 2.0, y + line_width / 2.0,
                                    width - line_width, height - line_width,
                                    radius, corners);

        ge_cairo_set_color (cr, &highlight);
        _gdk_pen_stroke (cr);

        _gdk_pen_restore (cr);
}

static void
clarus_draw_shadow (GdkPen *cr, const ClarusColors *colors, gfloat radius, int width, int height)
{
        CairoColor shadow;
        _gdk_pen_save (cr);

        ge_shade_color (&colors->shade[6], 0.92, &shadow);

        _gdk_pen_set_line_width (cr, 1.0);
        _gdk_pen_set_line_cap (cr, GDK_CAP_BUTT);

        _gdk_pen_set_rgba (cr, shadow.r, shadow.g, shadow.b, 0.1);

        _gdk_pen_move_to (cr, width - 0.5, radius);
        ge_cairo_rounded_corner (cr, width - 0.5, height - 0.5, radius, CR_CORNER_BOTTOMRIGHT);
        _gdk_pen_line_to (cr, radius, height - 0.5);

        _gdk_pen_stroke (cr);
        _gdk_pen_restore (cr);
}

/* This is copied at least in clarus_draw_gummy.c.
 * KEEP IN SYNC IF POSSIBLE! */
static void
clarus_draw_top_left_highlight (GdkPen *cr, const CairoColor *color,
                                const WidgetParameters *params,
                                int x, int y, int width, int height,
                                gdouble radius, CairoCorners corners)
{
        CairoColor hilight;

        double line_width = _gdk_pen_get_line_width (cr);
        double offset = line_width / 2.0;
        double light_top, light_bottom, light_left, light_right;

        _gdk_pen_save (cr);

        _gdk_pen_set_line_cap (cr, GDK_CAP_BUTT);

        light_top = y + offset;
        light_bottom = y + height;
        light_left = x + offset;
        light_right = x + width;

        if (corners & CR_CORNER_BOTTOMLEFT)
                light_bottom -= radius;
        if (corners & CR_CORNER_TOPRIGHT)
                light_right -= radius;

        ge_shade_color (color, params->style_constants->topleft_highlight_shade, &hilight);
        _gdk_pen_move_to         (cr, light_left, light_bottom);

        ge_cairo_rounded_corner (cr, light_left, light_top, radius, corners & CR_CORNER_TOPLEFT);

        _gdk_pen_line_to         (cr, light_right, light_top);
        _gdk_pen_set_rgba (cr, hilight.r, hilight.g, hilight.b, params->style_constants->topleft_highlight_alpha);
        _gdk_pen_stroke          (cr);

        _gdk_pen_restore (cr);
}

#ifdef DEVELOPMENT
#warning seems to be very slow in scrollbar_stepper
#endif

static void
clarus_draw_highlight_and_shade (GdkPen *cr, const ClarusColors *colors,
                                 const ShadowParameters *params,
                                 int width, int height, gdouble radius)
{
        CairoColor hilight;
        CairoColor shadow;
        uint8 corners = params->corners;
        double x = 1.0;
        double y = 1.0;

        ge_shade_color (&colors->bg[0], 1.06, &hilight);
        ge_shade_color (&colors->bg[0], 0.94, &shadow);

        width  -= 2;
        height -= 2;

        _gdk_pen_save (cr);

        /* Top/Left highlight */
        if (corners & CR_CORNER_BOTTOMLEFT)
                _gdk_pen_move_to (cr, x + 0.5, y+height-radius);
        else
                _gdk_pen_move_to (cr, x + 0.5, y+height);

        ge_cairo_rounded_corner (cr, x + 0.5, y + 0.5, radius, corners & CR_CORNER_TOPLEFT);

        if (corners & CR_CORNER_TOPRIGHT)
                _gdk_pen_line_to (cr, x+width-radius, y + 0.5);
        else
                _gdk_pen_line_to (cr, x+width, y + 0.5);

        if (params->shadow & CL_SHADOW_OUT)
                ge_cairo_set_color (cr, &hilight);
        else
                ge_cairo_set_color (cr, &shadow);

        _gdk_pen_stroke (cr);

        /* Bottom/Right highlight -- this includes the corners */
        _gdk_pen_arc (cr, x + width - 0.5 - radius, y + radius, radius, G_PI * (3/2.0+1/4.0), G_PI * 2);
        ge_cairo_rounded_corner (cr, x+width - 0.5, y+height - 0.5, radius, corners & CR_CORNER_BOTTOMRIGHT);
        _gdk_pen_arc (cr, x + radius, y + height - 0.5 - radius, radius, G_PI * 1/2, G_PI * 3/4);

        if (params->shadow & CL_SHADOW_OUT)
                ge_cairo_set_color (cr, &shadow);
        else
                ge_cairo_set_color (cr, &hilight);

        _gdk_pen_stroke (cr);

        _gdk_pen_restore (cr);
}

static void
clarus_set_border_gradient (GdkPen *cr, const CairoColor *color, double hilight, int width, int height)
{
        GdkPenPattern *pattern;

        CairoColor bottom_shade;
        ge_shade_color (color, hilight, &bottom_shade);

        pattern = _gdk_pen_pattern_new_linear (0, 0, width, height);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0, color->r, color->g, color->b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 1, bottom_shade.r, bottom_shade.g, bottom_shade.b);

        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_pattern_unref (pattern);
}

static void
clarus_draw_gripdots (GdkPen *cr, const ClarusColors *colors, int x, int y,
                      int width, int height, int xr, int yr,
                      float contrast)
{
        const CairoColor *dark = &colors->shade[4];
        CairoColor hilight;
        int i, j;
        int xoff, yoff;
        int x_start, y_start;

        ge_shade_color (dark, 1.5, &hilight);

        /* The "- 1" is because there is no space in front of the first dot. */
        x_start = x + width / 2 - ((xr * 3 - 1) / 2);
        y_start = y + height / 2 - ((yr * 3 - 1) / 2);

        for ( i = 0; i < xr; i++ )
        {
                for ( j = 0; j < yr; j++ )
                {
                        xoff = 3 * i;
                        yoff = 3 * j;

                        _gdk_pen_rectangle (cr, x_start + xoff, y_start + yoff, 2, 2);
                        _gdk_pen_set_rgba (cr, hilight.r, hilight.g, hilight.b, 0.8+contrast);
                        _gdk_pen_fill (cr);
                        _gdk_pen_rectangle (cr, x_start + xoff, y_start + yoff, 1, 1);
                        _gdk_pen_set_rgba (cr, dark->r, dark->g, dark->b, 0.8+contrast);
                        _gdk_pen_fill (cr);
                }
        }
}

static void
clarus_draw_button (GdkPen *cr,
                    const ClarusColors     *colors,
                    const WidgetParameters *params,
                    int x, int y, int width, int height)
{
        double xoffset = 0, yoffset = 0;
        double radius = params->radius;
        const CairoColor *fill = &colors->bg[params->state_type];
        CairoColor border_normal = colors->shade[6];
        CairoColor border_disabled = colors->shade[4];

        CairoColor shadow;
        ge_shade_color (&border_normal, 1.04, &border_normal);
        ge_shade_color (&border_normal, 0.94, &shadow);
        ge_shade_color (&border_disabled, 1.08, &border_disabled);

        _gdk_pen_save (cr);

        _gdk_pen_translate (cr, x, y);
        _gdk_pen_set_line_width (cr, 1.0);

        if (params->xthickness >= 3 && params->ythickness >= 3)
        {
                xoffset = 1;
                yoffset = 1;
        }

        radius = MIN (radius, MIN ((width - 2.0 - xoffset * 2.0) / 2.0, (height - 2.0 - yoffset * 2) / 2.0));

        if (params->xthickness >= 3 && params->ythickness >= 3)
        {
                params->style_functions->draw_inset (cr, &params->parentbg, 0, 0, width, height, radius+1, params->corners);
        }

        ge_cairo_rounded_rectangle (cr, xoffset+1, yoffset+1,
                                             width-(xoffset*2)-2,
                                             height-(yoffset*2)-2,
                                             radius, params->corners);

        if (!params->active)
        {
                GdkPenPattern *pattern;
                CairoColor top_shade, topmiddle_shade, bottom_shade, middle_shade;

                ge_shade_color (fill, SHADE_TOP, &top_shade);
                ge_shade_color (fill, SHADE_CENTER_TOP, &topmiddle_shade);
                ge_shade_color (fill, SHADE_CENTER_BOTTOM, &middle_shade);
                ge_shade_color (fill, SHADE_BOTTOM, &bottom_shade);

                _gdk_pen_save (cr);
                _gdk_pen_clip_preserve (cr);

                pattern = _gdk_pen_pattern_new_linear (0, 0, 0, height);
                _gdk_pen_pattern_add_stop_rgb (pattern, 0.0, top_shade.r, top_shade.g, top_shade.b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 0.3, topmiddle_shade.r, topmiddle_shade.g, topmiddle_shade.b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 0.7, middle_shade.r, middle_shade.g, middle_shade.b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 1.0, bottom_shade.r, bottom_shade.g, bottom_shade.b);
                _gdk_pen_set_pattern (cr, pattern);
                _gdk_pen_fill (cr);
                _gdk_pen_pattern_unref (pattern);

                _gdk_pen_move_to (cr, width-(xoffset*2)-0.5, 0);
                _gdk_pen_line_to (cr, width-(xoffset*2)-0.5, height);
                ge_cairo_set_color (cr, &bottom_shade);
                _gdk_pen_stroke (cr);

                /* Draw topleft shadow */
                params->style_functions->draw_top_left_highlight (cr, fill, params, xoffset + 1, yoffset + 1,
                                                                  width - 2*(xoffset + 1), height - 2*(yoffset + 1),
                                                                  MAX(radius-1, 0), params->corners);

                _gdk_pen_restore (cr);
        }
        else
        {
                GdkPenPattern *pattern;

                ge_cairo_set_color (cr, fill);
                _gdk_pen_fill_preserve (cr);

                pattern = _gdk_pen_pattern_new_linear (0, 0, 0, height);
                _gdk_pen_pattern_add_stop_rgba (pattern, 0.0, shadow.r, shadow.g, shadow.b, 0.0);
                _gdk_pen_pattern_add_stop_rgba (pattern, 0.4, shadow.r, shadow.g, shadow.b, 0.0);
                _gdk_pen_pattern_add_stop_rgba (pattern, 1.0, shadow.r, shadow.g, shadow.b, 0.2);
                _gdk_pen_set_pattern (cr, pattern);
                _gdk_pen_fill_preserve (cr);
                _gdk_pen_pattern_unref (pattern);

                pattern = _gdk_pen_pattern_new_linear (0, yoffset+1, 0, 3+yoffset);
                _gdk_pen_pattern_add_stop_rgba (pattern, 0.0, shadow.r, shadow.g, shadow.b, params->disabled ? 0.125 : 0.32);
                _gdk_pen_pattern_add_stop_rgba (pattern, 1.0, shadow.r, shadow.g, shadow.b, 0.0);
                _gdk_pen_set_pattern (cr, pattern);
                _gdk_pen_fill_preserve (cr);
                _gdk_pen_pattern_unref (pattern);

                pattern = _gdk_pen_pattern_new_linear (xoffset+1, 0, 3+xoffset, 0);
                _gdk_pen_pattern_add_stop_rgba (pattern, 0.0, shadow.r, shadow.g, shadow.b, params->disabled ? 0.125 : 0.32);
                _gdk_pen_pattern_add_stop_rgba (pattern, 1.0, shadow.r, shadow.g, shadow.b, 0.0);
                _gdk_pen_set_pattern (cr, pattern);
                _gdk_pen_fill (cr);
                _gdk_pen_pattern_unref (pattern);
        }

        /* Drawing the border */
        if (!params->active && params->is_default)
        {
                ge_shade_color (&border_normal, 0.74, &border_normal);
        }

        ge_cairo_inner_rounded_rectangle (cr, xoffset, yoffset, width-(xoffset*2), height-(yoffset*2), radius, params->corners);

        if (params->disabled)
        {
                ge_cairo_set_color (cr, &border_disabled);
        }
        else
        {
                if (!params->active)
                        clarus_set_border_gradient (cr, &border_normal,
                                                    params->is_default ? 1.1 : 1.3, 0, height);
                else
                {
                        ge_shade_color (&border_normal, 1.08, &border_normal);
                        ge_cairo_set_color (cr, &border_normal);
                }
        }

        _gdk_pen_stroke (cr);

        _gdk_pen_restore (cr);
}

static void
clarus_draw_entry (GdkPen *cr,
                   const ClarusColors     *colors,
                   const WidgetParameters *params,
                   const FocusParameters  *focus,
                   int x, int y, int width, int height)
{
        const CairoColor *base = &colors->base[params->state_type];
        CairoColor border = colors->shade[params->disabled ? 3 : 6];
        double radius = MIN (params->radius, MIN ((width - 4.0) / 2.0, (height - 4.0) / 2.0));
        int xoffset, yoffset;

        if (params->focus)
                border = focus->color;

        _gdk_pen_save (cr);

        _gdk_pen_translate (cr, x, y);
        _gdk_pen_set_line_width (cr, 1.0);

        if (params->xthickness >= 3 && params->ythickness >= 3)
        {
                params->style_functions->draw_inset (cr, &params->parentbg, 0, 0, width, height, radius+1, params->corners);
                xoffset = 1;
                yoffset = 1;
        }
        else
        {
                xoffset = 0;
                yoffset = 0;
        }

        /* Now fill the area we want to be base[NORMAL]. */
        ge_cairo_rounded_rectangle (cr, xoffset + 1, yoffset + 1, width - (xoffset + 1)*2,
                                    height - (yoffset + 1) * 2, MAX(0, radius-1),
                                    params->corners);
        ge_cairo_set_color (cr, base);
        _gdk_pen_fill (cr);

        /* Draw the inner shadow */
        if (params->focus)
        {
                CairoColor focus_shadow;
                ge_shade_color (&border, 1.61, &focus_shadow);

                clarus_set_mixed_color (cr, base, &focus_shadow, 0.5);
                ge_cairo_inner_rounded_rectangle (cr, xoffset + 1, yoffset + 1,
                                                  width - (xoffset + 1)*2, height - (yoffset + 1)*2,
                                                  MAX(0, radius-1), params->corners);
                _gdk_pen_stroke (cr);
        }
        else
        {
                CairoColor shadow;
                ge_shade_color (&border, 0.925, &shadow);

                _gdk_pen_set_rgba (cr, shadow.r, shadow.g, shadow.b, params->disabled ? 0.05 : 0.1);

                _gdk_pen_set_line_cap (cr, GDK_CAP_BUTT);
                _gdk_pen_move_to (cr, 2.5, height-radius);
                _gdk_pen_arc (cr, xoffset + 1.5 + MAX(0, radius-1), yoffset + 1.5 + MAX(0, radius-1),
                              MAX(0, radius-1), G_PI, 270*(G_PI/180));
                _gdk_pen_line_to (cr, width-radius, 2.5);
                _gdk_pen_stroke (cr);
        }

        ge_cairo_inner_rounded_rectangle (cr, xoffset, yoffset,
                                          width-2*xoffset, height-2*yoffset,
                                          radius, params->corners);
        if (params->focus || params->disabled)
                ge_cairo_set_color (cr, &border);
        else
                clarus_set_border_gradient (cr, &border, 1.32, 0, height);
        _gdk_pen_stroke (cr);

        _gdk_pen_restore (cr);
}

static void
clarus_draw_spinbutton (GdkPen *cr,
                        const ClarusColors     *colors,
                        const WidgetParameters *params,
                        int x, int y, int width, int height)
{
        const CairoColor *border = &colors->shade[!params->disabled ? 5 : 3];
        CairoColor hilight;

        params->style_functions->draw_button (cr, colors, params, x, y, width, height);

        ge_shade_color (&colors->bg[0], params->style_constants->topleft_highlight_shade, &hilight);
        hilight.a = params->style_constants->topleft_highlight_alpha;

        _gdk_pen_translate (cr, x, y);

        _gdk_pen_move_to (cr, params->xthickness + 0.5,       (height/2) + 0.5);
        _gdk_pen_line_to (cr, width-params->xthickness - 0.5, (height/2) + 0.5);
        ge_cairo_set_color (cr, border);
        _gdk_pen_stroke (cr);

        _gdk_pen_move_to (cr, params->xthickness + 0.5,       (height/2)+1.5);
        _gdk_pen_line_to (cr, width-params->xthickness - 0.5, (height/2)+1.5);
        ge_cairo_set_color (cr, &hilight);
        _gdk_pen_stroke (cr);
}

static void
clarus_draw_spinbutton_down (GdkPen *cr,
                             const ClarusColors     *colors,
                             const WidgetParameters *params,
                             int x, int y, int width, int height)
{
        GdkPenPattern *pattern;
        double radius = MIN (params->radius, MIN ((width - 4.0) / 2.0, (height - 4.0) / 2.0));
        CairoColor shadow;
        ge_shade_color (&colors->bg[0], 0.8, &shadow);

        _gdk_pen_translate (cr, x+1, y+1);

        ge_cairo_rounded_rectangle (cr, 1, 1, width-4, height-4, radius, params->corners);

        ge_cairo_set_color (cr, &colors->bg[params->state_type]);

        _gdk_pen_fill_preserve (cr);

        pattern = _gdk_pen_pattern_new_linear (0, 0, 0, height);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.0, shadow.r, shadow.g, shadow.b);
        _gdk_pen_pattern_add_stop_rgba (pattern, 1.0, shadow.r, shadow.g, shadow.b, 0.0);

        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_fill (cr);

        _gdk_pen_pattern_unref (pattern);
}

static void
clarus_scale_draw_gradient (GdkPen *cr,
                            const CairoColor *c1,
                            const CairoColor *c2,
                            const CairoColor *c3,
                            int x, int y, int width, int height,
                            boolean horizontal)
{
        GdkPenPattern *pattern;

        pattern = _gdk_pen_pattern_new_linear (0.5, 0.5, horizontal ? 0.5 :  width + 1, horizontal ? height + 1: 0.5);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.0, c1->r, c1->g, c1->b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 1.0, c2->r, c2->g, c2->b);

        _gdk_pen_rectangle (cr, x, y, width, height);
        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_fill (cr);
        _gdk_pen_pattern_unref (pattern);

        ge_cairo_set_color (cr, c3);
        ge_cairo_inner_rectangle (cr, x, y, width, height);
        _gdk_pen_stroke (cr);
}

#define TROUGH_SIZE 7
static void
clarus_draw_scale_trough (GdkPen *cr,
                          const ClarusColors     *colors,
                          const WidgetParameters *params,
                          const SliderParameters *slider,
                          int x, int y, int width, int height)
{
        int     trough_width, trough_height;
        double  translate_x, translate_y;

        _gdk_pen_save (cr);

        if (slider->horizontal)
        {
                trough_width  = width;
                trough_height = TROUGH_SIZE;

                translate_x   = x;
                translate_y   = y + (height/2) - (TROUGH_SIZE/2);
        }
        else
        {
                trough_width  = TROUGH_SIZE;
                trough_height = height;

                translate_x   = x + (width/2) - (TROUGH_SIZE/2);
                translate_y  = y;
        }

        _gdk_pen_set_line_width (cr, 1.0);
        _gdk_pen_translate (cr, translate_x, translate_y);

        if (!slider->fill_level)
                params->style_functions->draw_inset (cr, &params->parentbg, 0, 0, trough_width, trough_height, 0, 0);

        if (!slider->lower && !slider->fill_level)
        {
                CairoColor shadow;
                ge_shade_color (&colors->shade[2], 0.96, &shadow);

                clarus_scale_draw_gradient (cr, &shadow, /* top */
                                            &colors->shade[2], /* bottom */
                                            &colors->shade[4], /* border */
                                            1.0, 1.0, trough_width - 2, trough_height - 2,
                                            slider->horizontal);
        }
        else
        {
                CairoColor border = colors->spot[2];
                border.a = 0.64;

                clarus_scale_draw_gradient (cr, &colors->spot[1], /* top */
                                            &colors->spot[0], /* bottom */
                                            &border, /* border */
                                            1.0, 1.0, trough_width - 2, trough_height - 2,
                                            slider->horizontal);
        }
        _gdk_pen_restore (cr);
}

static void
clarus_draw_slider (GdkPen *cr,
                    const ClarusColors     *colors,
                    const WidgetParameters *params,
                    int x, int y, int width, int height)
{
        const CairoColor *spot   = &colors->spot[1];
        const CairoColor *fill   = &colors->shade[2];
        CairoColor border = colors->shade[params->disabled ? 4 : 6];
        double radius = MIN (params->radius, MIN ((width - 1.0) / 2.0, (height - 1.0) / 2.0));

        GdkPenPattern *pattern;

        _gdk_pen_set_line_width (cr, 1.0);
        _gdk_pen_translate      (cr, x, y);

        if (params->prelight)
                border = colors->spot[2];

        /* fill the widget */
        ge_cairo_rounded_rectangle (cr, 1.0, 1.0, width-2, height-2, radius, params->corners);

        /* Fake light */
        if (!params->disabled)
        {
                const CairoColor *top = &colors->shade[0];
                const CairoColor *bot = &colors->shade[2];

                pattern = _gdk_pen_pattern_new_linear (0, 0, 0, height);
                _gdk_pen_pattern_add_stop_rgb (pattern, 0.0,  top->r, top->g, top->b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 1.0,  bot->r, bot->g, bot->b);
                _gdk_pen_set_pattern (cr, pattern);
                _gdk_pen_fill (cr);
                _gdk_pen_pattern_unref (pattern);
        }
        else
        {
                ge_cairo_set_color (cr, fill);
                _gdk_pen_fill         (cr);
        }

        /* Set the clip */
        _gdk_pen_save (cr);
        _gdk_pen_rectangle (cr, 1.0, 1.0, 6, height-2);
        _gdk_pen_rectangle (cr, width-7.0, 1.0, 6, height-2);
        _gdk_pen_clip_preserve (cr);

        _gdk_pen_new_path (cr);

        /* Draw the handles */
        ge_cairo_rounded_rectangle (cr, 1.0, 1.0, width-1, height-1, radius, params->corners);
        pattern = _gdk_pen_pattern_new_linear (1.0, 1.0, 1.0, 1.0+height);

        if (params->prelight)
        {
                CairoColor highlight;
                ge_shade_color (spot, 1.3, &highlight);
                _gdk_pen_pattern_add_stop_rgb (pattern, 0.0, highlight.r, highlight.g, highlight.b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 1.0, spot->r, spot->g, spot->b);
                _gdk_pen_set_pattern (cr, pattern);
        }
        else
        {
                CairoColor hilight;
                ge_shade_color (fill, 1.3, &hilight);
                _gdk_pen_set_rgba (cr, hilight.r, hilight.g, hilight.b, 0.5);
        }

        _gdk_pen_fill (cr);
        _gdk_pen_pattern_unref (pattern);

        _gdk_pen_restore (cr);

        /* Draw the border */
        ge_cairo_inner_rounded_rectangle (cr, 0, 0, width, height, radius, params->corners);

        if (params->prelight || params->disabled)
                ge_cairo_set_color (cr, &border);
        else
                clarus_set_border_gradient (cr, &border, 1.2, 0, height);
        _gdk_pen_stroke (cr);

        /* Draw handle lines */
        if (width > 14)
        {
                _gdk_pen_move_to (cr, 6.5, 1.0);
                _gdk_pen_line_to (cr, 6.5, height-1);

                _gdk_pen_move_to (cr, width-6.5, 1.0);
                _gdk_pen_line_to (cr, width-6.5, height-1);

                _gdk_pen_set_line_width (cr, 1.0);
                border.a = params->disabled ? 0.6 : 0.3;
                ge_cairo_set_color (cr, &border);
                _gdk_pen_stroke (cr);
        }
}

static void
clarus_draw_slider_button (GdkPen *cr,
                           const ClarusColors     *colors,
                           const WidgetParameters *params,
                           const SliderParameters *slider,
                           int x, int y, int width, int height)
{
        double radius = MIN (params->radius, MIN ((width - 1.0) / 2.0, (height - 1.0) / 2.0));

        _gdk_pen_save (cr);
        _gdk_pen_set_line_width (cr, 1.0);

        if (!slider->horizontal)
                ge_cairo_exchange_axis (cr, &x, &y, &width, &height);
        _gdk_pen_translate (cr, x, y);

        params->style_functions->draw_shadow (cr, colors, radius, width, height);
        params->style_functions->draw_slider (cr, colors, params, 1, 1, width-2, height-2);

        if (width > 24)
                params->style_functions->draw_gripdots (cr, colors, 1, 1, width-2, height-2, 3, 3, 0);

        _gdk_pen_restore (cr);
}

static void
clarus_draw_progressbar_trough (GdkPen *cr,
                                const ClarusColors     *colors,
                                const WidgetParameters *params,
                                int x, int y, int width, int height)
{
        const CairoColor *border = &colors->shade[4];
        CairoColor        shadow;
        GdkPenPattern    *pattern;
        double            radius = MIN (params->radius, MIN ((height-2.0) / 2.0, (width-2.0) / 2.0));

        _gdk_pen_save (cr);

        _gdk_pen_set_line_width (cr, 1.0);

        /* Create trough box */
        ge_cairo_rounded_rectangle (cr, x+1, y+1, width-2, height-2, radius, params->corners);
        ge_cairo_set_color (cr, &colors->shade[2]);
        _gdk_pen_fill (cr);

        /* Draw border */
        ge_cairo_rounded_rectangle (cr, x+0.5, y+0.5, width-1, height-1, radius, params->corners);
        ge_cairo_set_color (cr, border);
        _gdk_pen_stroke (cr);

        /* clip the corners of the shadows */
        ge_cairo_rounded_rectangle (cr, x+1, y+1, width-2, height-2, radius, params->corners);
        _gdk_pen_clip (cr);

        ge_shade_color (border, 0.925, &shadow);

        /* Top shadow */
        _gdk_pen_rectangle (cr, x+1, y+1, width-2, 4);
        pattern = _gdk_pen_pattern_new_linear (x, y, x, y+4);
        _gdk_pen_pattern_add_stop_rgba (pattern, 0.0, shadow.r, shadow.g, shadow.b, 0.2);
        _gdk_pen_pattern_add_stop_rgba (pattern, 1.0, shadow.r, shadow.g, shadow.b, 0);
        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_fill (cr);
        _gdk_pen_pattern_unref (pattern);

        /* Left shadow */
        _gdk_pen_rectangle (cr, x+1, y+1, 4, height-2);
        pattern = _gdk_pen_pattern_new_linear (x, y, x+4, y);
        _gdk_pen_pattern_add_stop_rgba (pattern, 0.0, shadow.r, shadow.g, shadow.b, 0.2);
        _gdk_pen_pattern_add_stop_rgba (pattern, 1.0, shadow.r, shadow.g, shadow.b, 0);
        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_fill (cr);
        _gdk_pen_pattern_unref (pattern);

        _gdk_pen_restore (cr);
}

static void
clarus_draw_progressbar_fill (GdkPen *cr,
                              const ClarusColors     *colors,
                              const WidgetParameters *params,
                              const ProgressBarParameters *progressbar,
                              int x, int y, int width, int height,
                              gint offset)
{
        boolean      is_horizontal = progressbar->orientation < 2;
        double       tile_pos = 0;
        double       stroke_width;
        double       radius;
        int          x_step;

        GdkPenPattern *pattern;
        CairoColor       bg_shade;
        CairoColor       border;
        CairoColor       shadow;

        radius = MAX (0, params->radius - params->xthickness);

        _gdk_pen_save (cr);

        if (!is_horizontal)
                ge_cairo_exchange_axis (cr, &x, &y, &width, &height);

        if ((progressbar->orientation == CL_ORIENTATION_RIGHT_TO_LEFT) || (progressbar->orientation == CL_ORIENTATION_BOTTOM_TO_TOP))
                ge_cairo_mirror (cr, CR_MIRROR_HORIZONTAL, &x, &y, &width, &height);

        /* Clamp the radius so that the _height_ fits ...  */
        radius = MIN (radius, height / 2.0);

        stroke_width = height;
        x_step = (((float)stroke_width/10)*offset); /* This looks weird ... */

        _gdk_pen_translate (cr, x, y);

        _gdk_pen_save (cr);
        /* This is kind of nasty ... Clip twice from each side in case the length
         * of the fill is smaller than twice the radius. */
        ge_cairo_rounded_rectangle (cr, 0, 0, width + radius, height, radius, CR_CORNER_TOPLEFT | CR_CORNER_BOTTOMLEFT);
        _gdk_pen_clip (cr);
        ge_cairo_rounded_rectangle (cr, -radius, 0, width + radius, height, radius, CR_CORNER_TOPRIGHT | CR_CORNER_BOTTOMRIGHT);
        _gdk_pen_clip (cr);

        /* Draw the background gradient */
        ge_shade_color (&colors->spot[1], 1.1, &bg_shade);

        /* Just leave this disabled, maybe we could use the same gradient
         * as the buttons in the future, not flat fill */
/*      pattern = _gdk_pen_pattern_new_linear (0, 0, 0, height);*/
/*      _gdk_pen_pattern_add_stop_rgb (pattern, 0.0, bg_shade.r, bg_shade.g, bg_shade.b);*/
/*      _gdk_pen_pattern_add_stop_rgb (pattern, 0.6, colors->spot[1].r, colors->spot[1].g, colors->spot[1].b);*/
/*      _gdk_pen_pattern_add_stop_rgb (pattern, 1.0, bg_shade.r, bg_shade.g, bg_shade.b);*/
/*      _gdk_pen_set_pattern (cr, pattern);*/
/*      _gdk_pen_paint (cr);*/
/*      _gdk_pen_pattern_unref (pattern);*/

        ge_cairo_set_color (cr, &bg_shade);
        _gdk_pen_paint (cr);

        /* Draw the Strokes */
        while (stroke_width > 0 && tile_pos <= width+x_step)
        {
                _gdk_pen_move_to (cr, stroke_width/2-x_step, 0);
                _gdk_pen_line_to (cr, stroke_width-x_step,   0);
                _gdk_pen_line_to (cr, stroke_width/2-x_step, height);
                _gdk_pen_line_to (cr, -x_step, height);

                _gdk_pen_translate (cr, stroke_width, 0);
                tile_pos += stroke_width;
        }

        pattern = _gdk_pen_pattern_new_linear (0, 0, 0, height);
        _gdk_pen_pattern_add_stop_rgba (pattern, 0.0, colors->spot[2].r, colors->spot[2].g, colors->spot[2].b, 0);
        _gdk_pen_pattern_add_stop_rgba (pattern, 1.0, colors->spot[2].r, colors->spot[2].g, colors->spot[2].b, 0.24);
        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_fill (cr);
        _gdk_pen_pattern_unref (pattern);

        _gdk_pen_restore (cr); /* rounded clip region */

        /* Draw the dark lines and the shadow */
        _gdk_pen_save (cr);
        /* Again, this weird clip area. */
        ge_cairo_rounded_rectangle (cr, -1.0, 0, width + radius + 2.0, height, radius, CR_CORNER_TOPLEFT | CR_CORNER_BOTTOMLEFT);
        _gdk_pen_clip (cr);
        ge_cairo_rounded_rectangle (cr, -radius - 1.0, 0, width + radius + 2.0, height, radius, CR_CORNER_TOPRIGHT | CR_CORNER_BOTTOMRIGHT);
        _gdk_pen_clip (cr);

        shadow.r = 0.0;
        shadow.g = 0.0;
        shadow.b = 0.0;
        shadow.a = 0.1;

        if (progressbar->pulsing)
        {
                /* At the beginning of the bar. */
                _gdk_pen_move_to (cr, -0.5 + radius, height + 0.5);
                ge_cairo_rounded_corner (cr, -0.5, height + 0.5, radius + 1, CR_CORNER_BOTTOMLEFT);
                ge_cairo_rounded_corner (cr, -0.5, -0.5, radius + 1, CR_CORNER_TOPLEFT);
                ge_cairo_set_color (cr, &shadow);
                _gdk_pen_stroke (cr);
        }
        if (progressbar->value < 1.0 || progressbar->pulsing)
        {
                /* At the end of the bar. */
                _gdk_pen_move_to (cr, width + 0.5 - radius, -0.5);
                ge_cairo_rounded_corner (cr, width + 0.5, -0.5, radius + 1, CR_CORNER_TOPRIGHT);
                ge_cairo_rounded_corner (cr, width + 0.5, height + 0.5, radius + 1, CR_CORNER_BOTTOMRIGHT);
                ge_cairo_set_color (cr, &shadow);
                _gdk_pen_stroke (cr);
        }

/*      ge_cairo_rounded_rectangle (cr, 1.5,1.5, width-2, height-2, radius, CR_CORNER_ALL);*/
/*      _gdk_pen_set_rgba (cr, colors->spot[0].r, colors->spot[0].g, colors->spot[0].b, 1);*/
/*      _gdk_pen_stroke (cr);*/

        params->style_functions->draw_top_left_highlight (cr, &colors->spot[1], params, 1, 1,
                                                          width - 1, height - 1,
                                                          radius, params->corners);

        border = colors->spot[2];
        border.a = 0.6;
        ge_cairo_rounded_rectangle (cr, 0.5, 0.5, width-1, height-1, radius, CR_CORNER_ALL);
        ge_cairo_set_color (cr, &border);
        _gdk_pen_stroke (cr);

        _gdk_pen_restore (cr);

        _gdk_pen_restore (cr); /* rotation, mirroring */
}

static void
clarus_draw_optionmenu (GdkPen *cr,
                        const ClarusColors     *colors,
                        const WidgetParameters *params,
                        const OptionMenuParameters *optionmenu,
                        int x, int y, int width, int height)
{
        SeparatorParameters separator;
        int offset = params->ythickness + 2;

        params->style_functions->draw_button (cr, colors, params, x, y, width, height);

        separator.horizontal = FALSE;
        params->style_functions->draw_separator (cr, colors, params, &separator, x+optionmenu->linepos, y + offset, 2, height - offset*2);
}

static void
clarus_draw_menu_item_separator (GdkPen                    *cr,
                                 const ClarusColors        *colors,
                                 const WidgetParameters    *widget,
                                 const SeparatorParameters *separator,
                                 int x, int y, int width, int height)
{
        _gdk_pen_save (cr);
        _gdk_pen_set_line_cap (cr, GDK_CAP_BUTT);
        ge_cairo_set_color (cr, &colors->shade[5]);

        if (separator->horizontal)
                _gdk_pen_rectangle (cr, x, y, width, 1);
        else
                _gdk_pen_rectangle (cr, x, y, 1, height);

        _gdk_pen_fill      (cr);

        _gdk_pen_restore (cr);
}

static void
clarus_draw_menubar0 (GdkPen *cr,
                      const ClarusColors     *colors,
                      const WidgetParameters *params,
                      const MenuBarParameters *menubar,
                      int x, int y, int width, int height)
{
        const CairoColor *dark = &colors->shade[3];

        _gdk_pen_save (cr);

        _gdk_pen_set_line_width (cr, 1);
        _gdk_pen_translate (cr, x, y);

        _gdk_pen_move_to (cr, 0, height-0.5);
        _gdk_pen_line_to (cr, width, height-0.5);
        ge_cairo_set_color (cr, dark);
        _gdk_pen_stroke (cr);

        _gdk_pen_restore (cr);
}

static void
clarus_draw_menubar2 (GdkPen *cr,
                      const ClarusColors     *colors,
                      const WidgetParameters *params,
                      const MenuBarParameters *menubar,
                      int x, int y, int width, int height)
{
        CairoColor lower;
        GdkPenPattern *pattern;

        _gdk_pen_save (cr);

        ge_shade_color (&colors->bg[0], 0.96, &lower);

        _gdk_pen_translate (cr, x, y);
        _gdk_pen_rectangle (cr, 0, 0, width, height);

        /* Draw the gradient */
        pattern = _gdk_pen_pattern_new_linear (0, 0, 0, height);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.0, colors->bg[0].r,
                                                     colors->bg[0].g,
                                                     colors->bg[0].b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 1.0, lower.r,
                                                     lower.g,
                                                     lower.b);
        _gdk_pen_set_pattern      (cr, pattern);
        _gdk_pen_fill            (cr);
        _gdk_pen_pattern_unref (pattern);

        /* Draw bottom line */
        _gdk_pen_set_line_width (cr, 1.0);
        _gdk_pen_move_to        (cr, 0, height-0.5);
        _gdk_pen_line_to        (cr, width, height-0.5);
        ge_cairo_set_color   (cr, &colors->shade[3]);
        _gdk_pen_stroke         (cr);

        _gdk_pen_restore (cr);
}

static void
clarus_draw_menubar1 (GdkPen *cr,
                      const ClarusColors     *colors,
                      const WidgetParameters *params,
                      const MenuBarParameters *menubar,
                      int x, int y, int width, int height)
{
        const CairoColor *border = &colors->shade[3];

        clarus_draw_menubar2 (cr, colors, params, menubar,
                              x, y, width, height);

        ge_cairo_set_color (cr, border);
        ge_cairo_stroke_rectangle (cr, 0.5, 0.5, width-1, height-1);
}


static menubar_draw_proto clarus_menubar_draw[3] =
{
        clarus_draw_menubar0,
        clarus_draw_menubar1,
        clarus_draw_menubar2
};

static void
clarus_draw_menubar (GdkPen *cr,
                     const ClarusColors     *colors,
                     const WidgetParameters *params,
                     const MenuBarParameters *menubar,
                     int x, int y, int width, int height)
{
        if (menubar->style < 0 || menubar->style >= G_N_ELEMENTS (clarus_menubar_draw))
                return;

        clarus_menubar_draw[menubar->style](cr, colors, params, menubar,
                                     x, y, width, height);
}

static void
clarus_get_frame_gap_clip (int x, int y, int width, int height,
                           const FrameParameters     *frame,
                           ClarusRectangle     *bevel,
                           ClarusRectangle     *border)
{
        if (frame->gap_side == CL_GAP_TOP)
        {
                CLARUS_RECTANGLE_SET (*bevel,  2.0 + frame->gap_x,  0.0,
                                      frame->gap_width - 3, 2.0);
                CLARUS_RECTANGLE_SET (*border, 1.0 + frame->gap_x,  0.0,
                                         frame->gap_width - 2, 2.0);
        }
        else if (frame->gap_side == CL_GAP_BOTTOM)
        {
                CLARUS_RECTANGLE_SET (*bevel,  2.0 + frame->gap_x,  height - 2.0,
                                      frame->gap_width - 3, 2.0);
                CLARUS_RECTANGLE_SET (*border, 1.0 + frame->gap_x,  height - 1.0,
                                      frame->gap_width - 2, 2.0);
        }
        else if (frame->gap_side == CL_GAP_LEFT)
        {
                CLARUS_RECTANGLE_SET (*bevel,  0.0, 2.0 + frame->gap_x,
                                      2.0, frame->gap_width - 3);
                CLARUS_RECTANGLE_SET (*border, 0.0, 1.0 + frame->gap_x,
                                      1.0, frame->gap_width - 2);
        }
        else if (frame->gap_side == CL_GAP_RIGHT)
        {
                CLARUS_RECTANGLE_SET (*bevel,  width - 2.0, 2.0 + frame->gap_x,
                                      2.0, frame->gap_width - 3);
                CLARUS_RECTANGLE_SET (*border, width - 1.0, 1.0 + frame->gap_x,
                                      1.0, frame->gap_width - 2);
        }
}

static void
clarus_draw_frame            (GdkPen *cr,
                              const ClarusColors         *colors,
                              const WidgetParameters     *params,
                              const FrameParameters      *frame,
                              int x, int y, int width, int height)
{
        const CairoColor *border = frame->border;
        const CairoColor *dark   = (CairoColor*)&colors->shade[4];
        ClarusRectangle bevel_clip = {0, 0, 0, 0};
        ClarusRectangle frame_clip = {0, 0, 0, 0};
        double radius = MIN (params->radius, MIN ((width - 2.0) / 2.0, (height - 2.0) / 2.0));
        CairoColor hilight;

        ge_shade_color (&colors->bg[0], 1.05, &hilight);

        if (frame->shadow == CL_SHADOW_NONE)
                return;

        if (frame->gap_x != -1)
                clarus_get_frame_gap_clip (x, y, width, height,
                                           frame, &bevel_clip, &frame_clip);

        _gdk_pen_set_line_width (cr, 1.0);
        _gdk_pen_translate      (cr, x, y);

        /* save everything */
        _gdk_pen_save (cr);
        /* Set clip for the bevel */
        if (frame->gap_x != -1)
        {
                /* Set clip for gap */
                _gdk_pen_set_fill_rule  (cr, GDK_EVEN_ODD_RULE);
                _gdk_pen_rectangle      (cr, 0, 0, width, height);
                _gdk_pen_rectangle      (cr, bevel_clip.x, bevel_clip.y, bevel_clip.width, bevel_clip.height);
                _gdk_pen_clip           (cr);
        }

        /* Draw the bevel */
        if (frame->shadow == CL_SHADOW_ETCHED_IN || frame->shadow == CL_SHADOW_ETCHED_OUT)
        {
                ge_cairo_set_color (cr, &hilight);
                if (frame->shadow == CL_SHADOW_ETCHED_IN)
                        ge_cairo_inner_rounded_rectangle (cr, 1, 1, width-1, height-1, radius, params->corners);
                else
                        ge_cairo_inner_rounded_rectangle (cr, 0, 0, width-1, height-1, radius, params->corners);
                _gdk_pen_stroke (cr);
        }
        else if (frame->shadow != CL_SHADOW_NONE)
        {
                ShadowParameters shadow;
                shadow.corners = params->corners;
                shadow.shadow  = frame->shadow;
                clarus_draw_highlight_and_shade (cr, colors, &shadow, width, height, radius);
        }

        /* restore the previous clip region */
        _gdk_pen_restore    (cr);
        _gdk_pen_save       (cr);
        if (frame->gap_x != -1)
        {
                /* Set clip for gap */
                _gdk_pen_set_fill_rule  (cr, GDK_EVEN_ODD_RULE);
                _gdk_pen_rectangle      (cr, 0, 0, width, height);
                _gdk_pen_rectangle      (cr, frame_clip.x, frame_clip.y, frame_clip.width, frame_clip.height);
                _gdk_pen_clip           (cr);
        }

        /* Draw frame */
        if (frame->shadow == CL_SHADOW_ETCHED_IN || frame->shadow == CL_SHADOW_ETCHED_OUT)
        {
                ge_cairo_set_color (cr, dark);
                if (frame->shadow == CL_SHADOW_ETCHED_IN)
                        ge_cairo_inner_rounded_rectangle (cr, 0, 0, width-1, height-1, radius, params->corners);
                else
                        ge_cairo_inner_rounded_rectangle (cr, 1, 1, width-1, height-1, radius, params->corners);
        }
        else
        {
                ge_cairo_set_color (cr, border);
                ge_cairo_inner_rounded_rectangle (cr, 0, 0, width, height, radius, params->corners);
        }
        _gdk_pen_stroke (cr);

        _gdk_pen_restore (cr);
}

static void
clarus_draw_tab (GdkPen *cr,
                 const ClarusColors     *colors,
                 const WidgetParameters *params,
                 const TabParameters    *tab,
                 int x, int y, int width, int height)
{
        const CairoColor *border1       = &colors->shade[6];
        const CairoColor *border2       = &colors->shade[5];
        const CairoColor *stripe_fill   = &colors->spot[1];
        const CairoColor *stripe_border = &colors->spot[2];
        const CairoColor *fill;
        CairoColor        hilight;

        GdkPenPattern    *pattern;

        double            radius;
        double            stripe_size = 2.0;
        double            stripe_fill_size;
        double            length;

        radius = MIN (params->radius, MIN ((width - 2.0) / 2.0, (height - 2.0) / 2.0));

        _gdk_pen_save (cr);

        /* Set clip */
        _gdk_pen_rectangle      (cr, x, y, width, height);
        _gdk_pen_clip           (cr);
        _gdk_pen_new_path       (cr);

        /* Translate and set line width */
        _gdk_pen_set_line_width (cr, 1.0);
        _gdk_pen_translate      (cr, x, y);


        /* Make the tabs slightly bigger than they should be, to create a gap */
        /* And calculate the strip size too, while you're at it */
        if (tab->gap_side == CL_GAP_TOP || tab->gap_side == CL_GAP_BOTTOM)
        {
                height += 3.0;
                length = height;
                stripe_fill_size = (tab->gap_side == CL_GAP_TOP ? stripe_size/height : stripe_size/(height-2));

                if (tab->gap_side == CL_GAP_TOP)
                        _gdk_pen_translate (cr, 0.0, -3.0); /* gap at the other side */
        }
        else
        {
                width += 3.0;
                length = width;
                stripe_fill_size = (tab->gap_side == CL_GAP_LEFT ? stripe_size/width : stripe_size/(width-2));

                if (tab->gap_side == CL_GAP_LEFT)
                        _gdk_pen_translate (cr, -3.0, 0.0); /* gap at the other side */
        }

        /* Set the fill color */
        fill = &colors->bg[params->state_type];

        /* Set tab shape */
        ge_cairo_rounded_rectangle (cr, 0.5, 0.5, width-1, height-1,
                                    radius, params->corners);

        /* Draw fill */
        ge_cairo_set_color (cr, fill);
        _gdk_pen_fill   (cr);


        ge_shade_color (fill, 1.3, &hilight);

        /* Draw highlight */
        if (!params->active)
        {
                ShadowParameters shadow;

                shadow.shadow  = CL_SHADOW_OUT;
                shadow.corners = params->corners;

                clarus_draw_highlight_and_shade (cr, colors, &shadow,
                                                 width,
                                                 height, radius);
        }

        if (params->active)
        {
                CairoColor shadow;
                switch (tab->gap_side)
                {
                        case CL_GAP_TOP:
                                pattern = _gdk_pen_pattern_new_linear (0.5, height-1.5, 0.5, 0.5);
                                break;
                        case CL_GAP_BOTTOM:
                                pattern = _gdk_pen_pattern_new_linear (0.5, 1.5, 0.5, height+0.5);
                                break;
                        case CL_GAP_LEFT:
                                pattern = _gdk_pen_pattern_new_linear (width-1.5, 0.5, 1.5, 0.5);
                                break;
                        case CL_GAP_RIGHT:
                                pattern = _gdk_pen_pattern_new_linear (1.5, 0.5, width-1.5, 0.5);
                                break;
                        default:
                                pattern = NULL;
                }

                ge_cairo_rounded_rectangle (cr, 0.5, 0.5, width-1, height-1, radius, params->corners);

                ge_shade_color (fill, 0.92, &shadow);

                _gdk_pen_pattern_add_stop_rgba  (pattern, 0.0,        hilight.r, hilight.g, hilight.b, 0.4);
                _gdk_pen_pattern_add_stop_rgba  (pattern, 1.0/length, hilight.r, hilight.g, hilight.b, 0.4);
                _gdk_pen_pattern_add_stop_rgb   (pattern, 1.0/length, fill->r,fill->g,fill->b);
                _gdk_pen_pattern_add_stop_rgb   (pattern, 1.0,        shadow.r,shadow.g,shadow.b);
                _gdk_pen_set_pattern (cr, pattern);
                _gdk_pen_fill (cr);
                _gdk_pen_pattern_unref (pattern);
        }
        else
        {
                /* Draw shade */
                switch (tab->gap_side)
                {
                        case CL_GAP_TOP:
                                pattern = _gdk_pen_pattern_new_linear (0.5, height-1.5, 0.5, 0.5);
                                break;
                        case CL_GAP_BOTTOM:
                                pattern = _gdk_pen_pattern_new_linear (0.5, 0.5, 0.5, height+0.5);
                                break;
                        case CL_GAP_LEFT:
                                pattern = _gdk_pen_pattern_new_linear (width-1.5, 0.5, 0.5, 0.5);
                                break;
                        case CL_GAP_RIGHT:
                                pattern = _gdk_pen_pattern_new_linear (0.5, 0.5, width+0.5, 0.5);
                                break;
                        default:
                                pattern = NULL;
                }

                ge_cairo_rounded_rectangle (cr, 0.5, 0.5, width-1, height-1, radius, params->corners);

                _gdk_pen_pattern_add_stop_rgb  (pattern, 0.0,        stripe_fill->r, stripe_fill->g, stripe_fill->b);
                _gdk_pen_pattern_add_stop_rgb  (pattern, stripe_fill_size, stripe_fill->r, stripe_fill->g, stripe_fill->b);
                _gdk_pen_pattern_add_stop_rgba (pattern, stripe_fill_size, hilight.r, hilight.g, hilight.b, 0.5);
                _gdk_pen_pattern_add_stop_rgba (pattern, 0.8,        hilight.r, hilight.g, hilight.b, 0.0);
                _gdk_pen_set_pattern (cr, pattern);
                _gdk_pen_fill (cr);
                _gdk_pen_pattern_unref (pattern);
        }

        ge_cairo_inner_rounded_rectangle (cr, 0, 0, width, height, radius, params->corners);

        if (params->active)
        {
                ge_cairo_set_color (cr, border2);
                _gdk_pen_stroke (cr);
        }
        else
        {
                switch (tab->gap_side)
                {
                        case CL_GAP_TOP:
                                pattern = _gdk_pen_pattern_new_linear (2.5, height-1.5, 2.5, 2.5);
                                break;
                        case CL_GAP_BOTTOM:
                                pattern = _gdk_pen_pattern_new_linear (2.5, 2.5, 2.5, height+0.5);
                                break;
                        case CL_GAP_LEFT:
                                pattern = _gdk_pen_pattern_new_linear (width-1.5, 2.5, 2.5, 2.5);
                                break;
                        case CL_GAP_RIGHT:
                                pattern = _gdk_pen_pattern_new_linear (2.5, 2.5, width+0.5, 2.5);
                                break;
                        default:
                                pattern = NULL;
                }

                _gdk_pen_pattern_add_stop_rgb (pattern, 0.0,        stripe_border->r, stripe_border->g, stripe_border->b);
                _gdk_pen_pattern_add_stop_rgb (pattern, stripe_fill_size, stripe_border->r, stripe_border->g, stripe_border->b);
                _gdk_pen_pattern_add_stop_rgb (pattern, stripe_fill_size, border1->r,       border1->g,       border1->b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 1.0,        border2->r,       border2->g,       border2->b);
                _gdk_pen_set_pattern (cr, pattern);
                _gdk_pen_stroke (cr);
                _gdk_pen_pattern_unref (pattern);
        }

        _gdk_pen_restore (cr);
}

static void
clarus_draw_separator (GdkPen *cr,
                       const ClarusColors         *colors,
                       const WidgetParameters     *widget,
                       const SeparatorParameters  *separator,
                       int x, int y, int width, int height)
{
        CairoColor color = colors->shade[2];
        CairoColor hilight;
        ge_shade_color (&colors->bg[0], 1.065, &hilight);

        _gdk_pen_save (cr);
        _gdk_pen_set_line_cap (cr, GDK_CAP_BUTT);

        if (separator->horizontal)
        {
                _gdk_pen_set_line_width  (cr, 1.0);
                _gdk_pen_translate       (cr, x, y+0.5);

                _gdk_pen_move_to         (cr, 0.0,   0.0);
                _gdk_pen_line_to         (cr, width, 0.0);
                ge_cairo_set_color    (cr, &color);
                _gdk_pen_stroke          (cr);

                _gdk_pen_move_to         (cr, 0.0,   1.0);
                _gdk_pen_line_to         (cr, width, 1.0);
                ge_cairo_set_color    (cr, &hilight);
                _gdk_pen_stroke          (cr);
        }
        else
        {
                _gdk_pen_set_line_width  (cr, 1.0);
                _gdk_pen_translate       (cr, x+0.5, y);

                _gdk_pen_move_to         (cr, 0.0, 0.0);
                _gdk_pen_line_to         (cr, 0.0, height);
                ge_cairo_set_color    (cr, &color);
                _gdk_pen_stroke          (cr);

                _gdk_pen_move_to         (cr, 1.0, 0.0);
                _gdk_pen_line_to         (cr, 1.0, height);
                ge_cairo_set_color    (cr, &hilight);
                _gdk_pen_stroke          (cr);
        }

        _gdk_pen_restore (cr);
}

static void
clarus_draw_list_view_header (GdkPen *cr,
                              const ClarusColors              *colors,
                              const WidgetParameters          *params,
                              const ListViewHeaderParameters  *header,
                              int x, int y, int width, int height)
{
        const CairoColor *border = &colors->shade[4];
        CairoColor hilight;

        ge_shade_color (&colors->bg[params->state_type],
                        params->style_constants->topleft_highlight_shade, &hilight);
        hilight.a = params->style_constants->topleft_highlight_alpha;

        _gdk_pen_translate (cr, x, y);
        _gdk_pen_set_line_width (cr, 1.0);

        /* Draw highlight */
        if (header->order & CL_ORDER_FIRST)
        {
                _gdk_pen_move_to (cr, 0.5, height-1);
                _gdk_pen_line_to (cr, 0.5, 0.5);
        }
        else
                _gdk_pen_move_to (cr, 0.0, 0.5);

        _gdk_pen_line_to (cr, width, 0.5);

        ge_cairo_set_color (cr, &hilight);
        _gdk_pen_stroke (cr);

        /* Draw bottom border */
        _gdk_pen_move_to (cr, 0.0, height-0.5);
        _gdk_pen_line_to (cr, width, height-0.5);
        ge_cairo_set_color (cr, border);
        _gdk_pen_stroke (cr);

        /* Draw resize grip */
        if ((params->ltr && !(header->order & CL_ORDER_LAST)) ||
            (!params->ltr && !(header->order & CL_ORDER_FIRST)) || header->resizable)
        {
                SeparatorParameters separator;
                separator.horizontal = FALSE;

                if (params->ltr)
                        params->style_functions->draw_separator (cr, colors, params, &separator,
                                                                 (int) (width - 1.5), 4, 2, height - 8);
                else
                        params->style_functions->draw_separator (cr, colors, params, &separator,
                                                                 1, 4, 2, height - 8);
        }
}

/* We can't draw transparent things here, since it will be called on the same
 * surface multiple times, when placed on a handlebox_bin or dockitem_bin */
static void
clarus_draw_toolbar (GdkPen *cr,
                     const ClarusColors              *colors,
                     const WidgetParameters          *widget,
                     const ToolbarParameters         *toolbar,
                     int x, int y, int width, int height)
{
        const CairoColor *fill  = &colors->bg[0];
        const CairoColor *dark  = &colors->shade[3];
        CairoColor light;
        ge_shade_color (fill, 1.065, &light);

        _gdk_pen_set_line_width (cr, 1.0);
        _gdk_pen_translate (cr, x, y);

        ge_cairo_set_color (cr, fill);
        _gdk_pen_paint (cr);

        if (!toolbar->topmost)
        {
                /* Draw highlight */
                _gdk_pen_move_to       (cr, 0, 0.5);
                _gdk_pen_line_to       (cr, width-1, 0.5);
                ge_cairo_set_color  (cr, &light);
                _gdk_pen_stroke        (cr);
        }

        /* Draw shadow */
        _gdk_pen_move_to       (cr, 0, height-0.5);
        _gdk_pen_line_to       (cr, width-1, height-0.5);
        ge_cairo_set_color  (cr, dark);
        _gdk_pen_stroke        (cr);
}

static void
clarus_draw_menuitem (GdkPen *cr,
                      const ClarusColors              *colors,
                      const WidgetParameters          *widget,
                      int x, int y, int width, int height)
{
        const CairoColor *fill = &colors->spot[1];
        CairoColor fill_shade;
        CairoColor border = colors->spot[2];
        GdkPenPattern *pattern;

        ge_shade_color (&border, 1.05, &border);
        ge_shade_color (fill, 0.85, &fill_shade);
        _gdk_pen_set_line_width (cr, 1.0);

        ge_cairo_rounded_rectangle (cr, x+0.5, y+0.5, width - 1, height - 1, widget->radius, widget->corners);

        pattern = _gdk_pen_pattern_new_linear (x, y, x, y + height);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0,   fill->r, fill->g, fill->b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 1.0, fill_shade.r, fill_shade.g, fill_shade.b);

        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_fill_preserve  (cr);
        _gdk_pen_pattern_unref (pattern);

        ge_cairo_set_color (cr, &border);
        _gdk_pen_stroke (cr);
}

static void
clarus_draw_menubaritem (GdkPen *cr,
                          const ClarusColors          *colors,
                          const WidgetParameters      *widget,
                          int x, int y, int width, int height)
{
        const CairoColor *fill = &colors->spot[1];
        CairoColor fill_shade;
        CairoColor border = colors->spot[2];
        GdkPenPattern *pattern;

        ge_shade_color (&border, 1.05, &border);
        ge_shade_color (fill, 0.85, &fill_shade);

        _gdk_pen_set_line_width (cr, 1.0);
        ge_cairo_rounded_rectangle (cr, x + 0.5, y + 0.5, width - 1, height, widget->radius, widget->corners);

        pattern = _gdk_pen_pattern_new_linear (x, y, x, y + height);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0,   fill->r, fill->g, fill->b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 1.0, fill_shade.r, fill_shade.g, fill_shade.b);

        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_fill_preserve  (cr);
        _gdk_pen_pattern_unref (pattern);

        ge_cairo_set_color (cr, &border);
        _gdk_pen_stroke_preserve (cr);
}

static void
clarus_draw_scrollbar_trough (GdkPen *cr,
                              const ClarusColors               *colors,
                              const WidgetParameters           *widget,
                              const ScrollBarParameters        *scrollbar,
                              int x, int y, int width, int height)
{
        const CairoColor *bg     = &colors->shade[2];
        const CairoColor *border = &colors->shade[5];
        CairoColor        bg_shade;
        GdkPenPattern *pattern;
        double radius = MIN (widget->radius, MIN ((width - 2.0) / 2.0, (height - 2.0) / 2.0));

        ge_shade_color (bg, 0.95, &bg_shade);

        _gdk_pen_set_line_width (cr, 1);
        /* _gdk_pen_translate (cr, x, y); */

        if (scrollbar->horizontal)
                ge_cairo_exchange_axis (cr, &x, &y, &width, &height);

        _gdk_pen_translate (cr, x, y);

        /* Draw fill */
        if (radius > 3.0)
                ge_cairo_rounded_rectangle (cr, 1, 0, width-2, height,
                                            radius, widget->corners);
        else
                _gdk_pen_rectangle (cr, 1, 0, width-2, height);
        ge_cairo_set_color (cr, bg);
        _gdk_pen_fill (cr);

        /* Draw shadow */
        pattern = _gdk_pen_pattern_new_linear (1, 0, 3, 0);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0,   bg_shade.r, bg_shade.g, bg_shade.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 1.0, bg->r,      bg->g,      bg->b);
        _gdk_pen_rectangle (cr, 1, 0, 4, height);
        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_fill (cr);
        _gdk_pen_pattern_unref (pattern);

        /* Draw border */
        if (radius > 3.0)
                ge_cairo_rounded_rectangle (cr, 0.5, 0.5, width-1, height-1,
                                            radius, widget->corners);
        else
                _gdk_pen_rectangle (cr, 0.5, 0.5, width-1, height-1);
        ge_cairo_set_color (cr, border);
        _gdk_pen_stroke (cr);
}

static void
clarus_draw_scrollbar_stepper (GdkPen *cr,
                               const ClarusColors               *colors,
                               const WidgetParameters           *widget,
                               const ScrollBarParameters        *scrollbar,
                               const ScrollBarStepperParameters *stepper,
                               int x, int y, int width, int height)
{
        CairoCorners corners = CR_CORNER_NONE;
        CairoColor   border;
        CairoColor   s1, s2, s3, s4;
        GdkPenPattern *pattern;
        double radius = MIN (widget->radius, MIN ((width - 2.0) / 2.0, (height - 2.0) / 2.0));

        ge_shade_color(&colors->shade[6], 1.08, &border);

        if (scrollbar->horizontal)
        {
                if (stepper->stepper == CL_STEPPER_A)
                        corners = CR_CORNER_TOPLEFT | CR_CORNER_BOTTOMLEFT;
                else if (stepper->stepper == CL_STEPPER_D)
                        corners = CR_CORNER_TOPRIGHT | CR_CORNER_BOTTOMRIGHT;

                if (stepper->stepper == CL_STEPPER_B)
                {
                        x -= 1;
                        width += 1;
                }
                else if (stepper->stepper == CL_STEPPER_C)
                {
                        width += 1;
                }
        }
        else
        {
                if (stepper->stepper == CL_STEPPER_A)
                        corners = CR_CORNER_TOPLEFT | CR_CORNER_TOPRIGHT;
                else if (stepper->stepper == CL_STEPPER_D)
                        corners = CR_CORNER_BOTTOMLEFT | CR_CORNER_BOTTOMRIGHT;

                if (stepper->stepper == CL_STEPPER_B)
                {
                        y -= 1;
                        height += 1;
                }
                else if (stepper->stepper == CL_STEPPER_C)
                {
                        height += 1;
                }
        }

        _gdk_pen_translate (cr, x, y);
        _gdk_pen_set_line_width (cr, 1);

        ge_cairo_rounded_rectangle (cr, 1, 1, width-2, height-2, radius, corners);

        if (scrollbar->horizontal)
                pattern = _gdk_pen_pattern_new_linear (0, 0, 0, height);
        else
                pattern = _gdk_pen_pattern_new_linear (0, 0, width, 0);

        ge_shade_color (&colors->bg[widget->state_type], SHADE_TOP, &s1);
        ge_shade_color (&colors->bg[widget->state_type], SHADE_CENTER_TOP, &s2);
        ge_shade_color (&colors->bg[widget->state_type], SHADE_CENTER_BOTTOM, &s3);
        ge_shade_color (&colors->bg[widget->state_type], SHADE_BOTTOM, &s4);

        _gdk_pen_pattern_add_stop_rgb(pattern, 0,   s1.r, s1.g, s1.b);
        _gdk_pen_pattern_add_stop_rgb(pattern, 0.3, s2.r, s2.g, s2.b);
        _gdk_pen_pattern_add_stop_rgb(pattern, 0.7, s3.r, s3.g, s3.b);
        _gdk_pen_pattern_add_stop_rgb(pattern, 1.0, s4.r, s4.g, s4.b);
        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_fill (cr);
        _gdk_pen_pattern_unref (pattern);

        widget->style_functions->draw_top_left_highlight (cr, &s2, widget, 1, 1, width - 2, height - 2, MAX(radius - 1, 0), corners);

        ge_cairo_inner_rounded_rectangle (cr, 0, 0, width, height, radius, corners);
        clarus_set_border_gradient (cr, &border, 1.1, (scrollbar->horizontal ? 0 : width), (scrollbar->horizontal ? height: 0));
        _gdk_pen_stroke (cr);
}

static void
clarus_draw_scrollbar_slider (GdkPen *cr,
                                   const ClarusColors          *colors,
                                   const WidgetParameters      *widget,
                                   const ScrollBarParameters   *scrollbar,
                                   int x, int y, int width, int height)
{
        _gdk_pen_save (cr);

        if (scrollbar->junction & CL_JUNCTION_BEGIN)
        {
                if (scrollbar->horizontal)
                {
                        x -= 1;
                        width += 1;
                }
                else
                {
                        y -= 1;
                        height += 1;
                }
        }
        if (scrollbar->junction & CL_JUNCTION_END)
        {
                if (scrollbar->horizontal)
                        width += 1;
                else
                        height += 1;
        }

        if (!scrollbar->horizontal)
                ge_cairo_exchange_axis (cr, &x, &y, &width, &height);

        _gdk_pen_translate (cr, x, y);

        if (scrollbar->has_color)
        {
                const CairoColor *border  = &colors->shade[7];
                CairoColor  fill    = scrollbar->color;
                CairoColor  hilight;
                CairoColor  shade1, shade2, shade3;
                GdkPenPattern *pattern;

                if (widget->prelight)
                        ge_shade_color (&fill, 1.1, &fill);

                _gdk_pen_set_line_width (cr, 1);

                ge_shade_color (&fill, 1.3, &hilight);
                ge_shade_color (&fill, 1.1, &shade1);
                ge_shade_color (&fill, 1.05, &shade2);
                ge_shade_color (&fill, 0.98, &shade3);

                pattern = _gdk_pen_pattern_new_linear (1, 1, 1, height-2);
                _gdk_pen_pattern_add_stop_rgb (pattern, 0,   shade1.r, shade1.g, shade1.b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 0.5,    shade2.r, shade2.g, shade2.b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 0.5,    shade3.r, shade3.g, shade3.b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 1,      fill.r,  fill.g,  fill.b);
                _gdk_pen_rectangle (cr, 1, 1, width-2, height-2);
                _gdk_pen_set_pattern (cr, pattern);
                _gdk_pen_fill (cr);
                _gdk_pen_pattern_unref (pattern);

                _gdk_pen_set_rgba (cr, hilight.r, hilight.g, hilight.b, 0.5);
                ge_cairo_stroke_rectangle (cr, 1.5, 1.5, width-3, height-3);

                ge_cairo_set_color (cr, border);
                ge_cairo_stroke_rectangle (cr, 0.5, 0.5, width-1, height-1);
        }
        else
        {
                const CairoColor *dark  = &colors->shade[4];
                const CairoColor *light = &colors->shade[0];
                CairoColor border;
                CairoColor s1, s2, s3, s4, s5;
                GdkPenPattern *pattern;
                int bar_x, i;

                ge_shade_color (&colors->shade[6], 1.08, &border);
                ge_shade_color (&colors->bg[widget->state_type], SHADE_TOP, &s1);
                ge_shade_color (&colors->bg[widget->state_type], SHADE_CENTER_TOP, &s2);
                ge_shade_color (&colors->bg[widget->state_type], SHADE_CENTER_BOTTOM, &s3);
                ge_shade_color (&colors->bg[widget->state_type], SHADE_BOTTOM, &s4);

                pattern = _gdk_pen_pattern_new_linear(1, 1, 1, height-1);
                _gdk_pen_pattern_add_stop_rgb(pattern, 0,   s1.r, s1.g, s1.b);
                _gdk_pen_pattern_add_stop_rgb(pattern, 0.3, s2.r, s2.g, s2.b);
                _gdk_pen_pattern_add_stop_rgb(pattern, 0.7, s3.r, s3.g, s3.b);
                _gdk_pen_pattern_add_stop_rgb(pattern, 1.0, s4.r, s4.g, s4.b);

                _gdk_pen_rectangle (cr, 1, 1, width-2, height-2);
                _gdk_pen_set_pattern(cr, pattern);
                _gdk_pen_fill(cr);
                _gdk_pen_pattern_unref(pattern);

                clarus_set_border_gradient (cr, &border, 1.1, 0, height);
                ge_cairo_stroke_rectangle (cr, 0.5, 0.5, width-1, height-1);

                _gdk_pen_move_to (cr, 1.5, height-1.5);
                _gdk_pen_line_to (cr, 1.5, 1.5);
                _gdk_pen_line_to (cr, width-1.5, 1.5);
                ge_shade_color (&s2, widget->style_constants->topleft_highlight_shade, &s5);
                s5.a = widget->style_constants->topleft_highlight_alpha;
                ge_cairo_set_color (cr, &s5);
                _gdk_pen_stroke(cr);

                /* draw handles */
                _gdk_pen_set_line_width (cr, 1);
                _gdk_pen_set_line_cap (cr, GDK_CAP_BUTT);

                bar_x = width/2 - 4;

                for (i=0; i<3; i++)
                {
                        _gdk_pen_move_to (cr, bar_x + 0.5, 4);
                        _gdk_pen_line_to (cr, bar_x + 0.5, height-4);
                        ge_cairo_set_color (cr, dark);
                        _gdk_pen_stroke (cr);

                        _gdk_pen_move_to (cr, bar_x+1.5, 4);
                        _gdk_pen_line_to (cr, bar_x+1.5, height-4);
                        ge_cairo_set_color (cr, light);
                        _gdk_pen_stroke (cr);

                        bar_x += 3;
                }
        }

        _gdk_pen_restore (cr);
}

static void
clarus_draw_statusbar (GdkPen *cr,
                       const ClarusColors              *colors,
                       const WidgetParameters          *widget,
                       int x, int y, int width, int height)
{
        const CairoColor *dark = &colors->shade[3];
        CairoColor hilight;

        ge_shade_color (dark, 1.4, &hilight);

        _gdk_pen_set_line_width  (cr, 1);
        _gdk_pen_translate       (cr, x, y+0.5);
        _gdk_pen_move_to         (cr, 0, 0);
        _gdk_pen_line_to         (cr, width, 0);
        ge_cairo_set_color    (cr, dark);
        _gdk_pen_stroke          (cr);

        _gdk_pen_translate       (cr, 0, 1);
        _gdk_pen_move_to         (cr, 0, 0);
        _gdk_pen_line_to         (cr, width, 0);
        ge_cairo_set_color    (cr, &hilight);
        _gdk_pen_stroke          (cr);
}

static void
clarus_draw_menu_frame (GdkPen *cr,
                        const ClarusColors              *colors,
                        const WidgetParameters          *widget,
                        int x, int y, int width, int height)
{
        const CairoColor *border = &colors->shade[5];
        _gdk_pen_translate      (cr, x, y);
        _gdk_pen_set_line_width (cr, 1);

        ge_cairo_set_color (cr, border);
        ge_cairo_stroke_rectangle (cr, 0.5, 0.5, width-1, height-1);
}

static void
clarus_draw_tooltip (GdkPen *cr,
                     const ClarusColors              *colors,
                     const WidgetParameters          *widget,
                     int x, int y, int width, int height)
{
        CairoColor border;

        ge_shade_color (&colors->bg[widget->state_type], 0.6, &border);

        _gdk_pen_save (cr);

        _gdk_pen_translate      (cr, x, y);
        _gdk_pen_set_line_width (cr, 1);

        ge_cairo_set_color (cr, &colors->bg[widget->state_type]);
        _gdk_pen_rectangle (cr, 0, 0, width, height);
        _gdk_pen_fill (cr);

        ge_cairo_set_color (cr, &border);
        ge_cairo_stroke_rectangle (cr, 0.5, 0.5, width-1, height-1);

        _gdk_pen_restore (cr);
}

static void
clarus_draw_handle (GdkPen *cr,
                    const ClarusColors              *colors,
                    const WidgetParameters          *params,
                    const HandleParameters          *handle,
                    int x, int y, int width, int height)
{
        const CairoColor *fill  = &colors->bg[params->state_type];
        int num_bars = 6; /* shut up gcc warnings */

        _gdk_pen_save (cr);

        switch (handle->type)
        {
                case CL_HANDLE_TOOLBAR:
                        num_bars    = 6;
                break;
                case CL_HANDLE_SPLITTER:
                        num_bars    = 16;
                break;
        }

        if (params->prelight)
        {
                _gdk_pen_rectangle (cr, x, y, width, height);
                ge_cairo_set_color (cr, fill);
                _gdk_pen_fill (cr);
        }

        _gdk_pen_translate (cr, x, y);

        _gdk_pen_set_line_width (cr, 1);

        if (handle->horizontal)
        {
                params->style_functions->draw_gripdots (cr, colors, 0, 0, width, height, num_bars, 2, 0.1);
        }
        else
        {
                params->style_functions->draw_gripdots (cr, colors, 0, 0, width, height, 2, num_bars, 0.1);
        }

        _gdk_pen_restore (cr);
}

static void
clarus_draw_radiobutton (GdkPen *cr,
                         const ClarusColors      *colors,
                         const WidgetParameters  *widget,
                         const CheckboxParameters *checkbox,
                         int x, int y, int width, int height)
{
        const CairoColor *border;
        const CairoColor *dot;
        CairoColor shadow;
        CairoColor highlight;
        GdkPenPattern *pt;
        gboolean inconsistent;
        gboolean draw_bullet = (checkbox->shadow_type == CL_SHADOW_IN);
        gdouble cx, cy, radius;

        cx = width / 2.0;
        cy = height / 2.0;
        radius = MIN (width, height) / 2.0;

        inconsistent = (checkbox->shadow_type == CL_SHADOW_ETCHED_IN);
        draw_bullet |= inconsistent;

        if (widget->disabled)
        {
                border = &colors->shade[5];
                dot    = &colors->shade[6];
        }
        else
        {
                border = &colors->shade[6];
                dot    = &colors->text[0];
        }

        ge_shade_color (&widget->parentbg, 0.9, &shadow);
        ge_shade_color (&widget->parentbg, 1.1, &highlight);

        pt = _gdk_pen_pattern_new_linear (0, 0, radius * 2.0, radius * 2.0);
        _gdk_pen_pattern_add_stop_rgb (pt, 0.0, shadow.r, shadow.b, shadow.g);
        _gdk_pen_pattern_add_stop_rgba (pt, 0.5, shadow.r, shadow.b, shadow.g, 0.5);
        _gdk_pen_pattern_add_stop_rgba (pt, 0.5, highlight.r, highlight.g, highlight.b, 0.5);
        _gdk_pen_pattern_add_stop_rgb (pt, 1.0, highlight.r, highlight.g, highlight.b);

        _gdk_pen_translate (cr, x, y);

        _gdk_pen_set_line_width (cr, MAX (1.0, floor (radius/3)));
        _gdk_pen_arc (cr, ceil (cx), ceil (cy), floor (radius - 0.1), 0, G_PI*2);
        _gdk_pen_set_pattern (cr, pt);
        _gdk_pen_stroke (cr);
        _gdk_pen_pattern_unref (pt);

        _gdk_pen_set_line_width (cr, MAX (1.0, floor (radius/6)));

        _gdk_pen_arc (cr, ceil (cx), ceil (cy), MAX (1.0, ceil (radius) - 1.5), 0, G_PI*2);

        if (!widget->disabled)
        {
                ge_cairo_set_color (cr, &colors->base[0]);
                _gdk_pen_fill_preserve (cr);
        }

        ge_cairo_set_color (cr, border);
        _gdk_pen_stroke (cr);

        if (draw_bullet)
        {
                if (inconsistent)
                {
                        gdouble line_width = floor (radius * 2 / 3);

                        _gdk_pen_set_line_cap(cr, GDK_CAP_ROUND);
                        _gdk_pen_set_line_width (cr, line_width);

                        _gdk_pen_move_to (cr, ceil (cx - radius/3.0 - line_width) + line_width, ceil (cy - line_width) + line_width);
                        _gdk_pen_line_to (cr, floor (cx + radius/3.0 + line_width) - line_width, ceil (cy - line_width) + line_width);

                        ge_cairo_set_color (cr, dot);
                        _gdk_pen_stroke (cr);
                }
                else
                {
                        _gdk_pen_arc (cr, ceil (cx), ceil (cy), floor (radius/2.0), 0, G_PI*2);
                        ge_cairo_set_color (cr, dot);
                        _gdk_pen_fill (cr);

                        _gdk_pen_arc (cr, floor (cx - radius/10.0), floor (cy - radius/10.0), floor (radius/6.0), 0, G_PI*2);
                        _gdk_pen_set_rgba (cr, highlight.r, highlight.g, highlight.b, 0.5);
                        _gdk_pen_fill (cr);
                }
        }
}

static void
clarus_draw_checkbox (GdkPen *cr,
                      const ClarusColors      *colors,
                      const WidgetParameters  *widget,
                      const CheckboxParameters *checkbox,
                      int x, int y, int width, int height)
{
        const CairoColor *border;
        const CairoColor *dot;
        gboolean inconsistent = FALSE;
        gboolean draw_bullet = (checkbox->shadow_type == CL_SHADOW_IN);

        inconsistent = (checkbox->shadow_type == CL_SHADOW_ETCHED_IN);
        draw_bullet |= inconsistent;

        if (widget->disabled)
        {
                border = &colors->shade[5];
                dot    = &colors->shade[6];
        }
        else
        {
                border = &colors->shade[6];
                dot    = &colors->text[GTK_STATE_NORMAL];
        }

        _gdk_pen_translate (cr, x, y);
        _gdk_pen_set_line_width (cr, 1);

        if (widget->xthickness >= 3 && widget->ythickness >= 3)
        {
                widget->style_functions->draw_inset (cr, &widget->parentbg, 0, 0, width, height, 1, CR_CORNER_ALL);

                /* Draw the rectangle for the checkbox itself */
                ge_cairo_rounded_rectangle (cr, 1.5, 1.5, width-3, height-3, (widget->radius > 0)? 1 : 0, CR_CORNER_ALL);
        }
        else
        {
                /* Draw the rectangle for the checkbox itself */
                ge_cairo_rounded_rectangle (cr, 0.5, 0.5, width-1, height-1, (widget->radius > 0)? 1 : 0, CR_CORNER_ALL);
        }

        if (!widget->disabled)
        {
                ge_cairo_set_color (cr, &colors->base[0]);
                _gdk_pen_fill_preserve (cr);
        }

        ge_cairo_set_color (cr, border);
        _gdk_pen_stroke (cr);

        if (draw_bullet)
        {
                if (inconsistent) /* Inconsistent */
                {
                        _gdk_pen_set_line_width (cr, 2.0);
                        _gdk_pen_move_to (cr, 3, height*0.5);
                        _gdk_pen_line_to (cr, width-3, height*0.5);
                }
                else
                {
                        _gdk_pen_set_line_width (cr, 1.7);
                        _gdk_pen_move_to (cr, 0.5 + (width*0.2), (height*0.5));
                        _gdk_pen_line_to (cr, 0.5 + (width*0.4), (height*0.7));

                        _gdk_pen_curve_to (cr, 0.5 + (width*0.4), (height*0.7),
                                               0.5 + (width*0.5), (height*0.4),
                                               0.5 + (width*0.70), (height*0.25));

                }

                ge_cairo_set_color (cr, dot);
                _gdk_pen_stroke (cr);
        }
}

static void
clarus_draw_normal_arrow (GdkPen *cr, const CairoColor *color,
                          double x, double y, double width, double height)
{
        double arrow_width;
        double arrow_height;
        double line_width_2;

        _gdk_pen_save (cr);

        arrow_width = MIN (height * 2.0 + MAX (1.0, ceil (height * 2.0 / 6.0 * 2.0) / 2.0) / 2.0, width);
        line_width_2 = MAX (1.0, ceil (arrow_width / 6.0 * 2.0) / 2.0) / 2.0;
        arrow_height = arrow_width / 2.0 + line_width_2;

        _gdk_pen_translate (cr, x, y - arrow_height / 2.0);

        _gdk_pen_move_to (cr, -arrow_width / 2.0, line_width_2);
        _gdk_pen_line_to (cr, -arrow_width / 2.0 + line_width_2, 0);
        /* _gdk_pen_line_to (cr, 0, arrow_height - line_width_2); */
        _gdk_pen_arc_negative (cr, 0, arrow_height - 2*line_width_2 - 2*line_width_2 * sqrt(2), 2*line_width_2, G_PI_2 + G_PI_4, G_PI_4);
        _gdk_pen_line_to (cr, arrow_width / 2.0 - line_width_2, 0);
        _gdk_pen_line_to (cr, arrow_width / 2.0, line_width_2);
        _gdk_pen_line_to (cr, 0, arrow_height);
        _gdk_pen_close_path (cr);

        ge_cairo_set_color (cr, color);
        _gdk_pen_fill (cr);

        _gdk_pen_restore (cr);
}

static void
clarus_draw_combo_arrow (GdkPen *cr, const CairoColor *color,
                         double x, double y, double width, double height)
{
        double arrow_width = MIN (height * 2 / 3.0, width);
        double arrow_height = arrow_width / 2.0;
        double gap_size = 1.0 * arrow_height;

        _gdk_pen_save (cr);
        _gdk_pen_translate (cr, x, y - (arrow_height + gap_size) / 2.0);
        _gdk_pen_rotate (cr, G_PI);
        clarus_draw_normal_arrow (cr, color, 0, 0, arrow_width, arrow_height);
        _gdk_pen_restore (cr);

        clarus_draw_normal_arrow (cr, color, x, y + (arrow_height + gap_size) / 2.0, arrow_width, arrow_height);
}

static void
_clarus_draw_arrow (GdkPen *cr, const CairoColor *color,
                    ClarusDirection     dir, ClarusArrowType type,
                    double x, double y, double width, double height)
{
        double rotate;

        if (dir == CL_DIRECTION_LEFT)
                rotate = G_PI*1.5;
        else if (dir == CL_DIRECTION_RIGHT)
                rotate = G_PI*0.5;
        else if (dir == CL_DIRECTION_UP)
                rotate = G_PI;
        else if (dir == CL_DIRECTION_DOWN)
                rotate = 0;
        else
                return;

        if (type == CL_ARROW_NORMAL)
        {
                _gdk_pen_translate (cr, x, y);
                _gdk_pen_rotate (cr, -rotate);
                clarus_draw_normal_arrow (cr, color, 0, 0, width, height);
        }
        else if (type == CL_ARROW_COMBO)
        {
                _gdk_pen_translate (cr, x, y);
                clarus_draw_combo_arrow (cr, color, 0, 0, width, height);
        }
}

static void
clarus_draw_arrow (GdkPen *cr,
                   const ClarusColors     *colors,
                   const WidgetParameters *widget,
                   const ArrowParameters  *arrow,
                   int x, int y, int width, int height)
{
        const CairoColor *color = &colors->fg[widget->state_type];
        gdouble tx, ty;

        tx = x + width/2.0;
        ty = y + height/2.0;

        if (widget->disabled)
        {
                _clarus_draw_arrow (cr, &colors->shade[0],
                                    arrow->direction, arrow->type,
                                    tx+0.5, ty+0.5, width, height);
        }

        _gdk_pen_identity_matrix (cr);

        _clarus_draw_arrow (cr, color, arrow->direction, arrow->type,
                            tx, ty, width, height);
}

void
clarus_draw_focus (GdkPen *cr,
                   const ClarusColors     *colors,
                   const WidgetParameters *widget,
                   const FocusParameters  *focus,
                   int x, int y, int width, int height)
{
        if (focus->has_color)
                ge_cairo_set_color (cr, &focus->color);
        else if (focus->type == CL_FOCUS_COLOR_WHEEL_LIGHT)
                _gdk_pen_set_rgb (cr, 0., 0., 0.);
        else if (focus->type == CL_FOCUS_COLOR_WHEEL_DARK)
                _gdk_pen_set_rgb (cr, 1., 1., 1.);
        else
                _gdk_pen_set_rgba (cr,
                                   colors->fg[widget->state_type].r,
                                   colors->fg[widget->state_type].g,
                                   colors->fg[widget->state_type].b,
                                   0.7);

        _gdk_pen_set_line_width (cr, focus->line_width);

        if (focus->dash_list[0])
        {
                gint n_dashes = strlen ((gchar *)focus->dash_list);
                gdouble *dashes = g_new (gdouble, n_dashes);
                gdouble total_length = 0;
                gdouble dash_offset;
                gint i;

                for (i = 0; i < n_dashes; i++)
                {
                        dashes[i] = focus->dash_list[i];
                        total_length += focus->dash_list[i];
                }

                dash_offset = -focus->line_width / 2.0;
                while (dash_offset < 0)
                        dash_offset += total_length;

                _gdk_pen_set_dash (cr, dashes, n_dashes, dash_offset);
                g_free (dashes);
        }

        _gdk_pen_rectangle (cr,
                            x + focus->line_width / 2.0,
                            y + focus->line_width / 2.0,
                            width - focus->line_width, height - focus->line_width);
        _gdk_pen_stroke (cr);
}

void
clarus_set_mixed_color (GdkPen           *cr,
                        const CairoColor *color1,
                        const CairoColor *color2,
                        gdouble mix_factor)
{
        CairoColor composite;

        ge_mix_color (color1, color2, mix_factor, &composite);
        ge_cairo_set_color (cr, &composite);
}

void
clarus_register_style_classic (ClarusStyleFunctions *functions, ClarusStyleConstants *constants)
{
        g_assert (functions);

        functions->draw_top_left_highlight  = clarus_draw_top_left_highlight;
        functions->draw_button              = clarus_draw_button;
        functions->draw_scale_trough        = clarus_draw_scale_trough;
        functions->draw_progressbar_trough  = clarus_draw_progressbar_trough;
        functions->draw_progressbar_fill    = clarus_draw_progressbar_fill;
        functions->draw_slider_button       = clarus_draw_slider_button;
        functions->draw_entry               = clarus_draw_entry;
        functions->draw_spinbutton          = clarus_draw_spinbutton;
        functions->draw_spinbutton_down     = clarus_draw_spinbutton_down;
        functions->draw_optionmenu          = clarus_draw_optionmenu;
        functions->draw_inset               = clarus_draw_inset;
        functions->draw_menubar             = clarus_draw_menubar;
        functions->draw_tab                 = clarus_draw_tab;
        functions->draw_frame               = clarus_draw_frame;
        functions->draw_separator           = clarus_draw_separator;
        functions->draw_menu_item_separator = clarus_draw_menu_item_separator;
        functions->draw_list_view_header    = clarus_draw_list_view_header;
        functions->draw_toolbar             = clarus_draw_toolbar;
        functions->draw_menuitem            = clarus_draw_menuitem;
        functions->draw_menubaritem         = clarus_draw_menubaritem;
        functions->draw_scrollbar_stepper   = clarus_draw_scrollbar_stepper;
        functions->draw_scrollbar_slider    = clarus_draw_scrollbar_slider;
        functions->draw_scrollbar_trough    = clarus_draw_scrollbar_trough;
        functions->draw_statusbar           = clarus_draw_statusbar;
        functions->draw_menu_frame          = clarus_draw_menu_frame;
        functions->draw_tooltip             = clarus_draw_tooltip;
        functions->draw_handle              = clarus_draw_handle;
        functions->draw_arrow               = clarus_draw_arrow;
        functions->draw_focus               = clarus_draw_focus;
        functions->draw_checkbox            = clarus_draw_checkbox;
        functions->draw_radiobutton         = clarus_draw_radiobutton;
        functions->draw_shadow              = clarus_draw_shadow;
        functions->draw_slider              = clarus_draw_slider;
        functions->draw_gripdots            = clarus_draw_gripdots;

        constants->topleft_highlight_shade  = 1.3;
        constants->topleft_highlight_alpha  = 0.6;
}
