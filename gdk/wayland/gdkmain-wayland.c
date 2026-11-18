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
 * The Wayland backend's display: connecting, the globals the
 * compositor offers, the event loop's side of the connection, and the
 * small things -- atoms, key names, grabs -- that need no window.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <sys/mman.h>
#include "gdk.h"
#include "gdkprivate-wayland.h"
#include "gdkbackend-wayland.h"

struct wl_display *_gdk_wl_display = NULL;
static struct wl_registry *_gdk_wl_registry = NULL;
struct wl_compositor *_gdk_wl_compositor = NULL;
struct wl_subcompositor *_gdk_wl_subcompositor = NULL;
struct wl_shm *_gdk_wl_shm = NULL;
struct wl_seat *_gdk_wl_seat = NULL;
struct wl_pointer *_gdk_wl_pointer = NULL;
struct wl_keyboard *_gdk_wl_keyboard = NULL;
static struct wl_output *_gdk_wl_output = NULL;
struct xdg_wm_base *_gdk_xdg_wm_base = NULL;
struct zxdg_decoration_manager_v1 *_gdk_xdg_decoration_manager = NULL;
#ifdef HAVE_XDG_TOPLEVEL_ICON
struct xdg_toplevel_icon_manager_v1 *_gdk_xdg_toplevel_icon_manager = NULL;
#endif
struct wl_data_device_manager *_gdk_wl_data_device_manager = NULL;
struct wl_data_device *_gdk_wl_data_device = NULL;
struct zwp_primary_selection_device_manager_v1 *_gdk_wl_primary_manager = NULL;
struct zwp_text_input_manager_v3 *_gdk_wl_text_input_manager = NULL;
struct wl_cursor_theme *_gdk_wl_cursor_theme = NULL;
struct wl_surface *_gdk_wl_cursor_surface = NULL;

struct xkb_context *_gdk_xkb_context = NULL;
struct xkb_keymap *_gdk_xkb_keymap = NULL;
struct xkb_state *_gdk_xkb_state = NULL;

gint _gdk_wayland_zoom = 1;

/* The scale the pointer's images are at: the output's, not the zoom's. */
gint
_gdk_wayland_output_cursor_scale (void)
{
  return MAX (gdk_screen_scale_factor () / _gdk_wayland_zoom, 1);
}

gint
_gdk_wayland_from_wl (gint v)
{
  gint z = _gdk_wayland_zoom;

  if (z == 1)
    return v;
  return v >= 0 ? v / z : -((-v + z - 1) / z);
}

static gint _gdk_wayland_display_width = 1024;
static gint _gdk_wayland_display_height = 768;
static gint _gdk_wayland_display_width_mm = 0;
static gint _gdk_wayland_display_height_mm = 0;
guint32 _gdk_wayland_last_serial = 0;
guint32 _gdk_wayland_pointer_serial = 0;
guint32 _gdk_wayland_press_serial = 0;
guint32 _gdk_wayland_input_serial = 0;
guint32 _gdk_wayland_last_time = 0;
guint _gdk_wayland_modifiers = 0;
GdkWindowPrivate *_gdk_wayland_pointer_window = NULL;
GdkWindowPrivate *_gdk_wayland_keyboard_window = NULL;
GdkWindowPrivate *_gdk_wayland_grab_popup = NULL;

/* ---------------------------------------------------------------------
 * Globals
 */

