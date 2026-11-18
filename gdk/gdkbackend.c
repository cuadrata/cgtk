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
 * The public functions the backend implements, each a call through
 * _gdk_backend. Generated from the prototypes in gdk.h; the choice of
 * backend is at the end.
 */

#include <stdlib.h>
#include <string.h>
#include "gdk.h"
#include "gdkprivate.h"
#include "gdkbackend.h"
#include "gdkftfont.h"

const GdkBackend *_gdk_backend = NULL;

/* For what may be called before gdk_init: the backend chosen now. */
static void
_gdk_backend_ensure (void)
{
  if (!_gdk_backend)
    _gdk_backend = _gdk_backend_choose ();
}

/* The generic side's hooks. */
void
_gdk_backend_publish (GdkWindow *window, const GdkRectangle *rect)
{
  _gdk_backend->publish (window, rect);
}

void
_gdk_backend_get_image (GdkWindow *window, gint x, gint y, GdkImage *image)
{
  _gdk_backend->get_image (window, x, y, image);
}

void
_gdk_backend_pixmap_free (GdkWindowPrivate *private)
{
  _gdk_backend->pixmap_free (private);
}

void
_gdk_backend_image_free (GdkImagePrivate *private)
{
  _gdk_backend->image_free (private);
}

void
_gdk_backend_gc_free (GdkGCPrivate *private)
{
  _gdk_backend->gc_free (private);
}

void
_gdk_backend_window_free (GdkWindowPrivate *private)
{
  _gdk_backend->window_free (private);
}

gdouble
_gdk_backend_dpi (void)
{
  return _gdk_backend->dpi ();
}

void
_gdk_backend_cursor_changed (GdkWindow *native)
{
  _gdk_backend->cursor_changed (native);
}

gint
gdk_window_get_refresh_rate (GdkWindow *window)
{
  g_return_val_if_fail (window != NULL, 0);

  if (((GdkWindowPrivate *) window)->destroyed ||
      ((GdkWindowPrivate *) window)->window_type == GDK_WINDOW_PIXMAP)
    return 0;
  return _gdk_backend->window_refresh_rate (window);
}

/* display */

void
gdk_beep (void)
{
  _gdk_backend->beep ();
}

gint
gdk_screen_width (void)
{
  return _gdk_backend->screen_width ();
}

gint
gdk_screen_height (void)
{
  return _gdk_backend->screen_height ();
}

gint
gdk_screen_width_mm (void)
{
  return _gdk_backend->screen_width_mm ();
}

gint
gdk_screen_height_mm (void)
{
  return _gdk_backend->screen_height_mm ();
}

gboolean
gdk_can_place_toplevels (void)
{
  return _gdk_backend->can_place_toplevels ();
}

void
gdk_error_trap_push (void)
{
  _gdk_backend->error_trap_push ();
}

gint
gdk_error_trap_pop (void)
{
  return _gdk_backend->error_trap_pop ();
}

void
gdk_set_sm_client_id (const gchar *sm_client_id)
{
  _gdk_backend->set_sm_client_id (sm_client_id);
}

gchar*
gdk_set_locale (void)
{
  _gdk_backend_ensure ();
  return _gdk_backend->set_locale ();
}

gboolean
gdk_event_send_client_message (GdkEvent *event, guint32 xid)
{
  return _gdk_backend->event_send_client_message (event, xid);
}

void
gdk_event_send_clientmessage_toall (GdkEvent *event)
{
  g_return_if_fail (event != NULL);

  _gdk_backend->event_send_clientmessage_toall (event);
  _gdk_broadcast_send (&event->client);
}

GdkAtom
gdk_atom_intern (const gchar *atom_name, gint only_if_exists)
{
  return _gdk_backend->atom_intern (atom_name, only_if_exists);
}

gchar*
gdk_atom_name (GdkAtom atom)
{
  return _gdk_backend->atom_name (atom);
}

/* windows */

GdkWindow*
gdk_window_new (GdkWindow *parent, GdkWindowAttr *attributes, gint attributes_mask)
{
  return _gdk_backend->window_new (parent, attributes, attributes_mask);
}

void
gdk_window_destroy (GdkWindow *window)
{
  _gdk_backend->window_destroy (window);
}

void
gdk_window_show (GdkWindow *window)
{
  _gdk_backend->window_show (window);
}

void
gdk_window_hide (GdkWindow *window)
{
  _gdk_backend->window_hide (window);
}

