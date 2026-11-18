/* GTK - The GIMP Toolkit
 * Clarus: drawing for its gummy style
 *
 * Ported from the Clearlooks engine of gtk-engines 2.20.2:
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
#define SHADE_TOP 1.08
#define SHADE_CENTER_TOP 1.02
#define SHADE_BOTTOM 0.94

/* Listview */
#define LISTVIEW_SHADE_TOP 1.06
#define LISTVIEW_SHADE_CENTER_TOP 1.02
#define LISTVIEW_SHADE_BOTTOM 0.96

/* Toolbar */
#define TOOLBAR_SHADE_TOP 1.04
#define TOOLBAR_SHADE_CENTER_TOP 1.01
#define TOOLBAR_SHADE_BOTTOM 0.97

static void
clarus_draw_gummy_gradient (GdkPen           *cr,
                            double x, double y, int width, int height,
                            const CairoColor *color,
                            gboolean disabled, gboolean radius, CairoCorners corners)
{
        CairoColor fill;
        CairoColor shade1, shade2, shade3;
        GdkPenPattern *pt;

        ge_shade_color (color, disabled? 1.04 : SHADE_TOP, &shade1);
        ge_shade_color (color, disabled? 1.01 : SHADE_CENTER_TOP, &shade2);
        ge_shade_color (color, disabled? 0.99 : 1.0, &fill);
        ge_shade_color (color, disabled? 0.96 : SHADE_BOTTOM, &shade3);

        pt = _gdk_pen_pattern_new_linear (x, y, x, y+height);
        _gdk_pen_pattern_add_stop_rgb (pt, 0.0, shade1.r, shade1.g, shade1.b);
        _gdk_pen_pattern_add_stop_rgb (pt, 0.5, shade2.r, shade2.g, shade2.b);
        _gdk_pen_pattern_add_stop_rgb (pt, 0.5, fill.r, fill.g, fill.b);
        _gdk_pen_pattern_add_stop_rgb (pt, 1.0, shade3.r, shade3.g, shade3.b);

        _gdk_pen_set_pattern (cr, pt);
        ge_cairo_rounded_rectangle (cr, x, y, width, height, radius, corners);
        _gdk_pen_fill (cr);

        _gdk_pen_pattern_unref (pt);
}

static void
clarus_gummy_draw_highlight_and_shade (GdkPen                 *cr,
                                       const CairoColor       *bg_color,
                                       const ShadowParameters *params,
                                       int width, int height, gdouble radius)
{
        CairoColor shadow;
        CairoColor highlight;
        uint8 corners = params->corners;
        double x = 1.0;
        double y = 1.0;

        /* not really sure of shading ratios... we will think */
        ge_shade_color (bg_color, 0.8, &shadow);
        ge_shade_color (bg_color, 1.2, &highlight);

        _gdk_pen_save (cr);

        /* Top/Left highlight */
        if (corners & CR_CORNER_BOTTOMLEFT)
                _gdk_pen_move_to (cr, x, y+height-radius);
        else
                _gdk_pen_move_to (cr, x, y+height);

        ge_cairo_rounded_corner (cr, x, y, radius, corners & CR_CORNER_TOPLEFT);

        if (corners & CR_CORNER_TOPRIGHT)
                _gdk_pen_line_to (cr, x+width-radius, y);
        else
                _gdk_pen_line_to (cr, x+width, y);

        if (params->shadow & CL_SHADOW_OUT)
                _gdk_pen_set_rgba (cr, highlight.r, highlight.g, highlight.b, 0.5);
        else
                _gdk_pen_set_rgba (cr, shadow.r, shadow.g, shadow.b, 0.5);

        _gdk_pen_stroke (cr);

        /* Bottom/Right highlight -- this includes the corners */
        _gdk_pen_move_to (cr, x+width-radius, y); /* topright and by radius to the left */
        ge_cairo_rounded_corner (cr, x+width, y, radius, corners & CR_CORNER_TOPRIGHT);
        ge_cairo_rounded_corner (cr, x+width, y+height, radius, corners & CR_CORNER_BOTTOMRIGHT);
        ge_cairo_rounded_corner (cr, x, y+height, radius, corners & CR_CORNER_BOTTOMLEFT);

        if (params->shadow & CL_SHADOW_OUT)
                _gdk_pen_set_rgba (cr, shadow.r, shadow.g, shadow.b, 0.5);
        else
                _gdk_pen_set_rgba (cr, highlight.r, highlight.g, highlight.b, 0.5);

        _gdk_pen_stroke (cr);

        _gdk_pen_restore (cr);
}

static void
clarus_gummy_draw_button (GdkPen                 *cr,
                          const ClarusColors     *colors,
                          const WidgetParameters *params,
                          int x, int y, int width, int height)
{
        double xoffset = 0, yoffset = 0;
        CairoColor fill            = colors->bg[params->state_type];
        CairoColor border_normal   = colors->shade[6];
        CairoColor border_disabled = colors->shade[4];
        double radius;

        GdkPenPattern *pattern;

        _gdk_pen_save (cr);
        _gdk_pen_translate (cr, x, y);
        _gdk_pen_set_line_width (cr, 1.0);

        if (params->xthickness >= 3)
        {
                xoffset = 1;
                yoffset = 1;
        }

        radius = MIN (params->radius, MIN ((width - 2.0 - 2*xoffset) / 2.0, (height - 2.0 - 2*yoffset) / 2.0));

        if (params->xthickness >= 3 || params->ythickness >= 3)
        {
                if (params->enable_shadow && !params->active && !params->disabled && !params->is_default)
                {
                        CairoColor shadow;

                        radius = MIN (params->radius, MIN ((width - 2.0 - 2*xoffset) / 2.0 - 1.0, (height - 2.0 - 2*yoffset) / 2.0 - 1.0));

                        ge_cairo_inner_rounded_rectangle (cr, 0, 0, width, height, radius+1, params->corners);
                        ge_shade_color (&params->parentbg, 0.97, &shadow);
                        ge_cairo_set_color (cr, &shadow);
                        _gdk_pen_stroke (cr);

                        ge_cairo_inner_rounded_rectangle (cr, 1, 1, width-1, height-1, radius+1, params->corners);
                        ge_shade_color (&params->parentbg, 0.93, &shadow);
                        ge_cairo_set_color (cr, &shadow);
                        _gdk_pen_stroke (cr);
                }

                if (params->is_default && !params->disabled)
                {
                        CairoColor shadow = colors->spot[1];

                        radius = MIN (params->radius, MIN ((width - 2.0 - 2*xoffset) / 2.0 - 1.0, (height - 2.0 - 2*yoffset) / 2.0 - 1.0));

                        ge_cairo_inner_rounded_rectangle (cr, 0, 0, width, height, radius+1, params->corners);
                        clarus_set_mixed_color (cr, &params->parentbg, &shadow, 0.5);
                        _gdk_pen_stroke (cr);
                }

                if (!(params->enable_shadow && !params->active && !params->disabled))
                        params->style_functions->draw_inset (cr, &params->parentbg, 0, 0, width, height, params->radius+1, params->corners);
        }

        clarus_draw_gummy_gradient (cr, xoffset+1, yoffset+1,
                                    width-(xoffset*2)-2, height-(yoffset*2)-2,
                                    &fill, params->disabled, radius, params->corners);

        /* Pressed button shadow */
        if (params->active)
        {
                CairoColor shadow;
                ge_shade_color (&fill, 0.92, &shadow);

                _gdk_pen_save (cr);

                ge_cairo_rounded_rectangle (cr, xoffset+1, yoffset+1, width-(xoffset*2)-2, height, radius,
                                            params->corners & (CR_CORNER_TOPLEFT | CR_CORNER_TOPRIGHT | CR_CORNER_BOTTOMLEFT));
                _gdk_pen_clip (cr);
                _gdk_pen_rectangle (cr, xoffset+1, yoffset+1, width-(xoffset*2)-2, 3);

                pattern = _gdk_pen_pattern_new_linear (xoffset+1, yoffset+1, xoffset+1, yoffset+4);
                _gdk_pen_pattern_add_stop_rgba (pattern, 0.0, shadow.r, shadow.g, shadow.b, 0.58);
                _gdk_pen_pattern_add_stop_rgba (pattern, 1.0, shadow.r, shadow.g, shadow.b, 0.0);
                _gdk_pen_set_pattern (cr, pattern);
                _gdk_pen_fill (cr);
                _gdk_pen_pattern_unref (pattern);

                _gdk_pen_rectangle (cr, xoffset+1, yoffset+1, 3, height-(yoffset*2)-2);

                pattern = _gdk_pen_pattern_new_linear (xoffset+1, yoffset+1, xoffset+4, yoffset+1);
                _gdk_pen_pattern_add_stop_rgba (pattern, 0.0, shadow.r, shadow.g, shadow.b, 0.58);
                _gdk_pen_pattern_add_stop_rgba (pattern, 1.0, shadow.r, shadow.g, shadow.b, 0.0);
                _gdk_pen_set_pattern (cr, pattern);
                _gdk_pen_fill (cr);
                _gdk_pen_pattern_unref (pattern);

                _gdk_pen_restore (cr);
        }

        /* Border */
        if (params->is_default) /* || (params->prelight && params->enable_shadow)) */
                border_normal = colors->spot[2];
        if (params->disabled)
                ge_cairo_set_color (cr, &border_disabled);
        else
                clarus_set_mixed_color (cr, &border_normal, &fill, 0.2);
        ge_cairo_rounded_rectangle (cr, xoffset + 0.5, yoffset + 0.5,
                                    width-(xoffset*2)-1, height-(yoffset*2)-1,
                                    radius, params->corners);
        _gdk_pen_stroke (cr);

        if (!params->active)
        {
                params->style_functions->draw_top_left_highlight (cr, &fill, params, 1+xoffset, 1+xoffset,
                                                                  width-(1+xoffset)*2, height-(1+xoffset)*2,
                                                                  radius, params->corners);
        }
        _gdk_pen_restore (cr);
}

