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

/* The x11 backend, as GDK sees it. */

#include "gdk.h"
#include "gdkprivate-x11.h"
#include "gdkbackend-x11.h"

const GdkBackend _gdk_x11_backend = {
  "x11",

  _gdk_x11_init,
  _gdk_x11_exit,

  _gdk_x11_events_pending,
  _gdk_x11_events_queue_all,
  _gdk_x11_event_fd,
  _gdk_x11_flush,

  _gdk_x11_publish,
  _gdk_x11_get_image,
  _gdk_x11_pixmap_free,
  _gdk_x11_image_free,
  _gdk_x11_gc_free,
  _gdk_x11_window_free,
  _gdk_x11_dpi,
  _gdk_x11_cursor_changed,
  _gdk_x11_window_refresh_rate,

  /* display */
  _gdk_x11_beep,
  _gdk_x11_screen_width,
  _gdk_x11_screen_height,
  _gdk_x11_screen_width_mm,
  _gdk_x11_screen_height_mm,
  _gdk_x11_can_place_toplevels,
  _gdk_x11_error_trap_push,
  _gdk_x11_error_trap_pop,
  _gdk_x11_set_sm_client_id,
  _gdk_x11_set_locale,
  _gdk_x11_event_send_client_message,
  _gdk_x11_event_send_clientmessage_toall,
  _gdk_x11_atom_intern,
  _gdk_x11_atom_name,

  /* windows */
  _gdk_x11_window_new,
  _gdk_x11_window_destroy,
  _gdk_x11_window_show,
  _gdk_x11_window_hide,
  _gdk_x11_window_withdraw,
  _gdk_x11_window_move,
  _gdk_x11_window_resize,
  _gdk_x11_window_move_resize,
  _gdk_x11_window_reparent,
  _gdk_x11_window_raise,
  _gdk_x11_window_lower,
  _gdk_x11_window_ensure_native,
  _gdk_x11_window_begin_move_drag,
  _gdk_x11_window_lookup,
  _gdk_x11_window_foreign_new,
  _gdk_x11_window_set_hints,
  _gdk_x11_window_set_geometry_hints,
  _gdk_x11_window_set_title,
  _gdk_x11_window_set_role,
  _gdk_x11_window_set_transient_for,
  _gdk_x11_window_set_cursor,
  _gdk_x11_window_set_colormap,
  _gdk_x11_window_get_geometry,
  _gdk_x11_window_get_origin,
  _gdk_x11_window_get_deskrelative_origin,
  _gdk_x11_window_get_root_origin,
  _gdk_x11_window_get_pointer,
  _gdk_x11_window_at_pointer,
  _gdk_x11_window_set_events,
  _gdk_x11_window_shape_combine_mask,
  _gdk_x11_window_set_override_redirect,
  _gdk_x11_window_set_icon,
  _gdk_x11_window_set_icon_name,
  _gdk_x11_window_set_icon_images,
  _gdk_x11_window_set_themed_icon,
  _gdk_x11_window_set_group,
  _gdk_x11_window_set_group_resize,
  _gdk_x11_window_set_decorations,
  _gdk_x11_window_set_functions,
  _gdk_x11_window_set_title_colors,
  _gdk_x11_window_iconify,
  _gdk_x11_window_set_sticky,
  _gdk_x11_window_set_keep_above,
  _gdk_x11_window_set_skip_taskbar_hint,
  _gdk_x11_window_register_dnd,

  /* pointer and keyboard */
  _gdk_x11_pointer_grab,
  _gdk_x11_pointer_ungrab,
  _gdk_x11_pointer_is_grabbed,
  _gdk_x11_keyboard_grab,
  _gdk_x11_keyboard_ungrab,
  _gdk_x11_key_repeat_disable,
  _gdk_x11_key_repeat_restore,
  _gdk_x11_keyval_name,
  _gdk_x11_keyval_from_name,
  _gdk_x11_keyval_to_upper,
  _gdk_x11_keyval_to_lower,

  /* cursors */
  _gdk_x11_cursor_new,
  _gdk_x11_cursor_new_from_pixmap,
  _gdk_x11_cursor_destroy,

  /* properties, selections, drag and drop */
  _gdk_x11_property_get,
  _gdk_x11_property_change,
  _gdk_x11_property_delete,
  _gdk_x11_selection_owner_set,
  _gdk_x11_selection_owner_get,
  _gdk_x11_selection_convert,
  _gdk_x11_selection_property_get,
  _gdk_x11_selection_send_notify,
  _gdk_x11_selection_max_size,
  _gdk_x11_drag_context_new,
  _gdk_x11_drag_context_ref,
  _gdk_x11_drag_context_unref,
  _gdk_x11_drag_status,
  _gdk_x11_drop_reply,
  _gdk_x11_drop_finish,
  _gdk_x11_drag_get_selection,
  _gdk_x11_drag_begin,
  _gdk_x11_drag_get_protocol,
  _gdk_x11_drag_find_window,
  _gdk_x11_drag_motion,
  _gdk_x11_drag_drop,
  _gdk_x11_drag_abort,

  /* text and input methods */
  _gdk_x11_text_property_to_text_list,
  _gdk_x11_free_text_list,
  _gdk_x11_string_to_compound_text,
  _gdk_x11_free_compound_text,
#ifdef USE_XIM
  &_gdk_x11_input_context_class,
#else
  NULL,
#endif

  _gdk_x11_input_motion_events,
};