static void
xdg_wm_base_ping (void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
  xdg_wm_base_pong (wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
  xdg_wm_base_ping
};

/*
 * Outputs. The first is the display, for the screen size; every one
 * is kept for its scale, which is what its windows are drawn at.
 */
typedef struct
{
  struct wl_output *output;
  guint32 name;                   /* its global's, to know it when it goes */
  gint32 width_mm, height_mm;
  gint scale;                     /* as advertised */
  gint pending_scale;             /* until done says it is in effect */
  gint width, height;             /* the current mode, in its own pixels */
  gint refresh;                   /* and how often it is shown, in mHz */
} GdkWaylandOutput;

static GList *outputs = NULL;

static void
output_geometry (void *data, struct wl_output *output, int32_t x, int32_t y,
                 int32_t physical_width, int32_t physical_height,
                 int32_t subpixel, const char *make, const char *model,
                 int32_t transform)
{
  GdkWaylandOutput *out = data;

  out->width_mm = physical_width;
  out->height_mm = physical_height;
  if (output == _gdk_wl_output)
    {
      _gdk_wayland_display_width_mm = physical_width;
      _gdk_wayland_display_height_mm = physical_height;
    }
}

static void
output_mode (void *data, struct wl_output *output, uint32_t flags,
             int32_t width, int32_t height, int32_t refresh)
{
  GdkWaylandOutput *out = data;

  if (flags & WL_OUTPUT_MODE_CURRENT)
    {
      out->width = width;
      out->height = height;
      out->refresh = refresh;
    }
}

static void
output_done (void *data, struct wl_output *output)
{
  GdkWaylandOutput *out = data;

  if (out->scale != out->pending_scale)
    {
      out->scale = out->pending_scale;
      _gdk_wayland_outputs_changed ();
    }
  /* The mode is in the output's pixels; the screen is in ours. */
  if (output == _gdk_wl_output)
    {
      _gdk_wayland_display_width = out->width / out->scale / _gdk_wayland_zoom;
      _gdk_wayland_display_height = out->height / out->scale / _gdk_wayland_zoom;
    }
}

static void
output_scale (void *data, struct wl_output *output, int32_t factor)
{
  GdkWaylandOutput *out = data;

  out->pending_scale = CLAMP (factor, 1, 16);
}

gint
_gdk_wayland_output_scale (struct wl_output *output)
{
  GList *l;

  for (l = outputs; l; l = l->next)
    if (((GdkWaylandOutput *) l->data)->output == output)
      return ((GdkWaylandOutput *) l->data)->scale;
  return 1;
}

/* How often an output is refreshed, in mHz; of the first output that
 * says, for NULL; 0 if none does. */
gint
_gdk_wayland_output_refresh (struct wl_output *output)
{
  GList *l;

  for (l = outputs; l; l = l->next)
    {
      GdkWaylandOutput *out = l->data;

      if ((!output && out->refresh > 0) || out->output == output)
        return out->refresh;
    }
  return 0;
}

/* The scale a window on no output in particular is at: the biggest. */
static gint
outputs_scale (void)
{
  GList *l;
  gint scale = 1;

  for (l = outputs; l; l = l->next)
    scale = MAX (scale, ((GdkWaylandOutput *) l->data)->scale);
  return scale;
}

/* Outputs are bound at version 2 or lower, which send no later events. */
static const struct wl_output_listener output_listener = {
  .geometry = output_geometry,
  .mode = output_mode,
  .done = output_done,
  .scale = output_scale,
};

static void
seat_capabilities (void *data, struct wl_seat *seat, uint32_t capabilities)
{
  _gdk_wayland_seat_capabilities (capabilities);
}

static void
seat_name (void *data, struct wl_seat *seat, const char *name)
{
}

static const struct wl_seat_listener seat_listener = {
  seat_capabilities, seat_name
};

static void
registry_global (void *data, struct wl_registry *registry, uint32_t name,
                 const char *interface, uint32_t version)
{
  if (strcmp (interface, wl_compositor_interface.name) == 0)
    _gdk_wl_compositor = wl_registry_bind (registry, name, &wl_compositor_interface,
                                           MIN (version, 4));
  else if (strcmp (interface, wl_subcompositor_interface.name) == 0)
    _gdk_wl_subcompositor = wl_registry_bind (registry, name, &wl_subcompositor_interface, 1);
  else if (strcmp (interface, wl_shm_interface.name) == 0)
    _gdk_wl_shm = wl_registry_bind (registry, name, &wl_shm_interface, 1);
  else if (strcmp (interface, wl_seat_interface.name) == 0 && !_gdk_wl_seat)
    {
      _gdk_wl_seat = wl_registry_bind (registry, name, &wl_seat_interface,
                                       MIN (version, 5));
      wl_seat_add_listener (_gdk_wl_seat, &seat_listener, NULL);
    }
  else if (strcmp (interface, wl_output_interface.name) == 0)
    {
      GdkWaylandOutput *out = g_new0 (GdkWaylandOutput, 1);

      out->output = wl_registry_bind (registry, name, &wl_output_interface,
                                      MIN (version, 2));
      out->name = name;
      out->scale = out->pending_scale = 1;
      wl_output_add_listener (out->output, &output_listener, out);
      outputs = g_list_append (outputs, out);
      if (!_gdk_wl_output)
        _gdk_wl_output = out->output;
    }
  else if (strcmp (interface, xdg_wm_base_interface.name) == 0)
    {
      _gdk_xdg_wm_base = wl_registry_bind (registry, name, &xdg_wm_base_interface,
                                           MIN (version, 3));
      xdg_wm_base_add_listener (_gdk_xdg_wm_base, &xdg_wm_base_listener, NULL);
    }
  else if (strcmp (interface, zxdg_decoration_manager_v1_interface.name) == 0)
    _gdk_xdg_decoration_manager = wl_registry_bind (registry, name,
                                                    &zxdg_decoration_manager_v1_interface, 1);
#ifdef HAVE_XDG_TOPLEVEL_ICON
  else if (strcmp (interface, xdg_toplevel_icon_manager_v1_interface.name) == 0)
    _gdk_xdg_toplevel_icon_manager = wl_registry_bind (registry, name,
                                                       &xdg_toplevel_icon_manager_v1_interface, 1);
#endif
  else if (strcmp (interface, zwp_text_input_manager_v3_interface.name) == 0)
    _gdk_wl_text_input_manager = wl_registry_bind (registry, name,
                                                   &zwp_text_input_manager_v3_interface, 1);
  else if (strcmp (interface, zwp_primary_selection_device_manager_v1_interface.name) == 0)
    _gdk_wl_primary_manager = wl_registry_bind (registry, name,
                                                &zwp_primary_selection_device_manager_v1_interface, 1);
  else if (strcmp (interface, wl_data_device_manager_interface.name) == 0)
    _gdk_wl_data_device_manager = wl_registry_bind (registry, name,
                                                    &wl_data_device_manager_interface,
                                                    MIN (version, 3));
}

static void
registry_global_remove (void *data, struct wl_registry *registry, uint32_t name)
{
  GList *l;

  /* Of what can come and go, only outputs are kept track of. */
  for (l = outputs; l; l = l->next)
    {
      GdkWaylandOutput *out = l->data;

      if (out->name != name)
        continue;
      outputs = g_list_remove (outputs, out);
      _gdk_wayland_window_output_gone (out->output);
      if (out->output == _gdk_wl_output)
        {
          /* The screen's size is the next output's now. */
          GdkWaylandOutput *next = outputs ? outputs->data : NULL;

          _gdk_wl_output = next ? next->output : NULL;
          if (next)
            {
              _gdk_wayland_display_width = next->width / next->scale / _gdk_wayland_zoom;
              _gdk_wayland_display_height = next->height / next->scale / _gdk_wayland_zoom;
              _gdk_wayland_display_width_mm = next->width_mm;
              _gdk_wayland_display_height_mm = next->height_mm;
            }
        }
      wl_output_destroy (out->output);
      g_free (out);
      _gdk_wayland_outputs_changed ();
      return;
    }
}

static const struct wl_registry_listener registry_listener = {
  registry_global, registry_global_remove
};

/* ---------------------------------------------------------------------
 * Start and end
 */

gboolean
_gdk_wayland_init (const gchar *display_name,
                   gboolean     synchronize,
                   const gchar *program_class)
{
  _gdk_wl_display = wl_display_connect (display_name);
  if (!_gdk_wl_display)
    return FALSE;

  _gdk_wl_registry = wl_display_get_registry (_gdk_wl_display);
  wl_registry_add_listener (_gdk_wl_registry, &registry_listener, NULL);
  wl_display_roundtrip (_gdk_wl_display);     /* the globals */
  wl_display_roundtrip (_gdk_wl_display);     /* what they said about themselves */

  if (!_gdk_wl_compositor || !_gdk_wl_shm || !_gdk_xdg_wm_base)
    {
      g_warning ("the Wayland compositor lacks %s",
                 !_gdk_wl_compositor ? "wl_compositor" :
                 !_gdk_wl_shm ? "wl_shm" : "xdg_wm_base");
      wl_display_disconnect (_gdk_wl_display);
      _gdk_wl_display = NULL;
      return FALSE;
    }

  /*
   * The scale: what the outputs advertise, the biggest of them for a
   * window not yet on any, and each window's own from the outputs it
   * is on once shown. The cursor is loaded at that size too.
   */
  _gdk_window_set_default_scale (outputs_scale ());
  if (_gdk_scale_override ())
    {
      /*
       * Forced: what the outputs can show of it is sent as buffer
       * scale, the rest is windows that many times bigger. On an
       * output at that scale nothing changes; on one at 1, GDK_SCALE=2
       * doubles the windows, as it does on X11.
       */
      gint want = gdk_screen_scale_factor (), have = outputs_scale ();
      gint buffer_scale = want % have == 0 ? have : 1;

      _gdk_wayland_zoom = want / buffer_scale;
      _gdk_wayland_display_width /= _gdk_wayland_zoom;
      _gdk_wayland_display_height /= _gdk_wayland_zoom;
    }

  _gdk_xkb_context = xkb_context_new (XKB_CONTEXT_NO_FLAGS);
  {
    /*
     * The size every other client uses, at the scale the output wants:
     * the pointer is the desktop's, so a zoom does not enlarge it.
     */
    const gchar *env = getenv ("XCURSOR_SIZE");
    gint size = env ? atoi (env) : 0;

    if (size <= 0)
      size = 24;
    _gdk_wl_cursor_theme = wl_cursor_theme_load (getenv ("XCURSOR_THEME"),
                                                 size * _gdk_wayland_output_cursor_scale (),
                                                 _gdk_wl_shm);
  }
  _gdk_wl_cursor_surface = wl_compositor_create_surface (_gdk_wl_compositor);

  gdk_root_parent.window_type = GDK_WINDOW_ROOT;
  gdk_root_parent.width = _gdk_wayland_display_width;
  gdk_root_parent.height = _gdk_wayland_display_height;
  gdk_root_parent.native = TRUE;
  gdk_root_parent.mapped = TRUE;
  _gdk_window_private_init (&gdk_root_parent);
  gdk_root_parent.native = TRUE;
  gdk_root_parent.impl = g_new0 (GdkWindowImpl, 1);

  _gdk_wayland_events_init ();
  _gdk_wayland_selection_init ();
  _gdk_wayland_input_context_init ();
  gdk_image_init ();

  return TRUE;
}

/*
 * The compositor went away, or dropped the connection over a protocol
 * error, and nothing more can be said to it. As the X11 backend does on
 * losing its server: say why, and leave without the teardown.
 */
static gboolean connection_is_lost = FALSE;

static void
connection_lost (void)
{
  gint err = wl_display_get_error (_gdk_wl_display);

  connection_is_lost = TRUE;
  if (err == EPROTO)
    {
      const struct wl_interface *interface = NULL;
      guint32 id = 0;
      guint32 code = wl_display_get_protocol_error (_gdk_wl_display, &interface, &id);

      fprintf (stderr, "Gdk-ERROR **: Wayland protocol error %u on %s@%u.\n",
               code, interface ? interface->name : "an unknown object", id);
    }
  else
    fprintf (stderr, "Gdk-ERROR **: Lost the connection to the Wayland compositor (%s).\n",
             g_strerror (err ? err : EPIPE));
  exit (1);
}

void
_gdk_wayland_exit (void)
{
  if (!_gdk_wl_display || connection_is_lost)
    return;
  _gdk_wayland_selection_exit ();
  _gdk_wayland_input_context_exit ();
  _gdk_wayland_events_exit ();
  if (_gdk_wl_cursor_theme)
    wl_cursor_theme_destroy (_gdk_wl_cursor_theme);
  if (_gdk_xkb_state)
    xkb_state_unref (_gdk_xkb_state);
  if (_gdk_xkb_keymap)
    xkb_keymap_unref (_gdk_xkb_keymap);
  if (_gdk_xkb_context)
    xkb_context_unref (_gdk_xkb_context);
  wl_display_flush (_gdk_wl_display);
  wl_display_disconnect (_gdk_wl_display);
  _gdk_wl_display = NULL;
}

/* ---------------------------------------------------------------------
 * The connection's side of the event loop. Whatever is buffered goes
 * out before waiting, whatever has come in is dispatched to the
 * listeners, which put GDK events in the queue.
 */

/* Requests out; a full buffer only means they wait for the next flush. */
static void
flush (void)
{
  if (wl_display_flush (_gdk_wl_display) < 0 && errno != EAGAIN)
    connection_lost ();
}

static void
read_and_dispatch (gboolean block)
{
  struct pollfd pfd;

  if (wl_display_get_error (_gdk_wl_display))
    connection_lost ();

  while (wl_display_prepare_read (_gdk_wl_display) != 0)
    if (wl_display_dispatch_pending (_gdk_wl_display) < 0)
      connection_lost ();
  if (wl_display_flush (_gdk_wl_display) < 0 && errno != EAGAIN)
    {
      wl_display_cancel_read (_gdk_wl_display);
      connection_lost ();
    }

  pfd.fd = wl_display_get_fd (_gdk_wl_display);
  pfd.events = POLLIN;
  if (poll (&pfd, 1, block ? -1 : 0) > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR)))
    {
      /* A hangup with nothing left to read fails the read. */
      if (wl_display_read_events (_gdk_wl_display) < 0)
        connection_lost ();
    }
  else
    wl_display_cancel_read (_gdk_wl_display);

  if (wl_display_dispatch_pending (_gdk_wl_display) < 0)
    connection_lost ();

  /* Windows with something to commit, by the program's doing or by
   * what was just dispatched -- a frame done, a buffer released --
   * are published with the next that are. What was dispatched may
   * have sent requests too: they go out now, as the main loop may
   * sleep next with nothing else to send them until some other event
   * comes in. */
  _gdk_wayland_window_commit_all ();
  flush ();
}