void
gdk_window_withdraw (GdkWindow *window)
{
  _gdk_backend->window_withdraw (window);
}

void
gdk_window_move (GdkWindow *window, gint x, gint y)
{
  _gdk_backend->window_move (window, x, y);
}

void
gdk_window_resize (GdkWindow *window, gint width, gint height)
{
  _gdk_backend->window_resize (window, width, height);
}

void
gdk_window_move_resize (GdkWindow *window, gint x, gint y, gint width, gint height)
{
  _gdk_backend->window_move_resize (window, x, y, width, height);
}

void
gdk_window_reparent (GdkWindow *window, GdkWindow *new_parent, gint x, gint y)
{
  _gdk_backend->window_reparent (window, new_parent, x, y);
}

void
gdk_window_raise (GdkWindow *window)
{
  _gdk_backend->window_raise (window);
}

void
gdk_window_lower (GdkWindow *window)
{
  _gdk_backend->window_lower (window);
}

void
gdk_window_ensure_native (GdkWindow *window)
{
  _gdk_backend->window_ensure_native (window);
}

gboolean
gdk_window_begin_move_drag (GdkWindow *window, gint button, gint x_root, gint y_root, guint32 time)
{
  return _gdk_backend->window_begin_move_drag (window, button, x_root, y_root, time);
}

GdkWindow*
gdk_window_lookup (guint32 anid)
{
  return _gdk_backend->window_lookup (anid);
}

GdkWindow*
gdk_window_foreign_new (guint32 anid)
{
  return _gdk_backend->window_foreign_new (anid);
}

void
gdk_window_set_hints (GdkWindow *window, gint x, gint y, gint min_width, gint min_height, gint max_width, gint max_height, gint flags)
{
  _gdk_backend->window_set_hints (window, x, y, min_width, min_height, max_width, max_height, flags);
}

void
gdk_window_set_geometry_hints (GdkWindow *window, GdkGeometry *geometry, GdkWindowHints flags)
{
  _gdk_backend->window_set_geometry_hints (window, geometry, flags);
}

void
gdk_window_set_title (GdkWindow *window, const gchar *title)
{
  _gdk_backend->window_set_title (window, title);
}

void
gdk_window_set_role (GdkWindow *window, const gchar *role)
{
  _gdk_backend->window_set_role (window, role);
}

void
gdk_window_set_transient_for (GdkWindow *window, GdkWindow *leader)
{
  _gdk_backend->window_set_transient_for (window, leader);
}

void
gdk_window_set_cursor (GdkWindow *window, GdkCursor *cursor)
{
  _gdk_backend->window_set_cursor (window, cursor);
}

void
gdk_window_set_colormap (GdkWindow *window, GdkColormap *colormap)
{
  _gdk_backend->window_set_colormap (window, colormap);
}

void
gdk_window_get_geometry (GdkWindow *window, gint *x, gint *y, gint *width, gint *height, gint *depth)
{
  _gdk_backend->window_get_geometry (window, x, y, width, height, depth);
}

gint
gdk_window_get_origin (GdkWindow *window, gint *x, gint *y)
{
  return _gdk_backend->window_get_origin (window, x, y);
}

gboolean
gdk_window_get_deskrelative_origin (GdkWindow *window, gint *x, gint *y)
{
  return _gdk_backend->window_get_deskrelative_origin (window, x, y);
}

void
gdk_window_get_root_origin (GdkWindow *window, gint *x, gint *y)
{
  _gdk_backend->window_get_root_origin (window, x, y);
}

GdkWindow*
gdk_window_get_pointer (GdkWindow *window, gint *x, gint *y, GdkModifierType *mask)
{
  return _gdk_backend->window_get_pointer (window, x, y, mask);
}

GdkWindow*
gdk_window_at_pointer (gint *win_x, gint *win_y)
{
  return _gdk_backend->window_at_pointer (win_x, win_y);
}

void
gdk_window_set_events (GdkWindow *window, GdkEventMask event_mask)
{
  _gdk_backend->window_set_events (window, event_mask);
}

void
gdk_window_shape_combine_mask (GdkWindow *window, GdkBitmap *shape_mask, gint offset_x, gint offset_y)
{
  _gdk_backend->window_shape_combine_mask (window, shape_mask, offset_x, offset_y);
}

void
gdk_window_set_override_redirect (GdkWindow *window, gboolean override_redirect)
{
  _gdk_backend->window_set_override_redirect (window, override_redirect);
}

