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
 * Colormaps and colours. With one true colour visual a colour's pixel
 * is its red, green and blue, so allocating always succeeds and nothing
 * is ever scarce; the colormap functions keep their shapes for the
 * programs that call them. The colours array of a colormap is kept
 * for gdk_colormap_change and friends to look at.
 */

#include <stdlib.h>
#include <string.h>
#include "gdk.h"
#include "gdkprivate.h"

static GdkColormap *system_colormap = NULL;

static guint32
color_pixel (const GdkColor *color)
{
  return ((guint32) (color->red >> 8) << 16) |
    ((guint32) (color->green >> 8) << 8) |
    (guint32) (color->blue >> 8);
}

static void
color_from_pixel (GdkColor *color)
{
  guint32 pixel = color->pixel;

  color->red = (pixel >> 16 & 0xff) * 0x101;
  color->green = (pixel >> 8 & 0xff) * 0x101;
  color->blue = (pixel & 0xff) * 0x101;
}

GdkColormap*
gdk_colormap_new (GdkVisual *visual,
                  gboolean   private_cmap)
{
  GdkColormap *colormap;
  GdkColormapPrivate *private;

  g_return_val_if_fail (visual != NULL, NULL);

  private = g_new (GdkColormapPrivate, 1);
  colormap = (GdkColormap*) private;

  private->visual = visual;
  private->ref_count = 1;
  private->private_val = private_cmap;

  colormap->size = visual->colormap_size;
  colormap->colors = g_new0 (GdkColor, colormap->size);

  return colormap;
}

GdkColormap*
gdk_colormap_ref (GdkColormap *cmap)
{
  GdkColormapPrivate *private = (GdkColormapPrivate *)cmap;

  g_return_val_if_fail (cmap != NULL, NULL);

  private->ref_count += 1;
  return cmap;
}

void
gdk_colormap_unref (GdkColormap *cmap)
{
  GdkColormapPrivate *private = (GdkColormapPrivate *)cmap;

  g_return_if_fail (cmap != NULL);
  g_return_if_fail (private->ref_count > 0);

  private->ref_count -= 1;
  if (private->ref_count == 0)
    {
      if (cmap == system_colormap)
        system_colormap = NULL;
      g_free (cmap->colors);
      g_free (private);
    }
}

GdkVisual *
gdk_colormap_get_visual (GdkColormap *colormap)
{
  g_return_val_if_fail (colormap != NULL, NULL);

  return ((GdkColormapPrivate *) colormap)->visual;
}

void
gdk_colormap_sync (GdkColormap *colormap,
                   gboolean     force)
{
}

GdkColormap*
gdk_colormap_get_system (void)
{
  if (!system_colormap)
    system_colormap = gdk_colormap_new (gdk_visual_get_system (), FALSE);

  return system_colormap;
}

gint
gdk_colormap_get_system_size (void)
{
  return gdk_visual_get_system ()->colormap_size;
}

void
gdk_colormap_change (GdkColormap *colormap,
                     gint         ncolors)
{
  gint i;

  g_return_if_fail (colormap != NULL);

  for (i = 0; i < ncolors && i < colormap->size; i++)
    colormap->colors[i].pixel = color_pixel (&colormap->colors[i]);
}

void
gdk_colors_store (GdkColormap   *colormap,
                  GdkColor      *colors,
                  gint           ncolors)
{
  gint i;

  g_return_if_fail (colormap != NULL);

  for (i = 0; i < ncolors; i++)
    colors[i].pixel = color_pixel (&colors[i]);
}

/* Writeable cells with planes: there are none in a true colour visual. */
gboolean
gdk_colors_alloc (GdkColormap   *colormap,
                  gboolean       contiguous,
                  gulong        *planes,
                  gint           nplanes,
                  gulong        *pixels,
                  gint           npixels)
{
  return FALSE;
}

GdkColor*
gdk_color_copy (const GdkColor *color)
{
  GdkColor *new_color;

  g_return_val_if_fail (color != NULL, NULL);

  new_color = g_new (GdkColor, 1);
  *new_color = *color;
  return new_color;
}

void
gdk_color_free (GdkColor *color)
{
  g_return_if_fail (color != NULL);

  g_free (color);
}

gboolean
gdk_color_white (GdkColormap *colormap,
                 GdkColor    *color)
{
  g_return_val_if_fail (colormap != NULL, FALSE);
  g_return_val_if_fail (color != NULL, FALSE);

  color->red = color->green = color->blue = 65535;
  color->pixel = 0xffffff;
  return TRUE;
}

gboolean
gdk_color_black (GdkColormap *colormap,
                 GdkColor    *color)
{
  g_return_val_if_fail (colormap != NULL, FALSE);
  g_return_val_if_fail (color != NULL, FALSE);

  color->red = color->green = color->blue = 0;
  color->pixel = 0;
  return TRUE;
}