static void
clarus_gummy_draw_entry (GdkPen                 *cr,
                         const ClarusColors     *colors,
                         const WidgetParameters *params,
                         const FocusParameters  *focus,
                         int x, int y, int width, int height)
{
        const CairoColor *base = &colors->base[params->state_type];
        CairoColor border = colors->shade[params->disabled ? 4 : 6];
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
        ge_cairo_rounded_rectangle (cr, xoffset + 1, yoffset + 1,
                                    width-2*(xoffset + 1), height-2*(yoffset + 1),
                                    MAX(0, radius-1), params->corners);
        ge_cairo_set_color (cr, base);
        _gdk_pen_fill (cr);

        /* Draw the inner shadow */
        if (params->focus)
        {
                CairoColor focus_shadow;
                ge_shade_color (&border, 1.61, &focus_shadow);

                clarus_set_mixed_color (cr, base, &focus_shadow, 0.5);
                ge_cairo_inner_rounded_rectangle (cr, xoffset + 1, yoffset + 1,
                                                  width-2*(xoffset + 1), height-2*(yoffset + 1),
                                                  MAX(0, radius-1), params->corners);
                _gdk_pen_stroke (cr);
        }
        else
        {
                CairoColor shadow;
                ge_shade_color (&border, 0.92, &shadow);

                _gdk_pen_set_rgba (cr, shadow.r, shadow.g, shadow.b, params->disabled ? 0.09 : 0.18);

                _gdk_pen_set_line_cap (cr, GDK_CAP_BUTT);
                _gdk_pen_move_to (cr, xoffset + 1.5, height-radius);
                _gdk_pen_arc (cr, xoffset + 1.5 + MAX(0, radius-1), yoffset + 1.5 + MAX(0, radius-1),
                              MAX(0, radius-1), G_PI, 270*(G_PI/180));
                _gdk_pen_line_to (cr, width-radius, yoffset + 1.5);
                _gdk_pen_stroke (cr);
        }

        ge_cairo_inner_rounded_rectangle (cr, xoffset, yoffset, width-2*xoffset, height-2*yoffset, radius, params->corners);
        ge_cairo_set_color (cr, &border);
        _gdk_pen_stroke (cr);

        _gdk_pen_restore (cr);
}

static void
clarus_gummy_draw_progressbar_trough (GdkPen                 *cr,
                                      const ClarusColors     *colors,
                                      const WidgetParameters *params,
                                      int x, int y, int width, int height)
{
        const CairoColor *border = &colors->shade[7];
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
        clarus_set_mixed_color (cr, border, &colors->shade[2], 0.3);
        _gdk_pen_stroke (cr);

        /* clip the corners of the shadows */
        ge_cairo_rounded_rectangle (cr, x+1, y+1, width-2, height-2, radius, params->corners);
        _gdk_pen_clip (cr);

        ge_shade_color (border, 0.92, &shadow);

        /* Top shadow */
        _gdk_pen_rectangle (cr, x+1, y+1, width-2, 4);
        pattern = _gdk_pen_pattern_new_linear (x, y, x, y+4);
        _gdk_pen_pattern_add_stop_rgba (pattern, 0.0, shadow.r, shadow.g, shadow.b, 0.3);
        _gdk_pen_pattern_add_stop_rgba (pattern, 1.0, shadow.r, shadow.g, shadow.b, 0.);
        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_fill (cr);
        _gdk_pen_pattern_unref (pattern);

        /* Left shadow */
        _gdk_pen_rectangle (cr, x+1, y+1, 4, height-2);
        pattern = _gdk_pen_pattern_new_linear (x, y, x+4, y);
        _gdk_pen_pattern_add_stop_rgba (pattern, 0.0, shadow.r, shadow.g, shadow.b, 0.3);
        _gdk_pen_pattern_add_stop_rgba (pattern, 1.0, shadow.r, shadow.g, shadow.b, 0.);
        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_fill (cr);
        _gdk_pen_pattern_unref (pattern);

        _gdk_pen_restore (cr);
}

