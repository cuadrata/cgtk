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
 * Events for the windows inside a native window.
 *
 * Only a native window -- a toplevel, or a child made native on
 * purpose -- is a window to the window system, so that is what events
 * arrive on. Everything inside it is GDK's to sort out: which child
 * the pointer is in, which of its ancestors wants the event, what the
 * pointer crossed on its way, who has the grab, and, when the system
 * exposes a rectangle, which windows are under it. The rules are the
 * X server's, since that is what programs were written against.
 */

#include "gdk.h"
#include "gdkprivate.h"
#include "gdkframebuffer.h"
#include "gdkbackend.h"

/* ---------------------------------------------------------------------
 * The event queue and the main loop's source. The backend puts the
 * events it translates into the queue; the program takes them out.
 */

typedef struct _GdkIOClosure GdkIOClosure;

struct _GdkIOClosure
{
  GdkInputFunction function;
  GdkInputCondition condition;
  GdkDestroyNotify notify;
  gpointer data;
};

static gboolean  gdk_event_prepare      (gpointer   source_data,
                                         GTimeVal  *current_time,
                                         gint      *timeout,
                                         gpointer   user_data);
static gboolean  gdk_event_check        (gpointer   source_data,
                                         GTimeVal  *current_time,
                                         gpointer   user_data);
static gboolean  gdk_event_dispatch     (gpointer   source_data,
                                         GTimeVal  *current_time,
                                         gpointer   user_data);
static GdkEvent* gdk_event_unqueue      (void);
static GdkEvent* _gdk_event_queue_take_expose (GdkWindow *window);
static void      _gdk_event_dispatch    (GdkEvent  *event);
static void      gdk_io_destroy         (gpointer   data);

static GdkEventFunc   event_func = NULL;    /* Callback for events */
static gpointer       event_data = NULL;
static GDestroyNotify event_notify = NULL;

GList *_gdk_client_filters = NULL;          /* Filters for client messages */

/* FIFO's for event queue, and for events put back using
 * gdk_event_put().
 */
static GList *queued_events = NULL;
static GList *queued_tail = NULL;

static GSourceFuncs event_funcs = {
  gdk_event_prepare,
  gdk_event_check,
  gdk_event_dispatch,
  (GDestroyNotify)g_free
};

static GPollFD event_poll_fd;
static GMemChunk *event_chunk = NULL;

#define READ_CONDITION (G_IO_IN | G_IO_HUP | G_IO_ERR)
#define WRITE_CONDITION (G_IO_OUT | G_IO_ERR)
#define EXCEPTION_CONDITION (G_IO_PRI)

/*
 * What has been drawn goes to the screen when the main loop comes to
 * the end of what is more pressing: no event, no timeout, and none of
 * GTK+'s resizing and redrawing, which are idle functions of a higher
 * priority than this. A window that is resized is cleared, then laid
 * out again, then drawn widget by widget, each in a turn of the main
 * loop of its own; published after every turn, as it once was, it was
 * seen blank and then filling in.
 *
 * A source of the main loop's is asked whether it has anything to do
 * only when nothing more pressing has, so the publishing is done on
 * being asked, and the answer is always no: nothing is left pending
 * for a program that asks whether anything is.
 *
 * A main loop that never comes to that end would show nothing at all
 * this way, so there are two limits, both in milliseconds, which
 * gdk_event_prepare sees to, being asked every time round:
 *
 * PUBLISH_BUSY: events, timeouts and input, all that is as pressing
 * as events, have had every turn of the main loop for this long, and
 * no expose or change of size among them is still to be answered. It
 * is a timeout that takes longer than the time it is given, or the
 * pointer's motion coming in faster than it is handled: what is drawn
 * is then published every time round, as it always was. GTK+'s idle
 * functions get no turn in a loop like that, so there is nothing of
 * theirs to wait for.
 *
 * PUBLISH_LATEST: the main loop has kept something waiting this long,
 * whatever is going on.
 */
#define PUBLISH_PRIORITY (G_PRIORITY_HIGH_IDLE + 30)
#define PUBLISH_BUSY     16
#define PUBLISH_LATEST   50

/* Whether the main loop's last turn was one in which it published, or
 * gave nothing as pressing as events its turn; and since when none
 * has been. */
static gboolean loop_rested = TRUE;
static GTimeVal busy_since;

static gboolean
never_prepare (gpointer  source_data,
               GTimeVal *current_time,
               gint     *timeout,
               gpointer  user_data)
{
  *timeout = -1;

  return FALSE;
}

/* Asked, after the wait, when no event, timeout or input is ready:
 * this turn is for something less pressing, or for nothing. */
static gboolean
rest_check (gpointer  source_data,
            GTimeVal *current_time,
            gpointer  user_data)
{
  loop_rested = TRUE;

  return FALSE;
}