/*
 * A colour by name or as #rgb, #rrggbb, #rrrgggbbb or #rrrrggggbbbb;
 * the names are the X colour names, kept in gdkcolornames.h.
 */
#include "gdkcolornames.h"

static gint
color_name_compare (const void *a, const void *b)
{
  return g_strcasecmp (((const GdkColorName *) a)->name,
                       ((const GdkColorName *) b)->name);
}

gboolean
gdk_color_parse (const gchar *spec,
                 GdkColor *color)
{
  g_return_val_if_fail (spec != NULL, FALSE);
  g_return_val_if_fail (color != NULL, FALSE);

  if (spec[0] == '#')
    {
      gint len = strlen (spec + 1), per;
      guint value[3];
      gint i;

      if (len % 3 != 0 || len == 0 || len > 12)
        return FALSE;
      per = len / 3;
      for (i = 0; i < 3; i++)
        {
          gchar buf[5];
          gchar *end;

          memcpy (buf, spec + 1 + i * per, per);
          buf[per] = '\0';
          value[i] = strtoul (buf, &end, 16);
          if (*end)
            return FALSE;
          /* Scale to 16 bits: replicate the digits given. */
          switch (per)
            {
            case 1: value[i] *= 0x1111; break;
            case 2: value[i] *= 0x101; break;
            case 3: value[i] = (value[i] << 4) | (value[i] >> 8); break;
            }
        }
      color->red = value[0];
      color->green = value[1];
      color->blue = value[2];
      color->pixel = color_pixel (color);
      return TRUE;
    }
  else
    {
      GdkColorName key, *found;
      gchar *name = g_strdup (spec), *p, *q;

      /* The names are matched without their spaces or case. */
      for (p = q = name; *p; p++)
        if (*p != ' ')
          *q++ = *p;
      *q = '\0';
      key.name = name;
      found = bsearch (&key, gdk_color_names, sizeof (gdk_color_names) / sizeof (gdk_color_names[0]),
                       sizeof (GdkColorName), color_name_compare);
      g_free (name);
      if (!found)
        return FALSE;
      color->red = found->red * 0x101;
      color->green = found->green * 0x101;
      color->blue = found->blue * 0x101;
      color->pixel = color_pixel (color);
      return TRUE;
    }
}

void
gdk_colors_free (GdkColormap *colormap,
                 gulong      *pixels,
                 gint         npixels,
                 gulong       planes)
{
}

void
gdk_colormap_free_colors (GdkColormap *colormap,
                          GdkColor    *colors,
                          gint         ncolors)
{
}

gint
gdk_colormap_alloc_colors (GdkColormap *colormap,
                           GdkColor    *colors,
                           gint         ncolors,
                           gboolean     writeable,
                           gboolean     best_match,
                           gboolean    *success)
{
  gint i;

  g_return_val_if_fail (colormap != NULL, ncolors);
  g_return_val_if_fail (colors != NULL, ncolors);

  for (i = 0; i < ncolors; i++)
    {
      colors[i].pixel = color_pixel (&colors[i]);
      if (success)
        success[i] = TRUE;
    }

  return 0;
}

gboolean
gdk_colormap_alloc_color (GdkColormap *colormap,
                          GdkColor    *color,
                          gboolean     writeable,
                          gboolean     best_match)
{
  gboolean success = FALSE;

  gdk_colormap_alloc_colors (colormap, color, 1, writeable, best_match,
                             &success);

  return success;
}

gboolean
gdk_color_alloc (GdkColormap *colormap,
                 GdkColor    *color)
{
  return gdk_colormap_alloc_color (colormap, color, FALSE, TRUE);
}

gboolean
gdk_color_change (GdkColormap *colormap,
                  GdkColor    *color)
{
  g_return_val_if_fail (colormap != NULL, FALSE);
  g_return_val_if_fail (color != NULL, FALSE);

  color->pixel = color_pixel (color);
  return TRUE;
}

guint
gdk_color_hash (const GdkColor *colora,
                const GdkColor *colorb)
{
  return ((colora->red) +
          (colora->green << 11) +
          (colora->blue << 22) +
          (colora->blue >> 6));
}

gboolean
gdk_color_equal (const GdkColor *colora,
                 const GdkColor *colorb)
{
  g_return_val_if_fail (colora != NULL, FALSE);
  g_return_val_if_fail (colorb != NULL, FALSE);

  return ((colora->red == colorb->red) &&
          (colora->green == colorb->green) &&
          (colora->blue == colorb->blue));
}

/* The colour a pixel is, for the colour context. */
void
_gdk_color_from_pixel (GdkColor *color)
{
  color_from_pixel (color);
}