static void
clarus_gummy_draw_progressbar_fill (GdkPen                      *cr,
                                    const ClarusColors          *colors,
                                    const WidgetParameters      *params,
                                    const ProgressBarParameters *progressbar,
                                    int x, int y, int width, int height, gint offset)
{
        boolean is_horizontal = progressbar->orientation < 2;
        double  tile_pos = 0;
        double  stroke_width;
        double  radius;
        int     x_step;

        GdkPenPattern *pattern;
        CairoColor       shade1, shade2, shade3;
        CairoColor       border;
        CairoColor       shadow;

        radius = MAX (0, params->radius - params->xthickness);

        _gdk_pen_save (cr);

        if (!is_horizontal)
                ge_cairo_exchange_axis (cr, &x, &y, &width, &height);

        if ((progressbar->orientation == CL_ORIENTATION_RIGHT_TO_LEFT) || (progressbar->orientation == CL_ORIENTATION_BOTTOM_TO_TOP))
                ge_cairo_mirror (cr, CR_MIRROR_HORIZONTAL, &x, &y, &width, &height);

        /* Clamp the radius so that the _height_ fits ... */
        radius = MIN (radius, height / 2.0);

        stroke_width = height*2;
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
        ge_shade_color (&colors->spot[1], SHADE_TOP, &shade1);
        ge_shade_color (&colors->spot[1], SHADE_CENTER_TOP, &shade2);
        ge_shade_color (&colors->spot[1], SHADE_BOTTOM, &shade3);
        pattern = _gdk_pen_pattern_new_linear (0, 0, 0, height);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.0, shade1.r, shade1.g, shade1.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5, shade2.r, shade2.g, shade2.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5, colors->spot[1].r, colors->spot[1].g, colors->spot[1].b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 1.0, shade3.r, shade3.g, shade3.b);
        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_paint (cr);
        _gdk_pen_pattern_unref (pattern);

        /* Draw the strokes */
        while (stroke_width > 0 && tile_pos <= width+x_step)
        {
                _gdk_pen_move_to (cr, stroke_width/2-x_step, 0);
                _gdk_pen_line_to (cr, stroke_width-x_step,   0);
                _gdk_pen_line_to (cr, stroke_width/2-x_step, height);
                _gdk_pen_line_to (cr, -x_step, height);

                _gdk_pen_translate (cr, stroke_width, 0);
                tile_pos += stroke_width;
        }

        _gdk_pen_set_rgba (cr, colors->spot[2].r,
                               colors->spot[2].g,
                               colors->spot[2].b,
                               0.15);

        _gdk_pen_fill (cr);
        _gdk_pen_restore (cr); /* rounded clip region */

        /* inner highlight border
         * This is again kinda ugly. Draw once from each side, clipping away the other. */
        _gdk_pen_set_rgba (cr, colors->spot[0].r, colors->spot[0].g, colors->spot[0].b, 0.2);

        /* left side */
        _gdk_pen_save (cr);
        _gdk_pen_rectangle (cr, 0, 0, width / 2, height);
        _gdk_pen_clip (cr);

        if (progressbar->pulsing)
                ge_cairo_rounded_rectangle (cr, 1.5, 0.5, width + radius, height - 1, radius, CR_CORNER_TOPLEFT | CR_CORNER_BOTTOMLEFT);
        else
                ge_cairo_rounded_rectangle (cr, 0.5, 0.5, width + radius, height - 1, radius, CR_CORNER_TOPLEFT | CR_CORNER_BOTTOMLEFT);

        _gdk_pen_stroke (cr);
        _gdk_pen_restore (cr); /* clip */

        /* right side */
        _gdk_pen_save (cr);
        _gdk_pen_rectangle (cr, width / 2, 0, (width+1) / 2, height);
        _gdk_pen_clip (cr);

        if (progressbar->value < 1.0 || progressbar->pulsing)
                ge_cairo_rounded_rectangle (cr, -1.5 - radius, 0.5, width + radius, height - 1, radius, CR_CORNER_TOPRIGHT | CR_CORNER_BOTTOMRIGHT);
        else
                ge_cairo_rounded_rectangle (cr, -0.5 - radius, 0.5, width + radius, height - 1, radius, CR_CORNER_TOPRIGHT | CR_CORNER_BOTTOMRIGHT);

        _gdk_pen_stroke (cr);
        _gdk_pen_restore (cr); /* clip */


        /* Draw the dark lines and the shadow */
        _gdk_pen_save (cr);
        /* Again, this weird clip area. */
        ge_cairo_rounded_rectangle (cr, -1.0, 0, width + radius + 2.0, height, radius, CR_CORNER_TOPLEFT | CR_CORNER_BOTTOMLEFT);
        _gdk_pen_clip (cr);
        ge_cairo_rounded_rectangle (cr, -radius - 1.0, 0, width + radius + 2.0, height, radius, CR_CORNER_TOPRIGHT | CR_CORNER_BOTTOMRIGHT);
        _gdk_pen_clip (cr);

        border = colors->spot[2];
        border.a = 0.6;
        ge_shade_color (&colors->shade[7], 0.92, &shadow);
        shadow.a = 0.2;

        if (progressbar->pulsing)
        {
                /* At the beginning of the bar. */
                _gdk_pen_move_to (cr, 0.5 + radius, height + 0.5);
                ge_cairo_rounded_corner (cr, 0.5, height + 0.5, radius + 1, CR_CORNER_BOTTOMLEFT);
                ge_cairo_rounded_corner (cr, 0.5, -0.5, radius + 1, CR_CORNER_TOPLEFT);
                ge_cairo_set_color (cr, &border);
                _gdk_pen_stroke (cr);

                _gdk_pen_move_to (cr, -0.5 + radius, height + 0.5);
                ge_cairo_rounded_corner (cr, -0.5, height + 0.5, radius + 1, CR_CORNER_BOTTOMLEFT);
                ge_cairo_rounded_corner (cr, -0.5, -0.5, radius + 1, CR_CORNER_TOPLEFT);
                ge_cairo_set_color (cr, &shadow);
                _gdk_pen_stroke (cr);
        }
        if (progressbar->value < 1.0 || progressbar->pulsing)
        {
                /* At the end of the bar. */
                _gdk_pen_move_to (cr, width - 0.5 - radius, -0.5);
                ge_cairo_rounded_corner (cr, width - 0.5, -0.5, radius + 1, CR_CORNER_TOPRIGHT);
                ge_cairo_rounded_corner (cr, width - 0.5, height + 0.5, radius + 1, CR_CORNER_BOTTOMRIGHT);
                ge_cairo_set_color (cr, &border);
                _gdk_pen_stroke (cr);

                _gdk_pen_move_to (cr, width + 0.5 - radius, -0.5);
                ge_cairo_rounded_corner (cr, width + 0.5, -0.5, radius + 1, CR_CORNER_TOPRIGHT);
                ge_cairo_rounded_corner (cr, width + 0.5, height + 0.5, radius + 1, CR_CORNER_BOTTOMRIGHT);
                ge_cairo_set_color (cr, &shadow);
                _gdk_pen_stroke (cr);
        }

        _gdk_pen_restore (cr);

        _gdk_pen_restore (cr); /* rotation, mirroring */
}

static void
clarus_gummy_scale_draw_gradient (GdkPen           *cr,
                                  const CairoColor *fill,
                                  const CairoColor *border,
                                  int x, int y, int width, int height,
                                  gboolean horizontal, gboolean in)
{
        GdkPenPattern *pattern;

        CairoColor f1, f2;

        ge_shade_color (fill, in? 0.95 : 1.1, &f1);
        ge_shade_color (fill, in? 1.05 : 0.9, &f2);

        pattern = _gdk_pen_pattern_new_linear (0.5, 0.5, horizontal ? 0.5 :  width + 1.0, horizontal ? height + 1.0 : 0.5);
        _gdk_pen_pattern_add_stop_rgba (pattern, 0.0, f1.r, f1.g, f1.b, f1.a);
        _gdk_pen_pattern_add_stop_rgba (pattern, 1.0, f2.r, f2.g, f2.b, f2.a);

        _gdk_pen_rectangle (cr, x, y, width, height);
        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_fill (cr);
        _gdk_pen_pattern_unref (pattern);

        clarus_set_mixed_color (cr, border, fill, 0.2);
        ge_cairo_inner_rectangle (cr, x, y, width, height);
        _gdk_pen_stroke (cr);
}

