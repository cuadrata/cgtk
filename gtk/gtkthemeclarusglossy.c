/* GTK - The GIMP Toolkit
 * Clarus: drawing for its glossy style
 *
 * Ported from the Clearlooks engine of gtk-engines 2.20.2:
 * Copyright (C) 2006 Benjamin Berg <benjamin@sipsolutions.net>
 * Copyright (C) 2007-2008 Andrea Cimitan <andrea.cimitan@gmail.com>
 * Written by Andrea Cimitan <andrea.cimitan@gmail.com>
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

static void
clarus_draw_glossy_gradient (GdkPen          *cr,
                             double x, double y, int width, int height,
                             const CairoColor *color,
                             gboolean disabled, gboolean radius, CairoCorners corners)
{
        CairoColor a, b, c, d;
        GdkPenPattern *pt;

        ge_shade_color (color, disabled? 1.06 : 1.16, &a);
        ge_shade_color (color, disabled? 1.02 : 1.08, &b);
        ge_shade_color (color, disabled? 0.98 : 1.00, &c);
        ge_shade_color (color, disabled? 1.02 : 1.08, &d);

        pt = _gdk_pen_pattern_new_linear (x, y, x, y+height);
        _gdk_pen_pattern_add_stop_rgb (pt, 0.0, a.r, a.g, a.b);
        _gdk_pen_pattern_add_stop_rgb (pt, 0.5, b.r, b.g, b.b);
        _gdk_pen_pattern_add_stop_rgb (pt, 0.5, c.r, c.g, c.b);
        _gdk_pen_pattern_add_stop_rgb (pt, 1.0, d.r, d.g, d.b);

        _gdk_pen_set_pattern (cr, pt);
        ge_cairo_rounded_rectangle (cr, x, y, width, height, radius, corners);
        _gdk_pen_fill (cr);

        _gdk_pen_pattern_unref (pt);
}

/* This draw_inset implementation only differes in the shades values from the
 * default one. */
