/* GDK - The GIMP Drawing Kit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
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

#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include "gdk.h"
#include "gdkprivate-x11.h"
#include "gdkbackend-x11.h"


static GdkCursor *
gdk_cursor_wrap (Cursor xcursor, GdkCursorType type)
{
  GdkCursorPrivate *private;
  GdkCursorImpl *impl;
  GdkCursor *cursor;

  impl = g_new (GdkCursorImpl, 1);
  impl->xdisplay = gdk_display;
  impl->xcursor = xcursor;
  impl->ref_count = 1;
  private = g_new (GdkCursorPrivate, 1);
  private->impl = impl;
  cursor = (GdkCursor*) private;
  cursor->type = type;

  return cursor;
}

GdkCursor*
_gdk_x11_cursor_new (GdkCursorType cursor_type)
{
  return gdk_cursor_wrap (XCreateFontCursor (gdk_display, cursor_type),
                          cursor_type);
}

GdkCursor*
_gdk_x11_cursor_new_from_pixmap (GdkPixmap *source, GdkPixmap *mask, GdkColor *fg, GdkColor *bg, gint x, gint y)
{
  Pixmap source_pixmap, mask_pixmap;
  GdkPixmap *source_temp, *mask_temp;
  Cursor xcursor;
  XColor xfg, xbg;

  /* At the display's scale, or it would be a very small cursor. */
  source_pixmap = _gdk_x11_bitmap_xid_at_scale (source, &source_temp);
  mask_pixmap   = _gdk_x11_bitmap_xid_at_scale (mask, &mask_temp);
  x = GDK_X11_TO_X (x);
  y = GDK_X11_TO_X (y);
  xfg.pixel = fg->pixel;
  xfg.red = fg->red;
  xfg.blue = fg->blue;
  xfg.green = fg->green;
  xbg.pixel = bg->pixel;
  xbg.red = bg->red;
  xbg.blue = bg->blue;
  xbg.green = bg->green;

  xcursor = XCreatePixmapCursor (gdk_display, source_pixmap, mask_pixmap, &xfg, &xbg, x, y);
  if (source_temp)
    gdk_pixmap_unref (source_temp);
  if (mask_temp)
    gdk_pixmap_unref (mask_temp);

  return gdk_cursor_wrap (xcursor, GDK_CURSOR_IS_PIXMAP);
}

void
_gdk_x11_cursor_destroy (GdkCursor *cursor)
{
  GdkCursorPrivate *private;

  g_return_if_fail (cursor != NULL);

  private = (GdkCursorPrivate *) cursor;
  _gdk_x11_cursor_impl_unref (private->impl);
  g_free (private);
}

GdkCursorImpl *
_gdk_x11_cursor_impl_ref (GdkCursorImpl *impl)
{
  impl->ref_count++;
  return impl;
}

void
_gdk_x11_cursor_impl_unref (GdkCursorImpl *impl)
{
  if (--impl->ref_count > 0)
    return;
  XFreeCursor (impl->xdisplay, impl->xcursor);
  g_free (impl);
}

Cursor
gdk_x11_cursor_xcursor (GdkCursor *cursor)
{
  g_return_val_if_fail (cursor != NULL, None);

  return GDK_CURSOR_IMPL (cursor)->xcursor;
}