/* Asked when nothing of GTK+'s is ready either. */
static gboolean
publish_prepare (gpointer  source_data,
                 GTimeVal *current_time,
                 gint     *timeout,
                 gpointer  user_data)
{
  GDK_THREADS_ENTER ();

  *timeout = -1;
  loop_rested = TRUE;

  /* Writing to the window system may read in what it had to say, and
   * nothing would wake the main loop for an event read that way: it
   * does not wait before it has come round and asked once more. */
  if (_gdk_windows_flush ())
    *timeout = 0;

  GDK_THREADS_LEAVE ();

  return FALSE;
}

/* Asked again after the wait, of which a frame let go by the window
 * system may have been the end. The main loop comes round at once
 * after this, and asks for events again. */
static gboolean
publish_check (gpointer  source_data,
               GTimeVal *current_time,
               gpointer  user_data)
{
  GDK_THREADS_ENTER ();
  _gdk_windows_flush ();
  GDK_THREADS_LEAVE ();

  return FALSE;
}

static gboolean
never_dispatch (gpointer  source_data,
                GTimeVal *current_time,
                gpointer  user_data)
{
  return TRUE;
}

static GSourceFuncs rest_funcs = {
  never_prepare,
  rest_check,
  never_dispatch,
  NULL
};

static GSourceFuncs publish_funcs = {
  publish_prepare,
  publish_check,
  never_dispatch,
  NULL
};

void
_gdk_events_init (void)
{
  g_source_add (GDK_PRIORITY_EVENTS, TRUE, &event_funcs, NULL, NULL, NULL);
  g_source_add (GDK_PRIORITY_EVENTS + 1, TRUE, &rest_funcs, NULL, NULL, NULL);
  g_source_add (PUBLISH_PRIORITY, TRUE, &publish_funcs, NULL, NULL, NULL);

  event_poll_fd.fd = _gdk_backend->event_fd ();
  event_poll_fd.events = G_IO_IN;
  g_main_add_poll (&event_poll_fd, GDK_PRIORITY_EVENTS);
}


/*
 * The pointer grab. An implicit one is what a button press makes:
 * the events until the last button goes up belong to the window
 * pressed in, with owner_events, as the X server did it.
 */
static struct
{
  GdkWindowPrivate *window;
  gboolean owner_events;
  GdkEventMask mask;
  gboolean implicit;
} grab;

/* For the backends: whether the explicit grab reports events to their own windows. */
gboolean _gdk_pointer_grab_owner_events = FALSE;

void
_gdk_pointer_grab_set (GdkWindowPrivate *window,
                       gboolean          owner_events,
                       GdkEventMask      mask,
                       gboolean          implicit)
{
  if (implicit && grab.window && !grab.implicit)
    return;
  grab.window = window;
  grab.owner_events = owner_events;
  grab.mask = mask;
  grab.implicit = implicit;
  if (!implicit)
    _gdk_pointer_grab_owner_events = owner_events;
}

GdkWindowPrivate *
_gdk_pointer_grab_window (gboolean *implicit)
{
  if (implicit)
    *implicit = grab.implicit;
  return grab.window;
}

/*
 * The events the grab reports to its window, as the server took them:
 * for an explicit grab the mask it was asked for, not the window's own.
 */
GdkEventMask
_gdk_pointer_grab_mask (void)
{
  return grab.mask;
}

/*
 * A window hidden takes any grab on it, or on a window inside it,
 * with it, as the server let go of a grab whose window was no longer
 * viewable. A menu popped down leaves no grab behind this way.
 */
void
_gdk_pointer_grab_window_hidden (GdkWindowPrivate *window)
{
  GdkWindowPrivate *w;

  for (w = grab.window; w; w = (GdkWindowPrivate *) w->parent)
    if (w == window)
      {
        grab.window = NULL;
        gdk_xgrab_window = NULL;
        return;
      }
}

void
_gdk_pointer_grab_clear (gboolean implicit_only)
{
  if (implicit_only && !grab.implicit)
    return;
  grab.window = NULL;
}

/* ---------------------------------------------------------------------
 * Where things are
 */

/* The deepest mapped window containing the point, in native's pixels. */
GdkWindowPrivate *
_gdk_window_child_at (GdkWindowPrivate *native, gint x, gint y)
{
  GdkWindowPrivate *w = native;
  GList *l;

  for (;;)
    {
      GdkWindowPrivate *below = NULL;

      for (l = w->children; l; l = l->next)
        {
          GdkWindowPrivate *c = l->data;

          if (c->mapped && !c->destroyed && !c->native &&
              x >= c->x && y >= c->y &&
              x < c->x + c->width && y < c->y + c->height)
            {
              below = c;
              break;
            }
        }
      if (!below)
        return w;
      x -= below->x;
      y -= below->y;
      w = below;
    }
}

/* The window's origin within its native window, and its native. */
static GdkWindowPrivate *
origin_in_native (GdkWindowPrivate *w, gint *x, gint *y)
{
  gint ox = 0, oy = 0;

  while (!w->native && w->parent)
    {
      ox += w->x;
      oy += w->y;
      w = (GdkWindowPrivate *) w->parent;
    }
  *x = ox;
  *y = oy;
  return w;
}

