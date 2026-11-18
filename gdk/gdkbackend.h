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
 * What a backend does for GDK: the window system's side of every GDK
 * function that has one. Internal to GDK; not installed. The generic
 * side calls through _gdk_backend, set at gdk_init from the backends
 * compiled in; each backend fills in one of these structs, with a
 * function of its own behind each pointer. The signatures are those
 * of the public functions in gdk.h, so gdkbackend.c is nothing but a
 * call through for each.
 */

#ifndef __GDK_BACKEND_H__
#define __GDK_BACKEND_H__

#include <gdk/gdkprivate.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * A backend's input contexts start with this; the class makes them, and
 * reports through the funcs with the helpers in gdkinputcontext.c.
 */
typedef struct _GdkInputContextClass GdkInputContextClass;

struct _GdkInputContext
{
  const GdkInputContextClass *klass;
  const GdkInputContextFuncs *funcs;
  gpointer data;
};

struct _GdkInputContextClass
{
  GdkInputContext * (*create) (void);
  void (*destroy) (GdkInputContext *context);
  void (*set_window) (GdkInputContext *context, GdkWindow *window);
  void (*focus_in) (GdkInputContext *context);
  void (*focus_out) (GdkInputContext *context);
  void (*reset) (GdkInputContext *context);
  gboolean (*filter_key) (GdkInputContext *context, GdkEventKey *event);
  void (*get_preedit) (GdkInputContext *context, gchar **utf8, gint *cursor,
                       gint *mark_start, gint *mark_end);
  void (*set_cursor_location) (GdkInputContext *context, GdkRectangle *area);
  void (*set_surrounding) (GdkInputContext *context, const gchar *utf8,
                           gint length, gint cursor_index);
  void (*set_purpose) (GdkInputContext *context, GdkInputPurpose purpose);
};

void     _gdk_input_context_commit          (GdkInputContext *context, const gchar *utf8);
void     _gdk_input_context_preedit_changed (GdkInputContext *context);
gboolean _gdk_input_context_retrieve_surrounding (GdkInputContext *context);
gboolean _gdk_input_context_delete_surrounding   (GdkInputContext *context,
                                                  gint offset, gint n_chars);

typedef struct _GdkBackend GdkBackend;

struct _GdkBackend
{
  const gchar *name;

  /*
   * Opening the display named, or the default; FALSE if it cannot be.
   * synchronize asks for every request to be answered before the next.
   * exit closes it.
   */
  gboolean (*init)   (const gchar *display_name, gboolean synchronize,
                      const gchar *program_class);
  void     (*exit)   (void);

  /*
   * Events: whether the system has any waiting, putting those it has
   * into GDK's queue (all of them, or until the queue has one), the
   * file descriptor the main loop waits on, and sending everything
   * buffered to the system and waiting until it is done.
   */
  gboolean (*events_pending) (void);
  void     (*events_queue)   (gboolean all);
  gint     (*event_fd)       (void);
  void     (*flush)          (void);

  /* The generic side's hooks, described in gdkprivate.h. */
  void    (*publish)        (GdkWindow *window, const GdkRectangle *rect);
  void    (*get_image)      (GdkWindow *window, gint x, gint y, GdkImage *image);
  void    (*pixmap_free)    (GdkWindowPrivate *private);
  void    (*image_free)     (GdkImagePrivate *private);
  void    (*gc_free)        (GdkGCPrivate *private);
  void    (*window_free)    (GdkWindowPrivate *private);
  gdouble (*dpi)            (void);
  void    (*cursor_changed) (GdkWindow *native);
  gint    (*window_refresh_rate) (GdkWindow *window);

  /* display */
  void (*beep) (void);
  gint (*screen_width) (void);
  gint (*screen_height) (void);
  gint (*screen_width_mm) (void);
  gint (*screen_height_mm) (void);
  gboolean (*can_place_toplevels) (void);
  void (*error_trap_push) (void);
  gint (*error_trap_pop) (void);
  void (*set_sm_client_id) (const gchar *sm_client_id);
  gchar* (*set_locale) (void);
  gboolean (*event_send_client_message) (GdkEvent *event, guint32 xid);
  void (*event_send_clientmessage_toall) (GdkEvent *event);
  GdkAtom (*atom_intern) (const gchar *atom_name, gint only_if_exists);
  gchar* (*atom_name) (GdkAtom atom);

