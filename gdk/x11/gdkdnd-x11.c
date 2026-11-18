/* GDK - The GIMP Drawing Kit
 * Copyright (C) 1995-1999 Peter Mattis, Spencer Kimball and Josh MacDonald
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

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <string.h>
#include "gdkx.h"
#include "gdkprivate-x11.h"
#include "gdkbackend-x11.h"
#include "gdk.h"

typedef struct _GdkDragContextPrivate GdkDragContextPrivate;

typedef enum {
  GDK_DRAG_STATUS_DRAG,
  GDK_DRAG_STATUS_MOTION_WAIT,
  GDK_DRAG_STATUS_ACTION_WAIT,
  GDK_DRAG_STATUS_DROP
} GtkDragStatus;

typedef struct {
  guint32 xid;
  gint x, y, width, height;
  gboolean mapped;
} GdkCacheChild;

typedef struct {
  GList *children;
  GHashTable *child_hash;
  guint old_event_mask;
} GdkWindowCache;

/* Structure that holds information about a drag in progress.
 * this is used on both source and destination sides.
 */
struct _GdkDragContextPrivate {
  GdkDragContext context;

  GdkAtom xdnd_selection;
  guint   ref_count;

  guint16 last_x;               /* Coordinates from last event */
  guint16 last_y;
  GdkDragAction xdnd_actions;     /* What is currently set in XdndActionList */

  Window dest_xid;              /* The last window we looked up */
  Window drop_xid;            /* The (non-proxied) window that is receiving drops */
  guint xdnd_targets_set : 1;   /* Whether we've already set XdndTypeList */
  guint xdnd_actions_set : 1;   /* Whether we've already set XdndActionList */
  guint xdnd_have_actions : 1; /* Whether an XdndActionList was provided */
  guint drag_status : 4;        /* current status of drag */

  GdkWindowCache *window_cache;
};

static GdkDragContext *current_dest_drag = NULL;

/* Forward declarations */

static void gdk_window_cache_destroy (GdkWindowCache *cache);

static GdkFilterReturn xdnd_enter_filter (GdkXEvent *xev,
                                          GdkEvent  *event,
                                          gpointer   data);

static GdkFilterReturn xdnd_leave_filter (GdkXEvent *xev,
                                          GdkEvent  *event,
                                          gpointer   data);

static GdkFilterReturn xdnd_position_filter (GdkXEvent *xev,
                                             GdkEvent  *event,
                                             gpointer   data);

static GdkFilterReturn xdnd_status_filter (GdkXEvent *xev,
                                           GdkEvent  *event,
                                           gpointer   data);

static GdkFilterReturn xdnd_finished_filter (GdkXEvent *xev,
                                            GdkEvent  *event,
                                            gpointer   data);

static GdkFilterReturn xdnd_drop_filter (GdkXEvent *xev,
                                         GdkEvent  *event,
                                         gpointer   data);

static void   xdnd_manage_source_filter (GdkDragContext *context,
                                         GdkWindow      *window,
                                         gboolean        add_filter);

/* Drag Contexts */

static GList *contexts;

GdkDragContext *
_gdk_x11_drag_context_new        (void)
{
  GdkDragContextPrivate *result;

  result = g_new0 (GdkDragContextPrivate, 1);

  result->ref_count = 1;
  /* Until a destination is found: zero would be GDK_DRAG_PROTO_MOTIF. */
  result->context.protocol = GDK_DRAG_PROTO_NONE;

  contexts = g_list_prepend (contexts, result);

  return (GdkDragContext *)result;
}

void
_gdk_x11_drag_context_ref (GdkDragContext *context)
{
  g_return_if_fail (context != NULL);

  ((GdkDragContextPrivate *)context)->ref_count++;
}

void
_gdk_x11_drag_context_unref (GdkDragContext *context)
{
  GdkDragContextPrivate *private = (GdkDragContextPrivate *)context;

  g_return_if_fail (context != NULL);
  g_return_if_fail (private->ref_count > 0);

  private->ref_count--;

  if (private->ref_count == 0)
    {
      g_dataset_destroy (private);

      g_list_free (context->targets);

      if (context->source_window)
        {
          if ((context->protocol == GDK_DRAG_PROTO_XDND) &&
              !context->is_source)
            xdnd_manage_source_filter (context, context->source_window, FALSE);

          gdk_window_unref (context->source_window);
        }

      if (context->dest_window)
        gdk_window_unref (context->dest_window);

      if (private->window_cache)
        gdk_window_cache_destroy (private->window_cache);

      contexts = g_list_remove (contexts, private);
      g_free (private);
    }
}

static GdkDragContext *
gdk_drag_context_find (gboolean is_source,
                       Window   source_xid,
                       Window   dest_xid)
{
  GList *tmp_list = contexts;
  GdkDragContext *context;
  GdkDragContextPrivate *private;
  Window context_dest_xid;

  while (tmp_list)
    {
      context = (GdkDragContext *)tmp_list->data;
      private = (GdkDragContextPrivate *)context;

      context_dest_xid = context->dest_window ?
                           (private->drop_xid ?
                              private->drop_xid :
                              GDK_WINDOW_XWINDOW (context->dest_window)) :
                           None;

      if ((!context->is_source == !is_source) &&
          ((source_xid == None) || (context->source_window &&
            (GDK_WINDOW_XWINDOW (context->source_window) == source_xid))) &&
          ((dest_xid == None) || (context_dest_xid == dest_xid)))
        return context;

      tmp_list = tmp_list->next;
    }

  return NULL;
}

/* Utility functions */

static void
gdk_window_cache_add (GdkWindowCache *cache,
                      guint32 xid,
                      gint x, gint y, gint width, gint height,
                      gboolean mapped)
{
  GdkCacheChild *child = g_new (GdkCacheChild, 1);

  child->xid = xid;
  child->x = x;
  child->y = y;
  child->width = width;
  child->height = height;
  child->mapped = mapped;

  cache->children = g_list_prepend (cache->children, child);
  g_hash_table_insert (cache->child_hash, GUINT_TO_POINTER (xid),
                       cache->children);
}