static void
clarus_glossy_draw_inset (GdkPen           *cr,
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
        ge_shade_color (bg_color, 0.93, &shadow);
        ge_shade_color (bg_color, 1.07, &highlight);

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
clarus_glossy_draw_light_inset (GdkPen           *cr,
                                const CairoColor *bg_color,
                                double x, double y, double w, double h,
                                double radius, uint8 corners)
{
        CairoColor shadow;
        CairoColor highlight;

        /* not really sure of shading ratios... we will think */
        ge_shade_color (bg_color, 0.95, &shadow);
        ge_shade_color (bg_color, 1.05, &highlight);

        /* highlight */
        _gdk_pen_move_to (cr, x + w + (radius * -0.2928932188), y - (radius * -0.2928932188)); /* 0.2928932... 1-sqrt(2)/2 gives middle of curve */

        if (corners & CR_CORNER_TOPRIGHT)
                _gdk_pen_arc (cr, x + w - radius, y + radius, radius, G_PI * 1.75, G_PI * 2);
        else
                _gdk_pen_line_to (cr, x + w, y);

        if (corners & CR_CORNER_BOTTOMRIGHT)
                _gdk_pen_arc (cr, x + w - radius, y + h - radius, radius, 0, G_PI * 0.5);
        else
                _gdk_pen_line_to (cr, x + w, y + h);

        if (corners & CR_CORNER_BOTTOMLEFT)
                _gdk_pen_arc (cr, x + radius, y + h - radius, radius, G_PI * 0.5, G_PI * 0.75);
        else
                _gdk_pen_line_to (cr, x, y + h);

        ge_cairo_set_color (cr, &highlight);
        _gdk_pen_stroke (cr);

        /* shadow */
        _gdk_pen_move_to (cr, x + (radius * 0.2928932188), y + h + (radius * -0.2928932188));

        if (corners & CR_CORNER_BOTTOMLEFT)
                _gdk_pen_arc (cr, x + radius, y + h - radius, radius, G_PI * 0.75, G_PI);
        else
                _gdk_pen_line_to (cr, x, y + h);

        if (corners & CR_CORNER_TOPLEFT)
                _gdk_pen_arc (cr, x + radius, y + radius, radius, G_PI, G_PI * 1.5);
        else
                _gdk_pen_line_to (cr, x, y);

        if (corners & CR_CORNER_TOPRIGHT)
            _gdk_pen_arc (cr, x + w - radius, y + radius, radius, G_PI * 1.5, G_PI * 1.75);
        else
                _gdk_pen_line_to (cr, x + w, y);

        ge_cairo_set_color (cr, &shadow);
        _gdk_pen_stroke (cr);
}

/* This function currently assumes that the input is translated by 0.5px! */
static void
clarus_glossy_draw_highlight_and_shade (GdkPen           *cr,
                                        const CairoColor *bg_color,
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
clarus_glossy_draw_button (GdkPen *cr,
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

        /* Shadows and shadow */
        if (params->xthickness == 3 || params->ythickness == 3)
        {
                if (params->xthickness == 3)
                        xoffset = 1;
                if (params->ythickness == 3)
                        yoffset = 1;
        }

        radius = MIN (params->radius, MIN ((width - 2.0 - 2*xoffset) / 2.0, (height - 2.0 - 2*yoffset) / 2.0));

        if (params->xthickness == 3 || params->ythickness == 3)
        {
                /* if (params->enable_shadow && !params->active && !params->disabled) */
                if (!params->active && (params->prelight || params->enable_shadow))
                {
                        /* shadow becomes a shadow to have 3d prelight buttons :) */
                        CairoColor shadow;

                        radius = MIN (params->radius, MIN ((width - 2.0 - 2*xoffset) / 2.0 - 1.0, (height - 2.0 - 2*yoffset) / 2.0 - 1.0));

                        ge_cairo_rounded_rectangle (cr, 0.5, 0.5, width-1, height-1, radius+1, params->corners);
                        ge_shade_color (&params->parentbg, 0.96, &shadow);
                        ge_cairo_set_color (cr, &shadow);
                        _gdk_pen_stroke (cr);

                        ge_cairo_rounded_rectangle (cr, 1.5, 1.5, width-2, height-2, radius+1, params->corners);
                        ge_shade_color (&params->parentbg, 0.92, &shadow);
                        ge_cairo_set_color (cr, &shadow);
                        _gdk_pen_stroke (cr);
                }
                else
                {
                        if (!(params->disabled))
                                params->style_functions->draw_inset (cr, &params->parentbg, 0, 0, width, height, params->radius+1, params->corners);
                        else
                                /*Draw a lighter inset */
                                clarus_glossy_draw_light_inset (cr, &params->parentbg, 0, 0, width, height, params->radius+1, params->corners);
                }
        }

        clarus_draw_glossy_gradient (cr, xoffset+1, yoffset+1,
                                      width-(xoffset*2)-2, height-(yoffset*2)-2,
                                      &fill, params->disabled, radius, params->corners);

        /* Pressed button shadow */
        if (params->active)
        {
                CairoColor shadow;
                ge_shade_color (&fill, 0.92, &shadow);

                _gdk_pen_save (cr);

                ge_cairo_rounded_rectangle (cr, xoffset+1, yoffset+1, width-(xoffset*2)-2, height, radius, params->corners & (CR_CORNER_TOPLEFT | CR_CORNER_TOPRIGHT | CR_CORNER_BOTTOMLEFT));
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

        /* Default button highlight */
        if (params->is_default && !params->active && !params->disabled)
        {
                const CairoColor *shadow = &colors->spot[0];
                double hh = (height-5)/2.0 + 1;

                _gdk_pen_rectangle (cr, 3.5, 3.5, width-7, height-7);
                ge_cairo_set_color (cr, shadow);
                _gdk_pen_stroke (cr);

                shadow = &colors->spot[0];
                _gdk_pen_move_to (cr, 2.5, 2.5+hh); _gdk_pen_rel_line_to (cr, 0, -hh);
                _gdk_pen_rel_line_to (cr, width-5, 0); _gdk_pen_rel_line_to (cr, 0, hh);
                ge_cairo_set_color (cr, shadow);
                _gdk_pen_stroke (cr);

                hh--;

                shadow = &colors->spot[1];
                _gdk_pen_move_to (cr, 2.5, 2.5+hh); _gdk_pen_rel_line_to (cr, 0, hh);
                _gdk_pen_rel_line_to (cr, width-5, 0); _gdk_pen_rel_line_to (cr, 0, -hh);
                ge_cairo_set_color (cr, shadow);
                _gdk_pen_stroke (cr);
        }

        /* Border */
        if (params->is_default || (params->prelight && params->enable_shadow))
                border_normal = colors->spot[2];
                /* ge_mix_color (&border_normal, &colors->spot[2], 0.5, &border_normal); */
        if (params->disabled)
                ge_cairo_set_color (cr, &border_disabled);
        else
                clarus_set_mixed_color (cr, &border_normal, &fill, 0.2);
        ge_cairo_rounded_rectangle (cr, xoffset + 0.5, yoffset + 0.5,
                                  width-(xoffset*2)-1, height-(yoffset*2)-1,
                                  radius, params->corners);
        _gdk_pen_stroke (cr);
        _gdk_pen_restore (cr);
}

static void
clarus_glossy_draw_progressbar_trough (GdkPen *cr,
                                    const ClarusColors *colors,
                                    const WidgetParameters *params,
                                    int x, int y, int width, int height)
{
        const CairoColor *border = &colors->shade[6];
        CairoColor        shadow;
        GdkPenPattern    *pattern;
        double           radius = MIN (params->radius, MIN ((height-2.0) / 2.0, (width-2.0) / 2.0));

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
clarus_glossy_draw_progressbar_fill (GdkPen *cr,
                                  const ClarusColors *colors,
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
        CairoColor       a;
        CairoColor       b;
        CairoColor       e;
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
        ge_shade_color (&colors->spot[1], 1.16, &a);
        ge_shade_color (&colors->spot[1], 1.08, &b);
        ge_shade_color (&colors->spot[1], 1.08, &e);
        pattern = _gdk_pen_pattern_new_linear (0, 0, 0, height);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.0, a.r, a.g, a.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5, b.r, b.g, b.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5, colors->spot[1].r, colors->spot[1].g, colors->spot[1].b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 1.0, e.r, e.g, e.b);
        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_paint (cr);
        _gdk_pen_pattern_unref (pattern);

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

        _gdk_pen_set_rgba (cr, colors->spot[2].r,
                               colors->spot[2].g,
                               colors->spot[2].b,
                               0.15);

        _gdk_pen_fill (cr);
        _gdk_pen_restore (cr); /* rounded clip region */

        /* inner highlight border
         * This is again kinda ugly. Draw once from each side, clipping away the other. */
        _gdk_pen_set_rgba (cr, colors->spot[0].r, colors->spot[0].g, colors->spot[0].b, 0.3);

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
        border.a = 0.5;
        ge_shade_color (&colors->shade[6], 0.92, &shadow);
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
clarus_glossy_scale_draw_gradient (GdkPen *cr,
                                const CairoColor *c1,
                                const CairoColor *c2,
                                const CairoColor *c3,
                                int x, int y, int width, int height,
                                boolean horizontal)
{
        GdkPenPattern *pattern;

        pattern = _gdk_pen_pattern_new_linear (0.5, 0.5, horizontal ? 0.5 :  width + 1, horizontal ? height + 1 : 0.5);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.0, c1->r, c1->g, c1->b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 1.0, c2->r, c2->g, c2->b);

        _gdk_pen_rectangle (cr, x, y, width, height);
        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_fill (cr);
        _gdk_pen_pattern_unref (pattern);

        clarus_set_mixed_color (cr, c3, c1, 0.3);
        ge_cairo_inner_rectangle (cr, x, y, width, height);
        _gdk_pen_stroke (cr);
}

#define TROUGH_SIZE 7
static void
clarus_glossy_draw_scale_trough (GdkPen *cr,
                              const ClarusColors *colors,
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
                clarus_glossy_scale_draw_gradient (cr, &colors->shade[3], /* top */
                                                    &colors->shade[2], /* bottom */
                                                    &colors->shade[6], /* border */
                                                    1.0, 1.0, trough_width - 2, trough_height - 2,
                                                    slider->horizontal);
        else
                clarus_glossy_scale_draw_gradient (cr, &colors->spot[1], /* top */
                                                    &colors->spot[0], /* bottom */
                                                    &colors->spot[2], /* border */
                                                    1.0, 1.0, trough_width - 2, trough_height - 2,
                                                    slider->horizontal);
        _gdk_pen_restore (cr);
}

static void
clarus_glossy_draw_tab (GdkPen *cr,
                        const ClarusColors     *colors,
                        const WidgetParameters *params,
                        const TabParameters    *tab,
                        int x, int y, int width, int height)
{

        const CairoColor    *border       = &colors->shade[5];
        const CairoColor    *stripe_fill   = &colors->spot[1];
        const CairoColor    *stripe_border = &colors->spot[2];
        const CairoColor    *fill;
        CairoColor           hilight;

        GdkPenPattern       *pattern;

        double               radius;
        double               length;

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
                height += 3.0;
                length = height;

                if (tab->gap_side == CL_GAP_TOP)
                        _gdk_pen_translate (cr, 0.0, -3.0); /* gap at the other side */
        }
        else
        {
                width += 3.0;
                length = width;

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
        _gdk_pen_fill   (cr);

        ge_shade_color (fill, 1.3, &hilight);

        /* Draw highlight */
        if (!params->active)
        {
                ShadowParameters shadow;

                shadow.shadow  = CL_SHADOW_OUT;
                shadow.corners = params->corners;

                /* This is the only usage of clarus_glossy_draw_highlight_and_shade,
                 * the function assumes currently that the input is translated by 0.5 px. */
                clarus_glossy_draw_highlight_and_shade (cr, &colors->bg[0], &shadow,
                                                     width,
                                                     height, radius);
        }

        if (params->active)
        {
                CairoColor shadow, hilight, f1, f2;

                switch (tab->gap_side)
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
                        default:
                                pattern = NULL;
                }

                ge_cairo_rounded_rectangle (cr, 0, 0, width-1, height-1, radius, params->corners);

                ge_shade_color (fill, 1.06, &shadow);
                ge_shade_color (fill, 1.18, &hilight);
                ge_shade_color (fill, 1.12, &f1);
                ge_shade_color (fill, 1.06, &f2);

                _gdk_pen_pattern_add_stop_rgb (pattern, 0.0,        hilight.r, hilight.g, hilight.b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 1.0/length, hilight.r, hilight.g, hilight.b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 1.0/length, f1.r, f1.g, f1.b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 0.45,       f2.r, f2.g, f2.b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 0.45,       fill->r, fill->g, fill->b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 1.0,        shadow.r, shadow.g, shadow.b);
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
                        default:
                                pattern = NULL;
                }

                ge_cairo_rounded_rectangle (cr, 0, 0, width-1, height-1, radius, params->corners);


                _gdk_pen_pattern_add_stop_rgba (pattern, 0.0, stripe_fill->r, stripe_fill->g, stripe_fill->b, 0.5);
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
                switch (tab->gap_side)
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
                        default:
                                pattern = NULL;
                }

                _gdk_pen_pattern_add_stop_rgb (pattern, 0.0, stripe_border->r, stripe_border->g, stripe_border->b);
                _gdk_pen_pattern_add_stop_rgb (pattern, 0.8, border->r,        border->g,        border->b);
                _gdk_pen_set_pattern (cr, pattern);
                _gdk_pen_stroke (cr);
                _gdk_pen_pattern_unref (pattern);
        }
}