  /* windows */
  GdkWindow* (*window_new) (GdkWindow *parent, GdkWindowAttr *attributes, gint attributes_mask);
  void (*window_destroy) (GdkWindow *window);
  void (*window_show) (GdkWindow *window);
  void (*window_hide) (GdkWindow *window);
  void (*window_withdraw) (GdkWindow *window);
  void (*window_move) (GdkWindow *window, gint x, gint y);
  void (*window_resize) (GdkWindow *window, gint width, gint height);
  void (*window_move_resize) (GdkWindow *window, gint x, gint y, gint width, gint height);
  void (*window_reparent) (GdkWindow *window, GdkWindow *new_parent, gint x, gint y);
  void (*window_raise) (GdkWindow *window);
  void (*window_lower) (GdkWindow *window);
  void (*window_ensure_native) (GdkWindow *window);
  gboolean (*window_begin_move_drag) (GdkWindow *window, gint button, gint x_root, gint y_root, guint32 time);
  GdkWindow* (*window_lookup) (guint32 anid);
  GdkWindow* (*window_foreign_new) (guint32 anid);
  void (*window_set_hints) (GdkWindow *window, gint x, gint y, gint min_width, gint min_height, gint max_width, gint max_height, gint flags);
  void (*window_set_geometry_hints) (GdkWindow *window, GdkGeometry *geometry, GdkWindowHints flags);
  void (*window_set_title) (GdkWindow *window, const gchar *title);
  void (*window_set_role) (GdkWindow *window, const gchar *role);
  void (*window_set_transient_for) (GdkWindow *window, GdkWindow *leader);
  void (*window_set_cursor) (GdkWindow *window, GdkCursor *cursor);
  void (*window_set_colormap) (GdkWindow *window, GdkColormap *colormap);
  void (*window_get_geometry) (GdkWindow *window, gint *x, gint *y, gint *width, gint *height, gint *depth);
  gint (*window_get_origin) (GdkWindow *window, gint *x, gint *y);
  gboolean (*window_get_deskrelative_origin) (GdkWindow *window, gint *x, gint *y);
  void (*window_get_root_origin) (GdkWindow *window, gint *x, gint *y);
  GdkWindow* (*window_get_pointer) (GdkWindow *window, gint *x, gint *y, GdkModifierType *mask);
  GdkWindow* (*window_at_pointer) (gint *win_x, gint *win_y);
  void (*window_set_events) (GdkWindow *window, GdkEventMask event_mask);
  void (*window_shape_combine_mask) (GdkWindow *window, GdkBitmap *shape_mask, gint offset_x, gint offset_y);
  void (*window_set_override_redirect) (GdkWindow *window, gboolean override_redirect);
  void (*window_set_icon) (GdkWindow *window, GdkWindow *icon_window, GdkPixmap *pixmap, GdkBitmap *mask);
  void (*window_set_icon_name) (GdkWindow *window, const gchar *name);
  void (*window_set_icon_images) (GdkWindow *window, const GdkIconImage *images, gint n_images);
  void (*window_set_themed_icon) (GdkWindow *window, const gchar *name);
  void (*window_set_group) (GdkWindow *window, GdkWindow *leader);
  void (*window_set_group_resize) (GdkWindow *window, gboolean horizontally, gboolean vertically);
  void (*window_set_decorations) (GdkWindow *window, GdkWMDecoration decorations);
  void (*window_set_functions) (GdkWindow *window, GdkWMFunction functions);
  void (*window_set_title_colors) (GdkWindow *window, const GdkColor *colors);
  void (*window_iconify) (GdkWindow *window);
  void (*window_set_sticky) (GdkWindow *window, gboolean sticky);
  void (*window_set_keep_above) (GdkWindow *window, gboolean setting);
  void (*window_set_skip_taskbar_hint) (GdkWindow *window, gboolean setting);
  void (*window_register_dnd) (GdkWindow *window);

