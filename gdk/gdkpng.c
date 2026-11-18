/* GTK - The GIMP Toolkit
 * PNG files, read into pixels with libpng
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

#include "config.h"

#include <string.h>
#include <png.h>

#include "gdk.h"

/* An image larger than this either way, or with more pixels than
 * this, is refused: a file of a few bytes can say it is of any size,
 * and the pixels are four bytes each. */
#define MAX_SIDE   32767
#define MAX_PIXELS (64 * 1024 * 1024)

GQuark
gdk_png_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("gdk-png-error-quark");

  return quark;
}

/* The pixels of a PNG file that is in memory: a byte each of red,
 * green, blue and alpha, in rows of width * 4 bytes from the top.
 * Whatever the file has, 16 bits a channel, a palette, grey or
 * interlaced, comes out so. A file keeps its colours straight, not
 * multiplied by the alpha, and they are multiplied here unless they
 * are wanted as they are.
 */
static guchar*
png_read (const guchar  *data,
          gsize          length,
          gint          *width,
          gint          *height,
          gboolean       straight,
          GError       **error)
{
  png_image image;
  guchar *pixels, *p;
  gsize n;

  memset (&image, 0, sizeof image);
  image.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_memory (&image, data, length))
    {
      g_set_error (error, GDK_PNG_ERROR, GDK_PNG_ERROR_FAILED, "%s", image.message);
      png_image_free (&image);
      return NULL;
    }
  n = (gsize) image.width * image.height;
  if (image.width == 0 || image.height == 0 ||
      image.width > MAX_SIDE || image.height > MAX_SIDE || n > MAX_PIXELS)
    {
      g_set_error (error, GDK_PNG_ERROR, GDK_PNG_ERROR_TOO_LARGE,
                   "The image is too large (%lu by %lu)",
                   (gulong) image.width, (gulong) image.height);
      png_image_free (&image);
      return NULL;
    }

  image.format = PNG_FORMAT_RGBA;
  pixels = g_malloc (n * 4);
  if (!png_image_finish_read (&image, NULL, pixels, 0, NULL))
    {
      g_set_error (error, GDK_PNG_ERROR, GDK_PNG_ERROR_FAILED, "%s", image.message);
      png_image_free (&image);
      g_free (pixels);
      return NULL;
    }

  if (!straight)
    for (p = pixels; n > 0; n--, p += 4)
      if (p[3] != 255)
        {
          p[0] = (p[0] * p[3] + 127) / 255;
          p[1] = (p[1] * p[3] + 127) / 255;
          p[2] = (p[2] * p[3] + 127) / 255;
        }

  *width = image.width;
  *height = image.height;

  return pixels;
}

static guchar*
png_read_file (const gchar  *filename,
               gint         *width,
               gint         *height,
               gboolean      straight,
               GError      **error)
{
  gchar *contents;
  guchar *pixels;
  gsize length;

  if (!g_file_get_contents (filename, &contents, &length, error))
    return NULL;
  pixels = png_read ((const guchar *) contents, length, width, height, straight, error);
  g_free (contents);

  return pixels;
}

/* The pixels of a PNG file that is in memory, as
 * gdk_draw_rgba_image_scaled() takes them: the colours premultiplied
 * by the alpha. NULL with the error set if it is no PNG file, is
 * damaged, or is too large; g_free() otherwise.
 */
guchar*
gdk_png_load_from_data (const guchar  *data,
                        gsize          length,
                        gint          *width,
                        gint          *height,
                        GError       **error)
{
  g_return_val_if_fail (data != NULL, NULL);
  g_return_val_if_fail (width != NULL && height != NULL, NULL);

  return png_read (data, length, width, height, FALSE, error);
}

/* The same of a file. A file that cannot be read is a G_FILE_ERROR,
 * and one that is read and is not a picture a GDK_PNG_ERROR.
 */
guchar*
gdk_png_load (const gchar  *filename,
              gint         *width,
              gint         *height,
              GError      **error)
{
  g_return_val_if_fail (filename != NULL, NULL);
  g_return_val_if_fail (width != NULL && height != NULL, NULL);

  return png_read_file (filename, width, height, FALSE, error);
}

/* The same two with straight alpha: the colours as the file has them,
 * not multiplied by the alpha, to the bit, for a program that lays
 * its pictures over one another itself and reckons that way, or hands
 * them to something that does. They are not for
 * gdk_draw_rgba_image_scaled(). What colour a pixel that is wholly
 * clear has is whatever the file says.
 */
guchar*
gdk_png_load_from_data_straight (const guchar  *data,
                                 gsize          length,
                                 gint          *width,
                                 gint          *height,
                                 GError       **error)
{
  g_return_val_if_fail (data != NULL, NULL);
  g_return_val_if_fail (width != NULL && height != NULL, NULL);

  return png_read (data, length, width, height, TRUE, error);
}

guchar*
gdk_png_load_straight (const gchar  *filename,
                       gint         *width,
                       gint         *height,
                       GError      **error)
{
  g_return_val_if_fail (filename != NULL, NULL);
  g_return_val_if_fail (width != NULL && height != NULL, NULL);

  return png_read_file (filename, width, height, TRUE, error);
}
