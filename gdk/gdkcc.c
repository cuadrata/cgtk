/* GDK - The GIMP Drawing Kit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 * Copyright (C) 2026 The CGTK authors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, see
 * <https://www.gnu.org/licenses/>.
 */

/*
 * Modified by the GTK+ Team and others 1997-1999. See the AUTHORS
 * file for a list of people on the GTK+ Team. See the ChangeLog
 * files for a list of changes. These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/.
 */

/*
 * Colour contexts. These found the nearest pixel for a colour on a
 * screen with few of them; with a true colour visual every colour has
 * its own pixel and nothing can fail, so what is left is the
 * arithmetic.
 */

#include <string.h>
#include "gdk.h"
#include "gdkprivate.h"

static gulong
pixel_from_rgb (gushort red, gushort green, gushort blue)
{
  return ((gulong) (red >> 8) << 16) | ((gulong) (green >> 8) << 8) | (blue >> 8);
}

GdkColorContext *
gdk_color_context_new (GdkVisual   *visual,
                       GdkColormap *colormap)
{
  GdkColorContextPrivate *ccp;
  GdkColorContext *cc;

  g_return_val_if_fail (visual != NULL, NULL);
  g_return_val_if_fail (colormap != NULL, NULL);

  ccp = g_new0 (GdkColorContextPrivate, 1);
  cc = (GdkColorContext *) ccp;

  cc->visual = visual;
  cc->colormap = colormap;
  cc->clut = NULL;
  cc->cmap = NULL;
  cc->mode = GDK_CC_MODE_TRUE;
  cc->need_to_free_colormap = FALSE;
  cc->num_colors = cc->max_colors = 1 << 24;
  cc->num_allocated = 0;
  cc->color_hash = NULL;
  cc->palette = NULL;
  cc->num_palette = 0;
  cc->fast_dither = NULL;

  cc->masks.red = visual->red_mask;
  cc->masks.green = visual->green_mask;
  cc->masks.blue = visual->blue_mask;
  cc->shifts.red = visual->red_shift;
  cc->shifts.green = visual->green_shift;
  cc->shifts.blue = visual->blue_shift;
  cc->bits.red = visual->red_prec;
  cc->bits.green = visual->green_prec;
  cc->bits.blue = visual->blue_prec;

  return cc;
}

GdkColorContext *
gdk_color_context_new_mono (GdkVisual   *visual,
                            GdkColormap *colormap)
{
  GdkColorContext *cc = gdk_color_context_new (visual, colormap);

  if (cc)
    cc->mode = GDK_CC_MODE_BW;
  return cc;
}

void
gdk_color_context_free (GdkColorContext *cc)
{
  g_return_if_fail (cc != NULL);

  g_free (cc->palette);
  g_free (cc);
}

gulong
gdk_color_context_get_pixel (GdkColorContext *cc,
                             gushort          red,
                             gushort          green,
                             gushort          blue,
                             gint            *failed)
{
  g_return_val_if_fail (cc != NULL, 0);
  g_return_val_if_fail (failed != NULL, 0);

  *failed = FALSE;
  if (cc->mode == GDK_CC_MODE_BW)
    return (red + green + blue) / 3 >= 32768 ? 0xffffff : 0;
  return pixel_from_rgb (red, green, blue);
}

void
gdk_color_context_get_pixels (GdkColorContext *cc,
                              gushort         *reds,
                              gushort         *greens,
                              gushort         *blues,
                              gint             ncolors,
                              gulong          *colors,
                              gint            *nallocated)
{
  gint i, failed;

  g_return_if_fail (cc != NULL);

  for (i = 0; i < ncolors; i++)
    colors[i] = gdk_color_context_get_pixel (cc, reds[i], greens[i], blues[i],
                                             &failed);
  if (nallocated)
    *nallocated = ncolors;
}

