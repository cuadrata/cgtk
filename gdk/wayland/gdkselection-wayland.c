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
 * Properties and selections. Properties are what X kept on a window
 * for a program: here they are kept in the window, since the selection
 * code in GTK moves data through them. The clipboard is Wayland's
 * data device, spoken through the same events X used: when another
 * program wants what this one offered, the program gets a
 * GDK_SELECTION_REQUEST, answers it into a property, and the property
 * is written down the pipe; when this program asks for another's,
 * the pipe is read into a property and a GDK_SELECTION_NOTIFY follows.
 * Drag and drop is not there yet.
 */

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include "gdk.h"
#include "gdkprivate-wayland.h"
#include "gdkbackend-wayland.h"

/* ---------------------------------------------------------------------
 * Window ids: what a requestor is named by in the selection events.
 */

static GHashTable *window_ids = NULL;   /* id -> GdkWindowPrivate */
static GHashTable *id_of_window = NULL; /* GdkWindowPrivate -> id */
static guint32 next_window_id = 1;

static guint32
window_id (GdkWindow *window)
{
  gpointer id;

  if (!window_ids)
    {
      window_ids = g_hash_table_new (NULL, NULL);
      id_of_window = g_hash_table_new (NULL, NULL);
    }
  id = g_hash_table_lookup (id_of_window, window);
  if (!id)
    {
      id = GUINT_TO_POINTER (next_window_id++);
      g_hash_table_insert (id_of_window, window, id);
      g_hash_table_insert (window_ids, id, window);
    }
  return GPOINTER_TO_UINT (id);
}

GdkWindow *
_gdk_wayland_window_lookup (guint32 id)
{
  if (!window_ids)
    return NULL;
  return g_hash_table_lookup (window_ids, GUINT_TO_POINTER (id));
}

GdkWindow *
_gdk_wayland_window_foreign_new (guint32 id)
{
  return NULL;
}

/* ---------------------------------------------------------------------
 * Properties
 */

static void
property_free (gpointer data)
{
  GdkWaylandProperty *prop = data;

  g_free (prop->data);
  g_free (prop);
}

static void
property_free_entry (gpointer key, gpointer value, gpointer data)
{
  property_free (value);
}

static GdkWaylandProperty *
_gdk_wayland_property_lookup (GdkWindow *window, GdkAtom property)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;

  if (!private->impl || !private->impl->properties)
    return NULL;
  return g_hash_table_lookup (private->impl->properties, GUINT_TO_POINTER (property));
}

void
_gdk_wayland_properties_free (GdkWindowPrivate *private)
{
  if (private->impl && private->impl->properties)
    {
      g_hash_table_foreach (private->impl->properties, property_free_entry, NULL);
      g_hash_table_destroy (private->impl->properties);
    }
  if (private->impl)
    private->impl->properties = NULL;
  if (id_of_window)
    {
      gpointer id = g_hash_table_lookup (id_of_window, private);

      if (id)
        {
          g_hash_table_remove (id_of_window, private);
          g_hash_table_remove (window_ids, id);
        }
    }
}

static gint
format_bytes (gint format)
{
  return format == 32 ? sizeof (gulong) : format / 8;
}

gboolean
_gdk_wayland_property_get (GdkWindow *window, GdkAtom property, GdkAtom type,
                           gulong offset, gulong length, gint pdelete,
                           GdkAtom *actual_property_type, gint *actual_format,
                           gint *actual_length, guchar **data)
{
  GdkWaylandProperty *prop;
  gint bytes;
  gulong start, n;

  g_return_val_if_fail (window != NULL, FALSE);

  prop = _gdk_wayland_property_lookup (window, property);
  if (!prop)
    return FALSE;
  if (actual_property_type)
    *actual_property_type = prop->type;
  if (actual_format)
    *actual_format = prop->format;
  if (type != 0 && type != prop->type)
    {
      if (actual_length)
        *actual_length = 0;
      if (data)
        *data = NULL;
      return FALSE;
    }

  bytes = format_bytes (prop->format);
  start = MIN (offset * 4, (gulong) prop->nelements * bytes);
  n = MIN (length * 4, (gulong) prop->nelements * bytes - start);
  if (actual_length)
    *actual_length = n;
  if (data)
    {
      *data = g_malloc (n + 1);
      memcpy (*data, prop->data + start, n);
      (*data)[n] = 0;
    }
  if (pdelete)
    _gdk_wayland_property_delete (window, property);
  return TRUE;
}

void
_gdk_wayland_property_change (GdkWindow *window, GdkAtom property, GdkAtom type,
                              gint format, GdkPropMode mode, guchar *data,
                              gint nelements)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  GdkWaylandProperty *prop;
  gint bytes = format_bytes (format);

  g_return_if_fail (window != NULL);

  if (private->destroyed || !private->impl)
    return;
  if (!private->impl->properties)
    private->impl->properties = g_hash_table_new (NULL, NULL);

  prop = g_hash_table_lookup (private->impl->properties, GUINT_TO_POINTER (property));
  if (!prop || mode == GDK_PROP_MODE_REPLACE || prop->format != format)
    {
      if (prop)
        property_free (prop);
      prop = g_new0 (GdkWaylandProperty, 1);
      prop->type = type;
      prop->format = format;
      prop->data = g_malloc ((gsize) nelements * bytes);
      memcpy (prop->data, data, (gsize) nelements * bytes);
      prop->nelements = nelements;
      g_hash_table_insert (private->impl->properties, GUINT_TO_POINTER (property), prop);
      return;
    }

  prop->data = g_realloc (prop->data, (gsize) (prop->nelements + nelements) * bytes);
  if (mode == GDK_PROP_MODE_APPEND)
    memcpy (prop->data + (gsize) prop->nelements * bytes, data, (gsize) nelements * bytes);
  else
    {
      memmove (prop->data + (gsize) nelements * bytes, prop->data, (gsize) prop->nelements * bytes);
      memcpy (prop->data, data, (gsize) nelements * bytes);
    }
  prop->nelements += nelements;
}