void
gdk_window_set_icon (GdkWindow *window, GdkWindow *icon_window, GdkPixmap *pixmap, GdkBitmap *mask)
{
  _gdk_backend->window_set_icon (window, icon_window, pixmap, mask);
}

void
gdk_window_set_icon_name (GdkWindow *window, const gchar *name)
{
  _gdk_backend->window_set_icon_name (window, name);
}

void
gdk_window_set_icon_images (GdkWindow *window, const GdkIconImage *images, gint n_images)
{
  g_return_if_fail (window != NULL);
  g_return_if_fail (n_images == 0 || images != NULL);

  _gdk_backend->window_set_icon_images (window, images, n_images);
}

void
gdk_window_set_themed_icon (GdkWindow *window, const gchar *name)
{
  g_return_if_fail (window != NULL);
  g_return_if_fail (name != NULL);

  _gdk_backend->window_set_themed_icon (window, name);
}

void
gdk_window_set_group (GdkWindow *window, GdkWindow *leader)
{
  _gdk_backend->window_set_group (window, leader);
}

void
gdk_window_set_group_resize (GdkWindow *window, gboolean horizontally, gboolean vertically)
{
  _gdk_backend->window_set_group_resize (window, horizontally, vertically);
}

void
gdk_window_set_decorations (GdkWindow *window, GdkWMDecoration decorations)
{
  _gdk_backend->window_set_decorations (window, decorations);
}

void
gdk_window_set_functions (GdkWindow *window, GdkWMFunction functions)
{
  _gdk_backend->window_set_functions (window, functions);
}

void
gdk_window_set_title_colors (GdkWindow *window, const GdkColor *colors)
{
  g_return_if_fail (window != NULL);
  g_return_if_fail (colors != NULL);

  _gdk_backend->window_set_title_colors (window, colors);
}

void
gdk_window_iconify (GdkWindow *window)
{
  g_return_if_fail (window != NULL);

  _gdk_backend->window_iconify (window);
}

void
gdk_window_stick (GdkWindow *window)
{
  g_return_if_fail (window != NULL);

  _gdk_backend->window_set_sticky (window, TRUE);
}

void
gdk_window_unstick (GdkWindow *window)
{
  g_return_if_fail (window != NULL);

  _gdk_backend->window_set_sticky (window, FALSE);
}

void
gdk_window_set_keep_above (GdkWindow *window, gboolean setting)
{
  g_return_if_fail (window != NULL);

  _gdk_backend->window_set_keep_above (window, setting);
}

void
gdk_window_set_skip_taskbar_hint (GdkWindow *window, gboolean setting)
{
  g_return_if_fail (window != NULL);

  _gdk_backend->window_set_skip_taskbar_hint (window, setting);
}

void
gdk_window_set_child_shapes (GdkWindow *window)
{
  g_return_if_fail (window != NULL);
}

void
gdk_window_merge_child_shapes (GdkWindow *window)
{
  g_return_if_fail (window != NULL);
}

/*
 * Static gravity kept a window's pixels and its children where they were
 * on screen while it moved and resized; GTK+ 1.2 used it to scroll
 * GtkLayout. Nothing needs it, so, as in GTK 3, it is never there: TRUE
 * only when turning it off.
 */
gboolean
gdk_window_set_static_gravities (GdkWindow *window, gboolean use_static)
{
  g_return_val_if_fail (window != NULL, FALSE);

  return !use_static;
}

void
gdk_window_register_dnd (GdkWindow *window)
{
  _gdk_backend->window_register_dnd (window);
}

/* pointer and keyboard */

gint
gdk_pointer_grab (GdkWindow *window, gint owner_events, GdkEventMask event_mask, GdkWindow *confine_to, GdkCursor *cursor, guint32 time)
{
  return _gdk_backend->pointer_grab (window, owner_events, event_mask, confine_to, cursor, time);
}

void
gdk_pointer_ungrab (guint32 time)
{
  _gdk_backend->pointer_ungrab (time);
}

gboolean
gdk_pointer_is_grabbed (void)
{
  return _gdk_backend->pointer_is_grabbed ();
}

gint
gdk_keyboard_grab (GdkWindow *window, gboolean owner_events, guint32 time)
{
  return _gdk_backend->keyboard_grab (window, owner_events, time);
}

void
gdk_keyboard_ungrab (guint32 time)
{
  _gdk_backend->keyboard_ungrab (time);
}