#define TROUGH_SIZE 7
static void
clarus_gummy_draw_scale_trough (GdkPen                 *cr,
                                const ClarusColors     *colors,
                                const WidgetParameters *params,
                                const SliderParameters *slider,
                                int x, int y, int width, int height)
{
        int    trough_width, trough_height;
        double translate_x, translate_y;
        CairoColor fill, border;
        gboolean in;

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
                translate_y   = y;
        }

        _gdk_pen_set_line_width (cr, 1.0);
        _gdk_pen_translate (cr, translate_x, translate_y);

        if (!slider->fill_level)
                params->style_functions->draw_inset (cr, &params->parentbg, 0, 0, trough_width, trough_height, 0, 0);

        if (!slider->lower && !slider->fill_level)
        {
                ge_shade_color (&params->parentbg, 0.896, &fill);
                border = colors->shade[6];
                in = TRUE;
        }
        else if (!slider->fill_level)
        {
                fill = colors->spot[1];
                border = colors->spot[2];
                in = FALSE;
        }
        else
        {
                fill = colors->spot[1];
                border = colors->spot[2];

                fill.a = 0.25;
                border.a = 0.25;

                in = FALSE;
        }

        clarus_gummy_scale_draw_gradient (cr,
                                          &fill,
                                          &border,
                                          1, 1, trough_width - 2, trough_height - 2,
                                          slider->horizontal, in);

        _gdk_pen_restore (cr);
}

static void
clarus_gummy_draw_tab (GdkPen                 *cr,
                       const ClarusColors     *colors,
                       const WidgetParameters *params,
                       const TabParameters    *tab,
                       int x, int y, int width, int height)
{
        const CairoColor *border        = &colors->shade[5];
        const CairoColor *stripe_fill   = &colors->spot[1];
        const CairoColor *stripe_border = &colors->spot[2];
        const CairoColor *fill;

        GdkPenPattern    *pattern = NULL;

        double            radius;
        double            stripe_size = 2.0;
        double            stripe_fill_size;
        double            stripe_border_pos;

        gboolean horizontal = FALSE;

        radius = MIN (params->radius, MIN ((width - 2.0) / 2.0, (height - 2.0) / 2.0));

        /* Set clip */
        _gdk_pen_rectangle      (cr, x, y, width, height);
        _gdk_pen_clip           (cr);
        _gdk_pen_new_path       (cr);

        /* Translate and set line width */
        _gdk_pen_set_line_width (cr, 1.0);
        _gdk_pen_translate      (cr, x+0.5, y+0.5);

        /* Make the tabs slightly bigger than they should be, to create a gap */
        /* And calculate the strip size too, while you're at it */
        if (tab->gap_side == CL_GAP_TOP || tab->gap_side == CL_GAP_BOTTOM)
        {
                if (params->ythickness == 3)
                        stripe_size = 3.0;

                height += 3.0;
                stripe_fill_size = (tab->gap_side == CL_GAP_TOP ? stripe_size/height : stripe_size/(height-2));
                stripe_border_pos = (tab->gap_side == CL_GAP_TOP ? (stripe_size+1.0)/height : (stripe_size+1.0)/(height-2));

                horizontal = TRUE;

                if (tab->gap_side == CL_GAP_TOP)
                        _gdk_pen_translate (cr, 0.0, -3.0); /* gap at the other side */
        }
        else
        {
                if (params->xthickness == 3)
                        stripe_size = 3.0;

                width += 3.0;
                stripe_fill_size = (tab->gap_side == CL_GAP_LEFT ? stripe_size/width : stripe_size/(width-2));
                stripe_border_pos = (tab->gap_side == CL_GAP_LEFT ? (stripe_size+1.0)/width : (stripe_size+1.0)/(width-2));

                if (tab->gap_side == CL_GAP_LEFT)
                        _gdk_pen_translate (cr, -3.0, 0.0); /* gap at the other side */
        }

        /* Set the fill color */
        fill = &colors->bg[params->state_type];

        /* Set tab shape */
        ge_cairo_rounded_rectangle (cr, 0, 0, width-1, height-1,
                                    radius, params->corners);

        /* Draw fill */
        ge_cairo_set_color (cr, fill);
        _gdk_pen_fill  (cr);

        /* Draw highlight */
        if (!params->active)
        {
                ShadowParameters shadow;

                shadow.shadow  = CL_SHADOW_OUT;
                shadow.corners = params->corners;

                clarus_gummy_draw_highlight_and_shade (cr, &colors->bg[0], &shadow,
                                                       width, height, radius);
        }

        if (params->active)
        {
                CairoColor hilight;
                CairoColor shade1, shade2, shade3;

                ge_shade_color (fill, 1.15, &hilight);
                ge_shade_color (fill, SHADE_TOP, &shade1);
                ge_shade_color (fill, SHADE_CENTER_TOP, &shade2);
                ge_shade_color (fill, SHADE_BOTTOM, &shade3);

                switch  (tab->gap_side)
                {
                        case CL_GAP_TOP:
                                pattern = _gdk_pen_pattern_new_linear (0, height-2, 0, 0);
                                break;
                        case CL_GAP_BOTTOM:
                                pattern = _gdk_pen_pattern_new_linear (0, 1, 0, height);
                                break;
                        case CL_GAP_LEFT:
                                pattern = _gdk_pen_pattern_new_linear (width-2, 0, 1, 0);
                                break;
                        case CL_GAP_RIGHT:
                                pattern = _gdk_pen_pattern_new_linear (1, 0, width-2, 0);
                                break;
                }

                ge_cairo_rounded_rectangle (cr, 0, 0, width-1, height-1, radius, params->corners);

                _gdk_pen_pattern_add_stop_rgb (pattern, 0.0, hilight.r, hilight.g, hilight.b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 1.0/(horizontal ? height : width), hilight.r, hilight.g, hilight.b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 1.0/(horizontal ? height : width), shade1.r, shade1.g, shade1.b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 0.45, shade2.r, shade2.g, shade2.b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 0.45, fill->r, fill->g, fill->b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 1.0, shade3.r, shade3.g, shade3.b);
                _gdk_pen_set_pattern (cr, pattern);
                _gdk_pen_fill (cr);
                _gdk_pen_pattern_unref (pattern);
        }
        else
        {
                CairoColor shade1;

                ge_shade_color (fill, SHADE_TOP, &shade1);

                switch  (tab->gap_side)
                {
                        case CL_GAP_TOP:
                                pattern = _gdk_pen_pattern_new_linear (0, height-2, 0, 0);
                                break;
                        case CL_GAP_BOTTOM:
                                pattern = _gdk_pen_pattern_new_linear (0, 0, 0, height);
                                break;
                        case CL_GAP_LEFT:
                                pattern = _gdk_pen_pattern_new_linear (width-2, 0, 0, 0);
                                break;
                        case CL_GAP_RIGHT:
                                pattern = _gdk_pen_pattern_new_linear (0, 0, width, 0);
                                break;
                }

                ge_cairo_rounded_rectangle (cr, 0, 0, width-1, height-1, radius, params->corners);

                _gdk_pen_pattern_add_stop_rgba (pattern, 0.0, stripe_fill->r, stripe_fill->g, stripe_fill->b, 0.6);
                /* _gdk_pen_pattern_add_stop_rgba (pattern, 1.0/(horizontal ? height : width), stripe_fill->r, stripe_fill->g, stripe_fill->b, 0.34);
                   _gdk_pen_pattern_add_stop_rgba (pattern, 1.0/(horizontal ? height : width), stripe_fill->r, stripe_fill->g, stripe_fill->b, 0.5); */
                _gdk_pen_pattern_add_stop_rgb  (pattern, stripe_fill_size, stripe_fill->r, stripe_fill->g, stripe_fill->b);
                _gdk_pen_pattern_add_stop_rgba (pattern, stripe_fill_size, stripe_border->r, stripe_border->g, stripe_border->b, 0.72);
                _gdk_pen_pattern_add_stop_rgba (pattern, stripe_border_pos, stripe_border->r, stripe_border->g, stripe_border->b, 0.72);
                _gdk_pen_pattern_add_stop_rgb  (pattern, stripe_border_pos, shade1.r, shade1.g, shade1.b);
                _gdk_pen_pattern_add_stop_rgba (pattern, 0.8, fill->r, fill->g, fill->b, 0.0);
                _gdk_pen_set_pattern (cr, pattern);
                _gdk_pen_fill (cr);
                _gdk_pen_pattern_unref (pattern);
        }

        ge_cairo_rounded_rectangle (cr, 0, 0, width-1, height-1, radius, params->corners);

        if (params->active)
        {
                ge_cairo_set_color (cr, border);
                _gdk_pen_stroke (cr);
        }
        else
        {
                switch  (tab->gap_side)
                {
                        case CL_GAP_TOP:
                                pattern = _gdk_pen_pattern_new_linear (2, height-2, 2, 2);
                                break;
                        case CL_GAP_BOTTOM:
                                pattern = _gdk_pen_pattern_new_linear (2, 2, 2, height);
                                break;
                        case CL_GAP_LEFT:
                                pattern = _gdk_pen_pattern_new_linear (width-2, 2, 2, 2);
                                break;
                        case CL_GAP_RIGHT:
                                pattern = _gdk_pen_pattern_new_linear (2, 2, width, 2);
                                break;
                }

                _gdk_pen_pattern_add_stop_rgb (pattern, 0.0, stripe_border->r, stripe_border->g, stripe_border->b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 0.8, border->r,        border->g,        border->b);
                _gdk_pen_set_pattern (cr, pattern);
                _gdk_pen_stroke (cr);
                _gdk_pen_pattern_unref (pattern);
        }

        /* In current GTK+ focus and active cannot happen together, but we are robust against it. */
        if (params->focus && !params->active)
        {
                CairoColor focus_fill = tab->focus.color;
                CairoColor fill_shade1, fill_shade2, fill_shade3;
                CairoColor focus_border;

                double focus_inset_x = ((tab->gap_side == CL_GAP_TOP || tab->gap_side == CL_GAP_BOTTOM) ? 4 : stripe_size + 3);
                double focus_inset_y = ((tab->gap_side == CL_GAP_TOP || tab->gap_side == CL_GAP_BOTTOM) ? stripe_size + 3 : 4);
                double border_alpha = 0.54;
                double fill_alpha = 0.17;

                ge_shade_color (&focus_fill, 0.65, &focus_border);
                ge_shade_color (&focus_fill, 1.18, &fill_shade1);
                ge_shade_color (&focus_fill, 1.02, &fill_shade2);
                ge_shade_color (&focus_fill, 0.84, &fill_shade3);

                ge_cairo_rounded_rectangle (cr, focus_inset_x, focus_inset_y, width-focus_inset_x*2-1, height-focus_inset_y*2-1, radius-1, CR_CORNER_ALL);
                pattern = _gdk_pen_pattern_new_linear (0, 0, 0, height);

                _gdk_pen_pattern_add_stop_rgba (pattern, 0.0, fill_shade1.r, fill_shade1.g, fill_shade1.b, fill_alpha);
                _gdk_pen_pattern_add_stop_rgba (pattern, 0.5, fill_shade2.r, fill_shade2.g, fill_shade2.b, fill_alpha);
                _gdk_pen_pattern_add_stop_rgba (pattern, 0.5, focus_fill.r,   focus_fill.g, focus_fill.b,  fill_alpha);
                _gdk_pen_pattern_add_stop_rgba (pattern, 1.0, fill_shade3.r, fill_shade3.g, fill_shade3.b, fill_alpha);
                _gdk_pen_set_pattern (cr, pattern);
                _gdk_pen_fill_preserve (cr);

                _gdk_pen_pattern_unref (pattern);

                clarus_set_mixed_color (cr, &params->parentbg, &focus_border, border_alpha);
                _gdk_pen_stroke (cr);
        }
}