/*
 * Where the point's event goes: to the deepest window under it that
 * asked for events of that kind, or an ancestor that did. needed is
 * the set of masks any one of which selects the event. NULL if no
 * window wants it.
 *
 * A native window inside another's is no wall: what nobody in it
 * asked for goes on up through the window it sits in, as the server
 * passed an event on to a window's parent.
 */
GdkWindowPrivate *
_gdk_window_event_target (GdkWindowPrivate *native,
                          gint              x,
                          gint              y,
                          GdkEventMask      needed,
                          gint             *tx,
                          gint             *ty)
{
  GdkWindowPrivate *w = _gdk_window_child_at (native, x, y);
  gint ox, oy;

  while (w && !(w->event_mask & needed))
    {
      if (w == native)
        {
          GdkWindowPrivate *parent = (GdkWindowPrivate *) w->parent;

          if (w->window_type != GDK_WINDOW_CHILD || !parent ||
              parent->window_type == GDK_WINDOW_ROOT)
            return NULL;
          /* The point, in the pixels of the native window above. */
          native = origin_in_native (parent, &ox, &oy);
          x += w->x + ox;
          y += w->y + oy;
        }
      w = (GdkWindowPrivate *) w->parent;
    }
  if (!w)
    return NULL;

  origin_in_native (w, &ox, &oy);
  *tx = x - ox;
  *ty = y - oy;
  return w;
}

/* ---------------------------------------------------------------------
 * Crossing events
 */

static gboolean
crossing_wanted (GdkWindowPrivate *w, GdkEventMask mask)
{
  if (!(w->event_mask & mask))
    return grab.window == w && (grab.mask & mask);
  if (!grab.window || grab.owner_events)
    return TRUE;
  return grab.window == w;
}

static void
crossing_event (GdkWindowPrivate *w,
                GdkWindowPrivate *subwindow,
                GdkEventType      type,
                GdkNotifyType     detail,
                GdkCrossingMode   mode,
                gint x_root, gint y_root, gint nx, gint ny,
                guint32 time, guint state,
                GdkWindowPrivate *native)
{
  GdkEvent event;
  gint ox, oy;

  /* A native window's crossing events come from the window system. */
  if (w->native)
    return;
  if (!crossing_wanted (w, type == GDK_ENTER_NOTIFY
                        ? GDK_ENTER_NOTIFY_MASK : GDK_LEAVE_NOTIFY_MASK))
    return;

  origin_in_native (w, &ox, &oy);
  event.crossing.type = type;
  event.crossing.window = (GdkWindow *) w;
  event.crossing.send_event = FALSE;
  event.crossing.subwindow = (GdkWindow *) subwindow;
  event.crossing.time = time;
  event.crossing.x = nx - ox;
  event.crossing.y = ny - oy;
  event.crossing.x_root = x_root;
  event.crossing.y_root = y_root;
  event.crossing.mode = mode;
  event.crossing.detail = detail;
  event.crossing.focus = FALSE;
  event.crossing.state = state;
  gdk_window_ref ((GdkWindow *) w);
  gdk_event_put (&event);
  gdk_window_unref ((GdkWindow *) w);
}

/* The chain from w up to, and including, its native window. */
static GList *
ancestry (GdkWindowPrivate *w)
{
  GList *chain = NULL;

  while (w)
    {
      chain = g_list_prepend (chain, w);
      if (w->native)
        break;
      w = (GdkWindowPrivate *) w->parent;
    }
  return g_list_reverse (chain);   /* w first, native last */
}

static GdkWindowPrivate *
child_towards (GdkWindowPrivate *from, GList *chain)
{
  /* The child of from on the chain, or NULL. */
  GList *l = g_list_find (chain, from);
  return l && l->prev ? l->prev->data : NULL;
}

/*
 * The pointer is at (x, y) in the native window, or has left it. If
 * that puts it in another window than before, the windows on the way
 * get their leave and enter events, with the detail the X server would
 * have given: ANCESTOR and INFERIOR between a window and its child,
 * VIRTUAL for the windows passed through, NONLINEAR otherwise, and
 * coming from or going outside the native window counts as nonlinear.
 */
/* Which window the pointer is in: held, so that if it is destroyed while
 * the pointer is there it is not freed, only marked destroyed, until the
 * pointer is somewhere else. */
static void
set_pointer_window (GdkWindowPrivate *native, GdkWindowPrivate *window)
{
  if (window)
    gdk_window_ref ((GdkWindow *) window);
  if (native->pointer_window)
    gdk_window_unref ((GdkWindow *) native->pointer_window);
  native->pointer_window = window;
}

