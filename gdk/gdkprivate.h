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
 * What GDK keeps behind its public types. Nothing here belongs to a
 * window system: a drawable is a framebuffer that GDK draws into itself,
 * and a backend (gdk/x11) is what puts a window's framebuffer on the
 * screen. A backend's own state hangs off the impl pointer, whose
 * struct only the backend defines.
 */

#ifndef __GDK_PRIVATE_H__
#define __GDK_PRIVATE_H__

#include <gdk/gdktypes.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct _GdkWindowPrivate       GdkWindowPrivate;
typedef struct _GdkWindowPrivate       GdkPixmapPrivate;
typedef struct _GdkWindowImpl          GdkWindowImpl;
typedef struct _GdkFramebuffer         GdkFramebuffer;
typedef struct _GdkImagePrivate        GdkImagePrivate;
typedef struct _GdkGCPrivate           GdkGCPrivate;
typedef struct _GdkColormapPrivate     GdkColormapPrivate;
typedef struct _GdkVisualPrivate       GdkVisualPrivate;
typedef struct _GdkFontPrivate         GdkFontPrivate;
typedef struct _GdkCursorPrivate       GdkCursorPrivate;
typedef struct _GdkEventFilter         GdkEventFilter;
typedef struct _GdkClientFilter        GdkClientFilter;
typedef struct _GdkColorContextPrivate GdkColorContextPrivate;
typedef struct _GdkRegionPrivate       GdkRegionPrivate;

/*
 * A framebuffer: the pixels of a drawable, one 32 bit word each. For a
 * drawable of depth 1 a word is 0 or 1; for any other depth it is
 * 0x00RRGGBB, which is also what a GdkColor's pixel is. stride is in
 * words.
 *
 * width and height are in device pixels: a drawable of w by h at scale
 * s has a framebuffer of w*s by h*s. Every coordinate GDK's drawing
 * functions take is in the drawable's own pixels and is scaled once, on
 * the way into the rasteriser, so a display that wants two device
 * pixels for each of the toolkit's has only the scale set. A native
 * window's framebuffer is at the window's scale; a pixmap's is at 1.
 */
struct _GdkFramebuffer
{
  gint     width;
  gint     height;
  gint     stride;
  gint     depth;
  gint     scale;
  guint32 *pixels;
};

struct _GdkWindowPrivate
{
  GdkWindow window;
  GdkWindow *parent;
  gint16 x;
  gint16 y;
  guint16 width;
  guint16 height;
  guint8 resize_count;
  guint8 window_type;
  guint ref_count;
  guint destroyed : 2;
  guint mapped : 1;
  guint bg_parent_relative : 1;
  guint native : 1;         /* has a window of the window system's own */
  guint external : 1;       /* and something else draws it: GL, say */
  gint scale;               /* a native window's: device pixels per pixel */
  gint extension_events;
  GdkEventMask event_mask;
  GList *filters;
  GdkColormap *colormap;
  GList *children;
  gint depth;

  /*
   * What has been drawn: made when first needed, and for a window,
   * filled with the background. Only a native window or a pixmap has
   * one; a child draws into its native ancestor's. damage is what
   * the backend has not put on the screen yet; a pixmap's damage is
   * what a copy of it the backend holds no longer matches.
   */
  GdkFramebuffer *fb;
  GdkRegion *damage;
  GdkColor bg_color;
  GdkPixmap *bg_pixmap;

  /* For a native window: the child window the pointer is in, if any. */
  GdkWindowPrivate *pointer_window;

  /*
   * And where the pointer last was in it, as the window system last
   * told, for when the windows change and the pointer has not moved.
   */
  gint pointer_x, pointer_y, pointer_x_root, pointer_y_root;
  guint32 pointer_time;
  guint pointer_state;
  guint pointer_in : 1;
  guint pointer_stale : 1;

  /* For an external window: told before the system's window goes. */
  GDestroyNotify external_release;
  gpointer external_data;

  GdkWindowImpl *impl;
};

struct _GdkImagePrivate
{
  GdkImage image;
  gpointer impl;     /* the backend's, if it has looked at the image */
};

struct _GdkGCPrivate
{
  GdkGC gc;
  guint ref_count;
  GdkGCValues values;
  GdkRegion *clip_region;
  gint8 *dashes;
  gint n_dashes;
  gint dash_offset;
  gboolean antialias;  /* shapes drawn by coverage are antialiased */
  gdouble alpha;       /* and drawn this opaque */
  gpointer impl;     /* the backend's, if it has looked at the GC */
};

struct _GdkColormapPrivate
{
  GdkColormap colormap;
  GdkVisual *visual;
  gint private_val;
  guint ref_count;
};

struct _GdkVisualPrivate
{
  GdkVisual visual;
};

struct _GdkFontPrivate
{
  GdkFont font;
  guint ref_count;
  GSList *names;
  /* The set of faces the font draws with, and an id GTK hashes by. */
  gpointer ft;
  gint ft_id;
  /* The same faces opened ft_scale times bigger, for drawing on a
   * scaled framebuffer; made when first drawn at that scale. */
  gpointer ft_scaled;
  gint ft_scale;
};