static void
clarus_gummy_draw_separator (GdkPen                    *cr,
                             const ClarusColors        *colors,
                             const WidgetParameters    *widget,
                             const SeparatorParameters *separator,
                             int x, int y, int width, int height)
{
        CairoColor color = colors->shade[3];
        CairoColor hilight;
        ge_shade_color (&color, 1.3, &hilight);

        _gdk_pen_save (cr);
        _gdk_pen_set_line_cap (cr, GDK_CAP_BUTT);

        if (separator->horizontal)
        {
                _gdk_pen_set_line_width (cr, 1.0);
                _gdk_pen_translate      (cr, x, y+0.5);

                _gdk_pen_move_to        (cr, 0.0,   0.0);
                _gdk_pen_line_to        (cr, width, 0.0);
                ge_cairo_set_color   (cr, &color);
                _gdk_pen_stroke         (cr);

                _gdk_pen_move_to        (cr, 0.0,   1.0);
                _gdk_pen_line_to        (cr, width, 1.0);
                ge_cairo_set_color   (cr, &hilight);
                _gdk_pen_stroke         (cr);
        }
        else
        {
                _gdk_pen_set_line_width (cr, 1.0);
                _gdk_pen_translate      (cr, x+0.5, y);

                _gdk_pen_move_to        (cr, 0.0, 0.0);
                _gdk_pen_line_to        (cr, 0.0, height);
                ge_cairo_set_color   (cr, &color);
                _gdk_pen_stroke         (cr);

                _gdk_pen_move_to        (cr, 1.0, 0.0);
                _gdk_pen_line_to        (cr, 1.0, height);
                ge_cairo_set_color   (cr, &hilight);
                _gdk_pen_stroke         (cr);
        }

        _gdk_pen_restore (cr);
}

static void
clarus_gummy_draw_slider (GdkPen                 *cr,
                          const ClarusColors     *colors,
                          const WidgetParameters *params,
                          int x, int y, int width, int height)
{
        const CairoColor *border = &colors->shade[7];
        CairoColor fill;
        CairoColor shade1, shade2, shade3;
        GdkPenPattern *pattern;
        int bar_x, i;
        int shift_x;

        _gdk_pen_set_line_width (cr, 1.0);
        _gdk_pen_translate      (cr, x, y);

        ge_shade_color (&colors->bg[params->state_type], 1.0, &fill);
        if (params->prelight)
                ge_shade_color (&fill, 1.04, &fill);

        ge_shade_color (&fill, SHADE_TOP, &shade1);
        ge_shade_color (&fill, SHADE_CENTER_TOP, &shade2);
        ge_shade_color (&fill, SHADE_BOTTOM, &shade3);

        pattern = _gdk_pen_pattern_new_linear (1, 1, 1, height-2);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0,   shade1.r, shade1.g, shade1.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5, shade2.r, shade2.g, shade2.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5, fill.r, fill.g, fill.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 1.0, shade3.r, shade3.g, shade3.b);
        _gdk_pen_rectangle (cr, 1, 1, width-2, height-2);
        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_fill (cr);
        _gdk_pen_pattern_unref (pattern);

        clarus_set_mixed_color (cr, border, &fill, 0.2);
        if (params->prelight)
                ge_cairo_set_color (cr, &colors->spot[2]);
        ge_cairo_rounded_rectangle (cr, 0.5, 0.5, width-1, height-1, 2.5, params->corners);
        _gdk_pen_stroke (cr);

        /* Handle */
        shift_x = (width%2 == 0 ? 1 : 0);
        bar_x = width/2-3+shift_x;
        _gdk_pen_translate (cr, 0.5, 0.5);
        ge_cairo_set_color (cr, border);
        for (i=0; i<3-shift_x; i++)
        {
                _gdk_pen_move_to (cr, bar_x, 4);
                _gdk_pen_line_to (cr, bar_x, height-5);
                bar_x += 3;
        }
        _gdk_pen_stroke (cr);

        params->style_functions->draw_top_left_highlight (cr, &fill, params, 1, 1, width-2, height-2, 2.0, params->corners);
}