static GdkFilterReturn
gdk_window_cache_filter (GdkXEvent *xev,
                         GdkEvent  *event,
                         gpointer   data)
{
  XEvent *xevent = (XEvent *)xev;
  GdkWindowCache *cache = data;

  switch (xevent->type)
    {
    case CirculateNotify:
      break;
    case ConfigureNotify:
      {
        XConfigureEvent *xce = &xevent->xconfigure;
        GList *node;

        node = g_hash_table_lookup (cache->child_hash,
                                    GUINT_TO_POINTER (xce->window));
        if (node)
          {
            GdkCacheChild *child = node->data;
            child->x = xce->x;
            child->y = xce->y;
            child->width = xce->width;
            child->height = xce->height;
            if (xce->above == None && (node->next))
              {
                GList *last = g_list_last (cache->children);
                cache->children = g_list_remove_link (cache->children, node);
                last->next = node;
                node->next = NULL;
                node->prev = last;
              }
            else
              {
                GList *above_node = g_hash_table_lookup (cache->child_hash,
                                                         GUINT_TO_POINTER (xce->above));
                if (above_node && node->prev != above_node)
                  {
                    cache->children = g_list_remove_link (cache->children, node);
                    node->next = above_node->next;
                    if (node->next)
                      node->next->prev = node;
                    node->prev = above_node;
                    above_node->next = node;
                  }
              }
          }
        break;
      }
    case CreateNotify:
      {
        XCreateWindowEvent *xcwe = &xevent->xcreatewindow;

        if (!g_hash_table_lookup (cache->child_hash,
                                  GUINT_TO_POINTER (xcwe->window)))
          gdk_window_cache_add (cache, xcwe->window,
                                xcwe->x, xcwe->y, xcwe->width, xcwe->height,
                                FALSE);
        break;
      }
    case DestroyNotify:
      {
        XDestroyWindowEvent *xdwe = &xevent->xdestroywindow;
        GList *node;

        node = g_hash_table_lookup (cache->child_hash,
                                    GUINT_TO_POINTER (xdwe->window));
        if (node)
          {
            g_hash_table_remove (cache->child_hash,
                                 GUINT_TO_POINTER (xdwe->window));
            cache->children = g_list_remove_link (cache->children, node);
            g_free (node->data);
            g_list_free_1 (node);
          }
        break;
      }
    case MapNotify:
      {
        XMapEvent *xme = &xevent->xmap;
        GList *node;

        node = g_hash_table_lookup (cache->child_hash,
                                    GUINT_TO_POINTER (xme->window));
        if (node)
          {
            GdkCacheChild *child = node->data;
            child->mapped = TRUE;
          }
        break;
      }
    case ReparentNotify:
      break;
    case UnmapNotify:
      {
        XMapEvent *xume = &xevent->xmap;
        GList *node;

        node = g_hash_table_lookup (cache->child_hash,
                                    GUINT_TO_POINTER (xume->window));
        if (node)
          {
            GdkCacheChild *child = node->data;
            child->mapped = FALSE;
          }
        break;
      }
    default:
      return GDK_FILTER_CONTINUE;
    }
  return GDK_FILTER_REMOVE;
}

static GdkWindowCache *
gdk_window_cache_new (void)
{
  XWindowAttributes xwa;
  Window root, parent, *children;
  unsigned int nchildren;
  int i;

  gint old_warnings = gdk_error_warnings;

  GdkWindowCache *result = g_new (GdkWindowCache, 1);

  result->children = NULL;
  result->child_hash = g_hash_table_new (g_direct_hash, NULL);

  XGetWindowAttributes (gdk_display, gdk_root_window, &xwa);
  result->old_event_mask = xwa.your_event_mask;
  XSelectInput (gdk_display, gdk_root_window,
                result->old_event_mask | SubstructureNotifyMask);
  gdk_window_add_filter ((GdkWindow *)&gdk_root_parent,
                         gdk_window_cache_filter, result);

  gdk_error_code = 0;
  gdk_error_warnings = 0;

  if (XQueryTree(gdk_display, gdk_root_window,
                 &root, &parent, &children, &nchildren) == 0)
    return result;

  for (i = 0; (unsigned int) i < nchildren ; i++)
    {
      XGetWindowAttributes (gdk_display, children[i], &xwa);

      gdk_window_cache_add (result, children[i],
                            xwa.x, xwa.y, xwa.width, xwa.height,
                            xwa.map_state != IsUnmapped);

      if (gdk_error_code)
        gdk_error_code = 0;
      else
        {
          gdk_window_cache_add (result, children[i],
                                xwa.x, xwa.y, xwa.width, xwa.height,
                                (xwa.map_state != IsUnmapped));
        }
    }

  XFree (children);

  gdk_error_warnings = old_warnings;

  return result;
}

static void
gdk_window_cache_destroy (GdkWindowCache *cache)
{
  XSelectInput (gdk_display, gdk_root_window, cache->old_event_mask);
  gdk_window_remove_filter ((GdkWindow *)&gdk_root_parent,
                            gdk_window_cache_filter, cache);

  g_list_foreach (cache->children, (GFunc)g_free, NULL);
  g_list_free (cache->children);
  g_hash_table_destroy (cache->child_hash);

  g_free (cache);
}

static Window
get_client_window_at_coords_recurse (Window  win,
                                     gint    x,
                                     gint    y)
{
  Window root, tmp_parent, *children;
  unsigned int nchildren;
  int i;
  Window child = None;
  Atom type = None;
  int format;
  unsigned long nitems, after;
  unsigned char *data;

  static Atom wm_state_atom = None;

  if (!wm_state_atom)
    wm_state_atom = _gdk_x11_atom_intern ("WM_STATE", FALSE);

  XGetWindowProperty (gdk_display, win,
                      wm_state_atom, 0, 0, False, AnyPropertyType,
                      &type, &format, &nitems, &after, &data);

  if (gdk_error_code)
    {
      gdk_error_code = 0;

      return None;
    }

  if (type != None)
    {
      XFree (data);
      return win;
    }

  if (XQueryTree(gdk_display, win,
                 &root, &tmp_parent, &children, &nchildren) == 0)
    return 0;

  if (!gdk_error_code)
    {
      for (i = nchildren - 1; (i >= 0) && (child == None); i--)
        {
          XWindowAttributes xwa;

          XGetWindowAttributes (gdk_display, children[i], &xwa);

          if (gdk_error_code)
            gdk_error_code = 0;
          else if ((xwa.map_state == IsViewable) && (xwa.class == InputOutput) &&
                   (x >= xwa.x) && (x < xwa.x + (gint)xwa.width) &&
                   (y >= xwa.y) && (y < xwa.y + (gint)xwa.height))
            {
              x -= xwa.x;
              y -= xwa.y;
              child = children[i];
            }
        }

      XFree (children);
    }
  else
    gdk_error_code = 0;

  if (child)
    return get_client_window_at_coords_recurse (child, x, y);
  else
    return None;
}