static void
pointer_change (GdkWindowPrivate *native,
                GdkWindowPrivate *new,
                gint x, gint y, gint x_root, gint y_root,
                guint32 time, guint state,
                GdkCrossingMode mode)
{
  GdkWindowPrivate *old = native->pointer_window;
  GList *old_chain, *new_chain, *l;
  GdkWindowPrivate *common = NULL;

  if (old && old->destroyed)
    old = NULL;
  if (old == new)
    return;
  if (old)
    gdk_window_ref ((GdkWindow *) old);     /* for the leave events */
  set_pointer_window (native, new);

  old_chain = old ? ancestry (old) : NULL;
  new_chain = new ? ancestry (new) : NULL;

  /* The deepest window on both chains. */
  for (l = old_chain; l; l = l->next)
    if (g_list_find (new_chain, l->data))
      {
        common = l->data;
        break;
      }

  if (old && new && common == old)
    {
      /* Down into a child: the old window is an ancestor of the new. */
      crossing_event (old, child_towards (old, new_chain), GDK_LEAVE_NOTIFY,
                      GDK_NOTIFY_INFERIOR, mode, x_root, y_root, x, y, time, state, native);
      for (l = g_list_find (new_chain, old)->prev; l && l->data != new; l = l->prev)
        crossing_event (l->data, child_towards (l->data, new_chain), GDK_ENTER_NOTIFY,
                        GDK_NOTIFY_VIRTUAL, mode, x_root, y_root, x, y, time, state, native);
      crossing_event (new, NULL, GDK_ENTER_NOTIFY,
                      GDK_NOTIFY_ANCESTOR, mode, x_root, y_root, x, y, time, state, native);
    }
  else if (old && new && common == new)
    {
      /* Up out of a child into an ancestor. */
      crossing_event (old, NULL, GDK_LEAVE_NOTIFY,
                      GDK_NOTIFY_ANCESTOR, mode, x_root, y_root, x, y, time, state, native);
      for (l = old_chain->next; l && l->data != new; l = l->next)
        crossing_event (l->data, child_towards (l->data, old_chain), GDK_LEAVE_NOTIFY,
                        GDK_NOTIFY_VIRTUAL, mode, x_root, y_root, x, y, time, state, native);
      crossing_event (new, child_towards (new, old_chain), GDK_ENTER_NOTIFY,
                      GDK_NOTIFY_INFERIOR, mode, x_root, y_root, x, y, time, state, native);
    }
  else
    {
      /* Sideways: up from old to the common ancestor, down to new. */
      if (old)
        {
          crossing_event (old, NULL, GDK_LEAVE_NOTIFY,
                          GDK_NOTIFY_NONLINEAR, mode, x_root, y_root, x, y, time, state, native);
          for (l = old_chain->next; l && l->data != common; l = l->next)
            crossing_event (l->data, child_towards (l->data, old_chain), GDK_LEAVE_NOTIFY,
                            GDK_NOTIFY_NONLINEAR_VIRTUAL, mode, x_root, y_root, x, y, time, state, native);
        }
      if (new)
        {
          GList *start = common ? g_list_find (new_chain, common)->prev
            : g_list_last (new_chain);

          for (l = start; l && l->data != new; l = l->prev)
            crossing_event (l->data, child_towards (l->data, new_chain), GDK_ENTER_NOTIFY,
                            GDK_NOTIFY_NONLINEAR_VIRTUAL, mode, x_root, y_root, x, y, time, state, native);
          crossing_event (new, NULL, GDK_ENTER_NOTIFY,
                          GDK_NOTIFY_NONLINEAR, mode, x_root, y_root, x, y, time, state, native);
        }
    }

  g_list_free (old_chain);
  g_list_free (new_chain);
  if (old)
    gdk_window_unref ((GdkWindow *) old);
  _gdk_backend_cursor_changed ((GdkWindow *) native);
}

void
_gdk_window_pointer_moved (GdkWindowPrivate *native,
                           gint x, gint y, gint x_root, gint y_root,
                           guint32 time, guint state,
                           GdkCrossingMode mode, gboolean leaving)
{
  native->pointer_in = !leaving;
  native->pointer_x = x;
  native->pointer_y = y;
  native->pointer_x_root = x_root;
  native->pointer_y_root = y_root;
  native->pointer_time = time;
  native->pointer_state = state;
  pointer_change (native, leaving ? NULL : _gdk_window_child_at (native, x, y),
                  x, y, x_root, y_root, time, state, mode);
}

/*
 * A window inside a native window was shown or hidden, moved, resized,
 * restacked or destroyed. The pointer may be in another window for it
 * though it has not moved, and the X server sent the crossing events
 * for that as it does for a pointer that moves: a button that comes
 * under a still pointer when the one before it goes is told the
 * pointer is in it. The native window is looked at again when the
 * main loop next asks for events, by which time the program has made
 * its changes, and it is then as if the pointer had moved no distance.
 * A native child's comings and goings are the window system's to tell.
 */
static GSList *stale_pointers = NULL;

void
_gdk_window_pointer_stale (GdkWindowPrivate *private)
{
  GdkWindowPrivate *native = private;

  if (private->native || private->window_type == GDK_WINDOW_PIXMAP)
    return;
  while (!native->native && native->parent)
    native = (GdkWindowPrivate *) native->parent;
  if (!native->native || !native->pointer_in || native->pointer_stale ||
      native->destroyed)
    return;

  native->pointer_stale = TRUE;
  gdk_window_ref ((GdkWindow *) native);
  stale_pointers = g_slist_prepend (stale_pointers, native);
}

