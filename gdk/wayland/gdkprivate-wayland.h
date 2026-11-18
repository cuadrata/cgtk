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

/*
 * What the Wayland backend keeps for itself. Internal to GDK; not
 * installed.
 *
 * A native window is a wl_surface with a role: a toplevel or dialog is
 * an xdg_toplevel, a temporary window (a menu, a tooltip) an xdg_popup
 * of the toplevel it belongs to, and a native child a subsurface of
 * its parent's surface. Each publishes its framebuffer through a pair
 * of wl_shm buffers, the one the compositor is not holding.
 *
 * Wayland does not say where a toplevel is, so every toplevel is at
 * root coordinates (0, 0) and its popups are placed relative to it:
 * a program that positions a menu at the pointer's root position puts
 * it in the right place, since the pointer's root position is its
 * position in the toplevel.
 */

#ifndef __GDK_PRIVATE_WAYLAND_H__
#define __GDK_PRIVATE_WAYLAND_H__

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>
#include <gdk/gdkprivate.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "primary-selection-unstable-v1-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"
#ifdef HAVE_XDG_TOPLEVEL_ICON
#include "xdg-toplevel-icon-v1-client-protocol.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct _GdkWaylandBuffer  GdkWaylandBuffer;
typedef struct _GdkWaylandGroup   GdkWaylandGroup;
typedef struct _GdkCursorImpl     GdkCursorImpl;
typedef struct _GdkWaylandProperty GdkWaylandProperty;
typedef struct _GdkWaylandDecor   GdkWaylandDecor;
typedef struct _GdkWaylandIcon    GdkWaylandIcon;

/* One of a window's shm buffers. */
struct _GdkWaylandBuffer
{
  struct wl_buffer *buffer;
  guint32 *data;
  gint width, height;     /* device pixels */
  gsize size;
  gboolean busy;          /* attached, and not released yet */
  GdkWindowPrivate *window;   /* whose it is */
};

/*
 * A window group: toplevels a program places relative to each other,
 * as XMMS docks its windows. Wayland lets nobody place a toplevel, so
 * the group is one toplevel, the host, a transparent surface the size
 * of the members' bounding box, and each member a subsurface of it at
 * its position in the group. The group moves as one, by the
 * compositor; the members move among themselves, by the program.
 */
struct _GdkWaylandGroup
{
  GdkWindowPrivate *leader;       /* the toplevel the others hang from */
  GList *members;                 /* GdkWindowPrivate *, topmost first, the leader among them */
  gint x, y, width, height;       /* the box around them all, in group coordinates */
  GdkWindowPrivate *active;       /* the member the keyboard goes to: the one last under the pointer */
  struct wl_callback *sync;       /* until the compositor has seen the box's new size */
  gboolean box_changed;           /* since a member last committed: the leader commits again then */
};

struct _GdkWindowImpl
{
  struct wl_surface *surface;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *xdg_toplevel;
  struct xdg_popup *xdg_popup;
  struct xdg_positioner *positioner;
  struct zxdg_toplevel_decoration_v1 *decoration;
  struct wl_subsurface *subsurface;
  struct wl_callback *frame;      /* the frame callback awaited, if any */
  gint64 frame_asked;            /* when it was asked for, in ms on a clock not set */
  struct wl_region *shape;        /* the input region, from a shape mask */
  GdkBitmap *shape_mask;
  GList *outputs;                 /* the wl_outputs the surface is on */

  GdkWaylandBuffer buffers[2];
  GdkRegion *stale[2];            /* what changed since each buffer was filled; NULL: all */
  GdkRegion *pending_damage;      /* device pixels, since the last commit */
  guint configured : 1;           /* the initial xdg configure arrived */
  guint committed : 1;            /* the initial commit was made */
  guint commit_pending : 1;       /* something to commit */
  guint damage_all : 1;           /* the next commit is to send the whole picture */
  guint want_grab : 1;            /* a popup asked for a grab */
  guint want_move : 1;            /* to be moved by the compositor once shown */
  guint drag_icon : 1;            /* a temporary window that is a drag's icon */
  gint drag_dx, drag_dy;          /* and where it was shown, from the pointer */
  guint decorated : 1;
  guint title_colors_set : 1;     /* title_colors are the program's */

  guint32 title_colors[GDK_TITLE_N_COLORS];  /* as 0xrrggbb */

  GdkCursorImpl *cursor;          /* set on this window */

  /* For a native window: where the pointer last was in it. */
  gint pointer_x, pointer_y;
  gboolean pointer_in;