void
_gdk_wayland_property_delete (GdkWindow *window, GdkAtom property)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;

  g_return_if_fail (window != NULL);

  if (private->impl && private->impl->properties)
    {
      GdkWaylandProperty *prop = g_hash_table_lookup (private->impl->properties,
                                                       GUINT_TO_POINTER (property));

      if (prop)
        {
          g_hash_table_remove (private->impl->properties, GUINT_TO_POINTER (property));
          property_free (prop);
        }
    }
}

/* ---------------------------------------------------------------------
 * The clipboard
 */

typedef struct
{
  guint32 id;
  GdkAtom selection;
  GdkAtom target;
  GdkAtom property;
  int fd;
} PendingRequest;

static GdkWindow *clipboard_owner = NULL;
static struct wl_data_source *clipboard_source = NULL;
static struct wl_data_offer *clipboard_offer = NULL;   /* what others offer */
static GList *offer_mime_types = NULL;
static GList *pending_requests = NULL;
static guint32 next_request_id = 1;
static GdkAtom atom_clipboard, atom_utf8_string, atom_text, atom_targets, atom_compound_text;

static const gchar *
mime_for_target (GdkAtom target)
{
  if (target == atom_utf8_string || target == atom_text ||
      target == atom_compound_text)
    return "text/plain;charset=utf-8";
  if (target == GDK_TARGET_STRING)
    return "text/plain";
  return _gdk_wayland_atom_string (target);
}

static GdkAtom
target_for_mime (const gchar *mime)
{
  if (strcmp (mime, "text/plain;charset=utf-8") == 0 || strcmp (mime, "UTF8_STRING") == 0)
    return atom_utf8_string;
  if (strcmp (mime, "text/plain") == 0 || strcmp (mime, "STRING") == 0)
    return GDK_TARGET_STRING;
  return gdk_atom_intern (mime, FALSE);
}

/* ---- this program's offer */

static void
source_target (void *data, struct wl_data_source *source, const char *mime_type)
{
}

static GdkAtom atom_dnd;                       /* XdndSelection */
static struct wl_data_source *dnd_source = NULL;
static GdkWindow *dnd_owner = NULL;            /* the drag's source window */
static GdkDragContext *dnd_context = NULL;     /* this program's drag, if any */
static GdkDragContext *drop_context = NULL;    /* a drag over this program, if any */
static struct wl_data_offer *dnd_offer = NULL;
static GList *dnd_offer_mime_types = NULL;
static guint32 dnd_enter_serial = 0;
gboolean _gdk_wayland_drag_pending = FALSE;
static GdkWindowPrivate *dnd_icon = NULL;
static guint dnd_start_idle = 0;

/* Another program wants a selection this one holds: ask its owner, as X would. */
static void
request_from_owner (GdkWindow *owner, GdkAtom selection, const char *mime_type,
                    int32_t fd)
{
  PendingRequest *req;
  GdkEvent event;

  if (!owner)
    {
      close (fd);
      return;
    }
  req = g_new0 (PendingRequest, 1);
  req->id = next_request_id++;
  req->selection = selection;
  req->target = target_for_mime (mime_type);
  req->property = gdk_atom_intern ("GDK_SELECTION", FALSE);
  req->fd = fd;
  pending_requests = g_list_append (pending_requests, req);

  event.selection.type = GDK_SELECTION_REQUEST;
  event.selection.window = owner;
  event.selection.send_event = FALSE;
  event.selection.selection = selection;
  event.selection.target = req->target;
  event.selection.property = req->property;
  event.selection.requestor = window_id (owner);
  event.selection.time = _gdk_wayland_last_time;
  _gdk_wayland_put_event (&event);
}

/* Another program took a selection from owner. */
static void
selection_clear (GdkWindow *owner, GdkAtom selection)
{
  GdkEvent event;

  event.selection.type = GDK_SELECTION_CLEAR;
  event.selection.window = owner;
  event.selection.send_event = FALSE;
  event.selection.selection = selection;
  event.selection.target = 0;
  event.selection.property = 0;
  event.selection.requestor = 0;
  event.selection.time = _gdk_wayland_last_time;
  _gdk_wayland_put_event (&event);
}

static void
source_send (void *data, struct wl_data_source *source, const char *mime_type, int32_t fd)
{
  if (source == dnd_source)
    request_from_owner (dnd_owner, atom_dnd, mime_type, fd);
  else
    request_from_owner (clipboard_owner, atom_clipboard, mime_type, fd);
}

static void drag_finished (gboolean success);

static void
source_cancelled (void *data, struct wl_data_source *source)
{
  if (source == dnd_source)
    {
      drag_finished (FALSE);
      return;
    }
  if (source != clipboard_source)
    return;
  if (clipboard_owner)
    selection_clear (clipboard_owner, atom_clipboard);
  wl_data_source_destroy (source);
  clipboard_source = NULL;
  clipboard_owner = NULL;
}

static void
source_dnd_drop_performed (void *data, struct wl_data_source *source)
{
}

static void
source_dnd_finished (void *data, struct wl_data_source *source)
{
  if (source == dnd_source)
    drag_finished (TRUE);
}

static void
source_action (void *data, struct wl_data_source *source, uint32_t action)
{
}

static const struct wl_data_source_listener source_listener = {
  source_target, source_send, source_cancelled,
  source_dnd_drop_performed, source_dnd_finished, source_action
};

/*
 * The primary selection: other programs see it through the compositor's
 * primary selection protocol, when it has one; without, it is still this
 * program's, for its own middle-click pastes.
 */