struct _GdkCursorPrivate
{
  GdkCursor cursor;
  gpointer impl;
};

struct _GdkEventFilter {
  GdkFilterFunc function;
  gpointer data;
};

struct _GdkClientFilter {
  GdkAtom       type;
  GdkFilterFunc function;
  gpointer      data;
};

struct _GdkColorContextPrivate
{
  GdkColorContext color_context;
};

/* A region is a set of rectangles that do not overlap. */
struct _GdkRegionPrivate
{
  GdkRegion region;
  GdkRectangle *rects;
  gint n_rects;
  gint n_alloc;
  GdkRectangle extents;
};

typedef enum {
  GDK_DEBUG_MISC          = 1 << 0,
  GDK_DEBUG_EVENTS        = 1 << 1,
  GDK_DEBUG_DND           = 1 << 2,
  GDK_DEBUG_COLOR_CONTEXT = 1 << 3,
  GDK_DEBUG_XIM           = 1 << 4
} GdkDebugFlag;

void gdk_events_init (void);
void gdk_window_init (void);
void gdk_visual_init (void);
void gdk_dnd_init    (void);
void gdk_image_init  (void);
void gdk_image_exit (void);

void gdk_window_destroy_notify       (GdkWindow *window);

/* gdkcolor.c: a colour's red, green and blue from its pixel */
void _gdk_color_from_pixel (GdkColor *color);

/* A program's icon found by its name (gdkicon.c), with what a backend
 * has made of it, if it has: a pixmap and mask, or something of its
 * own. Its pictures have straight alpha, as the files have them and a
 * window system takes them, and not as gdk_window_set_icon_images() is
 * given them.
 */
typedef struct _GdkThemedIcon GdkThemedIcon;

struct _GdkThemedIcon
{
  gchar        *name;
  GdkIconImage *images;
  gint          n_images;
  GdkPixmap    *pixmap;
  GdkBitmap    *mask;
  gpointer      backend_data;
};

GdkThemedIcon* _gdk_themed_icon   (const gchar *name);
GdkPixmap*     _gdk_pixmap_of_rgba (GdkWindow  *window,
                                    GdkBitmap **mask,
                                    GdkColor   *transparent_color,
                                    guchar     *pixels,
                                    gint        width,
                                    gint        height);

/*
 * The generic side of a window (gdkwindow.c). A native window -- a
 * toplevel, or anything else that is not a child -- owns a framebuffer;
 * a child window is a rectangle of its native ancestor's, at an offset.
 * The backend tells the generic side when a window's geometry, mapping
 * or stacking changes, so that the framebuffer can follow.
 */
void      _gdk_window_private_init  (GdkWindowPrivate *private);
void      _gdk_window_private_free  (GdkWindowPrivate *private);
/*
 * An external window's owner lets go of it: called by the backend
 * before it destroys the system's window, which whatever draws there
 * may hold on to.
 */
void      _gdk_window_external_release (GdkWindowPrivate *private);

/*
 * Scale. The backend says at init what the display wants, which new
 * native windows are given, unless GDK_SCALE in the environment says
 * otherwise, in which case that wins and _gdk_scale_override says so.
 * A backend that knows a window has gone to a display of another
 * scale tells the generic side, which remakes the framebuffer and has
 * the program draw it all again.
 */
gint      _gdk_scale_override       (void);
void      _gdk_window_set_default_scale (gint scale);
void      _gdk_window_set_scale     (GdkWindow        *native,
                                     gint              scale);
GdkWindowPrivate *_gdk_window_native (GdkWindowPrivate *private,
                                      gint *x, gint *y);
gboolean  _gdk_window_fb_ensure     (GdkWindowPrivate *private);
GdkRegion *_gdk_window_visible_region (GdkWindowPrivate *private,
                                       gboolean include_inferiors);
void      _gdk_window_resized       (GdkWindow        *window);
void      _gdk_window_geometry_changed (GdkWindow     *window,
                                     gint old_x, gint old_y,
                                     gint old_width, gint old_height);
void      _gdk_window_mapped        (GdkWindow        *window,
                                     gboolean          mapped);
void      _gdk_window_restacked     (GdkWindow        *window);
void      _gdk_window_damage        (GdkWindowPrivate *private,
                                     const GdkRectangle *rect);
void      _gdk_window_publish       (GdkWindow        *window,
                                     const GdkRectangle *rect);
void      _gdk_window_publish_soon  (GdkWindowPrivate *private);
glong     _gdk_windows_waiting      (const GTimeVal   *now);
gboolean  _gdk_windows_flush        (void);
void      _gdk_window_expose_region (GdkWindowPrivate *private,
                                     GdkRegion        *region);

/*
 * Events for the windows inside a native window (gdkevents.c). The
 * backend hands the generic side what happened to the native window,
 * in its pixels, and the generic side finds the window it happened in,
 * makes the crossing events between the windows the pointer moves
 * through, keeps the grabs, and splits an expose among the windows it
 * covers.
 */