static void
clarus_glossy_draw_slider (GdkPen *cr,
                        const ClarusColors *colors,
                        const WidgetParameters *params,
                        int x, int y, int width, int height)
{
        const CairoColor *border  = &colors->shade[7];
        CairoColor  fill;
        CairoColor  hilight;
        CairoColor  a, b, c, d;
        GdkPenPattern *pattern;

        _gdk_pen_set_line_width (cr, 1.0);
        _gdk_pen_translate      (cr, x, y);

        ge_shade_color (&colors->bg[params->state_type], 1.0, &fill);
        if (params->prelight)
                ge_shade_color (&fill, 1.1, &fill);

        ge_shade_color (&fill, 1.25, &hilight);
        ge_shade_color (&fill, 1.16, &a);
        ge_shade_color (&fill, 1.08, &b);
        ge_shade_color (&fill, 1.0,  &c);
        ge_shade_color (&fill, 1.08, &d);

        pattern = _gdk_pen_pattern_new_linear (1, 1, 1, height-2);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0,   a.r, a.g, a.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5, b.r, b.g, b.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5, c.r, c.g, c.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 1.0, d.r, d.g, d.b);
        _gdk_pen_rectangle (cr, 1, 1, width-2, height-2);
        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_fill (cr);
        _gdk_pen_pattern_unref (pattern);

        clarus_set_mixed_color (cr, border, &fill, 0.2);
        if (params->prelight)
                ge_cairo_set_color (cr, &colors->spot[2]);
        ge_cairo_rounded_rectangle (cr, 0.5, 0.5, width-1, height-1, 2.5, params->corners);
        _gdk_pen_stroke (cr);

        _gdk_pen_set_rgba (cr, hilight.r, hilight.g, hilight.b, 0.5);
        ge_cairo_rounded_rectangle (cr, 1.5, 1.5, width-3, height-3, 2.0, params->corners);
        _gdk_pen_stroke (cr);
}

