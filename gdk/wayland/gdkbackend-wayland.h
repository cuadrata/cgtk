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
 * The wayland backend's functions behind the GdkBackend struct: one
 * for each public function in gdk.h that the backend implements, with
 * the same signature. Internal to GDK; not installed.
 */

#ifndef __GDK_BACKEND_WAYLAND_H__
#define __GDK_BACKEND_WAYLAND_H__

#include <gdk/gdk.h>
#include <gdk/gdkbackend.h>

/* start, end, events, and the generic side's hooks */
gboolean _gdk_wayland_init           (const gchar *display_name, gboolean synchronize,
                                  const gchar *program_class);
void     _gdk_wayland_exit           (void);
gboolean _gdk_wayland_events_pending (void);
void     _gdk_wayland_events_queue (gboolean all);
gint     _gdk_wayland_event_fd       (void);
void     _gdk_wayland_flush          (void);
void     _gdk_wayland_publish        (GdkWindow *window, const GdkRectangle *rect);
void     _gdk_wayland_get_image      (GdkWindow *window, gint x, gint y, GdkImage *image);
void     _gdk_wayland_pixmap_free    (GdkWindowPrivate *private);
void     _gdk_wayland_image_free     (GdkImagePrivate *private);
void     _gdk_wayland_gc_free        (GdkGCPrivate *private);
void     _gdk_wayland_window_free    (GdkWindowPrivate *private);
gdouble  _gdk_wayland_dpi            (void);
void     _gdk_wayland_cursor_changed (GdkWindow *native);
gint     _gdk_wayland_window_refresh_rate (GdkWindow *window);

/* display */
void _gdk_wayland_beep (void);
gint _gdk_wayland_screen_width (void);
gint _gdk_wayland_screen_height (void);
gint _gdk_wayland_screen_width_mm (void);
gint _gdk_wayland_screen_height_mm (void);
gboolean _gdk_wayland_can_place_toplevels (void);
void _gdk_wayland_error_trap_push (void);
gint _gdk_wayland_error_trap_pop (void);
void _gdk_wayland_set_sm_client_id (const gchar *sm_client_id);
gchar* _gdk_wayland_set_locale (void);
gboolean _gdk_wayland_event_send_client_message (GdkEvent *event, guint32 xid);
void _gdk_wayland_event_send_clientmessage_toall (GdkEvent *event);
GdkAtom _gdk_wayland_atom_intern (const gchar *atom_name, gint only_if_exists);
gchar* _gdk_wayland_atom_name (GdkAtom atom);

/* windows */
GdkWindow* _gdk_wayland_window_new (GdkWindow *parent, GdkWindowAttr *attributes, gint attributes_mask);
void _gdk_wayland_window_destroy (GdkWindow *window);
void _gdk_wayland_window_show (GdkWindow *window);
void _gdk_wayland_window_hide (GdkWindow *window);
void _gdk_wayland_window_withdraw (GdkWindow *window);
void _gdk_wayland_window_move (GdkWindow *window, gint x, gint y);
void _gdk_wayland_window_resize (GdkWindow *window, gint width, gint height);
void _gdk_wayland_window_move_resize (GdkWindow *window, gint x, gint y, gint width, gint height);
void _gdk_wayland_window_reparent (GdkWindow *window, GdkWindow *new_parent, gint x, gint y);
void _gdk_wayland_window_raise (GdkWindow *window);
void _gdk_wayland_window_lower (GdkWindow *window);
void _gdk_wayland_window_ensure_native (GdkWindow *window);
gboolean _gdk_wayland_window_begin_move_drag (GdkWindow *window, gint button, gint x_root, gint y_root, guint32 time);
GdkWindow* _gdk_wayland_window_lookup (guint32 anid);
GdkWindow* _gdk_wayland_window_foreign_new (guint32 anid);
void _gdk_wayland_window_set_hints (GdkWindow *window, gint x, gint y, gint min_width, gint min_height, gint max_width, gint max_height, gint flags);
void _gdk_wayland_window_set_geometry_hints (GdkWindow *window, GdkGeometry *geometry, GdkWindowHints flags);
void _gdk_wayland_window_set_title (GdkWindow *window, const gchar *title);
void _gdk_wayland_window_set_role (GdkWindow *window, const gchar *role);
void _gdk_wayland_window_set_transient_for (GdkWindow *window, GdkWindow *leader);
void _gdk_wayland_window_set_cursor (GdkWindow *window, GdkCursor *cursor);
void _gdk_wayland_window_set_colormap (GdkWindow *window, GdkColormap *colormap);
void _gdk_wayland_window_get_geometry (GdkWindow *window, gint *x, gint *y, gint *width, gint *height, gint *depth);
gint _gdk_wayland_window_get_origin (GdkWindow *window, gint *x, gint *y);
gboolean _gdk_wayland_window_get_deskrelative_origin (GdkWindow *window, gint *x, gint *y);
void _gdk_wayland_window_get_root_origin (GdkWindow *window, gint *x, gint *y);
GdkWindow* _gdk_wayland_window_get_pointer (GdkWindow *window, gint *x, gint *y, GdkModifierType *mask);
GdkWindow* _gdk_wayland_window_at_pointer (gint *win_x, gint *win_y);
void _gdk_wayland_window_set_events (GdkWindow *window, GdkEventMask event_mask);
void _gdk_wayland_window_shape_combine_mask (GdkWindow *window, GdkBitmap *shape_mask, gint offset_x, gint offset_y);
void _gdk_wayland_window_set_override_redirect (GdkWindow *window, gboolean override_redirect);
void _gdk_wayland_window_set_icon (GdkWindow *window, GdkWindow *icon_window, GdkPixmap *pixmap, GdkBitmap *mask);
void _gdk_wayland_window_set_icon_name (GdkWindow *window, const gchar *name);
void _gdk_wayland_window_set_icon_images (GdkWindow *window, const GdkIconImage *images, gint n_images);
void _gdk_wayland_window_set_themed_icon (GdkWindow *window, const gchar *name);
void _gdk_wayland_window_set_group (GdkWindow *window, GdkWindow *leader);
void _gdk_wayland_window_set_group_resize (GdkWindow *window, gboolean horizontally, gboolean vertically);
void _gdk_wayland_window_set_decorations (GdkWindow *window, GdkWMDecoration decorations);
void _gdk_wayland_window_set_functions (GdkWindow *window, GdkWMFunction functions);
void _gdk_wayland_window_set_title_colors (GdkWindow *window, const GdkColor *colors);
void _gdk_wayland_window_iconify (GdkWindow *window);
void _gdk_wayland_window_set_sticky (GdkWindow *window, gboolean sticky);
void _gdk_wayland_window_set_keep_above (GdkWindow *window, gboolean setting);
void _gdk_wayland_window_set_skip_taskbar_hint (GdkWindow *window, gboolean setting);
void _gdk_wayland_window_register_dnd (GdkWindow *window);

