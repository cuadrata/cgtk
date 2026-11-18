/* GDK - The GIMP Drawing Kit
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

#include <string.h>
#include "gdk.h"
#include "gdkprivate.h"
#include "gdkframebuffer.h"

GdkFramebuffer *
_gdk_framebuffer_new (gint width, gint height, gint depth, gint scale)
{
  GdkFramebuffer *fb;

  g_return_val_if_fail (scale >= 1, NULL);

  width = MAX (width, 1) * scale;
  height = MAX (height, 1) * scale;

  fb = g_new (GdkFramebuffer, 1);
  fb->width = width;
  fb->height = height;
  fb->stride = width;
  fb->depth = depth;
  fb->scale = scale;
  fb->pixels = g_malloc0 ((gsize) width * height * sizeof (guint32));

  return fb;
}

void
_gdk_framebuffer_free (GdkFramebuffer *fb)
{
  if (!fb)
    return;
  g_free (fb->pixels);
  g_free (fb);
}