static void
clarus_glossy_draw_slider_button (GdkPen *cr,
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
clarus_glossy_draw_scrollbar_stepper (GdkPen *cr,
                                   const ClarusColors           *colors,
                                   const WidgetParameters       *widget,
                                   const ScrollBarParameters    *scrollbar,
                                   const ScrollBarStepperParameters *stepper,
                                   int x, int y, int width, int height)
{
        CairoCorners corners = CR_CORNER_NONE;
        const CairoColor *border = &colors->shade[7];
        CairoColor  fill, s1, s2, s4;
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
        ge_shade_color(&fill, 1.16, &s1);
        ge_shade_color(&fill, 1.08, &s2);
        ge_shade_color(&fill, 1.08, &s4);

        _gdk_pen_pattern_add_stop_rgb(pattern, 0,    s1.r, s1.g, s1.b);
        _gdk_pen_pattern_add_stop_rgb(pattern, 0.5,     s2.r, s2.g, s2.b);
        _gdk_pen_pattern_add_stop_rgb(pattern, 0.5,     fill.r, fill.g, fill.b);
        _gdk_pen_pattern_add_stop_rgb(pattern, 1.0,  s4.r, s4.g, s4.b);
        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_fill (cr);
        _gdk_pen_pattern_unref (pattern);

        ge_cairo_inner_rounded_rectangle (cr, 0, 0, width, height, radius, corners);
        clarus_set_mixed_color (cr, border, &fill, 0.2);
        if (widget->prelight)
                ge_cairo_set_color (cr, &colors->spot[2]);
        _gdk_pen_stroke (cr);
}

static void
clarus_glossy_draw_scrollbar_slider (GdkPen *cr,
                                   const ClarusColors          *colors,
                                   const WidgetParameters      *widget,
                                   const ScrollBarParameters   *scrollbar,
                                   int x, int y, int width, int height)
{
        const CairoColor *border  = &colors->shade[7];
        CairoColor  fill  = scrollbar->color;
        CairoColor  hilight;
        CairoColor  shade1, shade2, shade3;
        GdkPenPattern *pattern;

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
                ge_shade_color (&fill, 1.1, &fill);

        _gdk_pen_set_line_width (cr, 1);

        ge_shade_color (&fill, 1.25, &hilight);
        ge_shade_color (&fill, 1.16, &shade1);
        ge_shade_color (&fill, 1.08, &shade2);
        ge_shade_color (&fill, 1.08, &shade3);

        pattern = _gdk_pen_pattern_new_linear (1, 1, 1, height-2);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0,   shade1.r, shade1.g, shade1.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5,    shade2.r, shade2.g, shade2.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5,    fill.r,  fill.g,  fill.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 1,      shade3.r, shade3.g, shade3.b);
        _gdk_pen_rectangle (cr, 1, 1, width-2, height-2);
        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_fill (cr);
        _gdk_pen_pattern_unref (pattern);

        if (scrollbar->has_color)
        {
                _gdk_pen_set_rgba (cr, hilight.r, hilight.g, hilight.b, 0.5);
                ge_cairo_stroke_rectangle (cr, 1.5, 1.5, width-3, height-3);
        }

        clarus_set_mixed_color (cr, border, &fill, scrollbar->has_color? 0.4 : 0.2);
        ge_cairo_stroke_rectangle (cr, 0.5, 0.5, width-1, height-1);
}