static Window
get_client_window_at_coords (GdkWindowCache *cache,
                             Window          ignore,
                             gint            x_root,
                             gint            y_root)
{
  GList *tmp_list;
  Window retval = None;

  gint old_warnings = gdk_error_warnings;

  gdk_error_code = 0;
  gdk_error_warnings = 0;

  tmp_list = cache->children;

  while (tmp_list && !retval)
    {
      GdkCacheChild *child = tmp_list->data;

      if ((child->xid != ignore) && (child->mapped))
        {
          if ((x_root >= child->x) && (x_root < child->x + child->width) &&
              (y_root >= child->y) && (y_root < child->y + child->height))
            {
              retval = get_client_window_at_coords_recurse (child->xid,
                                                            x_root - child->x,
                                                            y_root - child->y);
              if (!retval)
                retval = child->xid;
            }

        }
      tmp_list = tmp_list->next;
    }

  gdk_error_warnings = old_warnings;
  if (retval)
    return retval;
  else
    return gdk_root_window;
}

#ifdef G_ENABLE_DEBUG
static void
print_target_list (GList *targets)
{
  while (targets)
    {
      gchar *name = _gdk_x11_atom_name (GPOINTER_TO_INT (targets->data));
      g_message ("\t%s", name);
      g_free (name);
      targets = targets->next;
    }
}
#endif /* G_ENABLE_DEBUG */

/*************************************************************
 ***************************** XDND **************************
 *************************************************************/

/* Utility functions */

static struct {
  gchar *name;
  GdkAtom atom;
  GdkDragAction action;
} xdnd_actions_table[] = {
    { "XdndActionCopy",    GDK_NONE, GDK_ACTION_COPY },
    { "XdndActionMove",    GDK_NONE, GDK_ACTION_MOVE },
    { "XdndActionLink",    GDK_NONE, GDK_ACTION_LINK },
    { "XdndActionAsk",     GDK_NONE, GDK_ACTION_ASK  },
    { "XdndActionPrivate", GDK_NONE, GDK_ACTION_COPY },
  };

static const gint xdnd_n_actions = sizeof(xdnd_actions_table) / sizeof(xdnd_actions_table[0]);
static gboolean xdnd_actions_initialized = FALSE;

static void
xdnd_initialize_actions (void)
{
  gint i;

  xdnd_actions_initialized = TRUE;
  for (i=0; i < xdnd_n_actions; i++)
    xdnd_actions_table[i].atom = _gdk_x11_atom_intern (xdnd_actions_table[i].name, FALSE);
}

static GdkDragAction
xdnd_action_from_atom (GdkAtom atom)
{
  gint i;

  if (!xdnd_actions_initialized)
    xdnd_initialize_actions();

  for (i=0; i<xdnd_n_actions; i++)
    if (atom == xdnd_actions_table[i].atom)
      return xdnd_actions_table[i].action;

  return 0;
}

static GdkAtom
xdnd_action_to_atom (GdkDragAction action)
{
  gint i;

  if (!xdnd_actions_initialized)
    xdnd_initialize_actions();

  for (i=0; i<xdnd_n_actions; i++)
    if (action == xdnd_actions_table[i].action)
      return xdnd_actions_table[i].atom;

  return GDK_NONE;
}

static GdkAtom xdnd_aware_atom = GDK_NONE;

/* Source side */

static GdkFilterReturn
xdnd_status_filter (GdkXEvent *xev,
                    GdkEvent  *event,
                    gpointer   data)
{
  XEvent *xevent = (XEvent *)xev;
  guint32 dest_window = xevent->xclient.data.l[0];
  guint32 flags = xevent->xclient.data.l[1];
  GdkAtom action = xevent->xclient.data.l[4];
  GdkDragContext *context;

  GDK_NOTE (DND,
            g_message ("XdndStatus: dest_window: %#x  action: %ld",
                       dest_window, action));


  context = gdk_drag_context_find (TRUE, xevent->xclient.window, dest_window);
  if (context)
    {
      GdkDragContextPrivate *private = (GdkDragContextPrivate *)context;
      if (private->drag_status == GDK_DRAG_STATUS_MOTION_WAIT)
        private->drag_status = GDK_DRAG_STATUS_DRAG;

      event->dnd.send_event = FALSE;
      event->dnd.type = GDK_DRAG_STATUS;
      event->dnd.context = context;
      _gdk_x11_drag_context_ref (context);

      event->dnd.time = GDK_CURRENT_TIME; /* FIXME? */
      if (!(action != 0) != !(flags & 1))
        {
          GDK_NOTE (DND,
                    g_warning ("Received status event with flags not corresponding to action!\n"));
          action = 0;
        }

      context->action = xdnd_action_from_atom (action);

      return GDK_FILTER_TRANSLATE;
    }

  return GDK_FILTER_REMOVE;
}

static GdkFilterReturn
xdnd_finished_filter (GdkXEvent *xev,
                      GdkEvent  *event,
                      gpointer   data)
{
  XEvent *xevent = (XEvent *)xev;
  guint32 dest_window = xevent->xclient.data.l[0];
  GdkDragContext *context;

  GDK_NOTE (DND,
            g_message ("XdndFinished: dest_window: %#x", dest_window));

  context = gdk_drag_context_find (TRUE, xevent->xclient.window, dest_window);
  if (context)
    {
      event->dnd.type = GDK_DROP_FINISHED;
      event->dnd.context = context;
      _gdk_x11_drag_context_ref (context);

      return GDK_FILTER_TRANSLATE;
    }

  return GDK_FILTER_REMOVE;
}

static void
xdnd_set_targets (GdkDragContext *context)
{
  GdkDragContextPrivate *private = (GdkDragContextPrivate *)context;
  GdkAtom *atomlist;
  GList *tmp_list = context->targets;
  gint i;
  gint n_atoms = g_list_length (context->targets);

  atomlist = g_new (GdkAtom, n_atoms);
  i = 0;
  while (tmp_list)
    {
      atomlist[i] = GPOINTER_TO_INT (tmp_list->data);
      tmp_list = tmp_list->next;
      i++;
    }

  XChangeProperty (GDK_WINDOW_XDISPLAY (context->source_window),
                   GDK_WINDOW_XWINDOW (context->source_window),
                   _gdk_x11_atom_intern ("XdndTypeList", FALSE),
                   XA_ATOM, 32, PropModeReplace,
                   (guchar *)atomlist, n_atoms);

  g_free (atomlist);

  private->xdnd_targets_set = 1;
}

static void
xdnd_set_actions (GdkDragContext *context)
{
  GdkDragContextPrivate *private = (GdkDragContextPrivate *)context;
  GdkAtom *atomlist;
  gint i;
  gint n_atoms;
  guint actions;

  if (!xdnd_actions_initialized)
    xdnd_initialize_actions();

  actions = context->actions;
  n_atoms = 0;
  for (i=0; i<xdnd_n_actions; i++)
    {
      if (actions & xdnd_actions_table[i].action)
        {
          actions &= ~xdnd_actions_table[i].action;
          n_atoms++;
        }
    }

  atomlist = g_new (GdkAtom, n_atoms);

  actions = context->actions;
  n_atoms = 0;
  for (i=0; i<xdnd_n_actions; i++)
    {
      if (actions & xdnd_actions_table[i].action)
        {
          actions &= ~xdnd_actions_table[i].action;
          atomlist[n_atoms] = xdnd_actions_table[i].atom;
          n_atoms++;
        }
    }

  XChangeProperty (GDK_WINDOW_XDISPLAY (context->source_window),
                   GDK_WINDOW_XWINDOW (context->source_window),
                   _gdk_x11_atom_intern ("XdndActionList", FALSE),
                   XA_ATOM, 32, PropModeReplace,
                   (guchar *)atomlist, n_atoms);

  g_free (atomlist);

  private->xdnd_actions_set = 1;
  private->xdnd_actions = context->actions;
}