void
gdk_color_context_get_pixels_incremental (GdkColorContext *cc,
                                          gushort         *reds,
                                          gushort         *greens,
                                          gushort         *blues,
                                          gint             ncolors,
                                          gint            *used,
                                          gulong          *colors,
                                          gint            *nallocated)
{
  gint i, failed, n = 0;

  g_return_if_fail (cc != NULL);

  for (i = 0; i < ncolors; i++)
    if (used[i])
      {
        colors[i] = gdk_color_context_get_pixel (cc, reds[i], greens[i],
                                                 blues[i], &failed);
        n++;
      }
  if (nallocated)
    *nallocated = n;
}

gint
gdk_color_context_query_color (GdkColorContext *cc,
                               GdkColor        *color)
{
  return gdk_color_context_query_colors (cc, color, 1);
}

gint
gdk_color_context_query_colors (GdkColorContext *cc,
                                GdkColor        *colors,
                                gint             num_colors)
{
  gint i;

  g_return_val_if_fail (cc != NULL, 0);
  g_return_val_if_fail (colors != NULL, 0);

  for (i = 0; i < num_colors; i++)
    _gdk_color_from_pixel (&colors[i]);

  return 1;
}

gint
gdk_color_context_add_palette (GdkColorContext *cc,
                               GdkColor        *palette,
                               gint             num_palette)
{
  gint i;

  g_return_val_if_fail (cc != NULL, 0);

  g_free (cc->palette);
  cc->palette = NULL;
  cc->num_palette = 0;
  if (num_palette <= 0)
    return 0;

  cc->palette = g_new (GdkColor, num_palette);
  for (i = 0; i < num_palette; i++)
    {
      cc->palette[i] = palette[i];
      cc->palette[i].pixel = pixel_from_rgb (palette[i].red, palette[i].green,
                                             palette[i].blue);
    }
  cc->num_palette = num_palette;

  return num_palette;
}

void
gdk_color_context_init_dither (GdkColorContext *cc)
{
}

void
gdk_color_context_free_dither (GdkColorContext *cc)
{
}

/* The palette entry nearest a colour. */
static gint
palette_index (GdkColorContext *cc, gint red, gint green, gint blue)
{
  gint i, best = 0;
  glong best_dist = -1;

  for (i = 0; i < cc->num_palette; i++)
    {
      glong dr = (cc->palette[i].red >> 8) - red;
      glong dg = (cc->palette[i].green >> 8) - green;
      glong db = (cc->palette[i].blue >> 8) - blue;
      glong dist = dr * dr + dg * dg + db * db;

      if (best_dist < 0 || dist < best_dist)
        {
          best_dist = dist;
          best = i;
        }
    }
  return best;
}

gulong
gdk_color_context_get_pixel_from_palette (GdkColorContext *cc,
                                          gushort         *red,
                                          gushort         *green,
                                          gushort         *blue,
                                          gint            *failed)
{
  gint i;

  g_return_val_if_fail (cc != NULL, 0);
  g_return_val_if_fail (failed != NULL, 0);

  if (cc->num_palette == 0)
    {
      *failed = TRUE;
      return 0;
    }
  *failed = FALSE;
  i = palette_index (cc, *red >> 8, *green >> 8, *blue >> 8);
  *red = cc->palette[i].red;
  *green = cc->palette[i].green;
  *blue = cc->palette[i].blue;
  return cc->palette[i].pixel;
}

guchar
gdk_color_context_get_index_from_palette (GdkColorContext *cc,
                                          gint            *red,
                                          gint            *green,
                                          gint            *blue,
                                          gint            *failed)
{
  gint i;

  g_return_val_if_fail (cc != NULL, 0);
  g_return_val_if_fail (failed != NULL, 0);

  if (cc->num_palette == 0)
    {
      *failed = TRUE;
      return 0;
    }
  *failed = FALSE;
  i = palette_index (cc, *red, *green, *blue);
  *red = cc->palette[i].red >> 8;
  *green = cc->palette[i].green >> 8;
  *blue = cc->palette[i].blue >> 8;
  return i;
}