static void
clarus_glossy_draw_list_view_header (GdkPen *cr,
                                  const ClarusColors          *colors,
                                  const WidgetParameters      *params,
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

        ge_shade_color (fill, 1.2, &hilight);
        ge_shade_color (fill, 1.08, &shade1);
        ge_shade_color (fill, 1.04, &shade2);
        ge_shade_color (fill, 1.04, &shade3);

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
                _gdk_pen_move_to (cr, 0.5, height-1);
                _gdk_pen_line_to (cr, 0.5, 0.5);
        }
        else
                _gdk_pen_move_to (cr, 0.0, 0.5);

        _gdk_pen_line_to (cr, width, 0.5);

        _gdk_pen_set_rgba (cr, hilight.r, hilight.g, hilight.b, 0.5);
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
clarus_glossy_draw_toolbar (GdkPen *cr,
                         const ClarusColors          *colors,
                         const WidgetParameters      *widget,
                         const ToolbarParameters     *toolbar,
                         int x, int y, int width, int height)
{
        const CairoColor *fill  = &colors->bg[GTK_STATE_NORMAL];
        const CairoColor *dark  = &colors->shade[3];
        CairoColor light;
        ge_shade_color (fill, 1.1, &light);

        _gdk_pen_set_line_width (cr, 1.0);
        _gdk_pen_translate (cr, x, y);

        if (toolbar->style == 1) /* Enable Extra features */
        {
                GdkPenPattern *pattern;
                CairoColor shade1, shade2, shade3;

                ge_shade_color (fill, 1.08, &shade1);
                ge_shade_color (fill, 1.04, &shade2);
                ge_shade_color (fill, 1.04, &shade3);

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

                if (!toolbar->topmost)
                {
                        /* Draw highlight */
                        _gdk_pen_move_to       (cr, 0, 0.5);
                        _gdk_pen_line_to       (cr, width-1, 0.5);
                        ge_cairo_set_color  (cr, &light);
                        _gdk_pen_stroke        (cr);
                }
        }

        /* Draw shadow */
        _gdk_pen_move_to       (cr, 0, height-0.5);
        _gdk_pen_line_to       (cr, width-1, height-0.5);
        ge_cairo_set_color  (cr, dark);
        _gdk_pen_stroke        (cr);
}