static void
clarus_gummy_draw_slider_button (GdkPen                 *cr,
                                 const ClarusColors     *colors,
                                 const WidgetParameters *params,
                                 const SliderParameters *slider,
                                 int x, int y, int width, int height)
{
        double radius = MIN (params->radius, MIN ((width - 1.0) / 2.0, (height - 1.0) / 2.0));

        _gdk_pen_set_line_width (cr, 1.0);

        if (!slider->horizontal)
                ge_cairo_exchange_axis (cr, &x, &y, &width, &height);

        _gdk_pen_translate (cr, x, y);

        params->style_functions->draw_shadow (cr, colors, radius, width, height);
        params->style_functions->draw_slider (cr, colors, params, 1, 1, width-2, height-2);
}

static void
clarus_gummy_draw_scrollbar_stepper (GdkPen                           *cr,
                                     const ClarusColors               *colors,
                                     const WidgetParameters           *widget,
                                     const ScrollBarParameters        *scrollbar,
                                     const ScrollBarStepperParameters *stepper,
                                     int x, int y, int width, int height)
{
        CairoCorners corners = CR_CORNER_NONE;
        const CairoColor *border = &colors->shade[scrollbar->has_color ? 7 : 6];
        CairoColor fill;
        CairoColor shade1, shade2, shade3;
        GdkPenPattern *pattern;
        double radius = MIN (widget->radius, MIN ((width - 2.0) / 2.0, (height - 2.0) / 2.0));

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

        fill = colors->bg[widget->state_type];
        ge_shade_color (&fill, SHADE_TOP, &shade1);
        ge_shade_color (&fill, SHADE_CENTER_TOP, &shade2);
        ge_shade_color (&fill, SHADE_BOTTOM, &shade3);

        _gdk_pen_pattern_add_stop_rgb (pattern, 0,   shade1.r, shade1.g, shade1.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5, shade2.r, shade2.g, shade2.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5, fill.r, fill.g, fill.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 1.0, shade3.r, shade3.g, shade3.b);
        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_fill (cr);
        _gdk_pen_pattern_unref (pattern);

        widget->style_functions->draw_top_left_highlight (cr, &fill, widget, 1, 1, width - 2, height - 2,
                                                          radius, corners);

        ge_cairo_rounded_rectangle (cr, 0.5, 0.5, width-1, height-1, radius, corners);
        clarus_set_mixed_color (cr, border, &fill, 0.2);
        _gdk_pen_stroke (cr);
}

static void
clarus_gummy_draw_scrollbar_slider (GdkPen                    *cr,
                                    const ClarusColors        *colors,
                                    const WidgetParameters    *widget,
                                    const ScrollBarParameters *scrollbar,
                                    int x, int y, int width, int height)
{
        CairoColor fill = scrollbar->color;
        CairoColor border, handles;
        CairoColor hilight;
        CairoColor shade1, shade2, shade3;
        GdkPenPattern *pattern;
        int bar_x, i;

        gdouble hue_scroll, brightness_scroll, saturation_scroll;
        gdouble hue_bg, brightness_bg, saturation_bg;

        ge_hsb_from_color (&fill, &hue_scroll, &saturation_scroll, &brightness_scroll);
        ge_hsb_from_color (&colors->bg[0], &hue_bg, &saturation_bg, &brightness_bg);

        /* Set the right color for border and handles */
        if ((fabs(saturation_scroll - saturation_bg) < 0.30) &&
            (fabs(brightness_scroll - brightness_bg) < 0.20))
                ge_shade_color (&fill, 0.475, &border);
        else
                ge_shade_color (&fill, 0.575, &border);
        /* The following lines increase contrast when the HUE is between 25 and 195, */
        /* fixing a LOT of colorschemes! */
        if (scrollbar->has_color && (hue_scroll < 195) && (hue_scroll > 25))
                ge_shade_color (&border, 0.85, &border);

        handles = border;
        ge_mix_color (&border, &fill, scrollbar->has_color? 0.3 : 0.2, &border);

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

        if (widget->prelight)
                ge_shade_color (&fill, 1.04, &fill);

        _gdk_pen_set_line_width (cr, 1);

        ge_shade_color (&fill, widget->style_constants->topleft_highlight_shade, &hilight);
        ge_shade_color (&fill, SHADE_TOP, &shade1);
        ge_shade_color (&fill, SHADE_CENTER_TOP, &shade2);
        ge_shade_color (&fill, SHADE_BOTTOM, &shade3);

        pattern = _gdk_pen_pattern_new_linear (1, 1, 1, height-2);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0,   shade1.r, shade1.g, shade1.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5, shade2.r, shade2.g, shade2.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5, fill.r,  fill.g,  fill.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 1,   shade3.r, shade3.g, shade3.b);
        _gdk_pen_rectangle (cr, 1, 1, width-2, height-2);
        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_fill (cr);
        _gdk_pen_pattern_unref (pattern);

        if (scrollbar->has_color)
        {
                _gdk_pen_set_rgba (cr, hilight.r, hilight.g, hilight.b, 0.2);
                ge_cairo_stroke_rectangle (cr, 1.5, 1.5, width-3, height-3);
        }
        else
        {
                _gdk_pen_move_to (cr, 1.5, height-1.5);
                _gdk_pen_line_to (cr, 1.5, 1.5);
                _gdk_pen_line_to (cr, width-1.5, 1.5);
                _gdk_pen_set_rgba (cr, hilight.r, hilight.g, hilight.b, widget->style_constants->topleft_highlight_alpha);
                _gdk_pen_stroke (cr);
        }

        ge_cairo_set_color (cr, &border);
        ge_cairo_stroke_rectangle (cr, 0.5, 0.5, width-1, height-1);

        /* Handle */
        bar_x = width/2 - 4;
        _gdk_pen_translate(cr, 0.5, 0.5);
        ge_cairo_set_color (cr, &handles);
        for (i=0; i<3; i++)
        {
                _gdk_pen_move_to (cr, bar_x, 5);
                _gdk_pen_line_to (cr, bar_x, height-6);
                bar_x += 3;
        }
        _gdk_pen_stroke (cr);
}