GdkWindowPrivate *_gdk_window_child_at   (GdkWindowPrivate *native,
                                          gint x, gint y);
GdkWindowPrivate *_gdk_window_event_target (GdkWindowPrivate *native,
                                          gint x, gint y,
                                          GdkEventMask      needed,
                                          gint *tx, gint *ty);
void      _gdk_window_pointer_moved (GdkWindowPrivate *native,
                                     gint x, gint y, gint x_root, gint y_root,
                                     guint32 time, guint state,
                                     GdkCrossingMode mode, gboolean leaving);
void      _gdk_window_pointer_stale (GdkWindowPrivate *private);
void      _gdk_window_implicit_grab_ended (GdkWindowPrivate *native,
                                           gint x, gint y, gint x_root, gint y_root,
                                           guint32 time, guint state);
void      _gdk_window_grab_crossing (GdkWindowPrivate *grab_window,
                                     gboolean grabbed,
                                     gint x, gint y, gint x_root, gint y_root,
                                     guint32 time, guint state);
void      _gdk_window_expose_split  (GdkWindowPrivate *native,
                                     const GdkRectangle *area);
void      _gdk_pointer_grab_set     (GdkWindowPrivate *window,
                                     gboolean owner_events,
                                     GdkEventMask mask, gboolean implicit);
GdkWindowPrivate *_gdk_pointer_grab_window (gboolean *implicit);
GdkEventMask      _gdk_pointer_grab_mask (void);
void              _gdk_pointer_grab_window_hidden (GdkWindowPrivate *window);
extern gboolean   _gdk_pointer_grab_owner_events;
void      _gdk_pointer_grab_clear   (gboolean implicit_only);

/*
 * The event queue (gdkevents.c). A backend translates the window
 * system's events and appends them; one it has not finished with is
 * marked pending. The filters the program installed for client
 * messages are here too, for the backend to run.
 */
typedef struct _GdkEventPrivate GdkEventPrivate;

typedef enum
{
  /* Following flag is set for events on the event queue during
   * translation and cleared afterwards.
   */
  GDK_EVENT_PENDING = 1 << 0
} GdkEventFlags;

struct _GdkEventPrivate
{
  GdkEvent event;
  guint    flags;
};

void      _gdk_events_init             (void);
GdkEvent *_gdk_event_new               (void);
GList    *_gdk_event_queue_find_first  (void);
void      _gdk_event_queue_remove_link (GList     *node);
GList    *_gdk_event_queue_append      (GdkEvent  *event);
void      _gdk_event_queue_update_configures (const GdkEventConfigure *configure);

/* gdkbroadcast.c: client messages to every CGTK program, on any backend */
void      _gdk_broadcast_init          (void);
void      _gdk_broadcast_exit          (void);
void      _gdk_broadcast_send          (GdkEventClient *event);
extern GList *_gdk_client_filters;

/*
 * What the backend does for the generic side. publish puts a
 * rectangle of a window's framebuffer on the screen, and is called
 * once more with no rectangle when those were all there are for the
 * time being: the window can be shown as it now is; expose asks for
 * an expose event on a rectangle of a window; dpi is the resolution
 * text is sized for.
 */
void    _gdk_backend_publish (GdkWindow *window, const GdkRectangle *rect);
/* window is native; rect is in device pixels of its framebuffer. */
void    _gdk_backend_cursor_changed (GdkWindow *native);
void    _gdk_backend_get_image (GdkWindow *window, gint x, gint y,
                                GdkImage *image);
void    _gdk_backend_pixmap_free (GdkWindowPrivate *private);
void    _gdk_backend_image_free  (GdkImagePrivate  *private);
void    _gdk_backend_gc_free     (GdkGCPrivate     *private);
void    _gdk_backend_window_free (GdkWindowPrivate *private);
gdouble _gdk_backend_dpi     (void);

extern gint              gdk_use_xshm;
extern gchar            *gdk_display_name;
extern gint              gdk_screen;
extern GdkWindowPrivate  gdk_root_parent;
#define GDK_ROOT_PARENT()  ((GdkWindow *) &gdk_root_parent)
extern GdkAtom           gdk_selection_property;
extern gchar            *gdk_progclass;
extern gint              gdk_error_code;
extern gint              gdk_error_warnings;
extern gint              gdk_null_window_warnings;
extern GList            *gdk_default_filters;
extern GdkWindowPrivate *gdk_xgrab_window;  /* Window that currently holds the
                                             * x pointer grab
                                             */

#ifdef G_ENABLE_DEBUG

#define GDK_NOTE(type,action)                G_STMT_START { \
    if (gdk_debug_flags & GDK_DEBUG_##type)                 \
       { action; };                          } G_STMT_END

#else /* !G_ENABLE_DEBUG */

#define GDK_NOTE(type,action)

#endif /* G_ENABLE_DEBUG */

extern guint gdk_debug_flags;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GDK_PRIVATE_H__ */