/*************************************************************
 * xdnd_send_xevent:
 *     Like gdk_send_event, but if the target is the root
 *     window, sets an event mask of ButtonPressMask, otherwise
 *     an event mask of 0.
 *   arguments:
 *
 *   results:
 *************************************************************/

static gint
xdnd_send_xevent (Window window, gboolean propagate,
                  XEvent *event_send)
{
  if (window == gdk_root_window)
    return gdk_send_xevent (window, propagate, ButtonPressMask, event_send);
  else
    return gdk_send_xevent (window, propagate, 0, event_send);
}

static void
xdnd_send_enter (GdkDragContext *context)
{
  XEvent xev;
  GdkDragContextPrivate *private = (GdkDragContextPrivate *)context;

  xev.xclient.type = ClientMessage;
  xev.xclient.message_type = _gdk_x11_atom_intern ("XdndEnter", FALSE);
  xev.xclient.format = 32;
  xev.xclient.window = private->drop_xid ?
                           private->drop_xid :
                           GDK_WINDOW_XWINDOW (context->dest_window);
  xev.xclient.data.l[0] = GDK_WINDOW_XWINDOW (context->source_window);
  xev.xclient.data.l[1] = (3 << 24); /* version */
  xev.xclient.data.l[2] = 0;
  xev.xclient.data.l[3] = 0;
  xev.xclient.data.l[4] = 0;

  if (!private->xdnd_selection)
    private->xdnd_selection = _gdk_x11_atom_intern ("XdndSelection", FALSE);

  if (g_list_length (context->targets) > 3)
    {
      if (!private->xdnd_targets_set)
        xdnd_set_targets (context);
      xev.xclient.data.l[1] |= 1;
    }
  else
    {
      GList *tmp_list = context->targets;
      gint i = 2;

      while (tmp_list)
        {
          xev.xclient.data.l[i] = GPOINTER_TO_INT (tmp_list->data);
          tmp_list = tmp_list->next;
          i++;
        }
    }

  if (!xdnd_send_xevent (GDK_WINDOW_XWINDOW (context->dest_window),
                         FALSE, &xev))
    {
      GDK_NOTE (DND,
                g_message ("Send event to %lx failed",
                           GDK_WINDOW_XWINDOW (context->dest_window)));
      gdk_window_unref (context->dest_window);
      context->dest_window = NULL;
    }
}

static void
xdnd_send_leave (GdkDragContext *context)
{
  XEvent xev;

  GdkDragContextPrivate *private = (GdkDragContextPrivate *)context;

  xev.xclient.type = ClientMessage;
  xev.xclient.message_type = _gdk_x11_atom_intern ("XdndLeave", FALSE);
  xev.xclient.format = 32;
  xev.xclient.window = private->drop_xid ?
                           private->drop_xid :
                           GDK_WINDOW_XWINDOW (context->dest_window);
  xev.xclient.data.l[0] = GDK_WINDOW_XWINDOW (context->source_window);
  xev.xclient.data.l[1] = 0;
  xev.xclient.data.l[2] = 0;
  xev.xclient.data.l[3] = 0;
  xev.xclient.data.l[4] = 0;

  if (!xdnd_send_xevent (GDK_WINDOW_XWINDOW (context->dest_window),
                         FALSE, &xev))
    {
      GDK_NOTE (DND,
                g_message ("Send event to %lx failed",
                           GDK_WINDOW_XWINDOW (context->dest_window)));
      gdk_window_unref (context->dest_window);
      context->dest_window = NULL;
    }
}

static void
xdnd_send_drop (GdkDragContext *context, guint32 time)
{
  GdkDragContextPrivate *private = (GdkDragContextPrivate *)context;
  XEvent xev;

  xev.xclient.type = ClientMessage;
  xev.xclient.message_type = _gdk_x11_atom_intern ("XdndDrop", FALSE);
  xev.xclient.format = 32;
  xev.xclient.window = private->drop_xid ?
                           private->drop_xid :
                           GDK_WINDOW_XWINDOW (context->dest_window);
  xev.xclient.data.l[0] = GDK_WINDOW_XWINDOW (context->source_window);
  xev.xclient.data.l[1] = 0;
  xev.xclient.data.l[2] = time;
  xev.xclient.data.l[3] = 0;
  xev.xclient.data.l[4] = 0;

  if (!xdnd_send_xevent (GDK_WINDOW_XWINDOW (context->dest_window),
                         FALSE, &xev))
    {
      GDK_NOTE (DND,
                g_message ("Send event to %lx failed",
                           GDK_WINDOW_XWINDOW (context->dest_window)));
      gdk_window_unref (context->dest_window);
      context->dest_window = NULL;
    }
}

static void
xdnd_send_motion (GdkDragContext *context,
                  gint            x_root,
                  gint            y_root,
                  GdkDragAction   action,
                  guint32         time)
{
  GdkDragContextPrivate *private = (GdkDragContextPrivate *)context;
  XEvent xev;

  xev.xclient.type = ClientMessage;
  xev.xclient.message_type = _gdk_x11_atom_intern ("XdndPosition", FALSE);
  xev.xclient.format = 32;
  xev.xclient.window = private->drop_xid ?
                           private->drop_xid :
                           GDK_WINDOW_XWINDOW (context->dest_window);
  xev.xclient.data.l[0] = GDK_WINDOW_XWINDOW (context->source_window);
  xev.xclient.data.l[1] = 0;
  xev.xclient.data.l[2] = (x_root << 16) | y_root;
  xev.xclient.data.l[3] = time;
  xev.xclient.data.l[4] = xdnd_action_to_atom (action);

  if (!xdnd_send_xevent (GDK_WINDOW_XWINDOW (context->dest_window),
                         FALSE, &xev))
    {
      GDK_NOTE (DND,
                g_message ("Send event to %lx failed",
                           GDK_WINDOW_XWINDOW (context->dest_window)));
      gdk_window_unref (context->dest_window);
      context->dest_window = NULL;
    }
  private->drag_status = GDK_DRAG_STATUS_MOTION_WAIT;
}