static void
clarus_gummy_draw_list_view_header (GdkPen                         *cr,
                                    const ClarusColors             *colors,
                                    const WidgetParameters         *params,
                                    const ListViewHeaderParameters *header,
                                    int x, int y, int width, int height)
{
/*
        CairoColor *border = !params->prelight? (CairoColor*)&colors->shade[4] : (CairoColor*)&colors->spot[1];
*/
        const CairoColor *border = &colors->shade[4];
        const CairoColor *fill   = &colors->bg[params->state_type];
        CairoColor hilight;
        CairoColor shade1, shade2, shade3;

        GdkPenPattern *pattern;

        ge_shade_color (fill, 1.11, &hilight);
        ge_shade_color (fill, LISTVIEW_SHADE_TOP, &shade1);
        ge_shade_color (fill, LISTVIEW_SHADE_CENTER_TOP, &shade2);
        ge_shade_color (fill, LISTVIEW_SHADE_BOTTOM, &shade3);

        _gdk_pen_translate (cr, x, y);
        _gdk_pen_set_line_width (cr, 1.0);

        /* Draw the fill */
        pattern = _gdk_pen_pattern_new_linear (0, 0, 0, height);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.0, shade1.r, shade1.g, shade1.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5, shade2.r, shade2.g, shade2.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5, fill->r, fill->g, fill->b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 1.0-1.0/height, shade3.r, shade3.g, shade3.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 1.0-1.0/height, border->r, border->g, border->b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 1.0, border->r, border->g, border->b);

        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_rectangle (cr, 0, 0, width, height);
        _gdk_pen_fill (cr);

        _gdk_pen_pattern_unref (pattern);

        /* Draw highlight */
        if (header->order & CL_ORDER_FIRST)
        {
                _gdk_pen_move_to (cr, 0.5, height-1.5);
                _gdk_pen_line_to (cr, 0.5, 0.5);
        }
        else
                _gdk_pen_move_to (cr, 0.0, 0.5);

        _gdk_pen_line_to (cr, width, 0.5);

        ge_cairo_set_color (cr, &hilight);
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

static void
clarus_gummy_draw_toolbar (GdkPen                  *cr,
                           const ClarusColors      *colors,
                           const WidgetParameters  *widget,
                           const ToolbarParameters *toolbar,
                           int x, int y, int width, int height)
{
        const CairoColor *fill = &colors->bg[GTK_STATE_NORMAL];
        const CairoColor *dark = &colors->shade[3];
        CairoColor light;
        ge_shade_color (fill, toolbar->style == 1 ? 1.1 : 1.05, &light);

        _gdk_pen_set_line_width (cr, 1.0);
        _gdk_pen_translate (cr, x, y);

        if (toolbar->style == 1) /* Enable Extra features */
        {
                GdkPenPattern *pattern;
                CairoColor shade1, shade2, shade3;

                ge_shade_color (fill, TOOLBAR_SHADE_TOP, &shade1);
                ge_shade_color (fill, TOOLBAR_SHADE_CENTER_TOP, &shade2);
                ge_shade_color (fill, TOOLBAR_SHADE_BOTTOM, &shade3);

                /* Draw the fill */
                pattern = _gdk_pen_pattern_new_linear (0, 0, 0, height);
                _gdk_pen_pattern_add_stop_rgb (pattern, 0.0, shade1.r, shade1.g, shade1.b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 0.5, shade2.r, shade2.g, shade2.b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 0.5, fill->r, fill->g, fill->b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 1.0, shade3.r, shade3.g, shade3.b);

                _gdk_pen_set_pattern (cr, pattern);
                _gdk_pen_rectangle (cr, 0, 0, width, height);
                _gdk_pen_fill (cr);

                _gdk_pen_pattern_unref (pattern);
        }
        else /* Flat */
        {
                ge_cairo_set_color (cr, fill);
                _gdk_pen_paint (cr);
        }

        if (!toolbar->topmost)
        {
                /* Draw highlight */
                _gdk_pen_move_to       (cr, 0, 0.5);
                _gdk_pen_line_to       (cr, width-0.5, 0.5);
                ge_cairo_set_color  (cr, &light);
                _gdk_pen_stroke        (cr);
        }

        /* Draw shadow */
        _gdk_pen_move_to       (cr, 0, height-0.5);
        _gdk_pen_line_to       (cr, width-0.5, height-0.5);
        ge_cairo_set_color  (cr, dark);
        _gdk_pen_stroke        (cr);
}

static void
clarus_gummy_draw_menuitem (GdkPen                 *cr,
                            const ClarusColors     *colors,
                            const WidgetParameters *params,
                            int x, int y, int width, int height)
{
        const CairoColor *fill = &colors->spot[1];
        const CairoColor *border = &colors->spot[2];
        CairoColor shade1, shade2, shade3;
        GdkPenPattern *pattern;

        ge_shade_color (fill, SHADE_TOP, &shade1);
        ge_shade_color (fill, SHADE_CENTER_TOP, &shade2);
        ge_shade_color (fill, SHADE_BOTTOM, &shade3);
        _gdk_pen_set_line_width (cr, 1.0);

        ge_cairo_rounded_rectangle (cr, x+0.5, y+0.5, width - 1, height - 1, params->radius, params->corners);

        pattern = _gdk_pen_pattern_new_linear (x, y, x, y + height);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0,   shade1.r, shade1.g, shade1.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5,    shade2.r, shade2.g, shade2.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5, fill->r,  fill->g,  fill->b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 1,      shade3.r, shade3.g, shade3.b);

        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_fill_preserve (cr);
        _gdk_pen_pattern_unref (pattern);

        ge_cairo_set_color (cr, border);
        _gdk_pen_stroke (cr);
}

static void
clarus_gummy_draw_menubaritem (GdkPen                 *cr,
                               const ClarusColors     *colors,
                               const WidgetParameters *params,
                               int x, int y, int width, int height)
{
        const CairoColor *fill = &colors->spot[1];
        const CairoColor *border = &colors->spot[2];
        CairoColor shade1, shade2, shade3;
        GdkPenPattern *pattern;

        ge_shade_color (fill, SHADE_TOP, &shade1);
        ge_shade_color (fill, SHADE_CENTER_TOP, &shade2);
        ge_shade_color (fill, SHADE_BOTTOM, &shade3);
        _gdk_pen_set_line_width (cr, 1.0);

        ge_cairo_rounded_rectangle (cr, x+0.5, y+0.5, width - 1, height - 1, params->radius, params->corners);

        pattern = _gdk_pen_pattern_new_linear (x, y, x, y + height);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0,   shade1.r, shade1.g, shade1.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5,    shade2.r, shade2.g, shade2.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5, fill->r,  fill->g,  fill->b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 1,      shade3.r, shade3.g, shade3.b);

        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_fill_preserve (cr);
        _gdk_pen_pattern_unref (pattern);

        ge_cairo_set_color (cr, border);
        _gdk_pen_stroke (cr);
}