static void
clarus_glossy_draw_menuitem (GdkPen                    *cr,
                             const ClarusColors        *colors,
                             const WidgetParameters    *params,
                             int x, int y, int width, int height)
{
        const CairoColor *fill = &colors->spot[1];
        const CairoColor *border = &colors->spot[2];
        CairoColor shade1, shade2, shade3;
        GdkPenPattern *pattern;

        ge_shade_color (fill, 1.16, &shade1);
        ge_shade_color (fill, 1.08, &shade2);
        ge_shade_color (fill, 1.08, &shade3);
        _gdk_pen_set_line_width (cr, 1.0);

        ge_cairo_rounded_rectangle (cr, x+0.5, y+0.5, width - 1, height - 1, params->radius, params->corners);

        pattern = _gdk_pen_pattern_new_linear (x, y, x, y + height);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0,   shade1.r, shade1.g, shade1.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5,    shade2.r, shade2.g, shade2.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5, fill->r,  fill->g,  fill->b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 1,      shade3.r, shade3.g, shade3.b);

        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_fill_preserve  (cr);
        _gdk_pen_pattern_unref (pattern);

        ge_cairo_set_color (cr, border);
        _gdk_pen_stroke (cr);
}

static void
clarus_glossy_draw_menubaritem (GdkPen                    *cr,
                                const ClarusColors        *colors,
                                const WidgetParameters    *params,
                                int x, int y, int width, int height)
{
        const CairoColor *fill = &colors->spot[1];
        const CairoColor *border = &colors->spot[2];
        CairoColor shade1, shade2, shade3;
        GdkPenPattern *pattern;

        ge_shade_color (fill, 1.16, &shade1);
        ge_shade_color (fill, 1.08, &shade2);
        ge_shade_color (fill, 1.08, &shade3);
        _gdk_pen_set_line_width (cr, 1.0);

        ge_cairo_rounded_rectangle (cr, x+0.5, y+0.5, width - 1, height - 1, params->radius, params->corners);

        pattern = _gdk_pen_pattern_new_linear (x, y, x, y + height);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0,   shade1.r, shade1.g, shade1.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5,    shade2.r, shade2.g, shade2.b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 0.5, fill->r,  fill->g,  fill->b);
        _gdk_pen_pattern_add_stop_rgb (pattern, 1,      shade3.r, shade3.g, shade3.b);

        _gdk_pen_set_pattern (cr, pattern);
        _gdk_pen_fill_preserve  (cr);
        _gdk_pen_pattern_unref (pattern);

        ge_cairo_set_color (cr, border);
        _gdk_pen_stroke (cr);
}

