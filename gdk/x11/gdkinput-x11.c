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
#include "gdk.h"
#include "gdkx.h"
#include "gdkprivate-x11.h"
#include "gdkbackend-x11.h"


/* The core pointer's motion history: the server's, for window's X window. */
GdkTimeCoord *
_gdk_x11_input_motion_events (GdkWindow *window,
                              guint32    start,
                              guint32    stop,
                              gint      *nevents_return)
{
  GdkWindowPrivate *window_private = (GdkWindowPrivate *) window;
  XTimeCoord *xcoords;
  GdkTimeCoord *coords;
  int i;

  xcoords = XGetMotionEvents (gdk_display,
                              GDK_WINDOW_XID (window_private),
                              start, stop, nevents_return);
  if (!xcoords)
    {
      *nevents_return = 0;
      return NULL;
    }

  coords = g_new (GdkTimeCoord, *nevents_return);
  for (i=0; i<*nevents_return; i++)
    {
      coords[i].time = xcoords[i].time;
      coords[i].x = xcoords[i].x;
      coords[i].y = xcoords[i].y;
      coords[i].pressure = 0.5;
      coords[i].xtilt = 0.0;
      coords[i].ytilt = 0.0;
    }
  XFree (xcoords);

  return coords;
}