void
gdk_key_repeat_disable (void)
{
  _gdk_backend->key_repeat_disable ();
}

void
gdk_key_repeat_restore (void)
{
  _gdk_backend->key_repeat_restore ();
}

gchar*
gdk_keyval_name (guint keyval)
{
  return _gdk_backend->keyval_name (keyval);
}

guint
gdk_keyval_from_name (const gchar *keyval_name)
{
  return _gdk_backend->keyval_from_name (keyval_name);
}

guint
gdk_keyval_to_upper (guint keyval)
{
  return _gdk_backend->keyval_to_upper (keyval);
}

guint
gdk_keyval_to_lower (guint keyval)
{
  return _gdk_backend->keyval_to_lower (keyval);
}

gboolean
gdk_keyval_is_upper (guint keyval)
{
  /* 0, no key, is either, as Xlib has it */
  return !keyval || gdk_keyval_to_upper (keyval) == keyval;
}

gboolean
gdk_keyval_is_lower (guint keyval)
{
  return !keyval || gdk_keyval_to_lower (keyval) == keyval;
}

/* cursors */

GdkCursor*
gdk_cursor_new (GdkCursorType cursor_type)
{
  return _gdk_backend->cursor_new (cursor_type);
}

GdkCursor*
gdk_cursor_new_from_pixmap (GdkPixmap *source, GdkPixmap *mask, GdkColor *fg, GdkColor *bg, gint x, gint y)
{
  return _gdk_backend->cursor_new_from_pixmap (source, mask, fg, bg, x, y);
}

void
gdk_cursor_destroy (GdkCursor *cursor)
{
  _gdk_backend->cursor_destroy (cursor);
}

/* properties, selections, drag and drop */

gboolean
gdk_property_get (GdkWindow *window, GdkAtom property, GdkAtom type, gulong offset, gulong length, gint pdelete, GdkAtom *actual_property_type, gint *actual_format, gint *actual_length, guchar **data)
{
  return _gdk_backend->property_get (window, property, type, offset, length, pdelete, actual_property_type, actual_format, actual_length, data);
}

void
gdk_property_change (GdkWindow *window, GdkAtom property, GdkAtom type, gint format, GdkPropMode mode, guchar *data, gint nelements)
{
  _gdk_backend->property_change (window, property, type, format, mode, data, nelements);
}

void
gdk_property_delete (GdkWindow *window, GdkAtom property)
{
  _gdk_backend->property_delete (window, property);
}

gboolean
gdk_selection_owner_set (GdkWindow *owner, GdkAtom selection, guint32 time, gint send_event)
{
  return _gdk_backend->selection_owner_set (owner, selection, time, send_event);
}

GdkWindow*
gdk_selection_owner_get (GdkAtom selection)
{
  return _gdk_backend->selection_owner_get (selection);
}

void
gdk_selection_convert (GdkWindow *requestor, GdkAtom selection, GdkAtom target, guint32 time)
{
  _gdk_backend->selection_convert (requestor, selection, target, time);
}

gboolean
gdk_selection_property_get (GdkWindow *requestor, guchar **data, GdkAtom *prop_type, gint *prop_format)
{
  return _gdk_backend->selection_property_get (requestor, data, prop_type, prop_format);
}

void
gdk_selection_send_notify (guint32 requestor, GdkAtom selection, GdkAtom target, GdkAtom property, guint32 time)
{
  _gdk_backend->selection_send_notify (requestor, selection, target, property, time);
}

gint
gdk_selection_max_size (void)
{
  return _gdk_backend->selection_max_size ();
}

GdkDragContext *
gdk_drag_context_new (void)
{
  return _gdk_backend->drag_context_new ();
}

void
gdk_drag_context_ref (GdkDragContext *context)
{
  _gdk_backend->drag_context_ref (context);
}

void
gdk_drag_context_unref (GdkDragContext *context)
{
  _gdk_backend->drag_context_unref (context);
}

void
gdk_drag_status (GdkDragContext *context, GdkDragAction action, guint32 time)
{
  _gdk_backend->drag_status (context, action, time);
}

void
gdk_drop_reply (GdkDragContext *context, gboolean ok, guint32 time)
{
  _gdk_backend->drop_reply (context, ok, time);
}

void
gdk_drop_finish (GdkDragContext *context, gboolean success, guint32 time)
{
  _gdk_backend->drop_finish (context, success, time);
}

GdkAtom
gdk_drag_get_selection (GdkDragContext *context)
{
  return _gdk_backend->drag_get_selection (context);
}

