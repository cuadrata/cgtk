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
 * GdkRgb: RGB buffers onto drawables. Once the whole business of
 * matching a screen's visual and dithering to it; now every drawable
 * takes 0x00RRGGBB pixels, so a buffer is converted row by row into
 * an image and drawn. The dither argument is accepted and ignored,
 * and the functions that used to describe the colour cube still
 * answer, for programs that ask.
 */

#include <string.h>
#include "gdk.h"
#include "gdkprivate.h"
#include "gdkrgb.h"

void
gdk_rgb_init (void)
{
}

gulong
gdk_rgb_xpixel_from_rgb (guint32 rgb)
{
  return rgb & 0xffffff;
}

void
gdk_rgb_gc_set_foreground (GdkGC *gc, guint32 rgb)
{
  GdkColor color;

  color.pixel = gdk_rgb_xpixel_from_rgb (rgb);
  gdk_gc_set_foreground (gc, &color);
}

void
gdk_rgb_gc_set_background (GdkGC *gc, guint32 rgb)
{
  GdkColor color;

  color.pixel = gdk_rgb_xpixel_from_rgb (rgb);
  gdk_gc_set_background (gc, &color);
}

/*
 * The common path: a row converter fills an image the size of the
 * area, and the image is drawn.
 */
typedef void (*GdkRgbRowFunc) (guint32 *out, const guchar *in, gint width,
                               gpointer data);

static void
gdk_rgb_draw (GdkDrawable   *drawable,
              GdkGC         *gc,
              gint           x,
              gint           y,
              gint           width,
              gint           height,
              const guchar  *buf,
              gint           rowstride,
              GdkRgbRowFunc  func,
              gpointer       data)
{
  GdkImage *image;
  gint row, band, rows;

  g_return_if_fail (drawable != NULL);
  g_return_if_fail (gc != NULL);
  g_return_if_fail (buf != NULL);

  if (width <= 0 || height <= 0)
    return;

  /* In bands of rows, as an image holds at most 65535 of them. */
  for (band = 0; band < height; band += rows)
    {
      rows = MIN (height - band, 1024);
      image = gdk_image_new (GDK_IMAGE_NORMAL, gdk_rgb_get_visual (), width, rows);
      if (!image)
        {
          g_warning ("gdk_draw_rgb_image: %d pixels is too wide", width);
          return;
        }
      for (row = 0; row < rows; row++)
        func ((guint32 *) ((guchar *) image->mem + row * image->bpl),
              buf + (band + row) * rowstride, width, data);
      gdk_draw_image (drawable, gc, image, 0, 0, x, y + band, width, rows);
      gdk_image_destroy (image);
    }
}

static void
rgb_row (guint32 *out, const guchar *in, gint width, gpointer data)
{
  while (width-- > 0)
    {
      *out++ = (guint32) in[0] << 16 | (guint32) in[1] << 8 | in[2];
      in += 3;
    }
}

void
gdk_draw_rgb_image (GdkDrawable *drawable,
                    GdkGC *gc,
                    gint x,
                    gint y,
                    gint width,
                    gint height,
                    GdkRgbDither dith,
                    guchar *rgb_buf,
                    gint rowstride)
{
  gdk_rgb_draw (drawable, gc, x, y, width, height, rgb_buf, rowstride,
                rgb_row, NULL);
}

void
gdk_draw_rgb_image_dithalign (GdkDrawable *drawable,
                              GdkGC *gc,
                              gint x,
                              gint y,
                              gint width,
                              gint height,
                              GdkRgbDither dith,
                              guchar *rgb_buf,
                              gint rowstride,
                              gint xdith,
                              gint ydith)
{
  gdk_rgb_draw (drawable, gc, x, y, width, height, rgb_buf, rowstride,
                rgb_row, NULL);
}

static void
rgb_32_row (guint32 *out, const guchar *in, gint width, gpointer data)
{
  while (width-- > 0)
    {
      *out++ = (guint32) in[0] << 16 | (guint32) in[1] << 8 | in[2];
      in += 4;
    }
}

void
gdk_draw_rgb_32_image (GdkDrawable *drawable,
                       GdkGC *gc,
                       gint x,
                       gint y,
                       gint width,
                       gint height,
                       GdkRgbDither dith,
                       guchar *buf,
                       gint rowstride)
{
  gdk_rgb_draw (drawable, gc, x, y, width, height, buf, rowstride,
                rgb_32_row, NULL);
}

static void
gray_row (guint32 *out, const guchar *in, gint width, gpointer data)
{
  while (width-- > 0)
    {
      guint32 g = *in++;
      *out++ = g << 16 | g << 8 | g;
    }
}

void
gdk_draw_gray_image (GdkDrawable *drawable,
                     GdkGC *gc,
                     gint x,
                     gint y,
                     gint width,
                     gint height,
                     GdkRgbDither dith,
                     guchar *buf,
                     gint rowstride)
{
  gdk_rgb_draw (drawable, gc, x, y, width, height, buf, rowstride,
                gray_row, NULL);
}

GdkRgbCmap *
gdk_rgb_cmap_new (guint32 *colors, gint n_colors)
{
  GdkRgbCmap *cmap;
  gint i;

  g_return_val_if_fail (n_colors >= 0, NULL);
  g_return_val_if_fail (n_colors <= 256, NULL);

  cmap = g_new (GdkRgbCmap, 1);
  memset (cmap, 0, sizeof (GdkRgbCmap));
  for (i = 0; i < n_colors; i++)
    {
      cmap->colors[i] = colors[i] & 0xffffff;
      cmap->lut[i] = i;
    }

  return cmap;
}

void
gdk_rgb_cmap_free (GdkRgbCmap *cmap)
{
  g_free (cmap);
}

static void
indexed_row (guint32 *out, const guchar *in, gint width, gpointer data)
{
  GdkRgbCmap *cmap = data;

  while (width-- > 0)
    *out++ = cmap->colors[*in++];
}

void
gdk_draw_indexed_image (GdkDrawable *drawable,
                        GdkGC *gc,
                        gint x,
                        gint y,
                        gint width,
                        gint height,
                        GdkRgbDither dith,
                        guchar *buf,
                        gint rowstride,
                        GdkRgbCmap *cmap)
{
  g_return_if_fail (cmap != NULL);

  gdk_rgb_draw (drawable, gc, x, y, width, height, buf, rowstride,
                indexed_row, cmap);
}

gboolean
gdk_rgb_ditherable (void)
{
  return FALSE;
}

void
gdk_rgb_set_verbose (gboolean verbose)
{
}

void
gdk_rgb_set_install (gboolean install)
{
}

void
gdk_rgb_set_min_colors (gint min_colors)
{
}

GdkColormap *
gdk_rgb_get_cmap (void)
{
  return gdk_colormap_get_system ();
}

GdkVisual *
gdk_rgb_get_visual (void)
{
  return gdk_visual_get_system ();
}