static void
pointers_settle (void)
{
  while (stale_pointers)
    {
      GdkWindowPrivate *native = stale_pointers->data;

      stale_pointers = g_slist_remove (stale_pointers, native);
      native->pointer_stale = FALSE;
      if (!native->destroyed && native->mapped && native->pointer_in)
        pointer_change (native,
                        _gdk_window_child_at (native, native->pointer_x,
                                              native->pointer_y),
                        native->pointer_x, native->pointer_y,
                        native->pointer_x_root, native->pointer_y_root,
                        native->pointer_time, native->pointer_state,
                        GDK_CROSSING_NORMAL);
      gdk_window_unref ((GdkWindow *) native);
    }
}

/*
 * A grab began or ended on a window: the pointer is treated as having
 * jumped into the grab window, or back from it to where it really is,
 * and the windows in between hear of it with the grab or ungrab mode,
 * as the X server told them. The jump back starts at the grab window
 * whatever the pointer did meanwhile: a slider dragged with the pointer
 * wandering off it hears it leave at the release, and draws itself
 * plain again, as it did with the server's ungrab crossing.
 */
void
_gdk_window_grab_crossing (GdkWindowPrivate *grab_window,
                           gboolean          grabbed,
                           gint x, gint y, gint x_root, gint y_root,
                           guint32 time, guint state)
{
  GdkWindowPrivate *native = grab_window;

  while (!native->native && native->parent)
    native = (GdkWindowPrivate *) native->parent;
  if (!grabbed)
    set_pointer_window (native, grab_window);
  pointer_change (native, grabbed ? grab_window : _gdk_window_child_at (native, x, y),
                  x, y, x_root, y_root, time, state,
                  grabbed ? GDK_CROSSING_GRAB : GDK_CROSSING_UNGRAB);
}

/*
 * The last button came up: the implicit grab the press made ends, and
 * the pointer jumps back from the window that was pressed to where it
 * is, with ungrab crossings, as at the end of any grab.
 */
void
_gdk_window_implicit_grab_ended (GdkWindowPrivate *native,
                                 gint x, gint y, gint x_root, gint y_root,
                                 guint32 time, guint state)
{
  gboolean implicit;
  GdkWindowPrivate *grab_window = _gdk_pointer_grab_window (&implicit);

  if (grab_window && implicit && !grab_window->destroyed)
    {
      GdkWindowPrivate *w = grab_window;

      while (!w->native && w->parent)
        w = (GdkWindowPrivate *) w->parent;
      if (w == native)
        _gdk_window_grab_crossing (grab_window, FALSE, x, y, x_root, y_root,
                                   time, state);
    }
  _gdk_pointer_grab_clear (TRUE);
}

/* ---------------------------------------------------------------------
 * Exposes
 */

/*
 * An expose for a window on a region of its native's framebuffer, in
 * device pixels: one event, over the region's extents in the window's
 * own pixels, as the server sent one per rectangle it exposed.
 */
void
_gdk_window_expose_region (GdkWindowPrivate *private, GdkRegion *region)
{
  GdkWindowPrivate *native;
  GdkRectangle extents;
  GdkEvent event;
  gint ox, oy, scale;

  if (gdk_region_empty (region) || private->destroyed)
    return;
  native = origin_in_native (private, &ox, &oy);
  scale = native->scale;
  gdk_region_get_clipbox (region, &extents);

  event.expose.type = GDK_EXPOSE;
  event.expose.window = (GdkWindow *) private;
  event.expose.send_event = FALSE;
  event.expose.area.x = extents.x / scale - ox;
  event.expose.area.y = extents.y / scale - oy;
  event.expose.area.width = (extents.width + scale - 1) / scale;
  event.expose.area.height = (extents.height + scale - 1) / scale;
  event.expose.count = 0;
  gdk_window_ref ((GdkWindow *) private);
  gdk_event_put (&event);
  gdk_window_unref ((GdkWindow *) private);
}

/*
 * The window system exposed a rectangle of a native window: the
 * native window and every window inside it that shows there gets an
 * expose for its part. Native children are the system's to expose.
 */
static void
expose_tree (GdkWindowPrivate *private, GdkRegion *area)
{
  GdkRegion *region, *both;
  GList *l;

  if (!private->mapped || private->destroyed || private->depth == 0)
    return;

  region = _gdk_window_visible_region (private, FALSE);
  both = gdk_regions_intersect (region, area);
  gdk_region_destroy (region);
  _gdk_window_expose_region (private, both);
  gdk_region_destroy (both);

  for (l = private->children; l; l = l->next)
    {
      GdkWindowPrivate *child = l->data;

      if (!child->native)
        expose_tree (child, area);
    }
}