  /* pointer and keyboard */
  gint (*pointer_grab) (GdkWindow *window, gint owner_events, GdkEventMask event_mask, GdkWindow *confine_to, GdkCursor *cursor, guint32 time);
  void (*pointer_ungrab) (guint32 time);
  gboolean (*pointer_is_grabbed) (void);
  gint (*keyboard_grab) (GdkWindow *window, gboolean owner_events, guint32 time);
  void (*keyboard_ungrab) (guint32 time);
  void (*key_repeat_disable) (void);
  void (*key_repeat_restore) (void);
  gchar* (*keyval_name) (guint keyval);
  guint (*keyval_from_name) (const gchar *keyval_name);
  guint (*keyval_to_upper) (guint keyval);
  guint (*keyval_to_lower) (guint keyval);

  /* cursors */
  GdkCursor* (*cursor_new) (GdkCursorType cursor_type);
  GdkCursor* (*cursor_new_from_pixmap) (GdkPixmap *source, GdkPixmap *mask, GdkColor *fg, GdkColor *bg, gint x, gint y);
  void (*cursor_destroy) (GdkCursor *cursor);

  /* properties, selections, drag and drop */
  gboolean (*property_get) (GdkWindow *window, GdkAtom property, GdkAtom type, gulong offset, gulong length, gint pdelete, GdkAtom *actual_property_type, gint *actual_format, gint *actual_length, guchar **data);
  void (*property_change) (GdkWindow *window, GdkAtom property, GdkAtom type, gint format, GdkPropMode mode, guchar *data, gint nelements);
  void (*property_delete) (GdkWindow *window, GdkAtom property);
  gboolean (*selection_owner_set) (GdkWindow *owner, GdkAtom selection, guint32 time, gint send_event);
  GdkWindow* (*selection_owner_get) (GdkAtom selection);
  void (*selection_convert) (GdkWindow *requestor, GdkAtom selection, GdkAtom target, guint32 time);
  gboolean (*selection_property_get) (GdkWindow *requestor, guchar **data, GdkAtom *prop_type, gint *prop_format);
  void (*selection_send_notify) (guint32 requestor, GdkAtom selection, GdkAtom target, GdkAtom property, guint32 time);
  gint (*selection_max_size) (void);
  GdkDragContext * (*drag_context_new) (void);
  void (*drag_context_ref) (GdkDragContext *context);
  void (*drag_context_unref) (GdkDragContext *context);
  void (*drag_status) (GdkDragContext *context, GdkDragAction action, guint32 time);
  void (*drop_reply) (GdkDragContext *context, gboolean ok, guint32 time);
  void (*drop_finish) (GdkDragContext *context, gboolean success, guint32 time);
  GdkAtom (*drag_get_selection) (GdkDragContext *context);
  GdkDragContext * (*drag_begin) (GdkWindow *window, GList *targets);
  guint32 (*drag_get_protocol) (guint32 xid, GdkDragProtocol *protocol);
  void (*drag_find_window) (GdkDragContext *context, GdkWindow *drag_window, gint x_root, gint y_root, GdkWindow **dest_window, GdkDragProtocol *protocol);
  gboolean (*drag_motion) (GdkDragContext *context, GdkWindow *dest_window, GdkDragProtocol protocol, gint x_root, gint y_root, GdkDragAction suggested_action, GdkDragAction possible_actions, guint32 time);
  void (*drag_drop) (GdkDragContext *context, guint32 time);
  void (*drag_abort) (GdkDragContext *context, guint32 time);

  /* text and input methods */
  gint (*text_property_to_text_list) (GdkAtom encoding, gint format, guchar *text, gint length, gchar ***list);
  void (*free_text_list) (gchar **list);
  gint (*string_to_compound_text) (const gchar *str, GdkAtom *encoding, gint *format, guchar **ctext, gint *length);
  void (*free_compound_text) (guchar *ctext);
  const GdkInputContextClass *input_context_class;   /* NULL: none */

  /* the core pointer's motion history; NULL: none */
  GdkTimeCoord * (*input_motion_events) (GdkWindow *window, guint32 start, guint32 stop, gint *nevents_return);
};

extern const GdkBackend *_gdk_backend;

/* The backends compiled in, for gdk_init to choose from. */
#ifdef GDK_WINDOWING_X11
extern const GdkBackend _gdk_x11_backend;
#endif
#ifdef GDK_WINDOWING_WAYLAND
extern const GdkBackend _gdk_wayland_backend;
#endif

const GdkBackend *_gdk_backend_choose (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GDK_BACKEND_H__ */