  GdkWindow *popup_parent;        /* for a popup: whose it is */
  GdkWindow *transient_for;       /* for a toplevel: the window it belongs to, held */
  GdkWaylandGroup *group;         /* for a toplevel: the group it is in, if any */
  GHashTable *properties;         /* GdkAtom -> GdkWaylandProperty */

  /* The icon, kept likewise; icon_own if it is this window's alone and
   * goes with it, and not one that is the program's for all of them. */
  GdkWaylandIcon *icon;
  guint icon_own : 1;

  /* Size hints, kept until the toplevel exists to give them to. */
  gint min_width, min_height, max_width, max_height;
  guint group_resize_h : 1;       /* a member that takes the group's change of width */
  guint group_resize_v : 1;       /* and of height */
  gchar *title;
  gint asked_x, asked_y;          /* where the program last put the window */
  gint host_x, host_y;            /* a group member's place in the host */
  gint cfg_width, cfg_height;     /* the size the compositor gave, or 0 */
  guint cfg_fixed : 1;            /* and it is not to be changed */
  gint saved_width, saved_height; /* its size before it was maximized or the like */
  guint cfg_placed : 1;           /* maximized, full screen or tiled */

  /* A toplevel's state, as the compositor last configured it, and the
   * title bar drawn for it when the compositor draws none (ssd FALSE). */
  guint ssd : 1;
  guint activated : 1;
  guint maximized : 1;
  guint fullscreen : 1;
  GdkWaylandDecor *decor;
};

struct _GdkCursorImpl
{
  struct wl_buffer *buffer;       /* NULL for a cursor with no image */
  gint width, height;             /* of the buffer, in its pixels */
  gint hotspot_x, hotspot_y;      /* in the buffer's pixels */
  gint scale;                     /* the buffer's pixels per screen pixel */
  gboolean zoomed;                /* a pixmap cursor: grows with the zoom */
  gboolean own_buffer;            /* made from a pixmap; freed with it */
  guint ref_count;
};

/* A property a program stored on a window, as X would have kept it. */
struct _GdkWaylandProperty
{
  GdkAtom type;
  gint format;
  guchar *data;
  gint nelements;
};

#define GDK_CURSOR_IMPL(cursor)   ((GdkCursorImpl *) ((GdkCursorPrivate *) (cursor))->impl)

/* The display, its globals, the seat, and the keyboard's map. */
extern struct wl_display *_gdk_wl_display;
extern struct wl_compositor *_gdk_wl_compositor;
extern struct wl_subcompositor *_gdk_wl_subcompositor;
extern struct wl_shm *_gdk_wl_shm;
extern struct wl_seat *_gdk_wl_seat;
extern struct wl_pointer *_gdk_wl_pointer;
extern struct wl_keyboard *_gdk_wl_keyboard;
/* The scale an output advertises, 1 if it is not one we know. */
gint _gdk_wayland_output_scale (struct wl_output *output);
gint _gdk_wayland_output_refresh (struct wl_output *output);
/*
 * A forced scale (GDK_SCALE) on a display whose outputs want less:
 * the compositor owns the surface's pixels, so the only way to make
 * windows bigger is to make each of ours this many of its. 1 when
 * not forced. Every size and position that crosses to the compositor
 * is multiplied by it and divided on the way back, and a window's
 * buffer scale is its framebuffer's scale divided by it.
 */
extern gint _gdk_wayland_zoom;
#define GDK_WL_TO(v)   ((v) * _gdk_wayland_zoom)
#define GDK_WL_FROM(v) _gdk_wayland_from_wl (v)
gint _gdk_wayland_from_wl (gint v);
/* The scale the cursor theme was loaded for: the output's. */
gint _gdk_wayland_output_cursor_scale (void);
/* An output's scale changed: every window on it follows. */
void _gdk_wayland_outputs_changed (void);
extern struct xdg_wm_base *_gdk_xdg_wm_base;
extern struct zxdg_decoration_manager_v1 *_gdk_xdg_decoration_manager;
#ifdef HAVE_XDG_TOPLEVEL_ICON
extern struct xdg_toplevel_icon_manager_v1 *_gdk_xdg_toplevel_icon_manager;
#endif
extern struct wl_data_device_manager *_gdk_wl_data_device_manager;
extern struct wl_data_device *_gdk_wl_data_device;
extern struct zwp_primary_selection_device_manager_v1 *_gdk_wl_primary_manager;
extern struct zwp_text_input_manager_v3 *_gdk_wl_text_input_manager;
void _gdk_wayland_input_context_init (void);
void _gdk_wayland_input_context_exit (void);
extern struct wl_cursor_theme *_gdk_wl_cursor_theme;
extern struct wl_surface *_gdk_wl_cursor_surface;