void
_gdk_window_expose_split (GdkWindowPrivate *native, const GdkRectangle *area)
{
  GdkRegion *region;
  GdkRectangle dev;
  gint scale = native->scale;

  dev.x = area->x * scale;
  dev.y = area->y * scale;
  dev.width = area->width * scale;
  dev.height = area->height * scale;
  region = _gdk_region_from_rect (&dev);
  expose_tree (native, region);
  gdk_region_destroy (region);
}

/*
 * Exposes for a window that were put in the queue by the generic side
 * are handed to the program now, so that it can scroll again knowing
 * what it drew: GtkLayout wants this.
 */
void
gdk_window_process_exposes (GdkWindow *window)
{
  GdkEvent *event;

  g_return_if_fail (window != NULL);

  while ((event = _gdk_event_queue_take_expose (window)) != NULL)
    {
      _gdk_event_dispatch (event);
      gdk_event_free (event);
    }
}

/* ---------------------------------------------------------------------- */

GList*
_gdk_event_queue_find_first (void)
{
  GList *tmp_list = queued_events;

  while (tmp_list)
    {
      GdkEventPrivate *event = tmp_list->data;
      if (!(event->flags & GDK_EVENT_PENDING))
        return tmp_list;

      tmp_list = g_list_next (tmp_list);
    }

  return NULL;
}

void
_gdk_event_queue_remove_link (GList *node)
{
  if (node->prev)
    node->prev->next = node->next;
  else
    queued_events = node->next;

  if (node->next)
    node->next->prev = node->prev;
  else
    queued_tail = node->prev;

}

/* Returns the event's node, for a backend that may yet take it back. */
GList *
_gdk_event_queue_append (GdkEvent *event)
{
  queued_tail = g_list_append (queued_tail, event);

  if (!queued_events)
    queued_events = queued_tail;
  else
    queued_tail = queued_tail->next;

  return queued_tail;
}

/*
 * Every configure for the window still in the queue is brought up to
 * date with the one about to be queued: each tells the program the
 * size and place the window has, which is no longer what it was when
 * they were queued. They are still delivered, one for each request,
 * which GtkWindow counts on.
 */
void
_gdk_event_queue_update_configures (const GdkEventConfigure *configure)
{
  GList *l;

  for (l = queued_events; l; l = l->next)
    {
      GdkEvent *event = l->data;

      if (event->type == GDK_CONFIGURE && event->configure.window == configure->window)
        {
          event->configure.x = configure->x;
          event->configure.y = configure->y;
          event->configure.width = configure->width;
          event->configure.height = configure->height;
        }
    }
}

/* The first expose for a window in the queue, taken out of it. */
static GdkEvent *
_gdk_event_queue_take_expose (GdkWindow *window)
{
  GList *l;

  for (l = queued_events; l; l = l->next)
    {
      GdkEvent *event = l->data;

      if (event->type == GDK_EXPOSE && event->expose.window == window &&
          !(((GdkEventPrivate *) event)->flags & GDK_EVENT_PENDING))
        {
          _gdk_event_queue_remove_link (l);
          g_list_free_1 (l);
          return event;
        }
    }
  return NULL;
}

static void
_gdk_event_dispatch (GdkEvent *event)
{
  if (event_func)
    (*event_func) (event, event_data);
}

gboolean
gdk_events_pending (void)
{
  gboolean pending;

  /* A program that asks this, and not the main loop, what there is to
   * do has nothing left to do when there are no events: what it has
   * drawn is published, and the question asked again, as publishing
   * may bring an event in. */
  do
    {
      pointers_settle ();
      pending = _gdk_event_queue_find_first() || _gdk_backend->events_pending ();
    }
  while (!pending && _gdk_windows_flush ());

  return pending;
}

/*
 * There are no graphics exposes any more: a copy within a window copies
 * from the window's framebuffer, which has all of it, obscured or not.
 */
GdkEvent*
gdk_event_get_graphics_expose (GdkWindow *window)
{
  g_return_val_if_fail (window != NULL, NULL);

  return NULL;
}

void
gdk_event_handler_set (GdkEventFunc   func,
                       gpointer       data,
                       GDestroyNotify notify)
{
  if (event_notify)
    (*event_notify) (event_data);

  event_func = func;
  event_data = data;
  event_notify = notify;
}

GdkEvent*
gdk_event_get (void)
{
  _gdk_backend->events_queue (FALSE);

  return gdk_event_unqueue ();
}

GdkEvent*
gdk_event_peek (void)
{
  GList *tmp_list;

  tmp_list = _gdk_event_queue_find_first ();

  if (tmp_list)
    return gdk_event_copy (tmp_list->data);
  else
    return NULL;
}

void
gdk_event_put (GdkEvent *event)
{
  GdkEvent *new_event;

  g_return_if_fail (event != NULL);

  new_event = gdk_event_copy (event);

  _gdk_event_queue_append (new_event);
}