gboolean
_gdk_wayland_events_pending (void)
{
  read_and_dispatch (FALSE);
  return _gdk_event_queue_find_first () != NULL;
}

void
_gdk_wayland_events_queue (gboolean all)
{
  read_and_dispatch (FALSE);
}

gint
_gdk_wayland_event_fd (void)
{
  return wl_display_get_fd (_gdk_wl_display);
}

void
_gdk_wayland_flush (void)
{
  /* For gdk_flush: what waits to be committed is, now. */
  _gdk_wayland_window_commit_all ();
  _gdk_windows_flush ();
  flush ();
}

/* ---------------------------------------------------------------------
 * The screen
 */

void
_gdk_wayland_beep (void)
{
}

gint
_gdk_wayland_screen_width (void)
{
  return _gdk_wayland_display_width;
}

gint
_gdk_wayland_screen_height (void)
{
  return _gdk_wayland_display_height;
}

gint
_gdk_wayland_screen_width_mm (void)
{
  return _gdk_wayland_display_width_mm ? _gdk_wayland_display_width_mm
    : _gdk_wayland_display_width * 254 / 960;
}

gint
_gdk_wayland_screen_height_mm (void)
{
  return _gdk_wayland_display_height_mm ? _gdk_wayland_display_height_mm
    : _gdk_wayland_display_height * 254 / 960;
}

