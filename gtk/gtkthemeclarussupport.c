/* GTK - The GIMP Toolkit
 * Clarus, a theme engine ported from Clearlooks: the support
 * library's helpers it draws with
 *
 * Ported from cairo-support.c, of the support library of gtk-engines
 * 2.20.2:
 * Copyright (C) 2006 Andrew Johnson <acjgenius@earthlink.net>
 * Copyright (C) 2006 Thomas Wood <thos@gnome.org>
 * Copyright (C) 2006-2007 Benjamin Berg <benjamin@sipsolutions.net>
 * Copyright (C) 2007 Andrea Cimitan <andrea.cimitan@gmail.com>
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

/* The helpers Clearlooks' drawing uses, kept as they were. */

#include <math.h>

#include "gtkthemeclarus.h"

void
ge_hsb_from_color (const CairoColor *color,
                        gdouble *hue,
                        gdouble *saturation,
                        gdouble *brightness)
{
        gdouble min, max, delta;
        gdouble red, green, blue;

        red = color->r;
        green = color->g;
        blue = color->b;

        if (red > green)
        {
                max = MAX(red, blue);
                min = MIN(green, blue);
        }
        else
        {
                max = MAX(green, blue);
                min = MIN(red, blue);
        }

        *brightness = (max + min) / 2;

        if (fabs(max - min) < 0.0001)
        {
                *hue = 0;
                *saturation = 0;
        }
        else
        {
                if (*brightness <= 0.5)
                        *saturation = (max - min) / (max + min);
                else
                        *saturation = (max - min) / (2 - max - min);

                delta = max -min;

                if (red == max)
                        *hue = (green - blue) / delta;
                else if (green == max)
                        *hue = 2 + (blue - red) / delta;
                else if (blue == max)
                        *hue = 4 + (red - green) / delta;

                *hue *= 60;
                if (*hue < 0.0)
                        *hue += 360;
        }
}

#define MODULA(number, divisor) (((gint)number % divisor) + (number - (gint)number))

void
ge_color_from_hsb (gdouble hue,
                        gdouble saturation,
                        gdouble brightness,
                        CairoColor *color)
{
        gint i;
        gdouble hue_shift[3], color_shift[3];
        gdouble m1, m2, m3;

        if (!color) return;

        if (brightness <= 0.5)
                m2 = brightness * (1 + saturation);
        else
                m2 = brightness + saturation - brightness * saturation;

        m1 = 2 * brightness - m2;

        hue_shift[0] = hue + 120;
        hue_shift[1] = hue;
        hue_shift[2] = hue - 120;

        color_shift[0] = color_shift[1] = color_shift[2] = brightness;

        i = (saturation == 0)?3:0;

        for (; i < 3; i++)
        {
                m3 = hue_shift[i];

                if (m3 > 360)
                        m3 = MODULA(m3, 360);
                else if (m3 < 0)
                        m3 = 360 - MODULA(ABS(m3), 360);

                if (m3 < 60)
                        color_shift[i] = m1 + (m2 - m1) * m3 / 60;
                else if (m3 < 180)
                        color_shift[i] = m2;
                else if (m3 < 240)
                        color_shift[i] = m1 + (m2 - m1) * (240 - m3) / 60;
                else
                        color_shift[i] = m1;
        }

        color->r = color_shift[0];
        color->g = color_shift[1];
        color->b = color_shift[2];
        color->a = 1.0;
}

void
ge_gdk_color_to_cairo (const GdkColor *c, CairoColor *cc)
{
        gdouble r, g, b;

        g_return_if_fail (c && cc);

        r = c->red / 65535.0;
        g = c->green / 65535.0;
        b = c->blue / 65535.0;

        cc->r = r;
        cc->g = g;
        cc->b = b;
        cc->a = 1.0;
}

void
ge_shade_color(const CairoColor *base, gdouble shade_ratio, CairoColor *composite)
{
        gdouble hue = 0;
        gdouble saturation = 0;
        gdouble brightness = 0;

        g_return_if_fail (base && composite);

        if (shade_ratio == 1.0)
        {
                composite->r = base->r;
                composite->g = base->g;
                composite->b = base->b;
                composite->a = base->a;

                return;
        }

        ge_hsb_from_color (base, &hue, &saturation, &brightness);

        brightness = MIN(brightness*shade_ratio, 1.0);
        brightness = MAX(brightness, 0.0);

        saturation = MIN(saturation*shade_ratio, 1.0);
        saturation = MAX(saturation, 0.0);

        ge_color_from_hsb (hue, saturation, brightness, composite);
        composite->a = base->a;
}

void
ge_mix_color (const CairoColor *color1, const CairoColor *color2,
              gdouble mix_factor, CairoColor *composite)
{
        g_return_if_fail (color1 && color2 && composite);

        composite->r = color1->r * (1-mix_factor) + color2->r * mix_factor;
        composite->g = color1->g * (1-mix_factor) + color2->g * mix_factor;
        composite->b = color1->b * (1-mix_factor) + color2->b * mix_factor;
        composite->a = 1.0;
}

void
ge_cairo_set_color (GdkPen *cr, const CairoColor *color)
{
        g_return_if_fail (cr && color);

        _gdk_pen_set_rgba (cr, color->r, color->g, color->b, color->a);
}

