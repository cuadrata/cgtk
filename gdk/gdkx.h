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
 * The X11 backend as programs see it: the display, and the X window
 * behind a GdkWindow, for programs that talk to X themselves. The
 * macros are function calls now; drawing never goes through X, so
 * there is no X font to hand out, and an X GC or X image is made
 * only for the program that asks for one.
 *
 * GDK may be running another backend: then GDK_DISPLAY() is NULL and
 * every accessor here gives None. A program that talks to X checks
 * GDK_IS_X11() first, or does its X work under GDK_WINDOWING_X11 and
 * only when that is true.
 */

#ifndef __GDK_X_H__
#define __GDK_X_H__

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <gdk/gdkprivate.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern Display          *gdk_display;
extern Window            gdk_root_window;

Window   gdk_x11_window_xid   (GdkWindow *window);
Pixmap   gdk_x11_pixmap_xid   (GdkPixmap *pixmap);

/*
 * An external window (gdk_window_set_external) given another visual of
 * the screen's than the one GDK makes its windows with, for whatever
 * draws there and cannot use that one: the X window is made again, so
 * this comes before anything is told its id. FALSE if it cannot be.
 * gdk_x11_window_xvisualid says which visual a window's X window has.
 */
gboolean gdk_x11_window_set_xvisual (GdkWindow *window, VisualID xvisualid);
VisualID gdk_x11_window_xvisualid   (GdkWindow *window);
Visual  *gdk_x11_visual_xvisual (GdkVisual *visual);
Colormap gdk_x11_colormap_xcolormap (GdkColormap *colormap);

/*
 * For programs written against the X GDK: an XImage over the same
 * memory as the GdkImage, and an X GC carrying the GdkGC's values.
 * Drawing with the GC goes to the X window behind GDK's back and is
 * painted over the next time GDK publishes that part of the window.
 */
XImage  *gdk_x11_image_ximage (GdkImage *image);
GC       gdk_x11_gc_xgc       (GdkGC *gc);
Cursor   gdk_x11_cursor_xcursor (GdkCursor *cursor);

#define GDK_IS_X11()                  (gdk_display != NULL)
#define GDK_ROOT_WINDOW()             gdk_root_window
#define GDK_DISPLAY()                 gdk_display
#define GDK_WINDOW_XDISPLAY(win)      gdk_display
#define GDK_WINDOW_XWINDOW(win)       gdk_x11_window_xid (win)
#define GDK_IMAGE_XDISPLAY(image)     gdk_display
#define GDK_IMAGE_XIMAGE(image)       gdk_x11_image_ximage (image)
#define GDK_GC_XDISPLAY(gc)           gdk_display
#define GDK_GC_XGC(gc)                gdk_x11_gc_xgc (gc)
#define GDK_COLORMAP_XDISPLAY(cmap)   gdk_display
#define GDK_COLORMAP_XCOLORMAP(cmap)  gdk_x11_colormap_xcolormap (cmap)
#define GDK_VISUAL_XVISUAL(vis)       gdk_x11_visual_xvisual (vis)
#define GDK_FONT_XDISPLAY(font)       gdk_display
#define GDK_FONT_XFONT(font)          ((gpointer) NULL)
#define GDK_CURSOR_XCURSOR(cursor)    gdk_x11_cursor_xcursor (cursor)

GdkVisual*   gdkx_visual_get   (VisualID xvisualid);
GdkColormap* gdkx_colormap_get (Colormap xcolormap);

GdkPixmap    *gdk_pixmap_foreign_new (guint32     anid);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GDK_X_H__ */