/* The compositor places toplevels, and does not say where. */
gboolean
_gdk_wayland_can_place_toplevels (void)
{
  return FALSE;
}

gdouble
_gdk_wayland_dpi (void)
{
  return 96;
}

/* There are no X errors to trap; the counts keep push and pop paired. */
static gint error_traps = 0;

void
_gdk_wayland_error_trap_push (void)
{
  error_traps++;
}

gint
_gdk_wayland_error_trap_pop (void)
{
  if (error_traps > 0)
    error_traps--;
  return 0;
}

void
_gdk_wayland_set_sm_client_id (const gchar *sm_client_id)
{
}

gchar *
_gdk_wayland_set_locale (void)
{
  if (!setlocale (LC_ALL, ""))
    g_warning ("locale not supported by C library");
  return setlocale (LC_ALL, NULL);
}

gboolean
_gdk_wayland_event_send_client_message (GdkEvent *event, guint32 xid)
{
  return FALSE;
}

void
_gdk_wayland_event_send_clientmessage_toall (GdkEvent *event)
{
}

/* ---------------------------------------------------------------------
 * Atoms: names with numbers. The first 68 are X's predefined ones,
 * with X's numbers, since the public constants in gdktypes.h are
 * those.
 */

static const gchar *predefined_atoms[] = {
  NULL, "PRIMARY", "SECONDARY", "ARC", "ATOM", "BITMAP", "CARDINAL",
  "COLORMAP", "CURSOR", "CUT_BUFFER0", "CUT_BUFFER1", "CUT_BUFFER2",
  "CUT_BUFFER3", "CUT_BUFFER4", "CUT_BUFFER5", "CUT_BUFFER6", "CUT_BUFFER7",
  "DRAWABLE", "FONT", "INTEGER", "PIXMAP", "POINT", "RECTANGLE",
  "RESOURCE_MANAGER", "RGB_COLOR_MAP", "RGB_BEST_MAP", "RGB_BLUE_MAP",
  "RGB_DEFAULT_MAP", "RGB_GRAY_MAP", "RGB_GREEN_MAP", "RGB_RED_MAP", "STRING",
  "VISUALID", "WINDOW", "WM_COMMAND", "WM_HINTS", "WM_CLIENT_MACHINE",
  "WM_ICON_NAME", "WM_ICON_SIZE", "WM_NAME", "WM_NORMAL_HINTS",
  "WM_SIZE_HINTS", "WM_ZOOM_HINTS", "MIN_SPACE", "NORM_SPACE", "MAX_SPACE",
  "END_SPACE", "SUPERSCRIPT_X", "SUPERSCRIPT_Y", "SUBSCRIPT_X", "SUBSCRIPT_Y",
  "UNDERLINE_POSITION", "UNDERLINE_THICKNESS", "STRIKEOUT_ASCENT",
  "STRIKEOUT_DESCENT", "ITALIC_ANGLE", "X_HEIGHT", "QUAD_WIDTH", "WEIGHT",
  "POINT_SIZE", "RESOLUTION", "COPYRIGHT", "NOTICE", "FONT_NAME",
  "FAMILY_NAME", "FULL_NAME", "CAP_HEIGHT", "WM_CLASS", "WM_TRANSIENT_FOR"
};