void
ge_cairo_rounded_corner (GdkPen       *cr,
                         double        x,
                         double        y,
                         double        radius,
                         CairoCorners  corner)
{
        if (radius < 0.0001)
        {
                _gdk_pen_line_to (cr, x, y);
        }
        else
        {
                switch (corner)
                {
                        case CR_CORNER_NONE:
                                _gdk_pen_line_to (cr, x, y);
                                break;
                        case CR_CORNER_TOPLEFT:
                                _gdk_pen_arc (cr, x + radius, y + radius, radius, G_PI, G_PI * 3/2);
                                break;
                        case CR_CORNER_TOPRIGHT:
                                _gdk_pen_arc (cr, x - radius, y + radius, radius, G_PI * 3/2, G_PI * 2);
                                break;
                        case CR_CORNER_BOTTOMRIGHT:
                                _gdk_pen_arc (cr, x - radius, y - radius, radius, 0, G_PI * 1/2);
                                break;
                        case CR_CORNER_BOTTOMLEFT:
                                _gdk_pen_arc (cr, x + radius, y - radius, radius, G_PI * 1/2, G_PI);
                                break;

                        default:
                                /* A bitfield and not a sane value ... */
                                g_assert_not_reached ();
                                _gdk_pen_line_to (cr, x, y);
                                return;
                }
        }
}

void
ge_cairo_rounded_rectangle (GdkPen *cr,
                                 double x, double y, double w, double h,
                                 double radius, CairoCorners corners)
{
        g_return_if_fail (cr != NULL);

        if (radius < 0.0001 || corners == CR_CORNER_NONE)
        {
                _gdk_pen_rectangle (cr, x, y, w, h);
                return;
        }
#ifdef DEVELOPMENT
        if ((corners == CR_CORNER_ALL) && (radius > w / 2.0 || radius > h / 2.0))
                g_warning ("Radius is too large for width/height in ge_rounded_rectangle.\n");
        else if (radius > w || radius > h) /* This isn't perfect. Assumes that only one corner is set. */
                g_warning ("Radius is too large for width/height in ge_rounded_rectangle.\n");
#endif

        if (corners & CR_CORNER_TOPLEFT)
                _gdk_pen_move_to (cr, x+radius, y);
        else
                _gdk_pen_move_to (cr, x, y);

        if (corners & CR_CORNER_TOPRIGHT)
                _gdk_pen_arc (cr, x+w-radius, y+radius, radius, G_PI * 1.5, G_PI * 2);
        else
                _gdk_pen_line_to (cr, x+w, y);

        if (corners & CR_CORNER_BOTTOMRIGHT)
                _gdk_pen_arc (cr, x+w-radius, y+h-radius, radius, 0, G_PI * 0.5);
        else
                _gdk_pen_line_to (cr, x+w, y+h);

        if (corners & CR_CORNER_BOTTOMLEFT)
                _gdk_pen_arc (cr, x+radius,   y+h-radius, radius, G_PI * 0.5, G_PI);
        else
                _gdk_pen_line_to (cr, x, y+h);

        if (corners & CR_CORNER_TOPLEFT)
                _gdk_pen_arc (cr, x+radius,   y+radius,   radius, G_PI, G_PI * 1.5);
        else
                _gdk_pen_line_to (cr, x, y);
}

void
ge_cairo_stroke_rectangle (GdkPen *cr, double x, double y, double w, double h)
{
        _gdk_pen_rectangle (cr, x, y, w, h);
        _gdk_pen_stroke (cr);
}

void
ge_cairo_inner_rectangle (GdkPen *cr,
                          double x, double y,
                          double width, double height)
{
        double line_width = _gdk_pen_get_line_width (cr);

        _gdk_pen_rectangle (cr, x + line_width / 2.0,
                                y + line_width / 2.0,
                                width - line_width,
                                height - line_width);
}

void
ge_cairo_inner_rounded_rectangle (GdkPen *cr,
                                  double x, double y,
                                  double width, double height,
                                  double radius, CairoCorners corners)
{
        double line_width = _gdk_pen_get_line_width (cr);

        ge_cairo_rounded_rectangle (cr,
                                    x + line_width / 2.0,
                                    y + line_width / 2.0,
                                    width - line_width,
                                    height - line_width,
                                    radius, corners);
}

void
ge_cairo_mirror (GdkPen      *cr,
                 CairoMirror  mirror,
                 gint        *x,
                 gint        *y,
                 gint        *width,
                 gint        *height)
{
        GdkPenMatrix matrix;

        _gdk_pen_matrix_init_identity (&matrix);

        _gdk_pen_translate (cr, *x, *y);
        *x = 0;
        *y = 0;

        if (mirror & CR_MIRROR_HORIZONTAL)
        {
                _gdk_pen_matrix_scale (&matrix, -1, 1);
                *x = -*width;
        }
        if (mirror & CR_MIRROR_VERTICAL)
        {
                _gdk_pen_matrix_scale (&matrix, 1, -1);
                *y = -*height;
        }

        _gdk_pen_transform (cr, &matrix);
}

void
ge_cairo_exchange_axis (GdkPen   *cr,
                        gint     *x,
                        gint     *y,
                        gint     *width,
                        gint     *height)
{
        gint tmp;
        GdkPenMatrix matrix;

        _gdk_pen_translate (cr, *x, *y);
        _gdk_pen_matrix_init (&matrix, 0, 1, 1, 0, 0, 0);

        _gdk_pen_transform (cr, &matrix);

        /* swap width/height */
        tmp = *width;
        *x = 0;
        *y = 0;
        *width = *height;
        *height = tmp;
}