GdkDragContext *
gdk_drag_begin (GdkWindow *window, GList *targets)
{
  return _gdk_backend->drag_begin (window, targets);
}

guint32
gdk_drag_get_protocol (guint32 xid, GdkDragProtocol *protocol)
{
  return _gdk_backend->drag_get_protocol (xid, protocol);
}

void
gdk_drag_find_window (GdkDragContext *context, GdkWindow *drag_window, gint x_root, gint y_root, GdkWindow **dest_window, GdkDragProtocol *protocol)
{
  _gdk_backend->drag_find_window (context, drag_window, x_root, y_root, dest_window, protocol);
}

gboolean
gdk_drag_motion (GdkDragContext *context, GdkWindow *dest_window, GdkDragProtocol protocol, gint x_root, gint y_root, GdkDragAction suggested_action, GdkDragAction possible_actions, guint32 time)
{
  return _gdk_backend->drag_motion (context, dest_window, protocol, x_root, y_root, suggested_action, possible_actions, time);
}

void
gdk_drag_drop (GdkDragContext *context, guint32 time)
{
  _gdk_backend->drag_drop (context, time);
}

void
gdk_drag_abort (GdkDragContext *context, guint32 time)
{
  _gdk_backend->drag_abort (context, time);
}

/* text and input methods */

/*
 * Text is UTF-8 unless the locale has an encoding of its own, as fonts
 * read it. Bytes that are not valid text become escapes that
 * gdk_wcstombs turns back into the same bytes, so neither ever fails,
 * and a label is never left showing its old text because its new text
 * had a stray Latin-1 byte in it.
 */
gint
gdk_mbstowcs (GdkWChar *dest, const gchar *src, gint dest_max)
{
  gint length = strlen (src);
  GdkWChar *chars = g_new (GdkWChar, MAX (length, 1));
  gint n = _gdk_ft_decode_text (src, length, chars, TRUE);

  n = MIN (n, dest_max);
  memcpy (dest, chars, n * sizeof (GdkWChar));
  g_free (chars);

  return n;
}

gchar *
gdk_wcstombs (const GdkWChar *src)
{
  gint length = 0;

  while (src[length] != 0)
    length++;

  return _gdk_ft_encode_text (src, length);
}

gint
gdk_text_property_to_text_list (GdkAtom encoding, gint format, guchar *text, gint length, gchar ***list)
{
  return _gdk_backend->text_property_to_text_list (encoding, format, text, length, list);
}

void
gdk_free_text_list (gchar **list)
{
  _gdk_backend->free_text_list (list);
}

gint
gdk_string_to_compound_text (const gchar *str, GdkAtom *encoding, gint *format, guchar **ctext, gint *length)
{
  return _gdk_backend->string_to_compound_text (str, encoding, format, ctext, length);
}

void
gdk_free_compound_text (guchar *ctext)
{
  _gdk_backend->free_compound_text (ctext);
}

/*
 * The GTK+ 1.2 input method calls. Nothing used them but GtkEntry and
 * GtkText, which now go through input contexts (gdk_input_context_new),
 * so these are here for programs that still name them: there is never an
 * input method ready, and never a GdkIC.
 */

gboolean
gdk_im_ready (void)
{
  return FALSE;
}

void
gdk_im_begin (GdkIC *ic, GdkWindow *window)
{
}

void
gdk_im_end (void)
{
}

GdkIMStyle
gdk_im_decide_style (GdkIMStyle supported_style)
{
  return GDK_IM_PREEDIT_NONE | GDK_IM_STATUS_NONE;
}

GdkIMStyle
gdk_im_set_best_style (GdkIMStyle best_allowed_style)
{
  return GDK_IM_PREEDIT_NONE | GDK_IM_STATUS_NONE;
}

GdkIC*
gdk_ic_new (GdkICAttr *attr, GdkICAttributesType mask)
{
  return NULL;
}

void
gdk_ic_destroy (GdkIC *ic)
{
}

GdkIMStyle
gdk_ic_get_style (GdkIC *ic)
{
  return GDK_IM_PREEDIT_NONE | GDK_IM_STATUS_NONE;
}

GdkICAttributesType
gdk_ic_set_attr (GdkIC *ic, GdkICAttr *attr, GdkICAttributesType mask)
{
  return mask;
}

GdkICAttributesType
gdk_ic_get_attr (GdkIC *ic, GdkICAttr *attr, GdkICAttributesType mask)
{
  return mask;
}