static guint32
xdnd_check_dest (Window win)
{
  gboolean retval = FALSE;
  Atom type = None;
  int format;
  unsigned long nitems, after;
  GdkAtom *version;
  Window *proxy_data;
  Window proxy;
  static GdkAtom xdnd_proxy_atom = GDK_NONE;

  gint old_warnings = gdk_error_warnings;

  if (!xdnd_proxy_atom)
    xdnd_proxy_atom = _gdk_x11_atom_intern ("XdndProxy", FALSE);

  if (!xdnd_aware_atom)
    xdnd_aware_atom = _gdk_x11_atom_intern ("XdndAware", FALSE);

  proxy = GDK_NONE;

  gdk_error_code = 0;
  gdk_error_warnings = 0;

  XGetWindowProperty (gdk_display, win,
                      xdnd_proxy_atom, 0,
                      1, False, AnyPropertyType,
                      &type, &format, &nitems, &after,
                      (guchar **)&proxy_data);

  if (!gdk_error_code)
    {
      if (type != None)
        {
          if ((format == 32) && (nitems == 1))
            {
              proxy = *proxy_data;
            }
          else
            GDK_NOTE (DND,
                      g_warning ("Invalid XdndOwner property on window %ld\n", win));

          XFree (proxy_data);
        }

      XGetWindowProperty (gdk_display, proxy ? proxy : win,
                          xdnd_aware_atom, 0,
                          1, False, AnyPropertyType,
                          &type, &format, &nitems, &after,
                          (guchar **)&version);

      if (!gdk_error_code && type != None)
        {
          if ((format == 32) && (nitems == 1))
            {
              if (*version >= 3)
                retval = TRUE;
            }
          else
            GDK_NOTE (DND,
                      g_warning ("Invalid XdndAware property on window %ld\n", win));

          XFree (version);
        }

    }

  gdk_error_warnings = old_warnings;
  gdk_error_code = 0;

  return retval ? (proxy ? proxy : win) : GDK_NONE;
}

/* Target side */

static void
xdnd_read_actions (GdkDragContext *context)
{
  Atom type;
  int format;
  gulong nitems, after;
  Atom *data;

  gint i;

  gint old_warnings = gdk_error_warnings;

  gdk_error_code = 0;
  gdk_error_warnings = 0;

  /* Get the XdndActionList, if set */

  XGetWindowProperty (GDK_WINDOW_XDISPLAY (context->source_window),
                      GDK_WINDOW_XWINDOW (context->source_window),
                      _gdk_x11_atom_intern ("XdndActionList", FALSE), 0, 65536,
                      False, XA_ATOM, &type, &format, &nitems,
                      &after, (guchar **)&data);

  if (!gdk_error_code && (format == 32) && (type == XA_ATOM))
    {
      context->actions = 0;

      for (i=0; (gulong) i<nitems; i++)
        context->actions |= xdnd_action_from_atom (data[i]);

      ((GdkDragContextPrivate *)context)->xdnd_have_actions = TRUE;

#ifdef G_ENABLE_DEBUG
      if (gdk_debug_flags & GDK_DEBUG_DND)
        {
          GString *action_str = g_string_new (NULL);
          if (context->actions & GDK_ACTION_MOVE)
            g_string_append(action_str, "MOVE ");
          if (context->actions & GDK_ACTION_COPY)
            g_string_append(action_str, "COPY ");
          if (context->actions & GDK_ACTION_LINK)
            g_string_append(action_str, "LINK ");
          if (context->actions & GDK_ACTION_ASK)
            g_string_append(action_str, "ASK ");

          g_message("Xdnd actions = %s", action_str->str);
          g_string_free (action_str, TRUE);
        }
#endif /* G_ENABLE_DEBUG */

      XFree(data);
    }

  gdk_error_warnings = old_warnings;
  gdk_error_code = 0;
}

/* We have to make sure that the XdndActionList we keep internally
 * is up to date with the XdndActionList on the source window
 * because we get no notification, because Xdnd wasn't meant
 * to continually send actions. So we select on PropertyChangeMask
 * and add this filter.
 */
static GdkFilterReturn
xdnd_source_window_filter (GdkXEvent *xev,
                           GdkEvent  *event,
                           gpointer   cb_data)
{
  XEvent *xevent = (XEvent *)xev;
  GdkDragContext *context = cb_data;

  if ((xevent->xany.type == PropertyNotify) &&
      (xevent->xproperty.atom == _gdk_x11_atom_intern ("XdndActionList", FALSE)))
    {
      xdnd_read_actions (context);

      return GDK_FILTER_REMOVE;
    }

  return GDK_FILTER_CONTINUE;
}

static void
xdnd_manage_source_filter (GdkDragContext *context,
                           GdkWindow      *window,
                           gboolean        add_filter)
{
  gint old_warnings = 0;        /* quiet gcc */
  GdkWindowPrivate *private = (GdkWindowPrivate *)window;

  gboolean is_foreign = (private->window_type == GDK_WINDOW_FOREIGN);

  if (is_foreign)
    {
      old_warnings = gdk_error_warnings;
      gdk_error_warnings = 0;
    }

  if (!private->destroyed)
    {
      if (add_filter)
        {
          _gdk_x11_window_set_events (window,
                                 gdk_window_get_events (window) |
                                 GDK_PROPERTY_CHANGE_MASK);
          gdk_window_add_filter (window, xdnd_source_window_filter, context);

        }
      else
        {
          gdk_window_remove_filter (window,
                                    xdnd_source_window_filter,
                                    context);
          /* Should we remove the GDK_PROPERTY_NOTIFY mask?
           * but we might want it for other reasons. (Like
           * INCR selection transactions).
           */
        }
    }

  if (is_foreign)
    {
      gdk_flush();
      gdk_error_warnings = old_warnings;
    }
}