static GPtrArray *atom_names = NULL;
static GHashTable *atom_ids = NULL;

static void
atoms_init (void)
{
  guint i;

  if (atom_names)
    return;
  atom_names = g_ptr_array_new ();
  atom_ids = g_hash_table_new (g_str_hash, g_str_equal);
  for (i = 0; i < sizeof (predefined_atoms) / sizeof (predefined_atoms[0]); i++)
    {
      g_ptr_array_add (atom_names, (gpointer) predefined_atoms[i]);
      if (predefined_atoms[i])
        g_hash_table_insert (atom_ids, (gpointer) predefined_atoms[i],
                             GUINT_TO_POINTER (i));
    }
}

GdkAtom
_gdk_wayland_atom_intern (const gchar *atom_name, gint only_if_exists)
{
  gpointer id;
  gchar *name;

  g_return_val_if_fail (atom_name != NULL, GDK_NONE);

  atoms_init ();
  id = g_hash_table_lookup (atom_ids, atom_name);
  if (id)
    return GPOINTER_TO_UINT (id);
  if (only_if_exists)
    return GDK_NONE;

  name = g_strdup (atom_name);
  g_ptr_array_add (atom_names, name);
  g_hash_table_insert (atom_ids, name, GUINT_TO_POINTER (atom_names->len - 1));
  return atom_names->len - 1;
}

