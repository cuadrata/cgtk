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
 * Images: pixels in client memory, for programs that want to get at
 * them directly. A colour image is 32 bits a pixel, 0x00RRGGBB in the
 * machine's byte order, with rows of width*4 bytes; a bitmap is one
 * byte a pixel, 0 or 1. There is no longer a shared or a normal kind
 * of image, since nothing is sent anywhere, but both names still work.
 */

#include <stdlib.h>
#include <string.h>
#include "gdk.h"
#include "gdkprivate.h"
#include "gdkframebuffer.h"

void
gdk_image_init (void)
{
}

void
gdk_image_exit (void)
{
}

static GdkImage *
gdk_image_alloc (GdkImageType  type,
                 GdkVisual    *visual,
                 gint          width,
                 gint          height,
                 gint          depth)
{
  GdkImagePrivate *private;
  GdkImage *image;
  gint bpp = depth == 1 ? 1 : 4;

  /* GdkImage keeps its sizes in 16 bits, as XImage's were. */
  if (width < 0 || height < 0 ||
      width > 65535 / bpp || height > 65535)
    return NULL;

  private = g_new0 (GdkImagePrivate, 1);
  image = (GdkImage *) private;

  image->type = type;
  image->visual = visual;
  image->byte_order = (G_BYTE_ORDER == G_LITTLE_ENDIAN) ? GDK_LSB_FIRST : GDK_MSB_FIRST;
  image->width = width;
  image->height = height;
  image->depth = depth;
  image->bpp = bpp;
  image->bpl = width * image->bpp;
  image->mem = g_malloc0 ((gsize) image->bpl * height);

  return image;
}

/*
 * A bitmap from data laid out as X bitmaps are: rows of bytes, a pixel
 * a bit, low bit first. The data is the image's to free afterwards,
 * as it was when X took it.
 */
GdkImage *
gdk_image_new_bitmap(GdkVisual *visual, gpointer data, gint w, gint h)
{
  GdkImage *image;
  gint stride = (w + 7) / 8;
  gint x, y;

  image = gdk_image_alloc (GDK_IMAGE_NORMAL, visual, w, h, 1);
  if (!image)
    {
      g_free (data);
      return NULL;
    }
  for (y = 0; y < h; y++)
    {
      const guchar *row = (const guchar *) data + y * stride;
      guchar *out = (guchar *) image->mem + y * image->bpl;

      for (x = 0; x < w; x++)
        out[x] = (row[x >> 3] >> (x & 7)) & 1;
    }
  g_free (data);

  return image;
}

GdkImage*
gdk_image_new (GdkImageType  type,
               GdkVisual    *visual,
               gint          width,
               gint          height)
{
  g_return_val_if_fail (visual != NULL, NULL);

  return gdk_image_alloc (type, visual, width, height, visual->depth);
}

/*
 * What a drawable holds. For a window the backend has no framebuffer
 * for -- the root, another program's -- the backend reads the screen.
 */
GdkImage*
gdk_image_get (GdkWindow *window,
               gint       x,
               gint       y,
               gint       width,
               gint       height)
{
  GdkWindowPrivate *private;
  GdkFramebuffer *fb;
  GdkImage *image;
  gint row, ox, oy;

  g_return_val_if_fail (window != NULL, NULL);

  private = (GdkWindowPrivate *) window;
  if (private->destroyed)
    return NULL;

  if (private->window_type == GDK_WINDOW_ROOT ||
      private->window_type == GDK_WINDOW_FOREIGN)
    {
      image = gdk_image_alloc (GDK_IMAGE_NORMAL, gdk_visual_get_system (),
                               width, height, gdk_visual_get_system ()->depth);
      if (image)
        _gdk_backend_get_image (window, x, y, image);
      return image;
    }

  fb = _gdk_drawable_fb (window);
  if (!fb)
    return NULL;
  _gdk_window_native (private, &ox, &oy);

  image = gdk_image_alloc (GDK_IMAGE_NORMAL, gdk_visual_get_system (),
                           width, height, private->depth);
  if (!image)
    return NULL;
  for (row = 0; row < height; row++)
    {
      gint sy = (y + row + oy) * fb->scale;
      gint col;

      if (y + row < 0 || y + row >= private->height || sy < 0 || sy >= fb->height)
        continue;
      for (col = 0; col < width; col++)
        {
          gint sx = (x + col + ox) * fb->scale;
          guint32 pixel;

          if (x + col < 0 || x + col >= private->width || sx < 0 || sx >= fb->width)
            continue;
          pixel = fb->pixels[(gsize) sy * fb->stride + sx];
          if (image->bpp == 1)
            ((guchar *) image->mem)[row * image->bpl + col] = pixel & 1;
          else
            ((guint32 *) ((guchar *) image->mem + row * image->bpl))[col] = pixel;
        }
    }

  return image;
}

guint32
gdk_image_get_pixel (GdkImage *image,
                     gint x,
                     gint y)
{
  g_return_val_if_fail (image != NULL, 0);

  if (x < 0 || y < 0 || x >= image->width || y >= image->height)
    return 0;

  if (image->bpp == 1)
    return ((const guchar *) image->mem)[y * image->bpl + x];
  return ((const guint32 *) ((const guchar *) image->mem + y * image->bpl))[x];
}

void
gdk_image_put_pixel (GdkImage *image,
                     gint x,
                     gint y,
                     guint32 pixel)
{
  g_return_if_fail (image != NULL);

  if (x < 0 || y < 0 || x >= image->width || y >= image->height)
    return;

  if (image->bpp == 1)
    ((guchar *) image->mem)[y * image->bpl + x] = pixel & 1;
  else
    ((guint32 *) ((guchar *) image->mem + y * image->bpl))[x] = pixel & 0xffffff;
}

void
gdk_image_destroy (GdkImage *image)
{
  g_return_if_fail (image != NULL);

  if (((GdkImagePrivate *) image)->impl)
    _gdk_backend_image_free ((GdkImagePrivate *) image);
  g_free (image->mem);
  g_free (image);
}