static GdkFilterReturn
xdnd_enter_filter (GdkXEvent *xev,
                   GdkEvent  *event,
                   gpointer   cb_data)
{
  XEvent *xevent = (XEvent *)xev;
  GdkDragContext *new_context;
  gint i;

  Atom type;
  int format;
  gulong nitems, after;
  Atom *data;

  guint32 source_window = xevent->xclient.data.l[0];
  gboolean get_types = ((xevent->xclient.data.l[1] & 1) != 0);
  gint version = (xevent->xclient.data.l[1] & 0xff000000) >> 24;

  GDK_NOTE (DND,
            g_message ("XdndEnter: source_window: %#x, version: %#x",
                       source_window, version));

  if (version != 3)
    {
      /* Old source ignore */
      GDK_NOTE (DND, g_message ("Ignored old XdndEnter message"));
      return GDK_FILTER_REMOVE;
    }

  if (current_dest_drag != NULL)
    {
      _gdk_x11_drag_context_unref (current_dest_drag);
      current_dest_drag = NULL;
    }

  new_context = _gdk_x11_drag_context_new ();
  new_context->protocol = GDK_DRAG_PROTO_XDND;
  new_context->is_source = FALSE;

  new_context->source_window = _gdk_x11_window_lookup (source_window);
  if (new_context->source_window)
    gdk_window_ref (new_context->source_window);
  else
    {
      new_context->source_window = _gdk_x11_window_foreign_new (source_window);
      if (!new_context->source_window)
        {
          _gdk_x11_drag_context_unref (new_context);
          return GDK_FILTER_REMOVE;
        }
    }
  new_context->dest_window = event->any.window;
  gdk_window_ref (new_context->dest_window);

  new_context->targets = NULL;
  if (get_types)
    {
      _gdk_x11_error_trap_push ();
      XGetWindowProperty (GDK_WINDOW_XDISPLAY (event->any.window),
                          source_window,
                          _gdk_x11_atom_intern ("XdndTypeList", FALSE), 0, 65536,
                          False, XA_ATOM, &type, &format, &nitems,
                          &after, (guchar **)&data);

      if (_gdk_x11_error_trap_pop () || (format != 32) || (type != XA_ATOM))
        {
          _gdk_x11_drag_context_unref (new_context);
          return GDK_FILTER_REMOVE;
        }

      for (i=0; (gulong) i<nitems; i++)
        new_context->targets = g_list_append (new_context->targets,
                                              GUINT_TO_POINTER (data[i]));

      XFree(data);
    }
  else
    {
      for (i=0; i<3; i++)
        if (xevent->xclient.data.l[2+i])
          new_context->targets = g_list_append (new_context->targets,
                                                GUINT_TO_POINTER (xevent->xclient.data.l[2+i]));
    }

#ifdef G_ENABLE_DEBUG
  if (gdk_debug_flags & GDK_DEBUG_DND)
    print_target_list (new_context->targets);
#endif /* G_ENABLE_DEBUG */

  xdnd_manage_source_filter (new_context, new_context->source_window, TRUE);
  xdnd_read_actions (new_context);

  event->dnd.type = GDK_DRAG_ENTER;
  event->dnd.context = new_context;
  _gdk_x11_drag_context_ref (new_context);

  current_dest_drag = new_context;
  ((GdkDragContextPrivate *)new_context)->xdnd_selection =
    _gdk_x11_atom_intern ("XdndSelection", FALSE);

  return GDK_FILTER_TRANSLATE;
}

static GdkFilterReturn
xdnd_leave_filter (GdkXEvent *xev,
                   GdkEvent  *event,
                   gpointer   data)
{
  XEvent *xevent = (XEvent *)xev;
  guint32 source_window = xevent->xclient.data.l[0];

  GDK_NOTE (DND,
            g_message ("XdndLeave: source_window: %#x",
                       source_window));

  if ((current_dest_drag != NULL) &&
      (current_dest_drag->protocol == GDK_DRAG_PROTO_XDND) &&
      (GDK_WINDOW_XWINDOW (current_dest_drag->source_window) == source_window))
    {
      event->dnd.type = GDK_DRAG_LEAVE;
      /* Pass ownership of context to the event */
      event->dnd.context = current_dest_drag;

      current_dest_drag = NULL;

      return GDK_FILTER_TRANSLATE;
    }
  else
    return GDK_FILTER_REMOVE;
}

static GdkFilterReturn
xdnd_position_filter (GdkXEvent *xev,
                      GdkEvent  *event,
                      gpointer   data)
{
  XEvent *xevent = (XEvent *)xev;
  guint32 source_window = xevent->xclient.data.l[0];
  gint16 x_root = xevent->xclient.data.l[2] >> 16;
  gint16 y_root = xevent->xclient.data.l[2] & 0xffff;
  guint32 time = xevent->xclient.data.l[3];
  GdkAtom action = xevent->xclient.data.l[4];

  GDK_NOTE (DND,
            g_message ("XdndPosition: source_window: %#x  position: (%d, %d)  time: %d  action: %ld",
                       source_window, x_root, y_root, time, action));


  if ((current_dest_drag != NULL) &&
      (current_dest_drag->protocol == GDK_DRAG_PROTO_XDND) &&
      (GDK_WINDOW_XWINDOW (current_dest_drag->source_window) == source_window))
    {
      event->dnd.type = GDK_DRAG_MOTION;
      event->dnd.context = current_dest_drag;
      _gdk_x11_drag_context_ref (current_dest_drag);

      event->dnd.time = time;

      current_dest_drag->suggested_action = xdnd_action_from_atom (action);
      if (!((GdkDragContextPrivate *)current_dest_drag)->xdnd_have_actions)
        current_dest_drag->actions = current_dest_drag->suggested_action;

      event->dnd.x_root = GDK_X11_FROM_X (x_root);
      event->dnd.y_root = GDK_X11_FROM_X (y_root);

      ((GdkDragContextPrivate *)current_dest_drag)->last_x = event->dnd.x_root;
      ((GdkDragContextPrivate *)current_dest_drag)->last_y = event->dnd.y_root;

      return GDK_FILTER_TRANSLATE;
    }

  return GDK_FILTER_REMOVE;
}

static GdkFilterReturn
xdnd_drop_filter (GdkXEvent *xev,
                  GdkEvent  *event,
                  gpointer   data)
{
  XEvent *xevent = (XEvent *)xev;
  guint32 source_window = xevent->xclient.data.l[0];
  guint32 time = xevent->xclient.data.l[2];

  GDK_NOTE (DND,
            g_message ("XdndDrop: source_window: %#x  time: %d",
                       source_window, time));

  if ((current_dest_drag != NULL) &&
      (current_dest_drag->protocol == GDK_DRAG_PROTO_XDND) &&
      (GDK_WINDOW_XWINDOW (current_dest_drag->source_window) == source_window))
    {
      GdkDragContextPrivate *private;
      private = (GdkDragContextPrivate *)current_dest_drag;

      event->dnd.type = GDK_DROP_START;

      event->dnd.context = current_dest_drag;
      _gdk_x11_drag_context_ref (current_dest_drag);

      event->dnd.time = time;
      event->dnd.x_root = private->last_x;
      event->dnd.y_root = private->last_y;

      return GDK_FILTER_TRANSLATE;
    }

  return GDK_FILTER_REMOVE;
}

/*************************************************************
 ************************** Public API ***********************
 *************************************************************/