static void
clarus_gummy_draw_statusbar (GdkPen                 *cr,
                             const ClarusColors     *colors,
                             const WidgetParameters *widget,
                             int x, int y, int width, int height)
{
        const CairoColor *dark = &colors->shade[3];
        CairoColor hilight;

        ge_shade_color (dark, 1.3, &hilight);

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
clarus_gummy_draw_radiobutton (GdkPen                   *cr,
                               const ClarusColors       *colors,
                               const WidgetParameters   *widget,
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
                if (widget->prelight)
                        border = &colors->spot[2];
                else
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
                if (widget->prelight)
                        clarus_set_mixed_color (cr, &colors->base[0], &colors->spot[1], 0.5);
                else
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
clarus_gummy_draw_checkbox (GdkPen                   *cr,
                            const ClarusColors       *colors,
                            const WidgetParameters   *widget,
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
                if (widget->prelight)
                        border = &colors->spot[2];
                else
                        border = &colors->shade[6];
                dot    = &colors->text[GTK_STATE_NORMAL];
        }

        _gdk_pen_translate (cr, x, y);
        _gdk_pen_set_line_width (cr, 1);

        if (widget->xthickness >= 3 && widget->ythickness >= 3)
        {
                widget->style_functions->draw_inset (cr, &widget->parentbg, 0, 0,
                                                     width, height, (widget->radius > 0)? 1 : 0, CR_CORNER_ALL);

                /* Draw the rectangle for the checkbox itself */
                ge_cairo_rounded_rectangle (cr, 1.5, 1.5,
                                            width-3, height-3, (widget->radius > 0)? 1 : 0, CR_CORNER_ALL);
        }
        else
        {
                /* Draw the rectangle for the checkbox itself */
                ge_cairo_rounded_rectangle (cr, 0.5, 0.5,
                                            width-1, height-1, (widget->radius > 0)? 1 : 0, CR_CORNER_ALL);
        }

        if (!widget->disabled)
        {
                if (widget->prelight)
                        clarus_set_mixed_color (cr, &colors->base[0], &colors->spot[1], 0.5);
                else
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
clarus_gummy_draw_focus (GdkPen                 *cr,
                         const ClarusColors     *colors,
                         const WidgetParameters *widget,
                         const FocusParameters  *focus,
                         int x, int y, int width, int height)
{
        CairoColor fill = focus->color;
        CairoColor fill_shade1, fill_shade2, fill_shade3;
        CairoColor border;
        CairoColor parentbg = widget->parentbg;

        /* Default values */
        double xoffset = 1.5;
        double yoffset = 1.5;
        double radius = widget->radius-1.0;
        double border_alpha = 0.64;
        double fill_alpha = 0.18;
        double shadow_alpha = 0.5;
        boolean focus_fill = TRUE;
        boolean focus_border = TRUE;
        boolean focus_shadow = FALSE;

        ge_shade_color (&fill, 0.65, &border);
        ge_shade_color (&fill, 1.18, &fill_shade1);
        ge_shade_color (&fill, 1.02, &fill_shade2);
        ge_shade_color (&fill, 0.84, &fill_shade3);

        /* Do some useful things to adjust focus */
        switch (focus->type)
        {
                case CL_FOCUS_BUTTON:
                        xoffset = -1.5-(focus->padding);
                        yoffset = -1.5-(focus->padding);
                        radius++;
                        border_alpha = 0.9;
                        fill_alpha = 0.12;
                        if (!widget->active)
                                focus_shadow = TRUE;
                        break;
                case CL_FOCUS_BUTTON_FLAT:
                        xoffset = -1.5-(focus->padding);
                        yoffset = -1.5-(focus->padding);
                        radius++;
                        if (widget->active || widget->prelight)
                        {
                                border_alpha = 0.9;
                                fill_alpha = 0.12;
                                if (!widget->active)
                                        focus_shadow = TRUE;
                        }
                        break;
                case CL_FOCUS_LABEL:
                        xoffset = 0.5;
                        yoffset = 0.5;
                        break;
                case CL_FOCUS_TREEVIEW:
                        parentbg = colors->base[widget->state_type];
                        xoffset = -1.5;
                        yoffset = -1.5;
                        fill_alpha = 0.08;
                        focus_border = FALSE;
                        break;
                case CL_FOCUS_TREEVIEW_DND:
                        parentbg = colors->base[widget->state_type];
                        break;
                case CL_FOCUS_TREEVIEW_HEADER:
                        _gdk_pen_translate (cr, -1, 0);
                        break;
                case CL_FOCUS_TREEVIEW_ROW:
                        parentbg = colors->base[widget->state_type];
                        xoffset = -2.5; /* hack to hide vertical lines */
                        yoffset = 0.5;
                        radius = CLAMP (radius, 0.0, 2.0);
                        border_alpha = 0.7;
                        focus_fill = FALSE;
                        break;
                case CL_FOCUS_TAB:
                        /* In current GTK+ focus and active cannot happen together, but we are robust against it.
                         * IF the application sets the state to ACTIVE while drawing the tabs focus. */
                        if (widget->focus && !widget->active)
                                return;
                        break;
                case CL_FOCUS_SCALE:
                        break;
                case CL_FOCUS_ICONVIEW:
                        break;
                case CL_FOCUS_UNKNOWN:
                        /* Fallback to classic function, dots */
                        clarus_draw_focus (cr, colors, widget, focus, x, y, width, height);
                        return;
                        break;
                default:
                        break;
        };

        _gdk_pen_translate (cr, x, y);
        _gdk_pen_set_line_width (cr, focus->line_width);

        ge_cairo_rounded_rectangle (cr, xoffset, yoffset, width-(xoffset*2), height-(yoffset*2), radius, widget->corners);

        if (focus_fill)
        {
                GdkPenPattern *pattern;

                pattern = _gdk_pen_pattern_new_linear (0, 0, 0, height);
                _gdk_pen_pattern_add_stop_rgba (pattern, 0.0, fill_shade1.r, fill_shade1.g, fill_shade1.b, fill_alpha);
                _gdk_pen_pattern_add_stop_rgba (pattern, 0.5, fill_shade2.r, fill_shade2.g, fill_shade2.b, fill_alpha);
                _gdk_pen_pattern_add_stop_rgba (pattern, 0.5, fill.r,        fill.g,        fill.b,        fill_alpha);
                _gdk_pen_pattern_add_stop_rgba (pattern, 1.0, fill_shade3.r, fill_shade3.g, fill_shade3.b, fill_alpha);
                _gdk_pen_set_pattern (cr, pattern);
                _gdk_pen_fill_preserve (cr);

                _gdk_pen_pattern_unref (pattern);
        }

        if (focus_border)
        {
                clarus_set_mixed_color (cr, &parentbg, &border, border_alpha);
                _gdk_pen_stroke (cr);
        }

        if (focus_shadow)
        {
                if (radius > 0)
                        radius++;
                ge_cairo_rounded_rectangle (cr, xoffset-1, yoffset-1, width-(xoffset*2)+2, height-(yoffset*2)+2, radius, widget->corners);
                clarus_set_mixed_color (cr, &parentbg, &fill, shadow_alpha);
                _gdk_pen_stroke (cr);
        }
}

void
clarus_register_style_gummy (ClarusStyleFunctions *functions, ClarusStyleConstants *constants)
{
        functions->draw_button             = clarus_gummy_draw_button;
        functions->draw_entry              = clarus_gummy_draw_entry;
        functions->draw_progressbar_trough = clarus_gummy_draw_progressbar_trough;
        functions->draw_progressbar_fill   = clarus_gummy_draw_progressbar_fill;
        functions->draw_scale_trough       = clarus_gummy_draw_scale_trough;
        functions->draw_tab                = clarus_gummy_draw_tab;
        functions->draw_separator          = clarus_gummy_draw_separator;
        functions->draw_slider             = clarus_gummy_draw_slider;
        functions->draw_slider_button      = clarus_gummy_draw_slider_button;
        functions->draw_scrollbar_stepper  = clarus_gummy_draw_scrollbar_stepper;
        functions->draw_scrollbar_slider   = clarus_gummy_draw_scrollbar_slider;
        functions->draw_list_view_header   = clarus_gummy_draw_list_view_header;
        functions->draw_toolbar            = clarus_gummy_draw_toolbar;
        functions->draw_menuitem           = clarus_gummy_draw_menuitem;
        functions->draw_menubaritem        = clarus_gummy_draw_menubaritem;
        functions->draw_statusbar          = clarus_gummy_draw_statusbar;
        functions->draw_checkbox           = clarus_gummy_draw_checkbox;
        functions->draw_radiobutton        = clarus_gummy_draw_radiobutton;
        functions->draw_focus              = clarus_gummy_draw_focus;

        constants->topleft_highlight_shade = 1.3;
        constants->topleft_highlight_alpha = 0.4;
}