static void
clarus_glossy_draw_radiobutton (GdkPen *cr,
                             const ClarusColors  *colors,
                             const WidgetParameters *widget,
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
                        _gdk_pen_set_line_cap(cr, GDK_CAP_ROUND);
                        _gdk_pen_set_line_width (cr, ceil (radius * 2 / 3));

                        _gdk_pen_move_to (cr, ceil (cx - radius/3.0), ceil (cy));
                        _gdk_pen_line_to (cr, ceil (cx + radius/3.0), ceil (cy));

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
clarus_glossy_draw_checkbox (GdkPen *cr,
                          const ClarusColors  *colors,
                          const WidgetParameters *widget,
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

        if (widget->xthickness > 2 && widget->ythickness > 2)
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

void
clarus_register_style_glossy (ClarusStyleFunctions *functions, ClarusStyleConstants *constants)
{
        functions->draw_inset              = clarus_glossy_draw_inset;
        functions->draw_button             = clarus_glossy_draw_button;
        functions->draw_progressbar_trough = clarus_glossy_draw_progressbar_trough;
        functions->draw_progressbar_fill   = clarus_glossy_draw_progressbar_fill;
        functions->draw_scale_trough       = clarus_glossy_draw_scale_trough;
        functions->draw_tab                = clarus_glossy_draw_tab;
        functions->draw_slider             = clarus_glossy_draw_slider;
        functions->draw_slider_button      = clarus_glossy_draw_slider_button;
        functions->draw_scrollbar_stepper  = clarus_glossy_draw_scrollbar_stepper;
        functions->draw_scrollbar_slider   = clarus_glossy_draw_scrollbar_slider;
        functions->draw_list_view_header   = clarus_glossy_draw_list_view_header;
        functions->draw_toolbar            = clarus_glossy_draw_toolbar;
        functions->draw_menuitem           = clarus_glossy_draw_menuitem;
        functions->draw_menubaritem        = clarus_glossy_draw_menubaritem;
        functions->draw_checkbox           = clarus_glossy_draw_checkbox;
        functions->draw_radiobutton        = clarus_glossy_draw_radiobutton;
}