static GdkWindow *primary_owner = NULL;
static struct zwp_primary_selection_device_v1 *primary_device = NULL;
static struct zwp_primary_selection_source_v1 *primary_source = NULL;
static struct zwp_primary_selection_offer_v1 *primary_offer = NULL;
static GList *primary_mime_types = NULL;

static void
primary_source_send (void *data, struct zwp_primary_selection_source_v1 *source,
                     const char *mime_type, int32_t fd)
{
  if (source == primary_source)
    request_from_owner (primary_owner, GDK_SELECTION_PRIMARY, mime_type, fd);
  else
    close (fd);
}

static void
primary_source_cancelled (void *data, struct zwp_primary_selection_source_v1 *source)
{
  if (source == primary_source)
    {
      if (primary_owner)
        selection_clear (primary_owner, GDK_SELECTION_PRIMARY);
      primary_owner = NULL;
      primary_source = NULL;
    }
  zwp_primary_selection_source_v1_destroy (source);
}

static const struct zwp_primary_selection_source_v1_listener primary_source_listener = {
  primary_source_send, primary_source_cancelled
};

static gboolean
primary_owner_set (GdkWindow *owner)
{
  if (primary_source)
    {
      zwp_primary_selection_source_v1_destroy (primary_source);
      primary_source = NULL;
    }
  primary_owner = owner;
  if (!primary_device)
    return TRUE;
  if (!owner)
    {
      zwp_primary_selection_device_v1_set_selection (primary_device, NULL,
                                                     _gdk_wayland_last_serial);
      return TRUE;
    }
  primary_source = zwp_primary_selection_device_manager_v1_create_source (_gdk_wl_primary_manager);
  zwp_primary_selection_source_v1_add_listener (primary_source, &primary_source_listener, NULL);
  zwp_primary_selection_source_v1_offer (primary_source, "text/plain;charset=utf-8");
  zwp_primary_selection_source_v1_offer (primary_source, "text/plain");
  zwp_primary_selection_source_v1_offer (primary_source, "UTF8_STRING");
  zwp_primary_selection_source_v1_offer (primary_source, "STRING");
  zwp_primary_selection_device_v1_set_selection (primary_device, primary_source,
                                                 _gdk_wayland_last_serial);
  return TRUE;
}

gboolean
_gdk_wayland_selection_owner_set (GdkWindow *owner, GdkAtom selection,
                                  guint32 time, gint send_event)
{
  if (selection == atom_dnd)
    {
      dnd_owner = owner;
      return TRUE;
    }
  if (selection == GDK_SELECTION_PRIMARY)
    return primary_owner_set (owner);
  if (selection != atom_clipboard || !_gdk_wl_data_device)
    return selection != atom_clipboard;

  if (clipboard_source)
    {
      wl_data_source_destroy (clipboard_source);
      clipboard_source = NULL;
    }
  clipboard_owner = owner;
  if (!owner)
    {
      wl_data_device_set_selection (_gdk_wl_data_device, NULL, _gdk_wayland_last_serial);
      return TRUE;
    }
  clipboard_source = wl_data_device_manager_create_data_source (_gdk_wl_data_device_manager);
  wl_data_source_add_listener (clipboard_source, &source_listener, NULL);
  wl_data_source_offer (clipboard_source, "text/plain;charset=utf-8");
  wl_data_source_offer (clipboard_source, "text/plain");
  wl_data_source_offer (clipboard_source, "UTF8_STRING");
  wl_data_source_offer (clipboard_source, "STRING");
  wl_data_device_set_selection (_gdk_wl_data_device, clipboard_source, _gdk_wayland_last_serial);
  return TRUE;
}

GdkWindow *
_gdk_wayland_selection_owner_get (GdkAtom selection)
{
  if (selection == atom_dnd)
    return dnd_owner;
  if (selection == GDK_SELECTION_PRIMARY)
    return primary_owner;
  return selection == atom_clipboard ? clipboard_owner : NULL;
}

/* A pipe carries any amount: INCR, X's way around its request size, is never needed. */
gint
_gdk_wayland_selection_max_size (void)
{
  return G_MAXINT;
}

/*
 * An answer going down a pipe to another program, as fast as it reads:
 * a slow reader must not hold this program up, nor one that stops early
 * kill it with SIGPIPE.
 */
typedef struct
{
  gint fd;
  guchar *data;
  gsize length, done;
  gint watch;
} Sending;

static void
sending_free (Sending *s)
{
  if (s->watch)
    gdk_input_remove (s->watch);
  close (s->fd);
  g_free (s->data);
  g_free (s);
}

/* Write what the pipe will take: TRUE if there is more to come. */
static gboolean
sending_write (Sending *s)
{
  struct sigaction ignore, old;
  gboolean failed = FALSE;

  memset (&ignore, 0, sizeof ignore);
  ignore.sa_handler = SIG_IGN;
  sigemptyset (&ignore.sa_mask);
  sigaction (SIGPIPE, &ignore, &old);
  while (s->done < s->length)
    {
      ssize_t n = write (s->fd, s->data + s->done, s->length - s->done);

      if (n < 0)
        {
          if (errno == EINTR)
            continue;
          failed = errno != EAGAIN;
          break;
        }
      s->done += n;
    }
  sigaction (SIGPIPE, &old, NULL);
  return !failed && s->done < s->length;
}

static void
sending_ready (gpointer data, gint fd, GdkInputCondition condition)
{
  Sending *s = data;

  if (!sending_write (s))
    sending_free (s);
}