void
gdk_dnd_init (void)
{
  gdk_add_client_message_filter (
        _gdk_x11_atom_intern ("XdndEnter", FALSE),
        xdnd_enter_filter, NULL);
  gdk_add_client_message_filter (
        _gdk_x11_atom_intern ("XdndLeave", FALSE),
        xdnd_leave_filter, NULL);
  gdk_add_client_message_filter (
        _gdk_x11_atom_intern ("XdndPosition", FALSE),
        xdnd_position_filter, NULL);
  gdk_add_client_message_filter (
        _gdk_x11_atom_intern ("XdndStatus", FALSE),
        xdnd_status_filter, NULL);
  gdk_add_client_message_filter (
        _gdk_x11_atom_intern ("XdndFinished", FALSE),
        xdnd_finished_filter, NULL);
  gdk_add_client_message_filter (
        _gdk_x11_atom_intern ("XdndDrop", FALSE),
        xdnd_drop_filter, NULL);
}

/* Source side */

static void
gdk_drag_do_leave (GdkDragContext *context, guint32 time)
{
  if (context->dest_window)
    {
      switch (context->protocol)
        {
        case GDK_DRAG_PROTO_MOTIF:
          break;
        case GDK_DRAG_PROTO_XDND:
          xdnd_send_leave (context);
          break;
        case GDK_DRAG_PROTO_ROOTWIN:
        case GDK_DRAG_PROTO_NONE:
          break;
        }

      gdk_window_unref (context->dest_window);
      context->dest_window = NULL;
    }
}

GdkDragContext *
_gdk_x11_drag_begin (GdkWindow     *window,
                GList         *targets)
{
  GList *tmp_list;
  GdkDragContext *new_context;

  g_return_val_if_fail (window != NULL, NULL);

  new_context = _gdk_x11_drag_context_new ();
  new_context->is_source = TRUE;
  new_context->source_window = window;
  gdk_window_ref (window);

  tmp_list = g_list_last (targets);
  new_context->targets = NULL;
  while (tmp_list)
    {
      new_context->targets = g_list_prepend (new_context->targets,
                                             tmp_list->data);
      tmp_list = tmp_list->prev;
    }

  new_context->actions = 0;

  return new_context;
}

guint32
_gdk_x11_drag_get_protocol (guint32          xid,
                       GdkDragProtocol *protocol)
{
  guint32 retval;

  if ((retval = xdnd_check_dest (xid)))
    {
      *protocol = GDK_DRAG_PROTO_XDND;
      GDK_NOTE (DND, g_message ("Entering dnd window %#x\n", xid));
      return retval;
    }
  else
    {
      /* Check if this is a root window */

      gboolean rootwin = FALSE;
      gint old_warnings = gdk_error_warnings;
      Atom type = None;
      int format;
      unsigned long nitems, after;
      unsigned char *data;

      if (xid == gdk_root_window)
        rootwin = TRUE;

      gdk_error_warnings = 0;

      if (!rootwin)
        {
          gdk_error_code = 0;

          XGetWindowProperty (gdk_display, xid,
                              _gdk_x11_atom_intern ("ENLIGHTENMENT_DESKTOP", FALSE),
                              0, 0, False, AnyPropertyType,
                              &type, &format, &nitems, &after, &data);
          if ((gdk_error_code == 0) && type != None)
            {
              XFree (data);
              rootwin = TRUE;
            }
        }

      gdk_error_warnings = old_warnings;

      if (rootwin)
        {
          *protocol = GDK_DRAG_PROTO_ROOTWIN;
          return xid;
        }
    }

  *protocol = GDK_DRAG_PROTO_NONE;
  return GDK_NONE;
}

void
_gdk_x11_drag_find_window (GdkDragContext  *context,
                      GdkWindow       *drag_window,
                      gint             x_root,
                      gint             y_root,
                      GdkWindow      **dest_window,
                      GdkDragProtocol *protocol)
{
  GdkDragContextPrivate *private = (GdkDragContextPrivate *)context;
  Window dest;

  g_return_if_fail (context != NULL);

  /* Everything from here down speaks X's pixels. */
  x_root = GDK_X11_TO_X (x_root);
  y_root = GDK_X11_TO_X (y_root);

  if (!private->window_cache)
    private->window_cache = gdk_window_cache_new();

  dest = get_client_window_at_coords (private->window_cache,
                                      drag_window ?
                                      GDK_WINDOW_XWINDOW (drag_window) : GDK_NONE,
                                      x_root, y_root);

  if (private->dest_xid != dest)
    {
      Window recipient;
      private->dest_xid = dest;

      /* Check if new destination accepts drags, and which protocol */

      /* There is some ugliness here. We actually need to pass
       * _three_ pieces of information to drag_motion - dest_window,
       * protocol, and the XID of the unproxied window. The first
       * two are passed explicitely, the third implicitly through
       * protocol->dest_xid.
       */
      if ((recipient = _gdk_x11_drag_get_protocol (dest, protocol)))
        {
          *dest_window = _gdk_x11_window_lookup (recipient);
          if (*dest_window)
            gdk_window_ref (*dest_window);
          else
            *dest_window = _gdk_x11_window_foreign_new (recipient);
        }
      else
        *dest_window = NULL;
    }
  else
    {
      *dest_window = context->dest_window;
      if (*dest_window)
        gdk_window_ref (*dest_window);
      *protocol = context->protocol;
    }
}