/* pointer and keyboard */
gint _gdk_wayland_pointer_grab (GdkWindow *window, gint owner_events, GdkEventMask event_mask, GdkWindow *confine_to, GdkCursor *cursor, guint32 time);
void _gdk_wayland_pointer_ungrab (guint32 time);
gboolean _gdk_wayland_pointer_is_grabbed (void);
gint _gdk_wayland_keyboard_grab (GdkWindow *window, gboolean owner_events, guint32 time);
void _gdk_wayland_keyboard_ungrab (guint32 time);
void _gdk_wayland_key_repeat_disable (void);
void _gdk_wayland_key_repeat_restore (void);
gchar* _gdk_wayland_keyval_name (guint keyval);
guint _gdk_wayland_keyval_from_name (const gchar *keyval_name);
guint _gdk_wayland_keyval_to_upper (guint keyval);
guint _gdk_wayland_keyval_to_lower (guint keyval);

/* cursors */
GdkCursor* _gdk_wayland_cursor_new (GdkCursorType cursor_type);
GdkCursor* _gdk_wayland_cursor_new_from_pixmap (GdkPixmap *source, GdkPixmap *mask, GdkColor *fg, GdkColor *bg, gint x, gint y);
void _gdk_wayland_cursor_destroy (GdkCursor *cursor);

/* properties, selections, drag and drop */
gboolean _gdk_wayland_property_get (GdkWindow *window, GdkAtom property, GdkAtom type, gulong offset, gulong length, gint pdelete, GdkAtom *actual_property_type, gint *actual_format, gint *actual_length, guchar **data);
void _gdk_wayland_property_change (GdkWindow *window, GdkAtom property, GdkAtom type, gint format, GdkPropMode mode, guchar *data, gint nelements);
void _gdk_wayland_property_delete (GdkWindow *window, GdkAtom property);
gboolean _gdk_wayland_selection_owner_set (GdkWindow *owner, GdkAtom selection, guint32 time, gint send_event);
GdkWindow* _gdk_wayland_selection_owner_get (GdkAtom selection);
void _gdk_wayland_selection_convert (GdkWindow *requestor, GdkAtom selection, GdkAtom target, guint32 time);
gboolean _gdk_wayland_selection_property_get (GdkWindow *requestor, guchar **data, GdkAtom *prop_type, gint *prop_format);
void _gdk_wayland_selection_send_notify (guint32 requestor, GdkAtom selection, GdkAtom target, GdkAtom property, guint32 time);
gint _gdk_wayland_selection_max_size (void);
GdkDragContext * _gdk_wayland_drag_context_new (void);
void _gdk_wayland_drag_context_ref (GdkDragContext *context);
void _gdk_wayland_drag_context_unref (GdkDragContext *context);
void _gdk_wayland_drag_status (GdkDragContext *context, GdkDragAction action, guint32 time);
void _gdk_wayland_drop_reply (GdkDragContext *context, gboolean ok, guint32 time);
void _gdk_wayland_drop_finish (GdkDragContext *context, gboolean success, guint32 time);
GdkAtom _gdk_wayland_drag_get_selection (GdkDragContext *context);
GdkDragContext * _gdk_wayland_drag_begin (GdkWindow *window, GList *targets);
guint32 _gdk_wayland_drag_get_protocol (guint32 xid, GdkDragProtocol *protocol);
void _gdk_wayland_drag_find_window (GdkDragContext *context, GdkWindow *drag_window, gint x_root, gint y_root, GdkWindow **dest_window, GdkDragProtocol *protocol);
gboolean _gdk_wayland_drag_motion (GdkDragContext *context, GdkWindow *dest_window, GdkDragProtocol protocol, gint x_root, gint y_root, GdkDragAction suggested_action, GdkDragAction possible_actions, guint32 time);
void _gdk_wayland_drag_drop (GdkDragContext *context, guint32 time);
void _gdk_wayland_drag_abort (GdkDragContext *context, guint32 time);

/* text and input methods */
gint _gdk_wayland_text_property_to_text_list (GdkAtom encoding, gint format, guchar *text, gint length, gchar ***list);
void _gdk_wayland_free_text_list (gchar **list);
gint _gdk_wayland_string_to_compound_text (const gchar *str, GdkAtom *encoding, gint *format, guchar **ctext, gint *length);
void _gdk_wayland_free_compound_text (guchar *ctext);
extern const GdkInputContextClass _gdk_wayland_input_context_class;

#endif /* __GDK_BACKEND_WAYLAND_H__ */