const gchar *
_gdk_wayland_atom_string (GdkAtom atom)
{
  atoms_init ();
  if (atom == 0 || atom >= atom_names->len)
    return NULL;
  return g_ptr_array_index (atom_names, atom);
}

gchar *
_gdk_wayland_atom_name (GdkAtom atom)
{
  const gchar *name = _gdk_wayland_atom_string (atom);

  return name ? g_strdup (name) : NULL;
}

/* ---------------------------------------------------------------------
 * Keys: xkbcommon's keysyms are X's.
 */

gchar *
_gdk_wayland_keyval_name (guint keyval)
{
  static gchar buf[128];

  if (xkb_keysym_get_name (keyval, buf, sizeof buf) < 0)
    return NULL;
  return buf;
}

guint
_gdk_wayland_keyval_from_name (const gchar *keyval_name)
{
  g_return_val_if_fail (keyval_name != NULL, 0);

  return xkb_keysym_from_name (keyval_name, XKB_KEYSYM_NO_FLAGS);
}

guint
_gdk_wayland_keyval_to_upper (guint keyval)
{
  return keyval ? xkb_keysym_to_upper (keyval) : 0;
}

guint
_gdk_wayland_keyval_to_lower (guint keyval)
{
  return keyval ? xkb_keysym_to_lower (keyval) : 0;
}