GdkEventMask
gdk_ic_get_events (GdkIC *ic)
{
  return 0;
}

GdkICAttr*
gdk_ic_attr_new (void)
{
  return g_new0 (GdkICAttr, 1);
}

void
gdk_ic_attr_destroy (GdkICAttr *attr)
{
  g_free (attr);
}

/*
 * Input devices: the core pointer only, on every backend. Its source
 * can be set; its mode cannot, and it has no keys or other axes.
 */

static GdkAxisUse gdk_input_core_axes[] = { GDK_AXIS_X, GDK_AXIS_Y };

static GdkDeviceInfo gdk_input_core_info =
{
  GDK_CORE_POINTER,
  "Core Pointer",
  GDK_SOURCE_MOUSE,
  GDK_MODE_SCREEN,
  TRUE,
  2,
  gdk_input_core_axes,
  0,
  NULL
};

void
gdk_input_init (void)
{
}

void
gdk_input_exit (void)
{
}

GList *
gdk_input_list_devices (void)
{
  static GList *devices = NULL;

  if (!devices)
    devices = g_list_append (NULL, &gdk_input_core_info);
  return devices;
}

void
gdk_input_set_source (guint32 deviceid, GdkInputSource source)
{
  g_return_if_fail (deviceid == GDK_CORE_POINTER);

  gdk_input_core_info.source = source;
}

gboolean
gdk_input_set_mode (guint32 deviceid, GdkInputMode mode)
{
  return FALSE;
}

void
gdk_input_set_axes (guint32 deviceid, GdkAxisUse *axes)
{
}

void
gdk_input_set_key (guint32 deviceid, guint index, guint keyval, GdkModifierType modifiers)
{
}

/* The pointer's motion history, where the window system keeps one. */
GdkTimeCoord *
gdk_input_motion_events (GdkWindow *window, guint32 deviceid, guint32 start, guint32 stop, gint *nevents_return)
{
  g_return_val_if_fail (window != NULL, NULL);

  if (deviceid == GDK_CORE_POINTER && _gdk_backend->input_motion_events &&
      !((GdkWindowPrivate *) window)->destroyed)
    return _gdk_backend->input_motion_events (window, start, stop, nevents_return);

  *nevents_return = 0;
  return NULL;
}

/* There are no extension events. The mask is remembered all the same,
 * because GdkWindowPrivate, which holds it, is in an installed header. */
void
gdk_input_set_extension_events (GdkWindow *window, gint mask, GdkExtensionMode mode)
{
  GdkWindowPrivate *window_private = (GdkWindowPrivate *) window;

  g_return_if_fail (window != NULL);
  if (window_private->destroyed)
    return;

  if (mode == GDK_EXTENSION_EVENTS_NONE)
    mask = 0;
  window_private->extension_events = mask;
}

void
gdk_input_window_get_pointer (GdkWindow *window, guint32 deviceid, gdouble *x, gdouble *y, gdouble *pressure, gdouble *xtilt, gdouble *ytilt, GdkModifierType *mask)
{
  gint ix, iy;

  gdk_window_get_pointer (window, &ix, &iy, mask);
  if (x) *x = ix;
  if (y) *y = iy;
  if (pressure) *pressure = 0.5;
  if (xtilt) *xtilt = 0;
  if (ytilt) *ytilt = 0;
}

/*
 * Which backend: the one GDK_BACKEND names, if it is compiled in; else
 * Wayland when there is a Wayland display to talk to, else X11.
 */
const GdkBackend *
_gdk_backend_choose (void)
{
  const gchar *want = getenv ("GDK_BACKEND");

#ifdef GDK_WINDOWING_WAYLAND
  if (want && strcmp (want, "wayland") == 0)
    return &_gdk_wayland_backend;
#endif
#ifdef GDK_WINDOWING_X11
  if (want && strcmp (want, "x11") == 0)
    return &_gdk_x11_backend;
#endif
  if (want)
    g_warning ("GDK_BACKEND=%s is not a backend GDK was built with", want);

#ifdef GDK_WINDOWING_WAYLAND
  if (getenv ("WAYLAND_DISPLAY"))
    return &_gdk_wayland_backend;
#endif
#ifdef GDK_WINDOWING_X11
  return &_gdk_x11_backend;
#endif
#ifdef GDK_WINDOWING_WAYLAND
  return &_gdk_wayland_backend;
#endif
  return NULL;
}