GdkEvent*
_gdk_event_new (void)
{
  GdkEventPrivate *new_event;

  if (event_chunk == NULL)
    event_chunk = g_mem_chunk_new ("events",
                                   sizeof (GdkEventPrivate),
                                   4096,
                                   G_ALLOC_AND_FREE);

  new_event = g_chunk_new (GdkEventPrivate, event_chunk);
  new_event->flags = 0;

  return (GdkEvent*) new_event;
}

GdkEvent*
gdk_event_copy (GdkEvent *event)
{
  GdkEvent *new_event;

  g_return_val_if_fail (event != NULL, NULL);

  new_event = _gdk_event_new ();

  *new_event = *event;
  /* A client message for a window GDK knows nothing of has none. */
  if (new_event->any.window)
    gdk_window_ref (new_event->any.window);

  switch (event->any.type)
    {
    case GDK_KEY_PRESS:
    case GDK_KEY_RELEASE:
      new_event->key.string = g_strdup (event->key.string);
      break;

    case GDK_ENTER_NOTIFY:
    case GDK_LEAVE_NOTIFY:
      if (event->crossing.subwindow != NULL)
        gdk_window_ref (event->crossing.subwindow);
      break;

    case GDK_DRAG_ENTER:
    case GDK_DRAG_LEAVE:
    case GDK_DRAG_MOTION:
    case GDK_DRAG_STATUS:
    case GDK_DROP_START:
    case GDK_DROP_FINISHED:
      gdk_drag_context_ref (event->dnd.context);
      break;

    default:
      break;
    }

  return new_event;
}

void
gdk_event_free (GdkEvent *event)
{
  g_return_if_fail (event != NULL);

  g_assert (event_chunk != NULL); /* paranoid */

  if (event->any.window)
    gdk_window_unref (event->any.window);

  switch (event->any.type)
    {
    case GDK_KEY_PRESS:
    case GDK_KEY_RELEASE:
      g_free (event->key.string);
      break;

    case GDK_ENTER_NOTIFY:
    case GDK_LEAVE_NOTIFY:
      if (event->crossing.subwindow != NULL)
        gdk_window_unref (event->crossing.subwindow);
      break;

    case GDK_DRAG_ENTER:
    case GDK_DRAG_LEAVE:
    case GDK_DRAG_MOTION:
    case GDK_DRAG_STATUS:
    case GDK_DROP_START:
    case GDK_DROP_FINISHED:
      gdk_drag_context_unref (event->dnd.context);
      break;

    default:
      break;
    }

  g_mem_chunk_free (event_chunk, event);
}

guint32
gdk_event_get_time (GdkEvent *event)
{
  if (event)
    switch (event->type)
      {
      case GDK_MOTION_NOTIFY:
        return event->motion.time;
      case GDK_BUTTON_PRESS:
      case GDK_2BUTTON_PRESS:
      case GDK_3BUTTON_PRESS:
      case GDK_BUTTON_RELEASE:
        return event->button.time;
      case GDK_KEY_PRESS:
      case GDK_KEY_RELEASE:
        return event->key.time;
      case GDK_ENTER_NOTIFY:
      case GDK_LEAVE_NOTIFY:
        return event->crossing.time;
      case GDK_PROPERTY_NOTIFY:
        return event->property.time;
      case GDK_SELECTION_CLEAR:
      case GDK_SELECTION_REQUEST:
      case GDK_SELECTION_NOTIFY:
        return event->selection.time;
      case GDK_PROXIMITY_IN:
      case GDK_PROXIMITY_OUT:
        return event->proximity.time;
      case GDK_DRAG_ENTER:
      case GDK_DRAG_LEAVE:
      case GDK_DRAG_MOTION:
      case GDK_DRAG_STATUS:
      case GDK_DROP_START:
      case GDK_DROP_FINISHED:
        return event->dnd.time;
      default:                  /* use current time */
        break;
      }

  return GDK_CURRENT_TIME;
}

void
gdk_set_show_events (gboolean show_events)
{
  if (show_events)
    gdk_debug_flags |= GDK_DEBUG_EVENTS;
  else
    gdk_debug_flags &= ~GDK_DEBUG_EVENTS;
}

gboolean
gdk_get_show_events (void)
{
  return (gdk_debug_flags & GDK_DEBUG_EVENTS) != 0;
}

static void
gdk_io_destroy (gpointer data)
{
  GdkIOClosure *closure = data;

  if (closure->notify)
    closure->notify (closure->data);

  g_free (closure);
}

static gboolean
gdk_io_invoke (GIOChannel   *source,
               GIOCondition  condition,
               gpointer      data)
{
  GdkIOClosure *closure = data;
  GdkInputCondition gdk_cond = 0;

  if (condition & READ_CONDITION)
    gdk_cond |= GDK_INPUT_READ;
  if (condition & WRITE_CONDITION)
    gdk_cond |= GDK_INPUT_WRITE;
  if (condition & EXCEPTION_CONDITION)
    gdk_cond |= GDK_INPUT_EXCEPTION;

  if (closure->condition & gdk_cond)
    closure->function (closure->data, g_io_channel_unix_get_fd (source), gdk_cond);

  return TRUE;
}