/* The program answered a request into the property: down the pipe with it. */
void
_gdk_wayland_selection_send_notify (guint32 requestor, GdkAtom selection,
                                    GdkAtom target, GdkAtom property, guint32 time)
{
  GList *l;

  for (l = pending_requests; l; l = l->next)
    {
      PendingRequest *req = l->data;

          if (req->selection == selection && req->target == target)
        {
          GdkWindow *owner = _gdk_wayland_window_lookup (requestor);
          GdkWaylandProperty *prop = owner && property != GDK_NONE
            ? _gdk_wayland_property_lookup (owner, property) : NULL;

          if (prop)
            {
              Sending *s = g_new0 (Sending, 1);

              s->fd = req->fd;
              s->length = (gsize) prop->nelements * format_bytes (prop->format);
              s->data = g_memdup (prop->data, s->length);
              _gdk_wayland_property_delete (owner, property);
              fcntl (s->fd, F_SETFL, fcntl (s->fd, F_GETFL) | O_NONBLOCK);
              if (sending_write (s))
                s->watch = gdk_input_add (s->fd, GDK_INPUT_WRITE, sending_ready, s);
              else
                sending_free (s);
            }
          else
            close (req->fd);
          pending_requests = g_list_remove (pending_requests, req);
          g_free (req);
          return;
        }
    }
}

/* ---- what others offer */

/*
 * Each offer gathers its own types as they arrive, before it is named
 * the clipboard's or a drag's; whichever it turns out to be takes them.
 */
typedef struct
{
  GList *types;
} OfferTypes;

static void
free_types (GList *types)
{
  g_list_foreach (types, (GFunc) g_free, NULL);
  g_list_free (types);
}

static GList *
offer_take_types (struct wl_data_offer *offer)
{
  OfferTypes *t = offer ? wl_data_offer_get_user_data (offer) : NULL;
  GList *types = NULL;

  if (t)
    {
      types = t->types;
      t->types = NULL;
    }
  return types;
}

static void
offer_destroy (struct wl_data_offer *offer)
{
  OfferTypes *t = wl_data_offer_get_user_data (offer);

  if (t)
    {
      free_types (t->types);
      g_free (t);
    }
  wl_data_offer_destroy (offer);
}

static void
offer_offer (void *data, struct wl_data_offer *offer, const char *mime_type)
{
  OfferTypes *t = data;

  t->types = g_list_append (t->types, g_strdup (mime_type));
}

static void
offer_source_actions (void *data, struct wl_data_offer *offer, uint32_t actions)
{
}

static void
offer_action (void *data, struct wl_data_offer *offer, uint32_t action)
{
}

static const struct wl_data_offer_listener offer_listener = {
  offer_offer, offer_source_actions, offer_action
};

static void
device_data_offer (void *data, struct wl_data_device *device, struct wl_data_offer *offer)
{
  wl_data_offer_add_listener (offer, &offer_listener, g_new0 (OfferTypes, 1));
}

static GdkAtom
target_atom_for_mime (const gchar *mime)
{
  return gdk_atom_intern (mime, FALSE);
}

/* Where the surface a drag is over lies in the window that hears of the drag. */
static gint drop_ox, drop_oy;
static gboolean drop_inner = FALSE;

/* An event of a drag over this program, to the window it is over. */
static void
drop_event (GdkEventType type, gint x, gint y, guint32 time)
{
  GdkEvent event;
  GdkWindowPrivate *native;
  gint rx = 0, ry = 0;

  if (!drop_context || !drop_context->dest_window)
    return;
  native = (GdkWindowPrivate *) drop_context->dest_window;
  gdk_window_get_origin ((GdkWindow *) native, &rx, &ry);
  memset (&event, 0, sizeof event);
  event.dnd.type = type;
  event.dnd.window = drop_context->dest_window;
  event.dnd.send_event = FALSE;
  event.dnd.context = drop_context;
  event.dnd.time = time;
  event.dnd.x_root = rx + x;
  event.dnd.y_root = ry + y;
  _gdk_wayland_put_event (&event);
}

static gint drop_x, drop_y;
static gboolean drop_begun = FALSE;     /* dropped here, and not yet finished */

/*
 * Where the pointer is while a drag is over one of this program's
 * windows, until it is dropped. The pointer itself left when the drag
 * began, and its place comes with the drag.
 */
GdkWindowPrivate *
_gdk_wayland_drop_pointer (gint *x, gint *y)
{
  GdkWindowPrivate *native;

  if (!drop_context || !drop_context->dest_window || drop_begun)
    return NULL;
  native = (GdkWindowPrivate *) drop_context->dest_window;
  if (native->destroyed || !native->impl)
    return NULL;
  *x = drop_x;
  *y = drop_y;
  return native;
}

static void
device_enter (void *data, struct wl_data_device *device, uint32_t serial,
              struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y,
              struct wl_data_offer *offer)
{
  GdkWindowPrivate *native = _gdk_wayland_window_from_surface (surface);
  GList *l;

  dnd_enter_serial = serial;
  drop_begun = FALSE;
  if (dnd_offer != offer)
    {
      /* A new offer: the old one and its types go. */
      if (dnd_offer)
        offer_destroy (dnd_offer);
      free_types (dnd_offer_mime_types);
      dnd_offer = offer;
      dnd_offer_mime_types = offer_take_types (offer);
    }