gboolean
_gdk_x11_drag_motion (GdkDragContext *context,
                 GdkWindow      *dest_window,
                 GdkDragProtocol protocol,
                 gint            x_root,
                 gint            y_root,
                 GdkDragAction   suggested_action,
                 GdkDragAction   possible_actions,
                 guint32         time)
{
  GdkDragContextPrivate *private = (GdkDragContextPrivate *)context;

  g_return_val_if_fail (context != NULL, FALSE);

  /* Everything from here down speaks X's pixels. */
  x_root = GDK_X11_TO_X (x_root);
  y_root = GDK_X11_TO_X (y_root);

  /* When we have a Xdnd target, make sure our XdndActionList
   * matches the current actions;
   */
  context->actions = possible_actions;

  if ((protocol == GDK_DRAG_PROTO_XDND) &&
      (!private->xdnd_actions_set ||
       private->xdnd_actions != possible_actions))
    xdnd_set_actions (context);

  if (context->dest_window != dest_window)
    {
      GdkEvent temp_event;

      /* Send a leave to the last destination */
      gdk_drag_do_leave (context, time);
      private->drag_status = GDK_DRAG_STATUS_DRAG;

      /* Check if new destination accepts drags, and which protocol */

      if (dest_window)
        {
          context->dest_window = dest_window;
          private->drop_xid = private->dest_xid;
          gdk_window_ref (context->dest_window);
          context->protocol = protocol;

          switch (protocol)
            {
            case GDK_DRAG_PROTO_MOTIF:
              break;

            case GDK_DRAG_PROTO_XDND:
              xdnd_send_enter (context);
              break;

            case GDK_DRAG_PROTO_ROOTWIN:
            case GDK_DRAG_PROTO_NONE:
              break;
            }
          context->suggested_action = suggested_action;
        }
      else
        {
          context->dest_window = NULL;
          private->drop_xid = None;
          context->action = 0;
        }

      /* Push a status event, to let the client know that
       * the drag changed
       */

      temp_event.dnd.type = GDK_DRAG_STATUS;
      temp_event.dnd.window = context->source_window;
      /* We use this to signal a synthetic status. Perhaps
       * we should use an extra field...
       */
      temp_event.dnd.send_event = TRUE;

      temp_event.dnd.context = context;
      temp_event.dnd.time = time;

      gdk_event_put (&temp_event);
    }
  else
    {
      context->suggested_action = suggested_action;
    }

  /* Send a drag-motion event */

  private->last_x = x_root;
  private->last_y = y_root;

  if (context->dest_window)
    {
      if (private->drag_status == GDK_DRAG_STATUS_DRAG)
        {
          switch (context->protocol)
            {
            case GDK_DRAG_PROTO_MOTIF:
              break;

            case GDK_DRAG_PROTO_XDND:
              xdnd_send_motion (context, x_root, y_root, suggested_action, time);
              break;

            case GDK_DRAG_PROTO_ROOTWIN:
              {
                GdkEvent temp_event;

                if (g_list_find (context->targets,
                                 GUINT_TO_POINTER (_gdk_x11_atom_intern ("application/x-rootwin-drop", FALSE))))
                  context->action = context->suggested_action;
                else
                  context->action = 0;

                temp_event.dnd.type = GDK_DRAG_STATUS;
                temp_event.dnd.window = context->source_window;
                temp_event.dnd.send_event = FALSE;
                temp_event.dnd.context = context;
                temp_event.dnd.time = time;

                gdk_event_put (&temp_event);
              }
              break;
            case GDK_DRAG_PROTO_NONE:
              g_warning ("GDK_DRAG_PROTO_NONE is not valid in _gdk_x11_drag_motion()");
              break;
            }
        }
      else
        return TRUE;
    }

  return FALSE;
}

void
_gdk_x11_drag_drop (GdkDragContext *context,
               guint32         time)
{
  g_return_if_fail (context != NULL);

  if (context->dest_window)
    {
      switch (context->protocol)
        {
        case GDK_DRAG_PROTO_MOTIF:
          break;

        case GDK_DRAG_PROTO_XDND:
          xdnd_send_drop (context, time);
          break;

        case GDK_DRAG_PROTO_ROOTWIN:
          g_warning ("Drops for GDK_DRAG_PROTO_ROOTWIN must be handled internally");
          break;
        case GDK_DRAG_PROTO_NONE:
          g_warning ("GDK_DRAG_PROTO_NONE is not valid in _gdk_x11_drag_drop()");
          break;
        }
    }
}

void
_gdk_x11_drag_abort (GdkDragContext *context,
                guint32         time)
{
  g_return_if_fail (context != NULL);

  gdk_drag_do_leave (context, time);
}

/* Destination side */

void
_gdk_x11_drag_status (GdkDragContext   *context,
                 GdkDragAction     action,
                 guint32           time)
{
  XEvent xev;

  g_return_if_fail (context != NULL);

  context->action = action;

  if (context->protocol == GDK_DRAG_PROTO_XDND)
    {
      xev.xclient.type = ClientMessage;
      xev.xclient.message_type = _gdk_x11_atom_intern ("XdndStatus", FALSE);
      xev.xclient.format = 32;
      xev.xclient.window = GDK_WINDOW_XWINDOW (context->source_window);

      xev.xclient.data.l[0] = GDK_WINDOW_XWINDOW (context->dest_window);
      xev.xclient.data.l[1] = (action != 0) ? (2 | 1) : 0;
      xev.xclient.data.l[2] = 0;
      xev.xclient.data.l[3] = 0;
      xev.xclient.data.l[4] = xdnd_action_to_atom (action);

      if (!xdnd_send_xevent (GDK_WINDOW_XWINDOW (context->source_window),
                             FALSE, &xev))
        GDK_NOTE (DND,
                  g_message ("Send event to %lx failed",
                             GDK_WINDOW_XWINDOW (context->source_window)));
    }
}

void
_gdk_x11_drop_reply (GdkDragContext   *context,
                gboolean          ok,
                guint32           time)
{
  g_return_if_fail (context != NULL);

  /* Xdnd answers a drop when it is finished, and not before: the
   * reply was Motif's. */
}

void
_gdk_x11_drop_finish (GdkDragContext   *context,
                 gboolean          success,
                 guint32           time)
{
  g_return_if_fail (context != NULL);

  if (context->protocol == GDK_DRAG_PROTO_XDND)
    {
      XEvent xev;

      xev.xclient.type = ClientMessage;
      xev.xclient.message_type = _gdk_x11_atom_intern ("XdndFinished", FALSE);
      xev.xclient.format = 32;
      xev.xclient.window = GDK_WINDOW_XWINDOW (context->source_window);

      xev.xclient.data.l[0] = GDK_WINDOW_XWINDOW (context->dest_window);
      xev.xclient.data.l[1] = 0;
      xev.xclient.data.l[2] = 0;
      xev.xclient.data.l[3] = 0;
      xev.xclient.data.l[4] = 0;

      if (!xdnd_send_xevent (GDK_WINDOW_XWINDOW (context->source_window),
                             FALSE, &xev))
        GDK_NOTE (DND,
                  g_message ("Send event to %lx failed",
                             GDK_WINDOW_XWINDOW (context->source_window)));
    }
}


void
_gdk_x11_window_register_dnd (GdkWindow      *window)
{
  static gulong xdnd_version = 3;

  g_return_if_fail (window != NULL);

  /* Set XdndAware */

  if (!xdnd_aware_atom)
    xdnd_aware_atom = _gdk_x11_atom_intern ("XdndAware", FALSE);

  /* The property needs to be of type XA_ATOM, not XA_INTEGER. Blech */
  XChangeProperty (GDK_WINDOW_XDISPLAY (window),
                   GDK_WINDOW_XWINDOW (window),
                   xdnd_aware_atom, XA_ATOM,
                   32, PropModeReplace,
                   (guchar *)&xdnd_version, 1);
}

/*************************************************************
 * _gdk_x11_drag_get_selection:
 *     Returns the selection atom for the current source window
 *   arguments:
 *
 *   results:
 *************************************************************/

GdkAtom
_gdk_x11_drag_get_selection (GdkDragContext *context)
{
  g_return_val_if_fail (context != NULL, GDK_NONE);

  if (context->protocol == GDK_DRAG_PROTO_XDND)
    return ((GdkDragContextPrivate *)context)->xdnd_selection;
  else
    return GDK_NONE;
}