gint
gdk_input_add_full (gint              source,
                    GdkInputCondition condition,
                    GdkInputFunction  function,
                    gpointer          data,
                    GdkDestroyNotify  destroy)
{
  guint result;
  GdkIOClosure *closure = g_new (GdkIOClosure, 1);
  GIOChannel *channel;
  GIOCondition cond = 0;

  closure->function = function;
  closure->condition = condition;
  closure->notify = destroy;
  closure->data = data;

  if (condition & GDK_INPUT_READ)
    cond |= READ_CONDITION;
  if (condition & GDK_INPUT_WRITE)
    cond |= WRITE_CONDITION;
  if (condition & GDK_INPUT_EXCEPTION)
    cond |= EXCEPTION_CONDITION;

  channel = g_io_channel_unix_new (source);
  result = g_io_add_watch_full (channel, G_PRIORITY_DEFAULT, cond,
                                gdk_io_invoke,
                                closure, gdk_io_destroy);
  g_io_channel_unref (channel);

  return result;
}

gint
gdk_input_add (gint              source,
               GdkInputCondition condition,
               GdkInputFunction  function,
               gpointer          data)
{
  return gdk_input_add_full (source, condition, function, data, NULL);
}

void
gdk_input_remove (gint tag)
{
  g_source_remove (tag);
}

void
gdk_add_client_message_filter (GdkAtom       message_type,
                               GdkFilterFunc func,
                               gpointer      data)
{
  GdkClientFilter *filter = g_new (GdkClientFilter, 1);

  filter->type = message_type;
  filter->function = func;
  filter->data = data;

  _gdk_client_filters = g_list_prepend (_gdk_client_filters, filter);
}

/*
 * Whether something that is to change the picture is among the events
 * waiting: an expose, for which a background has been painted that the
 * program has yet to draw on, or a change of size it has yet to lay
 * itself out for. Everything the window system has sent is looked at.
 */
static gboolean
picture_awaited (void)
{
  GList *l;

  _gdk_backend->events_queue (TRUE);
  for (l = queued_events; l; l = l->next)
    {
      GdkEvent *event = l->data;

      if (event->type == GDK_EXPOSE || event->type == GDK_CONFIGURE ||
          event->type == GDK_MAP || event->type == GDK_UNMAP)
        return TRUE;
    }
  return FALSE;
}

static gboolean
gdk_event_prepare (gpointer  source_data,
                   GTimeVal *current_time,
                   gint     *timeout,
                   gpointer  user_data)
{
  gboolean retval;
  glong waited, busy;

  GDK_THREADS_ENTER ();

  *timeout = -1;

  /* Where the main loop does not come to the end of what it has to
   * do, what has been drawn still goes to the screen: see
   * PUBLISH_BUSY and PUBLISH_LATEST. */
  waited = _gdk_windows_waiting (current_time);
  if (loop_rested || (waited >= 0 && picture_awaited ()))
    busy_since = *current_time;
  loop_rested = FALSE;
  busy = (current_time->tv_sec - busy_since.tv_sec) * 1000 +
    (current_time->tv_usec - busy_since.tv_usec) / 1000;
  if (waited >= PUBLISH_LATEST || (waited >= 0 && busy >= PUBLISH_BUSY))
    _gdk_windows_flush ();

  /* The pointer is found again among windows that have changed. */
  pointers_settle ();

  retval = (_gdk_event_queue_find_first () != NULL) || _gdk_backend->events_pending ();

  GDK_THREADS_LEAVE ();

  return retval;
}

static gboolean
gdk_event_check (gpointer  source_data,
                 GTimeVal *current_time,
                 gpointer  user_data)
{
  gboolean retval;

  GDK_THREADS_ENTER ();

  if (event_poll_fd.revents & G_IO_IN)
    retval = (_gdk_event_queue_find_first () != NULL) || _gdk_backend->events_pending ();
  else
    retval = FALSE;

  GDK_THREADS_LEAVE ();

  return retval;
}

static GdkEvent*
gdk_event_unqueue (void)
{
  GdkEvent *event = NULL;
  GList *tmp_list;

  tmp_list = _gdk_event_queue_find_first ();

  if (tmp_list)
    {
      event = tmp_list->data;
      _gdk_event_queue_remove_link (tmp_list);
      g_list_free_1 (tmp_list);
    }

  return event;
}

static gboolean
gdk_event_dispatch (gpointer  source_data,
                    GTimeVal *current_time,
                    gpointer  user_data)
{
  GdkEvent *event;

  GDK_THREADS_ENTER ();

  _gdk_backend->events_queue (FALSE);
  event = gdk_event_unqueue();

  if (event)
    {
      if (event_func)
        (*event_func) (event, event_data);

      gdk_event_free (event);
    }

  GDK_THREADS_LEAVE ();

  return TRUE;
}

void
gdk_flush (void)
{
  _gdk_windows_flush ();
  _gdk_backend->flush ();
}