extern struct xkb_context *_gdk_xkb_context;
extern struct xkb_keymap *_gdk_xkb_keymap;
extern struct xkb_state *_gdk_xkb_state;

extern guint32 _gdk_wayland_last_serial;      /* of the last input event */
extern guint32 _gdk_wayland_pointer_serial;   /* of the last pointer enter */
extern guint32 _gdk_wayland_press_serial;     /* of the last button press */
extern guint32 _gdk_wayland_input_serial;     /* of the last button or key press */
void _gdk_wayland_window_output_gone (struct wl_output *output);
extern guint32 _gdk_wayland_last_time;
extern guint _gdk_wayland_modifiers;          /* as GDK state bits */
extern GdkWindowPrivate *_gdk_wayland_pointer_window;   /* native, under the pointer */
extern GdkWindowPrivate *_gdk_wayland_keyboard_window;  /* native, with the keyboard */
extern GdkWindowPrivate *_gdk_wayland_grab_popup;       /* the popup with the grab */

/* gdkwindow-wayland.c */
GdkWindowPrivate *_gdk_wayland_group_member_at (GdkWaylandGroup *group, gint x, gint y);
void     _gdk_wayland_group_set_active   (GdkWindowPrivate *member);
GdkWindowPrivate *_gdk_wayland_keyboard_target (GdkWindowPrivate *native);
void     _gdk_wayland_focus_event        (GdkWindowPrivate *native, gboolean in);
void     _gdk_wayland_window_commit_all  (void);
GdkWindowPrivate *_gdk_wayland_window_from_surface (struct wl_surface *surface);
void     _gdk_wayland_set_cursor_on_pointer (void);
gboolean _gdk_wayland_shm_buffer_new     (GdkWaylandBuffer *buffer,
                                          gint width, gint height,
                                          gboolean alpha);
void     _gdk_wayland_shm_buffer_free    (GdkWaylandBuffer *buffer);

/* gdkevents-wayland.c */
void     _gdk_wayland_seat_capabilities  (uint32_t capabilities);
void     _gdk_wayland_events_init        (void);
void     _gdk_wayland_events_exit        (void);
void     _gdk_wayland_put_event          (GdkEvent *event);

/* gdkdecor-wayland.c: a title bar where the compositor draws none */
gint     _gdk_wayland_decor_height         (GdkWindowPrivate *private);
void     _gdk_wayland_decor_changed        (GdkWindowPrivate *private);
void     _gdk_wayland_decor_before_commit  (GdkWindowPrivate *private);
void     _gdk_wayland_decor_listen         (GdkWindowPrivate *private);
void     _gdk_wayland_decor_frame_done     (GdkWindowPrivate *private);
gboolean _gdk_wayland_decor_needs_frame    (GdkWindowPrivate *private);
void     _gdk_wayland_decor_free           (GdkWindowPrivate *private);
gboolean _gdk_wayland_decor_pointer_enter  (struct wl_surface *surface, gint x, gint y);
gboolean _gdk_wayland_decor_pointer_leave  (struct wl_surface *surface);
gboolean _gdk_wayland_decor_pointer_motion (gint x, gint y);
gboolean _gdk_wayland_decor_pointer_button (guint32 serial, guint32 time, guint32 button,
                                            gboolean press);
void     _gdk_wayland_show_cursor          (GdkCursor *cursor);
void     _gdk_wayland_window_send_size_limits (GdkWindowPrivate *private);
void     _gdk_wayland_release_buttons    (void);
gboolean _gdk_wayland_buttons_down       (void);
GdkWindowPrivate *_gdk_wayland_pointer_native (void);
GdkWindowPrivate *_gdk_wayland_drop_pointer (gint *x, gint *y);
gboolean _gdk_wayland_drag_under_way (void);
void     _gdk_wayland_pointer_native_gone (GdkWindowPrivate *native);
guint    _gdk_wayland_button_mask        (void);

/* gdkselection-wayland.c */
extern gboolean _gdk_wayland_drag_pending;    /* a drag begun, not yet started */
void     _gdk_wayland_drag_start         (void);
void     _gdk_wayland_drag_icon_set      (GdkWindowPrivate *icon);
void     _gdk_wayland_selection_init     (void);
void     _gdk_wayland_selection_exit     (void);
void     _gdk_wayland_properties_free    (GdkWindowPrivate *private);

/* gdkmain-wayland.c */
const gchar *_gdk_wayland_atom_string    (GdkAtom atom);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GDK_PRIVATE_WAYLAND_H__ */
