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
 * What the X11 backend keeps for itself. Internal to GDK; not
 * installed. The generic side of GDK never includes this.
 */

#ifndef __GDK_PRIVATE_X11_H__
#define __GDK_PRIVATE_X11_H__

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <gdk/gdkprivate.h>
#include <gdk/gdkx.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * An X cursor, kept alive while a window shows it: the server held a
 * cursor a window was given, and so must GDK for the windows that are
 * not the server's.
 */
typedef struct _GdkCursorImpl GdkCursorImpl;
struct _GdkCursorImpl
{
  Cursor xcursor;
  Display *xdisplay;
  guint ref_count;
};

GdkCursorImpl *_gdk_x11_cursor_impl_ref   (GdkCursorImpl *impl);
void           _gdk_x11_cursor_impl_unref (GdkCursorImpl *impl);

/*
 * A window's X window. A pixmap has an impl only once the backend has
 * needed an X pixmap of it -- for a shape, an icon or a cursor -- and
 * that copy is remade when the pixmap's damage says it is out of date.
 */
struct _GdkWindowImpl
{
  Window xwindow;       /* None for a window that is not native */
  Display *xdisplay;
  Pixmap xpixmap;
  GdkCursorImpl *cursor; /* set on this window with _gdk_x11_window_set_cursor */
  Cursor xcursor_shown;  /* for a native window: what X shows now */
  Colormap xcolormap;    /* for a window given a visual of its own: its colormap */
  /* For a toplevel: where it is on the root window, once asked for and
   * until X says it has moved. */
  guint root_known : 1;
  gint root_x, root_y;
};



/* The X window behind a window: its native ancestor's for a child. */
#define GDK_WINDOW_XID(win)       gdk_x11_window_xid ((GdkWindow *) (win))
#define GDK_CURSOR_IMPL(cursor)   ((GdkCursorImpl *) ((GdkCursorPrivate *) (cursor))->impl)

#ifdef USE_XIM

typedef struct _GdkICPrivate GdkICPrivate;

struct _GdkICPrivate
{
  XIC xic;
};

#endif /* USE_XIM */

/*
 * Publishing (gdkpublish-x11.c): framebuffers onto X drawables.
 */
GdkWindowImpl *_gdk_x11_impl_new           (Window xwindow);
/* An event looked at for what it says of the screens. */
void           _gdk_x11_screen_event       (XEvent *xevent);
void           _gdk_x11_events_queue       (void);
Pixmap         _gdk_x11_pixmap_xid         (GdkPixmap *pixmap);
Visual        *_gdk_x11_visual             (void);
Colormap       _gdk_x11_colormap           (void);
gint           _gdk_x11_depth              (void);
void           _gdk_x11_publish_init       (void);

void     gdk_xid_table_insert (XID      *xid,
                               gpointer  data);
void     gdk_xid_table_remove (XID       xid);
gpointer gdk_xid_table_lookup (XID       xid);

gint gdk_send_xevent (Window window, gboolean propagate, glong event_mask,
                      XEvent *event_send);

/*
 * Device pixels for each of the toolkit's, for the whole display: X
 * knows nothing of scales, so every size and position that crosses
 * into Xlib is multiplied by this on the way in and divided on the
 * way out, and a window's X window is that much bigger than the
 * window. Set once at init.
 */
extern gint              _gdk_x11_scale;
#define GDK_X11_TO_X(v)   ((v) * _gdk_x11_scale)
#define GDK_X11_FROM_X(v) _gdk_x11_from_x (v)
gint   _gdk_x11_from_x (gint v);
/* A rectangle of X pixels to the toolkit's, rounded outwards. */
void   _gdk_x11_rect_from_x (GdkRectangle *rect);
/*
 * The X pixmap of a bitmap at the display's scale, for shapes and
 * cursors: the bitmap's own if it is at that scale, else one made
 * from it, which the caller frees with gdk_pixmap_unref (*temp) once
 * X has copied it.
 */
Pixmap _gdk_x11_bitmap_xid_at_scale (GdkBitmap *bitmap, GdkPixmap **temp);
extern Window            gdk_leader_window;
extern Atom              gdk_wm_delete_window;
extern Atom              gdk_wm_take_focus;
extern Atom              gdk_wm_protocols;
extern Atom              gdk_wm_window_protocols[];
extern const int         gdk_nevent_masks;
extern const int         gdk_event_mask_table[];

#ifdef USE_XIM
/* XIM support */
gint   gdk_im_open               (void);
void   gdk_im_close              (void);

extern GdkICPrivate *gdk_xim_ic;                /* currently using IC */
extern GdkWindow *gdk_xim_window;               /* currently using Window */
#endif /* USE_XIM */

/* The GdkWindow a selection event is for: a child may stand behind window. */
GdkWindow *_gdk_x11_selection_event_window (GdkWindow *window, XEvent *xevent);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GDK_PRIVATE_X11_H__ */