  if (!native || native->destroyed || !offer)
    return;
  drop_ox = drop_oy = 0;
  drop_inner = FALSE;
  while (native->window_type == GDK_WINDOW_CHILD)
    {
      /* A child with a surface of its own: a drag is over its top-level
       * window, which is the one that hears of it, as it is with X. */
      GdkWindowPrivate *outer;
      gint ox, oy;

      outer = _gdk_window_native ((GdkWindowPrivate *) native->parent, &ox, &oy);
      if (!outer || outer == native)
        break;
      drop_ox += ox + native->x;
      drop_oy += oy + native->y;
      drop_inner = TRUE;
      native = outer;
    }
  if (drop_inner)
    {
      x += wl_fixed_from_int (GDK_WL_TO (drop_ox));
      y += wl_fixed_from_int (GDK_WL_TO (drop_oy));
    }
  else if (native->impl->group && native->impl->group->leader == native)
    {
      /* The group's leader's surface: the member under the point. */
      GdkWindowPrivate *m;
      gint gx = GDK_WL_FROM (wl_fixed_to_int (x)) + native->x;
      gint gy = GDK_WL_FROM (wl_fixed_to_int (y)) + native->y;

      m = _gdk_wayland_group_member_at (native->impl->group, gx, gy);
      if (m)
        {
          x = wl_fixed_from_int (GDK_WL_TO (gx - m->x));
          y = wl_fixed_from_int (GDK_WL_TO (gy - m->y));
          native = m;
        }
    }
  if (drop_context)
    gdk_drag_context_unref (drop_context);
  drop_context = gdk_drag_context_new ();
  drop_context->protocol = GDK_DRAG_PROTO_XDND;
  drop_context->is_source = FALSE;
  drop_context->dest_window = gdk_window_ref ((GdkWindow *) native);
  drop_context->source_window = dnd_context ? gdk_window_ref (dnd_context->source_window) : NULL;
  for (l = dnd_offer_mime_types; l; l = l->next)
    drop_context->targets = g_list_append (drop_context->targets,
                                           GUINT_TO_POINTER (target_atom_for_mime (l->data)));
  drop_context->actions = GDK_ACTION_COPY | GDK_ACTION_MOVE;
  drop_context->suggested_action = GDK_ACTION_COPY;
  if (wl_data_offer_get_version (offer) >= 3)
    wl_data_offer_set_actions (offer,
                               WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY | WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE,
                               WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
  drop_x = GDK_WL_FROM (wl_fixed_to_int (x));
  drop_y = GDK_WL_FROM (wl_fixed_to_int (y));
  drop_event (GDK_DRAG_ENTER, drop_x, drop_y, _gdk_wayland_last_time);
  drop_event (GDK_DRAG_MOTION, drop_x, drop_y, _gdk_wayland_last_time);
}

static void
device_leave (void *data, struct wl_data_device *device)
{
  /*
   * A compositor may say the drag has left once it has dropped here.
   * The drop is still ours: the offer and its context stay until
   * gdk_drop_finish () has told the source how it went, and the
   * program, which was told of the drop, hears of no leave.
   */
  if (drop_begun)
    return;
  drop_event (GDK_DRAG_LEAVE, drop_x, drop_y, _gdk_wayland_last_time);
  if (drop_context)
    gdk_drag_context_unref (drop_context);
  drop_context = NULL;
}

static void
device_motion (void *data, struct wl_data_device *device, uint32_t time,
               wl_fixed_t x, wl_fixed_t y)
{
  GdkWindowPrivate *native;

  if (!drop_context)
    return;
  native = (GdkWindowPrivate *) drop_context->dest_window;
  drop_x = GDK_WL_FROM (wl_fixed_to_int (x));
  drop_y = GDK_WL_FROM (wl_fixed_to_int (y));
  if (drop_inner)
    {
      /* Positions come in those of the child the drag is over. */
      drop_x += drop_ox;
      drop_y += drop_oy;
    }
  else if (native->impl->group)
    {
      /* Positions come in the leader's coordinates. */
      GdkWindowPrivate *leader = native->impl->group->leader;

      drop_x += leader->x - native->x;
      drop_y += leader->y - native->y;
    }
  drop_event (GDK_DRAG_MOTION, drop_x, drop_y, time);
}

static void
device_drop (void *data, struct wl_data_device *device)
{
  drop_begun = drop_context != NULL && dnd_offer != NULL;
  drop_event (GDK_DROP_START, drop_x, drop_y, _gdk_wayland_last_time);
}

static void
device_selection (void *data, struct wl_data_device *device, struct wl_data_offer *offer)
{
  if (clipboard_offer != offer)
    {
      if (clipboard_offer)
        offer_destroy (clipboard_offer);
      free_types (offer_mime_types);
      offer_mime_types = offer_take_types (offer);
    }
  clipboard_offer = offer;
}

static void
primary_offer_offer (void *data, struct zwp_primary_selection_offer_v1 *offer,
                     const char *mime_type)
{
  OfferTypes *t = data;

  t->types = g_list_append (t->types, g_strdup (mime_type));
}

static const struct zwp_primary_selection_offer_v1_listener primary_offer_listener = {
  primary_offer_offer
};

static void
primary_offer_destroy (struct zwp_primary_selection_offer_v1 *offer)
{
  OfferTypes *t = zwp_primary_selection_offer_v1_get_user_data (offer);

  if (t)
    {
      free_types (t->types);
      g_free (t);
    }
  zwp_primary_selection_offer_v1_destroy (offer);
}

static void
primary_device_data_offer (void *data, struct zwp_primary_selection_device_v1 *device,
                           struct zwp_primary_selection_offer_v1 *offer)
{
  zwp_primary_selection_offer_v1_add_listener (offer, &primary_offer_listener,
                                               g_new0 (OfferTypes, 1));
}

static void
primary_device_selection (void *data, struct zwp_primary_selection_device_v1 *device,
                          struct zwp_primary_selection_offer_v1 *offer)
{
  if (primary_offer != offer)
    {
      OfferTypes *t = offer ? zwp_primary_selection_offer_v1_get_user_data (offer) : NULL;

      if (primary_offer)
        primary_offer_destroy (primary_offer);
      free_types (primary_mime_types);
      primary_mime_types = t ? t->types : NULL;
      if (t)
        t->types = NULL;
    }
  primary_offer = offer;
}

static const struct zwp_primary_selection_device_v1_listener primary_device_listener = {
  primary_device_data_offer, primary_device_selection
};

static const struct wl_data_device_listener device_listener = {
  device_data_offer, device_enter, device_leave, device_motion, device_drop,
  device_selection
};

typedef struct
{
  GdkWindow *requestor;
  GdkAtom selection, target;
  int fd;
  GByteArray *data;
  guint watch;
} Receive;

static void
receive_finish (Receive *r, gboolean ok)
{
  GdkEvent event;

  if (r->watch)
    g_source_remove (r->watch);
  close (r->fd);

  if (ok)
    {
      GdkAtom type = r->target == GDK_TARGET_STRING ? GDK_TARGET_STRING
        : r->selection == atom_dnd ? r->target : atom_utf8_string;

      _gdk_wayland_property_change (r->requestor, gdk_selection_property, type, 8,
                                    GDK_PROP_MODE_REPLACE, r->data->data,
                                    r->data->len);
    }
  event.selection.type = GDK_SELECTION_NOTIFY;
  event.selection.window = r->requestor;
  event.selection.send_event = FALSE;
  event.selection.selection = r->selection;
  event.selection.target = r->target;
  event.selection.property = ok ? gdk_selection_property : GDK_NONE;
  event.selection.requestor = window_id (r->requestor);
  event.selection.time = _gdk_wayland_last_time;
  _gdk_wayland_put_event (&event);

  gdk_window_unref (r->requestor);
  g_byte_array_free (r->data, TRUE);
  g_free (r);
}

static void
receive_read (gpointer data, gint fd, GdkInputCondition condition)
{
  Receive *r = data;
  gchar buf[4096];
  ssize_t n;

  n = read (fd, buf, sizeof buf);
  if (n > 0)
    {
      g_byte_array_append (r->data, (guint8 *) buf, n);
      return;
    }
  if (n < 0 && (errno == EINTR || errno == EAGAIN))
    return;
  receive_finish (r, n == 0);
}

void
_gdk_wayland_selection_convert (GdkWindow *requestor, GdkAtom selection,
                                GdkAtom target, guint32 time)
{
  const gchar *mime = NULL;
  GList *l, *types = NULL;
  gboolean text_offer = FALSE;
  int fds[2];
  Receive *r;
  GdkEvent event;

  g_return_if_fail (requestor != NULL);

  if (!gdk_selection_property)
    gdk_selection_property = gdk_atom_intern ("GDK_SELECTION", FALSE);

  if (selection == atom_dnd && dnd_offer && target != atom_targets)
    {
      const gchar *want = _gdk_wayland_atom_string (target);

      for (l = dnd_offer_mime_types; l; l = l->next)
        if (want && strcmp (l->data, want) == 0)
          mime = l->data;
    }
  if (selection == atom_clipboard && clipboard_offer)
    types = offer_mime_types, text_offer = TRUE;
  else if (selection == GDK_SELECTION_PRIMARY && primary_offer)
    types = primary_mime_types, text_offer = TRUE;

  if (text_offer && target != atom_targets)
    {
      const gchar *want = mime_for_target (target);

      for (l = types; l; l = l->next)
        if (want && strcmp (l->data, want) == 0)
          mime = l->data;
      if (!mime && (target == atom_utf8_string || target == GDK_TARGET_STRING ||
                    target == atom_text || target == atom_compound_text))
        for (l = types; l; l = l->next)
          if (strncmp (l->data, "text/plain", 10) == 0 || strcmp (l->data, "UTF8_STRING") == 0 ||
              strcmp (l->data, "STRING") == 0)
            mime = l->data;
    }

  if (text_offer && target == atom_targets)
    {
      /* The targets a text offer answers to. */
      GdkAtom targets[4];
      gint n = 0;

      targets[n++] = atom_utf8_string;
      targets[n++] = GDK_TARGET_STRING;
      targets[n++] = atom_text;
      targets[n++] = atom_compound_text;
      _gdk_wayland_property_change (requestor, gdk_selection_property,
                                    GDK_SELECTION_TYPE_ATOM, 32, GDK_PROP_MODE_REPLACE,
                                    (guchar *) targets, n);
      mime = NULL;
      event.selection.property = gdk_selection_property;
    }
  else if (!mime || pipe (fds) < 0)
    event.selection.property = GDK_NONE;
  else
    {
      if (selection == GDK_SELECTION_PRIMARY)
        zwp_primary_selection_offer_v1_receive (primary_offer, mime, fds[1]);
      else
        wl_data_offer_receive (selection == atom_dnd ? dnd_offer : clipboard_offer,
                               mime, fds[1]);
      close (fds[1]);
      wl_display_flush (_gdk_wl_display);
      fcntl (fds[0], F_SETFL, O_NONBLOCK);

      r = g_new0 (Receive, 1);
      r->requestor = gdk_window_ref (requestor);
      r->selection = selection;
      r->target = target;
      r->fd = fds[0];
      r->data = g_byte_array_new ();
      r->watch = gdk_input_add (fds[0], GDK_INPUT_READ, receive_read, r);
      return;
    }

  event.selection.type = GDK_SELECTION_NOTIFY;
  event.selection.window = requestor;
  event.selection.send_event = FALSE;
  event.selection.selection = selection;
  event.selection.target = target;
  event.selection.requestor = window_id (requestor);
  event.selection.time = time;
  _gdk_wayland_put_event (&event);
}

gboolean
_gdk_wayland_selection_property_get (GdkWindow *requestor, guchar **data,
                                     GdkAtom *prop_type, gint *prop_format)
{
  GdkWaylandProperty *prop;
  gint bytes;
  gsize n;

  g_return_val_if_fail (requestor != NULL, FALSE);

  prop = _gdk_wayland_property_lookup (requestor, gdk_selection_property);
  if (!prop)
    {
      *data = NULL;
      return 0;
    }
  bytes = format_bytes (prop->format);
  n = (gsize) prop->nelements * bytes;
  if (prop_type)
    *prop_type = prop->type;
  if (prop_format)
    *prop_format = prop->format;
  *data = g_malloc (n + 1);
  memcpy (*data, prop->data, n);
  (*data)[n] = 0;
  return n;
}

void
_gdk_wayland_selection_init (void)
{
  atom_clipboard = gdk_atom_intern ("CLIPBOARD", FALSE);
  atom_utf8_string = gdk_atom_intern ("UTF8_STRING", FALSE);
  atom_text = gdk_atom_intern ("TEXT", FALSE);
  atom_targets = gdk_atom_intern ("TARGETS", FALSE);
  atom_compound_text = gdk_atom_intern ("COMPOUND_TEXT", FALSE);
  atom_dnd = gdk_atom_intern ("XdndSelection", FALSE);
  gdk_selection_property = gdk_atom_intern ("GDK_SELECTION", FALSE);

  if (_gdk_wl_data_device_manager && _gdk_wl_seat)
    {
      _gdk_wl_data_device = wl_data_device_manager_get_data_device
        (_gdk_wl_data_device_manager, _gdk_wl_seat);
      wl_data_device_add_listener (_gdk_wl_data_device, &device_listener, NULL);
    }
  if (_gdk_wl_primary_manager && _gdk_wl_seat)
    {
      primary_device = zwp_primary_selection_device_manager_v1_get_device
        (_gdk_wl_primary_manager, _gdk_wl_seat);
      zwp_primary_selection_device_v1_add_listener (primary_device,
                                                    &primary_device_listener, NULL);
    }
}

void
_gdk_wayland_selection_exit (void)
{
  if (clipboard_source)
    wl_data_source_destroy (clipboard_source);
  clipboard_source = NULL;
  if (primary_source)
    zwp_primary_selection_source_v1_destroy (primary_source);
  primary_source = NULL;
  primary_owner = NULL;
  if (primary_offer)
    primary_offer_destroy (primary_offer);
  primary_offer = NULL;
  free_types (primary_mime_types);
  primary_mime_types = NULL;
  if (primary_device)
    zwp_primary_selection_device_v1_destroy (primary_device);
  primary_device = NULL;
  if (_gdk_wl_data_device)
    wl_data_device_destroy (_gdk_wl_data_device);
  _gdk_wl_data_device = NULL;
}

/* ---------------------------------------------------------------------
 * Drag and drop. A drag this program begins is the compositor's from
 * its start: it moves the icon, tells whoever is under the pointer,
 * and says when it is over; the program hears GDK_DROP_FINISHED then.
 * The data goes through the drag's selection, as it did with Xdnd.
 */

typedef struct
{
  GdkDragContext context;
  guint ref_count;
} DragContext;

static void
drag_finished (gboolean success)
{
  GdkEvent event;

  if (dnd_context)
    {
      /*
       * GTK ends a drag when the button that began it comes up: that
       * is when it lets go of its grab, and it drops if the drag is
       * over a window that takes one, else flies its icon home. The
       * compositor kept that release, and has had the drop and shown
       * how it went, so the context says "over a window" either way
       * and the release comes first; the finish that follows it then
       * finds a drag GTK has ended.
       */
      if (!dnd_context->action)
        dnd_context->action = GDK_ACTION_COPY;
      if (!dnd_context->dest_window)
        dnd_context->dest_window = gdk_window_ref (dnd_context->source_window);
      _gdk_wayland_release_buttons ();

      memset (&event, 0, sizeof event);
      event.dnd.type = GDK_DROP_FINISHED;
      event.dnd.window = dnd_context->source_window;
      event.dnd.send_event = FALSE;
      event.dnd.context = dnd_context;
      event.dnd.time = _gdk_wayland_last_time;
      _gdk_wayland_put_event (&event);
      gdk_drag_context_unref (dnd_context);
      dnd_context = NULL;
    }
  if (dnd_source)
    wl_data_source_destroy (dnd_source);
  dnd_source = NULL;
  dnd_owner = NULL;
  dnd_icon = NULL;
  _gdk_wayland_drag_pending = FALSE;
  if (dnd_start_idle)
    g_source_remove (dnd_start_idle);
  dnd_start_idle = 0;
}

void
_gdk_wayland_drag_icon_set (GdkWindowPrivate *icon)
{
  dnd_icon = icon;
}

/* The drag, begun, started with the compositor: with its icon, if one was shown. */
static gboolean
drag_start_now (gpointer data)
{
  GdkWindowPrivate *origin;

  dnd_start_idle = 0;
  if (!_gdk_wayland_drag_pending || !dnd_source || !_gdk_wl_data_device)
    return FALSE;
  _gdk_wayland_drag_pending = FALSE;
  origin = _gdk_wayland_pointer_window;
  if (!origin || origin->destroyed || !origin->impl->surface)
    {
      drag_finished (FALSE);
      return FALSE;
    }
  if (origin->impl->group && origin->impl->group->leader != origin)
    origin = origin->impl->group->leader;
  _gdk_wayland_window_commit_all ();
  wl_data_device_start_drag (_gdk_wl_data_device, dnd_source, origin->impl->surface,
                             dnd_icon && dnd_icon->impl->surface ? dnd_icon->impl->surface : NULL,
                             _gdk_wayland_press_serial);
  wl_display_flush (_gdk_wl_display);
  /* The button's release is the compositor's now; the program hears of it when the drag is over. */
  return FALSE;
}

/*
 * The compositor takes the drag's icon only as the drag starts, and a
 * program may show it after the drag began and grabbed the pointer, as
 * GTK shows its default icon. The drag starts once the event that
 * began it has been handled, with whatever icon is up by then; one
 * shown later would be a popup the compositor soon dismisses, and that
 * dismissal would end the drag.
 */
void
_gdk_wayland_drag_start (void)
{
  if (!_gdk_wayland_drag_pending || dnd_start_idle)
    return;
  dnd_start_idle = g_idle_add_full (G_PRIORITY_HIGH, drag_start_now, NULL, NULL);
}

GdkDragContext *
_gdk_wayland_drag_context_new (void)
{
  DragContext *c = g_new0 (DragContext, 1);

  c->ref_count = 1;
  c->context.protocol = GDK_DRAG_PROTO_NONE;
  return &c->context;
}

void
_gdk_wayland_drag_context_ref (GdkDragContext *context)
{
  ((DragContext *) context)->ref_count++;
}

void
_gdk_wayland_drag_context_unref (GdkDragContext *context)
{
  DragContext *c = (DragContext *) context;

  if (--c->ref_count > 0)
    return;
  g_list_free (context->targets);
  if (context->source_window)
    gdk_window_unref (context->source_window);
  if (context->dest_window)
    gdk_window_unref (context->dest_window);
  g_free (c);
}

/* The destination's answer to a motion: what it would do with a drop. */
void
_gdk_wayland_drag_status (GdkDragContext *context, GdkDragAction action, guint32 time)
{
  if (context != drop_context || !dnd_offer)
    return;
  context->action = action;
  if (action && context->targets)
    wl_data_offer_accept (dnd_offer, dnd_enter_serial,
                          _gdk_wayland_atom_string (GPOINTER_TO_UINT (context->targets->data)));
  else
    wl_data_offer_accept (dnd_offer, dnd_enter_serial, NULL);
  if (wl_data_offer_get_version (dnd_offer) >= 3)
    {
      uint32_t wl_action = action == GDK_ACTION_MOVE ? WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE
        : action ? WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY : WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;

      wl_data_offer_set_actions (dnd_offer, wl_action, wl_action);
    }
}

void
_gdk_wayland_drop_finish (GdkDragContext *context, gboolean success, guint32 time)
{
  if (context != drop_context || !dnd_offer)
    return;
  drop_begun = FALSE;
  if (success && wl_data_offer_get_version (dnd_offer) >= 3)
    wl_data_offer_finish (dnd_offer);
  offer_destroy (dnd_offer);
  dnd_offer = NULL;
  free_types (dnd_offer_mime_types);
  dnd_offer_mime_types = NULL;
  gdk_drag_context_unref (drop_context);
  drop_context = NULL;
}

/* A drop nothing here took is over at once: the source is told so. */
void
_gdk_wayland_drop_reply (GdkDragContext *context, gboolean ok, guint32 time)
{
  if (!ok)
    _gdk_wayland_drop_finish (context, FALSE, time);
}

/* A drag from this program is under way, begun and not yet finished. */
gboolean
_gdk_wayland_drag_under_way (void)
{
  return dnd_context != NULL;
}

GdkAtom
_gdk_wayland_drag_get_selection (GdkDragContext *context)
{
  return atom_dnd;
}

GdkDragContext *
_gdk_wayland_drag_begin (GdkWindow *window, GList *targets)
{
  GdkDragContext *context = _gdk_wayland_drag_context_new ();
  GList *l;

  context->protocol = GDK_DRAG_PROTO_XDND;
  context->is_source = TRUE;
  context->source_window = gdk_window_ref (window);
  context->targets = g_list_copy (targets);
  context->actions = GDK_ACTION_COPY | GDK_ACTION_MOVE;

  if (dnd_context)
    drag_finished (FALSE);
  if (!_gdk_wl_data_device_manager || !_gdk_wl_data_device)
    return context;
  dnd_context = context;
  gdk_drag_context_ref (context);
  dnd_owner = window;
  dnd_source = wl_data_device_manager_create_data_source (_gdk_wl_data_device_manager);
  wl_data_source_add_listener (dnd_source, &source_listener, NULL);
  for (l = targets; l; l = l->next)
    {
      const gchar *name = _gdk_wayland_atom_string (GPOINTER_TO_UINT (l->data));

      if (name)
        wl_data_source_offer (dnd_source, name);
    }
  if (wl_data_source_get_version (dnd_source) >= 3)
    wl_data_source_set_actions (dnd_source, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY |
                                WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE);
  dnd_icon = NULL;
  _gdk_wayland_drag_pending = TRUE;
  return context;
}

guint32
_gdk_wayland_drag_get_protocol (guint32 xid, GdkDragProtocol *protocol)
{
  *protocol = GDK_DRAG_PROTO_NONE;
  return 0;
}

void
_gdk_wayland_drag_find_window (GdkDragContext *context, GdkWindow *drag_window,
                               gint x_root, gint y_root, GdkWindow **dest_window,
                               GdkDragProtocol *protocol)
{
  *dest_window = NULL;
  *protocol = GDK_DRAG_PROTO_NONE;
}

gboolean
_gdk_wayland_drag_motion (GdkDragContext *context, GdkWindow *dest_window,
                          GdkDragProtocol protocol, gint x_root, gint y_root,
                          GdkDragAction suggested_action, GdkDragAction possible_actions,
                          guint32 time)
{
  if (context == dnd_context && _gdk_wayland_drag_pending)
    _gdk_wayland_drag_start ();
  return FALSE;
}

void
_gdk_wayland_drag_drop (GdkDragContext *context, guint32 time)
{
}

void
_gdk_wayland_drag_abort (GdkDragContext *context, guint32 time)
{
  if (context == dnd_context)
    drag_finished (FALSE);
}