void
_gdk_wayland_key_repeat_disable (void)
{
}

void
_gdk_wayland_key_repeat_restore (void)
{
}

/* ---------------------------------------------------------------------
 * Grabs. The generic side routes events to the grab window; what
 * Wayland offers besides is a popup's grab, which a popup that is
 * grabbed before its first commit asks for.
 */

gint
_gdk_wayland_pointer_grab (GdkWindow    *window,
                           gint          owner_events,
                           GdkEventMask  event_mask,
                           GdkWindow    *confine_to,
                           GdkCursor    *cursor,
                           guint32       time)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  GdkWindowPrivate *native;

  g_return_val_if_fail (window != NULL, 1);

  if (private->destroyed)
    return 1;
  native = _gdk_window_native (private, NULL, NULL);
  if (native->window_type == GDK_WINDOW_TEMP && !native->impl->committed)
    native->impl->want_grab = TRUE;
  /* A drag begun grabs the pointer once its icon is up: it starts here. */
  if (_gdk_wayland_drag_pending)
    _gdk_wayland_drag_start ();
  _gdk_pointer_grab_set (private, owner_events, event_mask, FALSE);
  gdk_xgrab_window = private;
  return 0;
}

void
_gdk_wayland_pointer_ungrab (guint32 time)
{
  gdk_xgrab_window = NULL;
  _gdk_pointer_grab_clear (FALSE);
}

gboolean
_gdk_wayland_pointer_is_grabbed (void)
{
  return _gdk_pointer_grab_window (NULL) != NULL;
}

gint
_gdk_wayland_keyboard_grab (GdkWindow *window, gboolean owner_events, guint32 time)
{
  return 0;
}

void
_gdk_wayland_keyboard_ungrab (guint32 time)
{
}

/* ---------------------------------------------------------------------
 * Text properties as UTF-8.
 */

gint
_gdk_wayland_text_property_to_text_list (GdkAtom encoding, gint format,
                                         guchar *text, gint length, gchar ***list)
{
  gchar **result;
  gint i, n = 0, start = 0;

  g_return_val_if_fail (list != NULL, 0);

  /* Nul separated strings, however they were labelled. */
  for (i = 0; i < length; i++)
    if (text[i] == 0)
      n++;
  if (length > 0 && text[length - 1] != 0)
    n++;
  result = g_new (gchar *, n + 1);
  n = 0;
  for (i = 0; i <= length; i++)
    if (i == length || text[i] == 0)
      {
        if (i > start || i < length)
          result[n++] = g_strndup ((const gchar *) text + start, i - start);
        start = i + 1;
      }
  result[n] = NULL;
  *list = result;
  return n;
}

void
_gdk_wayland_free_text_list (gchar **list)
{
  g_strfreev (list);
}

gint
_gdk_wayland_string_to_compound_text (const gchar *str, GdkAtom *encoding,
                                      gint *format, guchar **ctext, gint *length)
{
  if (encoding)
    *encoding = gdk_atom_intern ("UTF8_STRING", FALSE);
  if (format)
    *format = 8;
  if (ctext)
    *ctext = (guchar *) g_strdup (str);
  if (length)
    *length = strlen (str);
  return 0;
}

void
_gdk_wayland_free_compound_text (guchar *ctext)
{
  g_free (ctext);
}
