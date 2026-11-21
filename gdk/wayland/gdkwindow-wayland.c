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
 * Windows on Wayland: surfaces, their roles, the shm buffers a
 * framebuffer is published through, and cursors. See
 * gdkprivate-wayland.h for how the kinds of window map onto roles.
 */

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <time.h>
#include "gdk.h"
#include "gdkprivate-wayland.h"
#include "gdkbackend-wayland.h"
#include "gdkwayland.h"
#include "gdkframebuffer.h"

static GList *native_windows = NULL;   /* every native window with a surface */

static void window_scale_from_outputs (GdkWindowPrivate *private);
static void toplevel_send_parent (GdkWindowPrivate *private);
static void popup_parent_geometry (GdkWindowPrivate *parent, struct xdg_surface **xdg,
                                   gint *x, gint *y, gint *width, gint *height);
static void popup_parent_surface (GdkWindowPrivate *parent, struct xdg_surface **xdg,
                                  gint *x, gint *y);
static void expose_all (GdkWindowPrivate *private);
static void _gdk_wayland_window_configured (GdkWindowPrivate *private,
                                            gint width, gint height);
static void window_make_surface (GdkWindowPrivate *private);

/* ---------------------------------------------------------------------
 * Shared memory buffers
 */

static void
buffer_release (void *data, struct wl_buffer *wl_buffer)
{
  GdkWaylandBuffer *buffer = data;

  /* A commit may have waited for a free buffer: it is seen to when
   * everything that came with this has been dispatched. So may a
   * title bar, whose window has nothing else to commit. */
  buffer->busy = FALSE;
  if (buffer->window && !buffer->window->destroyed && _gdk_wayland_decor_waits_for (buffer->window, buffer))
    buffer->window->impl->commit_pending = TRUE;
}

static const struct wl_buffer_listener buffer_listener = {
  buffer_release
};

static int
anonymous_file (gsize size)
{
  static const char template[] = "/gdk-XXXXXX";
  const char *dir = getenv ("XDG_RUNTIME_DIR");
  char *path;
  int fd;

  if (!dir)
    dir = "/tmp";
  path = g_strconcat (dir, template, NULL);
  fd = mkstemp (path);
  if (fd >= 0)
    {
      unlink (path);
      if (ftruncate (fd, size) < 0)
        {
          close (fd);
          fd = -1;
        }
    }
  g_free (path);
  return fd;
}

gboolean
_gdk_wayland_shm_buffer_new (GdkWaylandBuffer *buffer,
                             gint width, gint height, gboolean alpha)
{
  struct wl_shm_pool *pool;
  gint stride = width * 4;
  gsize size = (gsize) stride * height;
  int fd;

  memset (buffer, 0, sizeof *buffer);
  fd = anonymous_file (size);
  if (fd < 0)
    return FALSE;
  buffer->data = mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (buffer->data == MAP_FAILED)
    {
      close (fd);
      buffer->data = NULL;
      return FALSE;
    }
  pool = wl_shm_create_pool (_gdk_wl_shm, fd, size);
  buffer->buffer = wl_shm_pool_create_buffer (pool, 0, width, height, stride,
                                              alpha ? WL_SHM_FORMAT_ARGB8888
                                              : WL_SHM_FORMAT_XRGB8888);
  wl_shm_pool_destroy (pool);
  close (fd);
  wl_buffer_add_listener (buffer->buffer, &buffer_listener, buffer);
  buffer->width = width;
  buffer->height = height;
  buffer->size = size;
  return TRUE;
}

void
_gdk_wayland_shm_buffer_free (GdkWaylandBuffer *buffer)
{
  if (buffer->buffer)
    wl_buffer_destroy (buffer->buffer);
  if (buffer->data)
    munmap (buffer->data, buffer->size);
  memset (buffer, 0, sizeof *buffer);
}

/*
 * A window's icon, for a compositor that takes one from the program:
 * the xdg-toplevel-icon protocol. An icon is a name, which the
 * compositor looks for among its own icons, or pictures, or both; a
 * compositor that cannot find the name uses the pictures. Once it has
 * been given to a toplevel it cannot be changed, but it can be given
 * to others, and its pictures are to stay for as long as it does.
 *
 * A compositor that does not know the protocol, which most do not,
 * finds an icon itself by the desktop file of the window's app id; so
 * does one that knows it for a window given none. Nothing is made and
 * no file is read then.
 */
struct _GdkWaylandIcon
{
#ifdef HAVE_XDG_TOPLEVEL_ICON
  struct xdg_toplevel_icon_v1 *icon;
#endif
  GdkWaylandBuffer **buffers;
  gint n_buffers;
};

#ifdef HAVE_XDG_TOPLEVEL_ICON
/*
 * An icon of that name, if it is given one, and of those pictures,
 * each of which is to be square: a compositor may not be given one
 * that is not. They are drawn into buffers the compositor reads, of
 * premultiplied pixels, which is how it takes them, and no more of
 * them than WAYLAND_ICON_MOST pixels together, which is as much as an
 * X server takes in a request.
 */
#define WAYLAND_ICON_MOST (4 * 1024 * 1024)

static GdkWaylandIcon*
wayland_icon_new (const gchar        *name,
                  const GdkIconImage *images,
                  gint                n_images,
                  gboolean            straight)
{
  GdkWaylandIcon *icon = g_new0 (GdkWaylandIcon, 1);
  gint i, total = 0;

  icon->icon = xdg_toplevel_icon_manager_v1_create_icon (_gdk_xdg_toplevel_icon_manager);
  if (name)
    xdg_toplevel_icon_v1_set_name (icon->icon, name);

  icon->buffers = g_new0 (GdkWaylandBuffer*, MAX (n_images, 1));
  for (i = 0; i < n_images; i++)
    {
      const guchar *p = images[i].pixels;
      gint size = images[i].width, count;
      GdkWaylandBuffer *buffer;
      guint32 *d;

      if (!p || size <= 0 || size != images[i].height)
        continue;
      if (size > 2048 || total + size * size > WAYLAND_ICON_MOST)
        break;
      total += size * size;
      buffer = g_new0 (GdkWaylandBuffer, 1);
      if (!_gdk_wayland_shm_buffer_new (buffer, size, size, TRUE))
        {
          g_free (buffer);
          continue;
        }
      for (d = buffer->data, count = size * size; count > 0; count--, p += 4)
        {
          guint a = p[3], r = p[0], g = p[1], b = p[2];

          if (straight)
            {
              r = (r * a + 127) / 255;
              g = (g * a + 127) / 255;
              b = (b * a + 127) / 255;
            }
          *d++ = a << 24 | r << 16 | g << 8 | b;
        }
      xdg_toplevel_icon_v1_add_buffer (icon->icon, buffer->buffer, 1);
      icon->buffers[icon->n_buffers++] = buffer;
    }

  return icon;
}

static void
wayland_icon_free (GdkWaylandIcon *icon)
{
  gint i;

  xdg_toplevel_icon_v1_destroy (icon->icon);
  for (i = 0; i < icon->n_buffers; i++)
    {
      _gdk_wayland_shm_buffer_free (icon->buffers[i]);
      g_free (icon->buffers[i]);
    }
  g_free (icon->buffers);
  g_free (icon);
}

/*
 * The window's icon is given to its toplevel, if it has one yet; it
 * is the window's when the toplevel's surface is next committed. A
 * window that is one of a group has no toplevel of its own, and its
 * icon waits until it has.
 */
static void
window_apply_icon (GdkWindowPrivate *private)
{
  GdkWindowImpl *impl = private->impl;

  if (!impl->xdg_toplevel || !_gdk_xdg_toplevel_icon_manager)
    return;
  xdg_toplevel_icon_manager_v1_set_icon (_gdk_xdg_toplevel_icon_manager, impl->xdg_toplevel,
                                         impl->icon ? impl->icon->icon : NULL);
  impl->commit_pending = TRUE;
}

/* The window has the icon it is given, or none, and one that was its
 * alone goes.
 */
static void
window_set_icon (GdkWindowPrivate *private,
                 GdkWaylandIcon   *icon,
                 gboolean          own)
{
  GdkWindowImpl *impl = private->impl;
  GdkWaylandIcon *old = impl->icon_own ? impl->icon : NULL;

  if (impl->icon == icon)
    return;
  impl->icon = icon;
  impl->icon_own = own;
  window_apply_icon (private);
  if (old)
    wayland_icon_free (old);
}
#endif /* HAVE_XDG_TOPLEVEL_ICON */

/* ---------------------------------------------------------------------
 * Window groups. The leader is an ordinary toplevel; the other
 * members are subsurfaces of its surface, at their positions relative
 * to it, and the toplevel's geometry is the box around them all, so a
 * compositor's frame goes around the cluster. Each member's surface
 * takes the pointer within it; the keyboard, which the compositor
 * gives the leader's surface for all of them, goes to the member last
 * under the pointer, the group's active member.
 */

static GList *groups = NULL;

static void _gdk_wayland_group_layout (GdkWaylandGroup *group);

static GdkWaylandGroup *
group_find (GdkWindowPrivate *leader)
{
  GList *l;

  for (l = groups; l; l = l->next)
    if (((GdkWaylandGroup *) l->data)->leader == leader)
      return l->data;
  return NULL;
}

static void
group_free (GdkWaylandGroup *group)
{
  groups = g_list_remove (groups, group);
  if (group->sync)
    wl_callback_destroy (group->sync);
  g_list_free (group->members);
  g_free (group);
}

/* The topmost member of the group under a point of the group's space. */
GdkWindowPrivate *
_gdk_wayland_group_member_at (GdkWaylandGroup *group, gint x, gint y)
{
  GList *l;

  for (l = group->members; l; l = l->next)
    {
      GdkWindowPrivate *m = l->data;

      if (!m->destroyed && m->mapped && m->impl->surface &&
          (m == group->leader || m->impl->subsurface) &&
          x >= m->x && y >= m->y && x < m->x + m->width && y < m->y + m->height)
        return m;
    }
  return NULL;
}

static void group_member_map (GdkWindowPrivate *private);

/* The member the keyboard goes to: the active one while it is shown, else the leader. */
static GdkWindowPrivate *
group_active (GdkWaylandGroup *group)
{
  GdkWindowPrivate *a = group->active;

  if (a && !a->destroyed && a->mapped && a->impl && a->impl->group == group &&
      (a == group->leader || a->impl->subsurface))
    return a;
  return group->leader;
}

/*
 * The native window the keyboard's events are for, given the one the
 * compositor sent them to: a group's leader stands for the member
 * that is active.
 */
GdkWindowPrivate *
_gdk_wayland_keyboard_target (GdkWindowPrivate *native)
{
  if (native && !native->destroyed && native->impl && native->impl->group &&
      native->impl->group->leader == native)
    return group_active (native->impl->group);
  return native;
}

void
_gdk_wayland_focus_event (GdkWindowPrivate *native, gboolean in)
{
  GdkEvent event;

  if (!native || native->destroyed || !(native->event_mask & GDK_FOCUS_CHANGE_MASK))
    return;
  event.focus_change.type = GDK_FOCUS_CHANGE;
  event.focus_change.window = (GdkWindow *) native;
  event.focus_change.send_event = FALSE;
  event.focus_change.in = in;
  _gdk_wayland_put_event (&event);
}

/*
 * The keyboard moves to another member of the group: as the program
 * sees it, the focus leaves the one it was in and enters the other,
 * if the group has the keyboard at all.
 */
static void
group_active_changed (GdkWaylandGroup *group, GdkWindowPrivate *was, GdkWindowPrivate *now)
{
  if (was == now || _gdk_wayland_keyboard_window != group->leader)
    return;
  _gdk_wayland_focus_event (was, FALSE);
  _gdk_wayland_focus_event (now, TRUE);
}

/* A member the pointer has entered becomes the one the keyboard goes to. */
void
_gdk_wayland_group_set_active (GdkWindowPrivate *member)
{
  GdkWaylandGroup *group;
  GdkWindowPrivate *was;

  if (!member || member->destroyed || !member->impl || !member->impl->group)
    return;
  group = member->impl->group;
  was = group_active (group);
  group->active = member;
  group_active_changed (group, was, group_active (group));
}

/* The active member, hidden or gone, leaves the keyboard to the leader. */
static void
group_check_active (GdkWaylandGroup *group)
{
  GdkWindowPrivate *a = group->active;

  if (!a || group_active (group) == a)
    return;
  group->active = NULL;
  group_active_changed (group, a, group->leader);
}

static void
group_sync_done (void *data, struct wl_callback *callback, uint32_t serial)
{
  GdkWaylandGroup *group = data;

  if (group->sync == callback)
    group->sync = NULL;
  wl_callback_destroy (callback);
}

static const struct wl_callback_listener group_sync_listener = {
  group_sync_done
};

/*
 * The cluster's box and the leader's input region, and where each
 * member's subsurface sits. Called whenever a member is shown,
 * hidden, moved or resized.
 */
static void
_gdk_wayland_group_layout (GdkWaylandGroup *group)
{
  GdkWindowPrivate *leader = group->leader;
  GList *l;
  gint x1, y1, x2, y2;
  gint min_w, min_h, max_w, max_h;
  gboolean resized;

  if (!leader || leader->destroyed || !leader->mapped ||
      !leader->impl->surface || !leader->impl->xdg_surface)
    return;

  x1 = leader->x;
  y1 = leader->y;
  x2 = leader->x + leader->width;
  y2 = leader->y + leader->height;

  for (l = group->members; l; l = l->next)
    {
      GdkWindowPrivate *m = l->data;
      gint hx, hy;

      if (m == leader || m->destroyed || !m->mapped)
        continue;
      if (!m->impl->subsurface)
        group_member_map (m);
      if (!m->impl->subsurface)
        continue;
      hx = m->x - leader->x;
      hy = m->y - leader->y;
      /* The member moves under a pointer that stays where it is. */
      if (m == _gdk_wayland_pointer_window)
        {
          m->impl->pointer_x -= hx - m->impl->host_x;
          m->impl->pointer_y -= hy - m->impl->host_y;
        }
      m->impl->host_x = hx;
      m->impl->host_y = hy;
      wl_subsurface_set_position (m->impl->subsurface, GDK_WL_TO (hx), GDK_WL_TO (hy));
      x1 = MIN (x1, m->x);
      y1 = MIN (y1, m->y);
      x2 = MAX (x2, m->x + m->width);
      y2 = MAX (y2, m->y + m->height);
    }
  resized = x2 - x1 != group->width || y2 - y1 != group->height;
  group->x = x1;
  group->y = y1;
  group->width = x2 - x1;
  group->height = y2 - y1;
  group_check_active (group);

  wl_surface_set_input_region (leader->impl->surface, leader->impl->shape);
  xdg_surface_set_window_geometry (leader->impl->xdg_surface,
                                   GDK_WL_TO (x1 - leader->x), GDK_WL_TO (y1 - leader->y),
                                   GDK_WL_TO (group->width), GDK_WL_TO (group->height));
  /*
   * Fixed at the cluster's size, so that compositors keep floating it:
   * the program's largest size for the leader alone would have the
   * compositor shrink the cluster to it. Where a member takes what the
   * compositor adds to the cluster or takes from it on an axis, the
   * cluster ranges on that axis as far as the member may.
   */
  min_w = max_w = group->width;
  min_h = max_h = group->height;
  for (l = group->members; l; l = l->next)
    {
      GdkWindowPrivate *m = l->data;

      if (m->destroyed || !m->mapped || (m != leader && !m->impl->subsurface))
        continue;
      if (m->impl->group_resize_h)
        {
          min_w = MAX (group->width - (m->width - MAX (m->impl->min_width, 1)), 1);
          max_w = m->impl->max_width ? group->width + (m->impl->max_width - m->width) : 0;
        }
      if (m->impl->group_resize_v)
        {
          min_h = MAX (group->height - (m->height - MAX (m->impl->min_height, 1)), 1);
          max_h = m->impl->max_height ? group->height + (m->impl->max_height - m->height) : 0;
        }
    }
  if (leader->impl->xdg_toplevel)
    {
      xdg_toplevel_set_min_size (leader->impl->xdg_toplevel,
                                 GDK_WL_TO (min_w), GDK_WL_TO (min_h));
      xdg_toplevel_set_max_size (leader->impl->xdg_toplevel,
                                 GDK_WL_TO (max_w), GDK_WL_TO (max_h));
    }
  wl_surface_commit (leader->impl->surface);
  /*
   * Until the compositor has seen the new size, a configure may still
   * carry the old one: the sync is answered once it has.
   */
  if (resized)
    {
      if (group->sync)
        wl_callback_destroy (group->sync);
      group->sync = wl_display_sync (_gdk_wl_display);
      wl_callback_add_listener (group->sync, &group_sync_listener, group);
      group->box_changed = TRUE;
    }
}

/*
 * A member has committed a new picture. The compositor clips the
 * leader's window geometry to what the surfaces cover, and reads it
 * at the leader's commits: with the box set before the member had
 * grown into it, the compositor took the old size, and keeps it until
 * the leader commits again. So it does, once the members have
 * committed.
 */
static void
group_member_committed (GdkWindowPrivate *member)
{
  GdkWaylandGroup *group = member->impl->group;
  GdkWindowPrivate *leader = group->leader;
  GList *l;

  if (!group->box_changed || !leader || leader->destroyed || !leader->mapped ||
      !leader->impl->surface)
    return;
  for (l = group->members; l; l = l->next)
    {
      GdkWindowPrivate *m = l->data;

      if (m != member && m != leader && !m->destroyed && m->mapped &&
          m->impl->subsurface && m->impl->commit_pending)
        return;
    }
  group->box_changed = FALSE;
  wl_surface_commit (leader->impl->surface);
}

/*
 * The compositor has sized the group's window: what it added or took
 * goes to the members that take it, which are resized as a window
 * manager resizes an X window -- the program hears of the new size
 * and may size the window again as it sees fit.
 *
 * A configure sent before the compositor saw the program change the
 * cluster's size still carries the size it had: an echo, not the
 * compositor's wish, and ignored -- unless the user is resizing the
 * window interactively, when its size is the compositor's to give.
 */
static void
group_configured (GdkWaylandGroup *group, gint width, gint height, gboolean resizing)
{
  GList *l;
  gint dw, dh;
  gboolean changed = FALSE;

  if (width <= 0 || height <= 0 || group->width <= 0 || group->height <= 0)
    return;
  dw = width - group->width;
  dh = height - group->height;
  if (!dw && !dh)
    return;
  if (group->sync && !resizing)
    return;
  for (l = group->members; l; l = l->next)
    {
      GdkWindowPrivate *m = l->data;
      gint w, h;
      GdkEvent event;

      if (m->destroyed || !m->mapped || !m->impl->surface ||
          (m != group->leader && !m->impl->subsurface))
        continue;
      w = m->width;
      h = m->height;
      if (m->impl->group_resize_h && dw)
        w = MAX (m->width + dw, MAX (m->impl->min_width, 1));
      if (m->impl->group_resize_v && dh)
        h = MAX (m->height + dh, MAX (m->impl->min_height, 1));
      if (w == m->width && h == m->height)
        continue;
      m->width = w;
      m->height = h;
      _gdk_window_resized ((GdkWindow *) m);
      expose_all (m);
      m->impl->commit_pending = TRUE;
      event.configure.type = GDK_CONFIGURE;
      event.configure.window = (GdkWindow *) m;
      event.configure.send_event = FALSE;
      event.configure.x = m->x;
      event.configure.y = m->y;
      event.configure.width = w;
      event.configure.height = h;
      _gdk_wayland_put_event (&event);
      changed = TRUE;
    }
  if (changed)
    _gdk_wayland_group_layout (group);
}

/* A member, other than the leader, given its subsurface of the leader. */
static void
group_member_map (GdkWindowPrivate *private)
{
  GdkWindowImpl *impl = private->impl;
  GdkWaylandGroup *group = impl->group;
  GdkWindowPrivate *leader = group->leader;

  if (impl->subsurface || !leader || leader->destroyed || !leader->mapped ||
      !leader->impl->surface)
    return;
  impl->subsurface = wl_subcompositor_get_subsurface (_gdk_wl_subcompositor,
                                                      impl->surface, leader->impl->surface);
  wl_subsurface_set_desync (impl->subsurface);
  /*
   * A member takes its own input: a surface accepts input only within
   * its buffer, so the leader's cannot take it for the others.
   */
  wl_surface_set_input_region (impl->surface, impl->shape);
  group->members = g_list_remove (group->members, private);
  group->members = g_list_prepend (group->members, private);   /* newest on top */
  impl->configured = TRUE;
  impl->commit_pending = TRUE;
  expose_all (private);
}

/* ---------------------------------------------------------------------
 * Surfaces and roles
 */

GdkWindowPrivate *
_gdk_wayland_window_from_surface (struct wl_surface *surface)
{
  return surface ? wl_surface_get_user_data (surface) : NULL;
}

/* The whole window, exposed: nobody else will ask the program to draw it. */
static void
expose_all (GdkWindowPrivate *private)
{
  GdkRectangle whole;

  whole.x = whole.y = 0;
  whole.width = private->width;
  whole.height = private->height;
  _gdk_window_expose_split (private, &whole);
}

static void
xdg_surface_configure (void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
  GdkWindowPrivate *private = data;

  xdg_surface_ack_configure (xdg_surface, serial);
  if (!private->impl->configured)
    expose_all (private);
  private->impl->configured = TRUE;
  private->impl->commit_pending = TRUE;
  if (private->impl->group && private->impl->group->leader == private)
    _gdk_wayland_group_layout (private->impl->group);
}

static const struct xdg_surface_listener xdg_surface_listener = {
  xdg_surface_configure
};

static void
xdg_toplevel_configure (void *data, struct xdg_toplevel *toplevel,
                        int32_t width, int32_t height, struct wl_array *states)
{
  GdkWindowPrivate *private = data;
  GdkWindowImpl *impl = private->impl;
  gboolean fixed = FALSE, placed = FALSE;
  gboolean activated = FALSE, maximized = FALSE, fullscreen = FALSE;
  uint32_t *state;

  /*
   * The size is the window's to keep only while it is maximized, full
   * screen, tiled or being resized; otherwise it is where the window
   * is now, and the program may ask for another, as a floating window
   * does when what is in it grows.
   */
  wl_array_for_each (state, states)
    {
      switch (*state)
        {
        case XDG_TOPLEVEL_STATE_ACTIVATED:
          activated = TRUE;
          break;
        case XDG_TOPLEVEL_STATE_MAXIMIZED:
          maximized = placed = fixed = TRUE;
          break;
        case XDG_TOPLEVEL_STATE_FULLSCREEN:
          fullscreen = placed = fixed = TRUE;
          break;
        case XDG_TOPLEVEL_STATE_RESIZING:
          fixed = TRUE;
          break;
        case XDG_TOPLEVEL_STATE_TILED_LEFT:
        case XDG_TOPLEVEL_STATE_TILED_RIGHT:
        case XDG_TOPLEVEL_STATE_TILED_TOP:
        case XDG_TOPLEVEL_STATE_TILED_BOTTOM:
          placed = fixed = TRUE;
          break;
        default:
          break;
        }
    }
  /*
   * Maximized, full screen or tiled, the window remembers the size it
   * had; let go with no size, it has that again, as the compositor
   * leaves it to. A resize ends at the size it was resized to.
   */
  if (placed && !impl->cfg_placed)
    {
      impl->saved_width = private->width;
      impl->saved_height = private->height;
    }
  else if (!placed && impl->cfg_placed && (width == 0 || height == 0) &&
           impl->saved_width > 0 && impl->saved_height > 0)
    {
      width = GDK_WL_TO (impl->saved_width);
      height = -1;      /* the saved height is the contents': no bar to take off */
    }
  impl->cfg_placed = placed;
  private->impl->cfg_fixed = fixed;
  if (impl->activated != activated || impl->maximized != maximized ||
      impl->fullscreen != fullscreen)
    {
      impl->activated = activated;
      impl->maximized = maximized;
      impl->fullscreen = fullscreen;
      _gdk_wayland_decor_changed (private);
    }
  /* A popup shown alone keeps its size, as it did under any window
   * manager: the picture in it was made for that size. */
  if (private->window_type == GDK_WINDOW_TEMP)
    width = height = 0;
  /* The size is the window's geometry's, which takes in its title bar. */
  if (height > 0)
    height = MAX (height - GDK_WL_TO (_gdk_wayland_decor_height (private)), 1);
  else if (height < 0)
    height = GDK_WL_TO (impl->saved_height);

  /*
   * A group's leader is configured with the size of the geometry, the
   * whole cluster's: its own size is its own, and the difference is
   * for the members that take it.
   */
  if (private->impl->group && private->impl->group->leader == private)
    {
      group_configured (private->impl->group, GDK_WL_FROM (width), GDK_WL_FROM (height),
                        fixed && !placed);
      width = height = 0;
    }
  _gdk_wayland_window_configured (private, GDK_WL_FROM (width), GDK_WL_FROM (height));
}

static void
xdg_toplevel_close (void *data, struct xdg_toplevel *toplevel)
{
  GdkWindowPrivate *private = data;
  GdkEvent event;

  event.any.type = GDK_DELETE;
  event.any.window = (GdkWindow *) private;
  event.any.send_event = FALSE;
  _gdk_wayland_put_event (&event);
}

/* xdg_wm_base is bound at version 3 or lower, which sends no later
 * toplevel events. */
static const struct xdg_toplevel_listener xdg_toplevel_listener = {
  .configure = xdg_toplevel_configure,
  .close = xdg_toplevel_close,
};

static void
xdg_popup_configure (void *data, struct xdg_popup *popup,
                     int32_t x, int32_t y, int32_t width, int32_t height)
{
  GdkWindowPrivate *private = data;
  GdkWindowPrivate *parent = (GdkWindowPrivate *) private->impl->popup_parent;

  /* Where the compositor put it, in the toplevel's coordinates. */
  if (parent)
    {
      struct xdg_surface *xdg;
      gint px, py;

      popup_parent_surface (parent, &xdg, &px, &py);
      private->x = px + GDK_WL_FROM (x);
      private->y = py + GDK_WL_FROM (y);
    }
  _gdk_wayland_window_configured (private, GDK_WL_FROM (width), GDK_WL_FROM (height));
}

static void
xdg_popup_done (void *data, struct xdg_popup *popup)
{
  /*
   * The compositor dismissed it: a press elsewhere, an escape. The
   * program hears a press and release beside its items, which is how
   * a menu learns to pop down; a delete would have GTK destroy it.
   */
  GdkWindowPrivate *private = data;
  GdkEvent event;

  /*
   * Not during a drag from this program. No menu is up then, and GTK
   * would take the release for the drag's own and end the drag as
   * failed before the compositor has said how it went. sway dismisses
   * a window's popups when a drag over them ends.
   */
  if (_gdk_wayland_drag_under_way ())
    return;
  memset (&event, 0, sizeof event);
  event.button.type = GDK_BUTTON_PRESS;
  event.button.window = (GdkWindow *) private;
  event.button.send_event = TRUE;
  event.button.time = _gdk_wayland_last_time + 1000;
  event.button.x = event.button.y = -1;
  event.button.x_root = private->x - 1;
  event.button.y_root = private->y - 1;
  event.button.button = 1;
  event.button.source = GDK_SOURCE_MOUSE;
  event.button.deviceid = GDK_CORE_POINTER;
  _gdk_wayland_put_event (&event);
  event.button.type = GDK_BUTTON_RELEASE;
  _gdk_wayland_put_event (&event);
}

static void
xdg_popup_repositioned (void *data, struct xdg_popup *popup, uint32_t token)
{
}

static const struct xdg_popup_listener xdg_popup_listener = {
  xdg_popup_configure, xdg_popup_done, xdg_popup_repositioned
};

/*
 * The compositor gave the window a size, or (0, 0) to keep its own.
 * The window follows, and the program hears of it as X told it.
 */
static void
_gdk_wayland_window_configured (GdkWindowPrivate *private, gint width, gint height)
{
  GdkEvent event;
  gboolean changed;

  if (private->destroyed)
    return;
  /* A size from the compositor is the window's until it says otherwise. */
  private->impl->cfg_width = width > 0 ? width : 0;
  private->impl->cfg_height = height > 0 ? height : 0;
  changed = width > 0 && height > 0 &&
    (width != private->width || height != private->height);
  if (changed)
    {
      private->width = width;
      private->height = height;
      _gdk_window_resized ((GdkWindow *) private);
      expose_all (private);
    }
  if (!private->impl->configured || changed)
    {
      event.configure.type = GDK_CONFIGURE;
      event.configure.window = (GdkWindow *) private;
      event.configure.send_event = FALSE;
      event.configure.x = private->x;
      event.configure.y = private->y;
      event.configure.width = private->width;
      event.configure.height = private->height;
      _gdk_wayland_put_event (&event);
    }
  private->impl->commit_pending = TRUE;
}

/*
 * The window a popup can hang from, for a native window: itself, unless
 * it is a child, which is a subsurface and has no xdg_surface of its
 * own. Then it is the window the child sits in, however deep. The
 * pointer is in such a child whenever it is over a GtkGLArea.
 */
static GdkWindowPrivate *
popup_anchor (GdkWindowPrivate *native)
{
  while (native && native->window_type == GDK_WINDOW_CHILD && native->parent)
    native = _gdk_window_native ((GdkWindowPrivate *) native->parent, NULL, NULL);
  return native && native->mapped && !native->destroyed ? native : NULL;
}

/*
 * The toplevel a popup belongs to: the popup with the grab, if there
 * is one and it is mapped, else the native window with the pointer or
 * the keyboard, else any mapped toplevel, a popup shown alone among
 * them (see popup_map_alone ()).
 */
static GdkWindowPrivate *
popup_parent (void)
{
  GdkWindowPrivate *w;
  GList *l;

  if (_gdk_wayland_grab_popup && _gdk_wayland_grab_popup->mapped &&
      !_gdk_wayland_grab_popup->destroyed)
    return _gdk_wayland_grab_popup;
  if ((w = popup_anchor (_gdk_wayland_pointer_window)))
    return w;
  if ((w = popup_anchor (_gdk_wayland_keyboard_window)))
    return w;
  for (l = native_windows; l; l = l->next)
    {
      GdkWindowPrivate *w = l->data;

      /* A popup shown alone is hung from once it is on the screen. */
      if (w->mapped && !w->destroyed && w->impl->xdg_toplevel &&
          (w->window_type != GDK_WINDOW_TEMP || w->impl->committed))
        return w;
    }
  return NULL;
}

/*
 * The xdg_surface a popup hangs from for its parent window, and where
 * that surface's window geometry is in root coordinates, which is what
 * a popup's place is given and told relative to: a group member's is
 * its host's, the box round the group; a toplevel's is at (0, 0), or
 * above it by the title bar GDK draws; a popup's is the popup.
 */
static void
popup_parent_surface (GdkWindowPrivate *parent, struct xdg_surface **xdg,
                      gint *x, gint *y)
{
  gint width, height;

  popup_parent_geometry (parent, xdg, x, y, &width, &height);
}

static void
popup_parent_geometry (GdkWindowPrivate *parent, struct xdg_surface **xdg,
                       gint *x, gint *y, gint *width, gint *height)
{
  GdkWindowImpl *impl = parent->impl;

  *xdg = NULL;
  *x = *y = 0;
  *width = *height = 0;
  if (!impl)
    return;
  if (impl->group && impl->group->leader && impl->group->leader->impl &&
      impl->group->leader->impl->xdg_surface)
    {
      *xdg = impl->group->leader->impl->xdg_surface;
      *x = impl->group->x;
      *y = impl->group->y;
      *width = impl->group->width;
      *height = impl->group->height;
    }
  else if (impl->xdg_surface)
    {
      *xdg = impl->xdg_surface;
      *width = parent->width;
      *height = parent->height;
      if (parent->window_type == GDK_WINDOW_TEMP)
        {
          *x = parent->x;
          *y = parent->y;
        }
      else
        {
          *y = -_gdk_wayland_decor_height (parent);
          *height -= *y;
        }
    }
}

/*
 * Puts a popup at (x, y) in root coordinates. The positioner's anchor
 * must lie in the parent's window geometry: it is the nearest point of
 * it, and the rest is an offset, so a popup can sit above or left of
 * its window (an option menu near a window's top). The compositor
 * slides it back on screen where it would leave it.
 */
static void
popup_set_place (struct xdg_positioner *positioner, GdkWindowPrivate *parent,
                 gint x, gint y)
{
  struct xdg_surface *xdg;
  gint px, py, width, height, ax, ay;

  popup_parent_geometry (parent, &xdg, &px, &py, &width, &height);
  x -= px;
  y -= py;
  ax = CLAMP (x, 0, MAX (width - 1, 0));
  ay = CLAMP (y, 0, MAX (height - 1, 0));
  xdg_positioner_set_anchor_rect (positioner, GDK_WL_TO (ax), GDK_WL_TO (ay), 1, 1);
  xdg_positioner_set_offset (positioner, GDK_WL_TO (x - ax), GDK_WL_TO (y - ay));
}

/* A toplevel's size limits, as the compositor counts: with its title bar. */
void
_gdk_wayland_window_send_size_limits (GdkWindowPrivate *private)
{
  GdkWindowImpl *impl = private->impl;
  gint bar = _gdk_wayland_decor_height (private);

  if (!impl->xdg_toplevel)
    return;
  if (private->window_type == GDK_WINDOW_TEMP)
    {
      /* Shown alone, it is the one size: what a tiling compositor
       * cannot resize, it floats. */
      xdg_toplevel_set_min_size (impl->xdg_toplevel,
                                 GDK_WL_TO (private->width), GDK_WL_TO (private->height));
      xdg_toplevel_set_max_size (impl->xdg_toplevel,
                                 GDK_WL_TO (private->width), GDK_WL_TO (private->height));
      return;
    }
  xdg_toplevel_set_min_size (impl->xdg_toplevel, GDK_WL_TO (impl->min_width),
                             GDK_WL_TO (impl->min_height ? impl->min_height + bar : 0));
  xdg_toplevel_set_max_size (impl->xdg_toplevel, GDK_WL_TO (impl->max_width),
                             GDK_WL_TO (impl->max_height ? impl->max_height + bar : 0));
}

static void
window_apply_decoration (GdkWindowPrivate *private)
{
  GdkWindowImpl *impl = private->impl;

  if (!impl->xdg_toplevel || !_gdk_xdg_decoration_manager)
    return;
  if (!impl->decoration)
    {
      impl->decoration = zxdg_decoration_manager_v1_get_toplevel_decoration
        (_gdk_xdg_decoration_manager, impl->xdg_toplevel);
      _gdk_wayland_decor_listen (private);
      /* Until it answers: no title bar to flash up and go. */
      impl->ssd = TRUE;
    }
  /* A popup shown alone has no decoration, whoever would draw it. */
  zxdg_toplevel_decoration_v1_set_mode (impl->decoration,
                                        impl->decorated &&
                                        private->window_type != GDK_WINDOW_TEMP
                                        ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
                                        : ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
}

/*
 * A popup shown when the program has no window for it to hang from: a
 * splash screen put up before the main window, or a program that is
 * one shaped window. X showed it where it asked. Here it can only be
 * a toplevel: one with no decoration and of the one size, which the
 * compositor places. Popups shown while it is up hang from it, where
 * they asked to be relative to it. Shown again when the program has a
 * window, it is a popup like any other.
 */
static void
popup_map_alone (GdkWindowPrivate *private)
{
  GdkWindowImpl *impl = private->impl;

  impl->popup_parent = NULL;
  impl->xdg_surface = xdg_wm_base_get_xdg_surface (_gdk_xdg_wm_base, impl->surface);
  xdg_surface_add_listener (impl->xdg_surface, &xdg_surface_listener, private);
  impl->xdg_toplevel = xdg_surface_get_toplevel (impl->xdg_surface);
  xdg_toplevel_add_listener (impl->xdg_toplevel, &xdg_toplevel_listener, private);
  xdg_toplevel_set_app_id (impl->xdg_toplevel, gdk_progclass ? gdk_progclass : "gtk");
  if (impl->title)
    xdg_toplevel_set_title (impl->xdg_toplevel, impl->title);
#ifdef HAVE_XDG_TOPLEVEL_ICON
  if (impl->icon)
    window_apply_icon (private);
#endif
  window_apply_decoration (private);
  _gdk_wayland_window_send_size_limits (private);
  impl->configured = FALSE;
  impl->committed = FALSE;
  wl_surface_commit (impl->surface);   /* the initial commit, no buffer */
}

/*
 * A child's subsurface is added to the surface it sits in, and placed
 * in it, when that surface is next committed: so it is to be.
 */
static void
subsurface_changed (GdkWindowPrivate *private)
{
  GdkWindowPrivate *parent = (GdkWindowPrivate *) private->parent;

  if (!parent)
    return;
  parent = _gdk_window_native (parent, NULL, NULL);
  if (parent->impl && parent->impl->surface)
    parent->impl->commit_pending = TRUE;
}

/* Giving a mapped native window its role, and taking it away. */
static void
window_map (GdkWindowPrivate *private)
{
  GdkWindowImpl *impl = private->impl;

  if (!impl->surface)
    return;
  /*
   * An input-only window has nothing to show, and the compositor gives
   * a surface input only where it shows it. It stays the program's:
   * a grab on it, like GtkDnD's on its GtkInvisible, still gets its
   * events. Made a popup, it would be dismissed, and the press and
   * release a dismissal makes would end the grab's drag.
   */
  if (private->depth == 0)
    return;

  switch (private->window_type)
    {
    case GDK_WINDOW_TOPLEVEL:
    case GDK_WINDOW_DIALOG:
      if (impl->xdg_toplevel || impl->subsurface)
        return;
      if (impl->group && impl->group->leader != private)
        {
          group_member_map (private);
          _gdk_wayland_group_layout (impl->group);
          return;
        }
      impl->xdg_surface = xdg_wm_base_get_xdg_surface (_gdk_xdg_wm_base, impl->surface);
      xdg_surface_add_listener (impl->xdg_surface, &xdg_surface_listener, private);
      impl->xdg_toplevel = xdg_surface_get_toplevel (impl->xdg_surface);
      xdg_toplevel_add_listener (impl->xdg_toplevel, &xdg_toplevel_listener, private);
      xdg_toplevel_set_app_id (impl->xdg_toplevel, gdk_progclass ? gdk_progclass : "gtk");
      if (impl->title)
        xdg_toplevel_set_title (impl->xdg_toplevel, impl->title);
#ifdef HAVE_XDG_TOPLEVEL_ICON
      if (impl->icon)
        window_apply_icon (private);
#endif
      window_apply_decoration (private);
      _gdk_wayland_window_send_size_limits (private);
      /* Whose it is, and whose the ones shown before it are that are its. */
      if (impl->transient_for)
        toplevel_send_parent (private);
      {
        GList *l;

        for (l = native_windows; l; l = l->next)
          {
            GdkWindowPrivate *w = l->data;

            if (w->impl->transient_for == (GdkWindow *) private)
              toplevel_send_parent (w);
          }
      }
      impl->configured = FALSE;
      impl->committed = FALSE;
      wl_surface_commit (impl->surface);   /* the initial commit, no buffer */
      break;

    case GDK_WINDOW_TEMP:
      {
        GdkWindowPrivate *parent = popup_parent ();
        struct xdg_surface *parent_xdg = NULL;
        gint px = 0, py = 0;

        if (impl->xdg_popup || impl->drag_icon)
          return;
        if (_gdk_wayland_drag_pending)
          {
            GdkWindowPrivate *pw = _gdk_wayland_pointer_window;

            /* The drag's icon: its surface goes with the pointer.
             * Where it is shown relative to the pointer is its offset
             * from it, and that is taken now: the pointer leaves when
             * the drag starts, which can be before the icon is drawn. */
            if (pw && !pw->destroyed)
              {
                gdk_window_get_origin ((GdkWindow *) pw, &px, &py);
                px += pw->impl->pointer_x;
                py += pw->impl->pointer_y;
              }
            impl->drag_dx = private->x - px;
            impl->drag_dy = private->y - py;
            impl->drag_icon = TRUE;
            impl->configured = TRUE;
            impl->commit_pending = TRUE;
            expose_all (private);
            _gdk_wayland_drag_icon_set (private);
            return;
          }

        if (parent)
          popup_parent_surface (parent, &parent_xdg, &px, &py);
        if (!parent_xdg)
          {
            /*
             * Nothing to hang it from yet. The program may be about to
             * show the window it belongs to, so it waits for the next
             * flush, which shows it alone if there is still nothing.
             */
            impl->commit_pending = TRUE;
            return;
          }
        impl->popup_parent = (GdkWindow *) parent;
        impl->xdg_surface = xdg_wm_base_get_xdg_surface (_gdk_xdg_wm_base, impl->surface);
        xdg_surface_add_listener (impl->xdg_surface, &xdg_surface_listener, private);
        impl->positioner = xdg_wm_base_create_positioner (_gdk_xdg_wm_base);
        xdg_positioner_set_size (impl->positioner,
                                 GDK_WL_TO (private->width), GDK_WL_TO (private->height));
        popup_set_place (impl->positioner, parent, private->x, private->y);
        xdg_positioner_set_anchor (impl->positioner, XDG_POSITIONER_ANCHOR_TOP_LEFT);
        xdg_positioner_set_gravity (impl->positioner, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
        xdg_positioner_set_constraint_adjustment (impl->positioner,
          XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y);
        impl->xdg_popup = xdg_surface_get_popup (impl->xdg_surface,
                                                 parent_xdg, impl->positioner);
        xdg_popup_add_listener (impl->xdg_popup, &xdg_popup_listener, private);
        impl->configured = FALSE;
        impl->committed = FALSE;
        /* A popup of a popup that has the grab must take the grab itself. */
        if (parent == _gdk_wayland_grab_popup)
          impl->want_grab = TRUE;
        /*
         * Not committed yet: a grab must be asked for before the first
         * commit, and the program asks after showing. The first flush
         * commits.
         */
        impl->commit_pending = TRUE;
      }
      break;

    case GDK_WINDOW_CHILD:
      {
        GdkWindowPrivate *parent_native;
        gint x, y;

        if (impl->subsurface)
          return;
        parent_native = _gdk_window_native ((GdkWindowPrivate *) private->parent, &x, &y);
        if (!parent_native->impl->surface || !_gdk_wl_subcompositor)
          return;
        impl->subsurface = wl_subcompositor_get_subsurface (_gdk_wl_subcompositor,
                                                            impl->surface,
                                                            parent_native->impl->surface);
        wl_subsurface_set_position (impl->subsurface,
                                    GDK_WL_TO (x + private->x), GDK_WL_TO (y + private->y));
        wl_subsurface_set_desync (impl->subsurface);
        subsurface_changed (private);
        window_scale_from_outputs (private);
        impl->configured = TRUE;
        impl->commit_pending = TRUE;
        expose_all (private);
      }
      break;

    default:
      break;
    }
}

static void
window_unmap (GdkWindowPrivate *private)
{
  GdkWindowImpl *impl = private->impl;
  gint i;

  if (!impl->surface)
    return;
  if (impl->decoration)
    {
      zxdg_toplevel_decoration_v1_destroy (impl->decoration);
      impl->decoration = NULL;
    }
  if (impl->xdg_popup)
    {
      xdg_popup_destroy (impl->xdg_popup);
      impl->xdg_popup = NULL;
    }
  if (impl->positioner)
    {
      xdg_positioner_destroy (impl->positioner);
      impl->positioner = NULL;
    }
  if (impl->xdg_toplevel)
    {
      _gdk_wayland_decor_free (private);
      xdg_toplevel_destroy (impl->xdg_toplevel);
      impl->xdg_toplevel = NULL;
    }
  if (impl->xdg_surface)
    {
      xdg_surface_destroy (impl->xdg_surface);
      impl->xdg_surface = NULL;
    }
  if (impl->subsurface)
    {
      wl_subsurface_destroy (impl->subsurface);
      impl->subsurface = NULL;
    }
  if (impl->group)
    _gdk_wayland_group_layout (impl->group);
  if (impl->frame)
    {
      wl_callback_destroy (impl->frame);
      impl->frame = NULL;
    }
  wl_surface_attach (impl->surface, NULL, 0, 0);
  wl_surface_commit (impl->surface);
  for (i = 0; i < 2; i++)
    _gdk_wayland_shm_buffer_free (&impl->buffers[i]);
  if (impl->drag_icon)
    {
      /* A surface that was a drag's icon has had its role. */
      impl->drag_icon = FALSE;
      wl_surface_destroy (impl->surface);
      native_windows = g_list_remove (native_windows, private);
      window_make_surface (private);
    }
  impl->configured = impl->committed = impl->commit_pending = FALSE;
  impl->want_grab = FALSE;
  if (_gdk_wayland_grab_popup == private)
    {
      GdkWindowPrivate *parent = (GdkWindowPrivate *) impl->popup_parent;

      _gdk_wayland_grab_popup = parent && parent->impl && parent->impl->xdg_popup &&
        parent->mapped ? parent : NULL;
    }
  _gdk_wayland_pointer_native_gone (private);
  if (_gdk_wayland_pointer_window == private)
    _gdk_wayland_pointer_window = NULL;
  if (_gdk_wayland_keyboard_window == private)
    _gdk_wayland_keyboard_window = NULL;
}

/* ---------------------------------------------------------------------
 * Publishing: the framebuffer into the buffer the compositor is not
 * holding, committed once per flush, and no faster than frames come.
 */

static void
frame_done (void *data, struct wl_callback *callback, uint32_t time)
{
  GdkWindowPrivate *private = data;

  wl_callback_destroy (callback);
  private->impl->frame = NULL;
  _gdk_wayland_decor_frame_done (private);
}

static const struct wl_callback_listener frame_listener = {
  frame_done
};

/*
 * A frame is waited for, but not for ever. A compositor need not send
 * one while it shows nothing of the window, and GNOME's sends none
 * until it next draws the screen: a window all covered by its own
 * subsurfaces -- a GtkGLArea, with no menubar or scrollbar beside it
 * -- would wait for that, and its title bar, and any change to its
 * size, with it. After FRAME_WAIT the window commits without the
 * frame.
 */
#define FRAME_WAIT 100  /* ms */

static guint frame_wait_tag = 0;

static gint64
now_ms (void)
{
  struct timespec ts;

  clock_gettime (CLOCK_MONOTONIC, &ts);
  return (gint64) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static gboolean
frame_wait_over (gpointer data)
{
  frame_wait_tag = 0;
  _gdk_wayland_flush ();
  return FALSE;
}

/* Whether the window waits for its frame still; if so, it is asked
 * after again when the wait is over. */
static gboolean
frame_awaited (GdkWindowPrivate *private)
{
  GdkWindowImpl *impl = private->impl;
  gint64 waited;

  if (!impl->frame)
    return FALSE;
  if (_gdk_wayland_decor_needs_frame (private))
    return TRUE;
  waited = now_ms () - impl->frame_asked;
  if (waited >= FRAME_WAIT || waited < 0)
    {
      wl_callback_destroy (impl->frame);
      impl->frame = NULL;
      return FALSE;
    }
  if (!frame_wait_tag)
    frame_wait_tag = g_timeout_add (FRAME_WAIT - waited, frame_wait_over, NULL);
  return TRUE;
}

/* A rectangle of the framebuffer, with the shape mask cut into its
 * alpha if it has one; NULL for all of it. */
static void
fill_buffer (GdkWindowPrivate *private, GdkWaylandBuffer *buffer,
             const GdkRectangle *rect)
{
  GdkFramebuffer *fb = private->fb;
  GdkFramebuffer *mask = private->impl->shape_mask
    ? _gdk_drawable_fb (private->impl->shape_mask) : NULL;
  gint w = MIN (buffer->width, fb->width), h = MIN (buffer->height, fb->height);
  gint x0 = 0, y0 = 0, x1 = w, y1 = h;
  gint y, x;

  if (rect)
    {
      x0 = MAX (rect->x, 0);
      y0 = MAX (rect->y, 0);
      x1 = MIN (rect->x + rect->width, w);
      y1 = MIN (rect->y + rect->height, h);
    }

  for (y = y0; y < y1 && x0 < x1; y++)
    {
      guint32 *out = buffer->data + (gsize) y * buffer->width;
      const guint32 *in = fb->pixels + (gsize) y * fb->stride;
      /* The mask may be at another scale than the window. */
      gint my = mask ? y * mask->scale / fb->scale : 0;

      if (!mask)
        {
          memcpy (out + x0, in + x0, (gsize) (x1 - x0) * sizeof (guint32));
          continue;
        }
      for (x = x0; x < x1; x++)
        {
          gint mx = x * mask->scale / fb->scale;
          gboolean on = my < mask->height && mx < mask->width &&
            (mask->pixels[(gsize) my * mask->stride + mx] & 1);
          out[x] = on ? (in[x] | 0xff000000) : 0;
        }
    }
}

/* Forgets what is known of a buffer's picture: it is filled whole when
 * next used. */
static void
buffer_stale_all (GdkWindowImpl *impl, gint i)
{
  if (impl->stale[i])
    gdk_region_destroy (impl->stale[i]);
  impl->stale[i] = NULL;
}

static gboolean
window_can_commit (GdkWindowPrivate *private)
{
  GdkWindowImpl *impl = private->impl;

  if (!impl->surface || !impl->configured || !private->mapped)
    return FALSE;
  if (private->external)
    {
      /* Its buffers, and so its commits, are its owner's. */
      impl->commit_pending = FALSE;
      return FALSE;
    }
  if (private->window_type == GDK_WINDOW_TEMP && !impl->xdg_popup &&
      !impl->xdg_toplevel && !impl->drag_icon)
    return FALSE;
  if (frame_awaited (private))
    return FALSE;
  if (impl->buffers[0].busy && impl->buffers[1].busy)
    return FALSE;    /* both held; the release will bring it back */
  return TRUE;
}

static void
window_commit (GdkWindowPrivate *private)
{
  GdkWindowImpl *impl = private->impl;
  GdkWaylandBuffer *buffer = NULL;
  GdkRegion *damage;
  gboolean whole;
  gint i, n, width, height;

  if (!window_can_commit (private))
    return;
  /* What cannot be committed now for want of a picture, or of memory
   * for one, is not asked after again until something changes. */
  if (!_gdk_window_fb_ensure (private))
    {
      impl->commit_pending = FALSE;
      return;
    }

  width = private->fb->width;
  height = private->fb->height;
  for (i = 0; i < 2; i++)
    if (!impl->buffers[i].busy)
      {
        buffer = &impl->buffers[i];
        break;
      }
  if (!buffer)
    return;

  /*
   * Only what changed since a buffer was last filled is copied into it,
   * and only what changed since the last commit is damaged, so that a
   * small change costs the program and the compositor little. A commit
   * that comes without damage, for a new shape or the like, sends the
   * whole picture.
   */
  damage = impl->pending_damage;
  whole = impl->damage_all || !damage || gdk_region_empty (damage);
  if (!whole && GDK_REGION_N_RECTS (damage) > 32)
    {
      /* Too many pieces cost more than the pixels between them. */
      GdkRectangle extents = GDK_REGION_EXTENTS (damage);

      _gdk_region_clear (damage);
      _gdk_region_add_rect (damage, &extents);
    }
  for (n = 0; n < 2; n++)
    {
      if (whole)
        buffer_stale_all (impl, n);
      else if (impl->stale[n])
        {
          gint r;

          for (r = 0; r < GDK_REGION_N_RECTS (damage); r++)
            _gdk_region_add_rect (impl->stale[n], &GDK_REGION_RECTS (damage)[r]);
        }
    }

  if (buffer->buffer && (buffer->width != width || buffer->height != height))
    _gdk_wayland_shm_buffer_free (buffer);
  if (!buffer->buffer)
    {
      buffer_stale_all (impl, i);
      if (!_gdk_wayland_shm_buffer_new (buffer, width, height,
                                        impl->shape_mask != NULL))
        {
          impl->commit_pending = FALSE;
          return;
        }
    }
  buffer->window = private;

  if (!impl->stale[i])
    {
      fill_buffer (private, buffer, NULL);
      impl->stale[i] = gdk_region_new ();
    }
  else
    {
      for (n = 0; n < GDK_REGION_N_RECTS (impl->stale[i]); n++)
        fill_buffer (private, buffer, &GDK_REGION_RECTS (impl->stale[i])[n]);
      _gdk_region_clear (impl->stale[i]);
    }
  if (impl->drag_icon && !impl->committed)
    wl_surface_attach (impl->surface, buffer->buffer,
                       GDK_WL_TO (impl->drag_dx), GDK_WL_TO (impl->drag_dy));
  else
    wl_surface_attach (impl->surface, buffer->buffer, 0, 0);
  if (whole)
    wl_surface_damage_buffer (impl->surface, 0, 0, width, height);
  else
    for (n = 0; n < GDK_REGION_N_RECTS (damage); n++)
      {
        const GdkRectangle *r = &GDK_REGION_RECTS (damage)[n];

        wl_surface_damage_buffer (impl->surface, r->x, r->y, r->width, r->height);
      }
  if (wl_proxy_get_version ((struct wl_proxy *) impl->surface) >= 3)
    wl_surface_set_buffer_scale (impl->surface,
                                 MAX (private->fb->scale / _gdk_wayland_zoom, 1));
  buffer->busy = TRUE;

  if (impl->pending_damage)
    _gdk_region_clear (impl->pending_damage);
  impl->damage_all = FALSE;

  impl->committed = TRUE;
  if (impl->want_move && impl->xdg_toplevel && _gdk_wl_seat)
    {
      impl->want_move = FALSE;
      xdg_toplevel_move (impl->xdg_toplevel, _gdk_wl_seat, _gdk_wayland_press_serial);
      _gdk_wayland_release_buttons ();
    }

  if (impl->xdg_toplevel && !impl->group)
    _gdk_wayland_decor_before_commit (private);

  impl->frame = wl_surface_frame (impl->surface);
  impl->frame_asked = now_ms ();
  wl_callback_add_listener (impl->frame, &frame_listener, private);
  wl_surface_commit (impl->surface);
  impl->commit_pending = FALSE;
  if (impl->subsurface && impl->group)
    group_member_committed (private);
}

/*
 * A popup's first commit has no buffer: the compositor configures the
 * popup in answer, and only then may it be drawn. Its grab, if the
 * program asked for one on showing it, must come before.
 */
static void
popup_initial_commit (GdkWindowPrivate *private)
{
  GdkWindowImpl *impl = private->impl;
  GdkWindowPrivate *parent = (GdkWindowPrivate *) impl->popup_parent;

  /*
   * A menubar's menu: the program grabbed the pointer on its own window,
   * which to the compositor is no grab at all. The menu takes the
   * compositor's grab in its place, so a click elsewhere closes it.
   */
  if (!impl->want_grab && gdk_xgrab_window && !gdk_xgrab_window->destroyed &&
      parent && parent->impl && parent->impl->xdg_toplevel &&
      _gdk_window_native (gdk_xgrab_window, NULL, NULL) == parent)
    impl->want_grab = TRUE;

  if (impl->want_grab && _gdk_wl_seat)
    {
      xdg_popup_grab (impl->xdg_popup, _gdk_wl_seat, _gdk_wayland_input_serial);
      _gdk_wayland_grab_popup = private;
    }
  impl->committed = TRUE;
  wl_surface_commit (impl->surface);
}

/*
 * The popups waiting for a window to hang from, when the program has
 * none: the one shown first is shown alone, and the others hang from
 * it once it is up. One at a time, then, and the list has the newest
 * first.
 */
static void
popups_alone (void)
{
  GList *l;

  if (_gdk_wayland_drag_pending)
    return;
  for (l = native_windows; l; l = l->next)
    {
      GdkWindowPrivate *w = l->data;

      if (w->window_type == GDK_WINDOW_TEMP && !w->destroyed &&
          w->impl->xdg_toplevel && !w->impl->committed)
        return;
    }
  for (l = g_list_last (native_windows); l; l = l->prev)
    {
      GdkWindowPrivate *w = l->data;

      if (w->window_type == GDK_WINDOW_TEMP && w->mapped && !w->destroyed &&
          w->depth != 0 && w->impl->surface && w->impl->commit_pending &&
          !w->impl->xdg_popup && !w->impl->xdg_toplevel && !w->impl->drag_icon)
        {
          if (!popup_parent ())
            popup_map_alone (w);
          return;
        }
    }
}

void
_gdk_wayland_window_commit_all (void)
{
  GList *l;

  popups_alone ();
  for (l = native_windows; l; l = l->next)
    {
      GdkWindowPrivate *private = l->data;

      if (private->impl->commit_pending && !private->destroyed)
        {
          /* A popup shown before it had a parent: try again now. */
          if (private->window_type == GDK_WINDOW_TEMP && !private->impl->xdg_popup &&
              !private->impl->xdg_toplevel && private->mapped)
            {
              window_map (private);
              if (!private->impl->xdg_popup)
                continue;
            }
          if (private->impl->xdg_popup && !private->impl->committed)
            {
              popup_initial_commit (private);
              continue;
            }
          /*
           * A commit shows the window's picture as it is at that very
           * moment, so it is not made here, where the program may be
           * half way through drawing: a window that could be committed
           * is published with the rest, when the picture is whole, and
           * committed then.
           */
          if (window_can_commit (private))
            _gdk_window_publish_soon (private);
        }
    }
}

void
_gdk_wayland_publish (GdkWindow *window, const GdkRectangle *rect)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;

  if (!private->impl || !private->impl->surface)
    return;
  if (!rect)
    {
      /* That was all: the picture is whole, and can be shown. The
       * compositor has it now, not when the connection is next
       * flushed: the program may be long in coming back for that. */
      if (private->impl->commit_pending)
        {
          window_commit (private);
          wl_display_flush (_gdk_wl_display);
        }
      return;
    }
  if (!private->impl->pending_damage)
    private->impl->pending_damage = gdk_region_new ();
  _gdk_region_add_rect (private->impl->pending_damage, rect);
  private->impl->commit_pending = TRUE;
}

void
_gdk_wayland_get_image (GdkWindow *window, gint x, gint y, GdkImage *image)
{
  /* Nothing of the screen is ours to read. */
}

void
_gdk_wayland_pixmap_free (GdkWindowPrivate *private)
{
  g_free (private->impl);
  private->impl = NULL;
}

void
_gdk_wayland_image_free (GdkImagePrivate *private)
{
}

void
_gdk_wayland_gc_free (GdkGCPrivate *private)
{
}

/* ---------------------------------------------------------------------
 * The window functions
 */

static GdkWindowImpl *
impl_new (void)
{
  GdkWindowImpl *impl = g_new0 (GdkWindowImpl, 1);

  impl->decorated = TRUE;
  return impl;
}

/* The native windows inside a window, at whatever depth, take its scale. */
static void
natives_follow_scale (GdkWindowPrivate *private)
{
  GList *l;

  for (l = private->children; l; l = l->next)
    {
      GdkWindowPrivate *child = l->data;

      if (child->destroyed)
        continue;
      if (child->native)
        window_scale_from_outputs (child);
      else
        natives_follow_scale (child);
    }
}

/*
 * Scale: a window is drawn at the scale of the output it is on, the
 * biggest if it straddles several, and at the biggest of all of them
 * before it is on any. The compositor scales a buffer at the wrong
 * scale itself, so a window is never wrong, only blurred for a frame.
 */
static void
window_scale_from_outputs (GdkWindowPrivate *private)
{
  GdkWindowImpl *impl = private->impl;
  GList *l;
  gint scale = 1;

  if (_gdk_scale_override ())
    return;
  if (private->window_type == GDK_WINDOW_CHILD && private->parent)
    {
      /*
       * A window inside another is drawn as that one is: it is on no
       * output of its own until it has shown something, and would
       * otherwise start at another scale than what is around it.
       */
      scale = _gdk_window_native ((GdkWindowPrivate *) private->parent, NULL, NULL)->scale;
    }
  else
    {
      if (!impl->outputs)
        scale = gdk_screen_scale_factor ();
      for (l = impl->outputs; l; l = l->next)
        scale = MAX (scale, _gdk_wayland_output_scale (l->data));
    }
  _gdk_window_set_scale ((GdkWindow *) private, scale);
  natives_follow_scale (private);
}

static void
surface_enter (void *data, struct wl_surface *surface, struct wl_output *output)
{
  GdkWindowPrivate *private = data;

  if (private->destroyed || !private->impl)
    return;
  if (!g_list_find (private->impl->outputs, output))
    private->impl->outputs = g_list_prepend (private->impl->outputs, output);
  window_scale_from_outputs (private);
}

static void
surface_leave (void *data, struct wl_surface *surface, struct wl_output *output)
{
  GdkWindowPrivate *private = data;

  if (private->destroyed || !private->impl)
    return;
  private->impl->outputs = g_list_remove (private->impl->outputs, output);
  window_scale_from_outputs (private);
}

/* An output was unplugged: no window is on it now. */
void
_gdk_wayland_window_output_gone (struct wl_output *output)
{
  GList *l;

  for (l = native_windows; l; l = l->next)
    {
      GdkWindowPrivate *private = l->data;

      if (private->destroyed || !private->impl ||
          !g_list_find (private->impl->outputs, output))
        continue;
      private->impl->outputs = g_list_remove (private->impl->outputs, output);
      window_scale_from_outputs (private);
    }
}

/* The compositor is bound at version 4 or lower, whose surfaces send no
 * later events. */
static const struct wl_surface_listener surface_listener = {
  .enter = surface_enter,
  .leave = surface_leave,
};

void
_gdk_wayland_outputs_changed (void)
{
  GList *l;

  for (l = native_windows; l; l = l->next)
    window_scale_from_outputs (l->data);
}

/*
 * The fastest of the outputs the window's toplevel is on -- a window
 * inside another is where that one is -- or, before the compositor has
 * said which those are, of any output at all.
 */
gint
_gdk_wayland_window_refresh_rate (GdkWindow *window)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  gint rate = 0;
  GList *l;

  for (;;)
    {
      GdkWindowPrivate *native = _gdk_window_native (private, NULL, NULL);

      for (l = native->impl ? native->impl->outputs : NULL; l; l = l->next)
        rate = MAX (rate, _gdk_wayland_output_refresh (l->data));
      if (rate > 0 || native->window_type != GDK_WINDOW_CHILD || !native->parent)
        break;
      private = (GdkWindowPrivate *) native->parent;
    }
  return rate > 0 ? rate : _gdk_wayland_output_refresh (NULL);
}

static void
window_make_surface (GdkWindowPrivate *private)
{
  GdkWindowImpl *impl = private->impl;

  impl->surface = wl_compositor_create_surface (_gdk_wl_compositor);
  wl_surface_set_user_data (impl->surface, private);
  wl_surface_add_listener (impl->surface, &surface_listener, private);
  native_windows = g_list_prepend (native_windows, private);
}

GdkWindow *
_gdk_wayland_window_new (GdkWindow     *parent,
                         GdkWindowAttr *attributes,
                         gint           attributes_mask)
{
  GdkWindow *window;
  GdkWindowPrivate *private;
  GdkWindowPrivate *parent_private;

  g_return_val_if_fail (attributes != NULL, NULL);

  if (!parent)
    parent = (GdkWindow*) &gdk_root_parent;
  parent_private = (GdkWindowPrivate*) parent;
  if (parent_private->destroyed)
    return NULL;

  private = g_new0 (GdkWindowPrivate, 1);
  window = (GdkWindow*) private;
  _gdk_window_private_init (private);
  private->impl = impl_new ();
  private->parent = parent;
  private->x = (attributes_mask & GDK_WA_X) ? attributes->x : 0;
  private->y = (attributes_mask & GDK_WA_Y) ? attributes->y : 0;
  if (attributes->window_type == GDK_WINDOW_TOPLEVEL ||
      attributes->window_type == GDK_WINDOW_DIALOG)
    {
      private->impl->asked_x = private->x;
      private->impl->asked_y = private->y;
      private->x = private->y = 0;
    }
  private->width = MAX (attributes->width, 1);
  private->height = MAX (attributes->height, 1);
  private->window_type = attributes->window_type;
  private->ref_count = 1;
  private->event_mask = attributes->event_mask;
  private->colormap = (attributes_mask & GDK_WA_COLORMAP) ? attributes->colormap
    : gdk_colormap_get_system ();
  gdk_colormap_ref (private->colormap);
  private->depth = attributes->wclass == GDK_INPUT_OUTPUT
    ? gdk_visual_get_system ()->depth : 0;
  private->native = private->window_type != GDK_WINDOW_CHILD ||
    (attributes_mask & GDK_WA_NATIVE) != 0;

  if (private->window_type == GDK_WINDOW_ROOT ||
      private->window_type == GDK_WINDOW_PIXMAP)
    g_error ("cannot make windows of type %d", private->window_type);
  if (private->window_type == GDK_WINDOW_CHILD &&
      parent_private->window_type == GDK_WINDOW_ROOT)
    private->window_type = GDK_WINDOW_TOPLEVEL;

  gdk_window_ref (window);
  parent_private->children = g_list_prepend (parent_private->children, window);

  if (private->native)
    window_make_surface (private);
  if (private->native && private->window_type == GDK_WINDOW_CHILD)
    window_scale_from_outputs (private);
  if (attributes_mask & GDK_WA_TITLE)
    gdk_window_set_title (window, attributes->title);
  if (attributes_mask & GDK_WA_CURSOR)
    gdk_window_set_cursor (window, attributes->cursor);

  return window;
}

static void
window_internal_destroy (GdkWindow *window)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  GList *children, *l;
  gboolean listed = FALSE;

  if (private->destroyed || private->window_type == GDK_WINDOW_ROOT)
    return;

  /* Whatever draws an external window lets go while its surface is there. */
  _gdk_window_external_release (private);

  if (private->mapped)
    {
      private->mapped = FALSE;
      _gdk_window_mapped (window, FALSE);
    }
  if (private->parent)
    {
      GdkWindowPrivate *parent = (GdkWindowPrivate *) private->parent;

      listed = g_list_find (parent->children, window) != NULL;
      parent->children = g_list_remove (parent->children, window);
    }

  children = private->children;
  private->children = NULL;
  for (l = children; l; l = l->next)
    {
      GdkWindowPrivate *child = l->data;

      child->parent = NULL;
      window_internal_destroy ((GdkWindow *) child);
      gdk_window_unref ((GdkWindow *) child);
    }
  g_list_free (children);

  for (l = private->filters; l; l = l->next)
    g_free (l->data);
  g_list_free (private->filters);
  private->filters = NULL;

  if (private->impl->transient_for)
    {
      gdk_window_unref (private->impl->transient_for);
      private->impl->transient_for = NULL;
    }

  if (private->impl->surface)
    {
      window_unmap (private);
      wl_surface_destroy (private->impl->surface);
      private->impl->surface = NULL;
      native_windows = g_list_remove (native_windows, private);
    }
  if (private->impl->group)
    {
      GdkWaylandGroup *group = private->impl->group;

      group->members = g_list_remove (group->members, private);
      private->impl->group = NULL;
      if (group->leader == private)
        {
          GList *l;

          for (l = group->members; l; l = l->next)
            ((GdkWindowPrivate *) l->data)->impl->group = NULL;
          g_list_free (group->members);
          group->members = NULL;
        }
      if (!group->members)
        group_free (group);
      else
        _gdk_wayland_group_layout (group);
    }
  if (private->impl->pending_damage)
    gdk_region_destroy (private->impl->pending_damage);
  buffer_stale_all (private->impl, 0);
  buffer_stale_all (private->impl, 1);
  private->impl->pending_damage = NULL;
  _gdk_wayland_properties_free (private);

  if (private->colormap)
    gdk_colormap_unref (private->colormap);
  private->colormap = NULL;
  _gdk_window_private_free (private);
  private->destroyed = TRUE;

  /* Its parent's list held a reference, as a parent destroying its
   * children drops for them: a window destroyed on its own drops it
   * here, last, with whoever destroyed it still holding theirs. */
  if (listed)
    gdk_window_unref (window);
}

void
_gdk_wayland_window_destroy (GdkWindow *window)
{
  g_return_if_fail (window != NULL);

  window_internal_destroy (window);
  gdk_window_unref (window);
}

void
_gdk_wayland_window_free (GdkWindowPrivate *private)
{
  GdkWindowImpl *impl = private->impl;

  if (!impl)
    return;
  if (impl->cursor && --impl->cursor->ref_count == 0)
    {
      _gdk_wayland_shm_buffer_free (&impl->cursor->own);
      g_free (impl->cursor);
    }
  if (impl->shape)
    wl_region_destroy (impl->shape);
  if (impl->shape_mask)
    gdk_pixmap_unref (impl->shape_mask);
  g_list_free (impl->outputs);
  g_free (impl->title);
#ifdef HAVE_XDG_TOPLEVEL_ICON
  if (impl->icon && impl->icon_own)
    wayland_icon_free (impl->icon);
#endif
  g_free (impl);
  private->impl = NULL;
}

/*
 * The native windows inside a window that is not native are
 * subsurfaces of its native ancestor's surface. Before that becomes
 * another -- the window is given another parent, or a surface of its
 * own -- they are taken out of it, to be put into the new one by
 * sync_native_children.
 */
static void
unmap_native_children (GdkWindowPrivate *private)
{
  GList *l;

  for (l = private->children; l; l = l->next)
    {
      GdkWindowPrivate *child = l->data;

      if (child->destroyed)
        continue;
      if (child->native)
        window_unmap (child);
      else
        unmap_native_children (child);
    }
}

/* Native descendants of a window that is not: placed and mapped after it. */
static void
sync_native_children (GdkWindowPrivate *private)
{
  GList *l;

  for (l = private->children; l; l = l->next)
    {
      GdkWindowPrivate *child = l->data;

      if (child->destroyed)
        continue;
      if (child->native)
        {
          GdkWindowPrivate *w;
          gboolean viewable = child->mapped;

          for (w = (GdkWindowPrivate *) child->parent; w && !w->native;
               w = (GdkWindowPrivate *) w->parent)
            if (!w->mapped)
              viewable = FALSE;
          if (viewable)
            {
              gint x, y;

              window_map (child);
              if (child->impl->subsurface)
                {
                  _gdk_window_native ((GdkWindowPrivate *) child->parent, &x, &y);
                  wl_subsurface_set_position (child->impl->subsurface,
                                              GDK_WL_TO (x + child->x),
                                              GDK_WL_TO (y + child->y));
                  subsurface_changed (child);
                  child->impl->commit_pending = TRUE;
                }
            }
          else
            window_unmap (child);
        }
      else
        sync_native_children (child);
    }
}

void
_gdk_wayland_window_show (GdkWindow *window)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;

  g_return_if_fail (window != NULL);

  if (private->destroyed)
    return;
  private->mapped = TRUE;
  if (private->parent)
    {
      GdkWindowPrivate *parent = (GdkWindowPrivate *) private->parent;
      parent->children = g_list_remove (parent->children, private);
      parent->children = g_list_prepend (parent->children, private);
      _gdk_window_restacked (window);
    }
  if (private->native)
    window_map (private);
  _gdk_window_mapped (window, TRUE);
  if (!private->native)
    sync_native_children (private);
}

void
_gdk_wayland_window_hide (GdkWindow *window)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;

  g_return_if_fail (window != NULL);

  if (private->destroyed)
    return;
  private->mapped = FALSE;
  if (private->native)
    window_unmap (private);
  _gdk_window_mapped (window, FALSE);
  if (!private->native)
    sync_native_children (private);
}

void
_gdk_wayland_window_withdraw (GdkWindow *window)
{
  _gdk_wayland_window_hide (window);
}

void
_gdk_wayland_window_move (GdkWindow *window, gint x, gint y)
{
  _gdk_wayland_window_move_resize (window, x, y,
                                   ((GdkWindowPrivate *) window)->width,
                                   ((GdkWindowPrivate *) window)->height);
}

void
_gdk_wayland_window_resize (GdkWindow *window, gint width, gint height)
{
  _gdk_wayland_window_move_resize (window, ((GdkWindowPrivate *) window)->x,
                                   ((GdkWindowPrivate *) window)->y, width, height);
}

void
_gdk_wayland_window_move_resize (GdkWindow *window, gint x, gint y,
                                 gint width, gint height)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  gint old_x, old_y, old_width, old_height;

  g_return_if_fail (window != NULL);

  if (private->destroyed)
    return;
  width = MAX (width, 1);
  height = MAX (height, 1);
  old_x = private->x;
  old_y = private->y;
  old_width = private->width;
  old_height = private->height;

  if (private->window_type == GDK_WINDOW_CHILD)
    {
      private->x = x;
      private->y = y;
      private->width = width;
      private->height = height;
      _gdk_window_geometry_changed (window, old_x, old_y, old_width, old_height);
      if (private->native && private->impl->subsurface)
        {
          gint ox, oy;

          _gdk_window_native ((GdkWindowPrivate *) private->parent, &ox, &oy);
          wl_subsurface_set_position (private->impl->subsurface,
                                      GDK_WL_TO (ox + x), GDK_WL_TO (oy + y));
          if (x != old_x || y != old_y)
            subsurface_changed (private);
          if (width != old_width || height != old_height)
            {
              /* Nothing drawn at the old size is of use at the new: the
               * program draws it all, as after an X window's resize. */
              _gdk_window_resized (window);
              if (private->mapped)
                expose_all (private);
            }
          private->impl->commit_pending = TRUE;
        }
      else if (!private->native)
        sync_native_children (private);
      return;
    }

  /*
   * A toplevel is at root (0, 0) whatever it asks: the compositor
   * places it, and the program's root coordinates are the window's.
   * A popup moves, relative to its toplevel; a group member among the
   * others in its group.
   */
  if (private->window_type == GDK_WINDOW_TEMP || private->impl->group)
    {
      private->x = x;
      private->y = y;
      if (private->impl->group && (x != old_x || y != old_y))
        _gdk_wayland_group_layout (private->impl->group);
      /*
       * A popup shown alone is the compositor's to place. Where the
       * program says it is, is where its root coordinates start; a
       * program dragging it with the pointer hands the drag over, as
       * a toplevel's does below.
       */
      if (!private->impl->group && private->impl->xdg_toplevel && private->mapped &&
          (x != old_x || y != old_y) && _gdk_wayland_buttons_down () &&
          _gdk_wayland_pointer_window == private && _gdk_wl_seat)
        {
          xdg_toplevel_move (private->impl->xdg_toplevel, _gdk_wl_seat, _gdk_wayland_press_serial);
          wl_display_flush (_gdk_wl_display);
          _gdk_wayland_release_buttons ();
        }
    }
  else
    {
      gboolean moved = x != private->impl->asked_x || y != private->impl->asked_y;

      private->impl->asked_x = x;
      private->impl->asked_y = y;
      x = y = 0;
      if (moved && private->mapped && private->impl->xdg_toplevel &&
          _gdk_wayland_buttons_down () && _gdk_wayland_pointer_window == private &&
          _gdk_wl_seat)
        {
          /*
           * The program is dragging its window with the pointer, which
           * is the compositor's to do here: it takes over, and the
           * program's drag ends with a release.
           */
          xdg_toplevel_move (private->impl->xdg_toplevel, _gdk_wl_seat, _gdk_wayland_press_serial);
          wl_display_flush (_gdk_wl_display);
          _gdk_wayland_release_buttons ();
        }

    }
  if (private->window_type == GDK_WINDOW_TEMP && private->impl->xdg_popup &&
      private->impl->popup_parent && (x != old_x || y != old_y))
    {
      GdkWindowPrivate *parent = (GdkWindowPrivate *) private->impl->popup_parent;

      popup_set_place (private->impl->positioner, parent, x, y);
      if (xdg_wm_base_get_version (_gdk_xdg_wm_base) >= 3)
        xdg_popup_reposition (private->impl->xdg_popup, private->impl->positioner, 0);
    }
  /*
   * While the compositor dictates the size (tiled, maximized), the
   * program's is not taken; either way the program hears what the
   * size is, after every request, as the window manager told it on X.
   */
  if (private->window_type != GDK_WINDOW_TEMP && private->impl->cfg_fixed &&
      private->impl->cfg_width > 0)
    {
      width = private->impl->cfg_width;
      height = private->impl->cfg_height;
    }
  if (width != old_width || height != old_height)
    {
      private->width = width;
      private->height = height;
      _gdk_window_resized (window);
      if (private->impl->positioner)
        xdg_positioner_set_size (private->impl->positioner,
                                 GDK_WL_TO (width), GDK_WL_TO (height));
      if (private->window_type == GDK_WINDOW_TEMP && private->impl->xdg_toplevel)
        _gdk_wayland_window_send_size_limits (private);
      if (private->impl->group)
        _gdk_wayland_group_layout (private->impl->group);
      private->impl->commit_pending = TRUE;
      if (private->mapped)
        expose_all (private);
    }
  if (private->mapped && private->window_type != GDK_WINDOW_TEMP)
    {
      GdkEvent event;

      event.configure.type = GDK_CONFIGURE;
      event.configure.window = window;
      event.configure.send_event = FALSE;
      event.configure.x = private->x;
      event.configure.y = private->y;
      event.configure.width = private->width;
      event.configure.height = private->height;
      _gdk_wayland_put_event (&event);
    }
}

void
_gdk_wayland_window_reparent (GdkWindow *window, GdkWindow *new_parent, gint x, gint y)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  GdkWindowPrivate *old_parent, *parent;
  gboolean was_mapped;

  g_return_if_fail (window != NULL);

  if (!new_parent)
    new_parent = (GdkWindow *) &gdk_root_parent;
  old_parent = (GdkWindowPrivate *) private->parent;
  parent = (GdkWindowPrivate *) new_parent;

  was_mapped = private->mapped;
  if (was_mapped)
    {
      private->mapped = FALSE;
      _gdk_window_mapped (window, FALSE);
    }
  if (private->native)
    window_unmap (private);
  else
    unmap_native_children (private);
  if (old_parent)
    old_parent->children = g_list_remove (old_parent->children, window);
  private->parent = new_parent;
  private->x = x;
  private->y = y;
  parent->children = g_list_prepend (parent->children, window);
  if (private->native)
    window_scale_from_outputs (private);
  else
    natives_follow_scale (private);
  if (was_mapped)
    {
      private->mapped = TRUE;
      if (private->native)
        window_map (private);
      _gdk_window_mapped (window, TRUE);
    }
  if (!private->native)
    sync_native_children (private);
}

void
_gdk_wayland_window_raise (GdkWindow *window)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  GdkWindowPrivate *parent;

  g_return_if_fail (window != NULL);

  if (private->destroyed || !private->parent)
    return;
  parent = (GdkWindowPrivate *) private->parent;
  parent->children = g_list_remove (parent->children, private);
  parent->children = g_list_prepend (parent->children, private);
  _gdk_window_restacked (window);
  if (private->impl->group && private->impl->subsurface)
    {
      GdkWaylandGroup *group = private->impl->group;
      GdkWindowPrivate *top = group->members ? group->members->data : NULL;

      if (top && top != private && top->impl->subsurface)
        wl_subsurface_place_above (private->impl->subsurface, top->impl->surface);
      group->members = g_list_remove (group->members, private);
      group->members = g_list_prepend (group->members, private);
      _gdk_wayland_group_layout (group);
    }
}

void
_gdk_wayland_window_lower (GdkWindow *window)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  GdkWindowPrivate *parent;

  g_return_if_fail (window != NULL);

  if (private->destroyed || !private->parent)
    return;
  parent = (GdkWindowPrivate *) private->parent;
  parent->children = g_list_remove (parent->children, private);
  parent->children = g_list_append (parent->children, private);
  _gdk_window_restacked (window);
}

void
_gdk_wayland_window_ensure_native (GdkWindow *window)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;

  g_return_if_fail (window != NULL);

  if (private->native || private->destroyed)
    return;
  unmap_native_children (private);
  private->native = TRUE;
  window_make_surface (private);
  window_scale_from_outputs (private);
  if (private->mapped)
    sync_native_children ((GdkWindowPrivate *) private->parent);
  /* The native windows inside it are in its surface now. */
  sync_native_children (private);
}

void
_gdk_wayland_window_set_hints (GdkWindow *window, gint x, gint y,
                               gint min_width, gint min_height,
                               gint max_width, gint max_height, gint flags)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;

  g_return_if_fail (window != NULL);

  if (private->destroyed)
    return;
  /*
   * Kept, and given to the toplevel when it exists: a window whose
   * smallest and largest sizes are the same is one the compositors
   * float rather than tile, which is what a fixed-size window wants.
   */
  if (flags & GDK_HINT_MIN_SIZE)
    {
      private->impl->min_width = MAX (min_width, 0);
      private->impl->min_height = MAX (min_height, 0);
    }
  if (flags & GDK_HINT_MAX_SIZE)
    {
      private->impl->max_width = MAX (max_width, 0);
      private->impl->max_height = MAX (max_height, 0);
    }
  if (private->impl->group &&
      (private->impl->group->leader == private ||
       private->impl->group_resize_h || private->impl->group_resize_v))
    {
      /* The cluster's limits are the leader's, and a member's that takes its resizes. */
      _gdk_wayland_group_layout (private->impl->group);
      return;
    }
  if (private->impl->xdg_toplevel)
    {
      _gdk_wayland_window_send_size_limits (private);
      private->impl->commit_pending = TRUE;
    }
}

void
_gdk_wayland_window_set_geometry_hints (GdkWindow *window, GdkGeometry *geometry,
                                        GdkWindowHints flags)
{
  g_return_if_fail (window != NULL);

  if (!geometry)
    return;
  _gdk_wayland_window_set_hints (window, 0, 0, geometry->min_width, geometry->min_height,
                                 geometry->max_width, geometry->max_height,
                                 flags & (GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE));
}

void
_gdk_wayland_window_set_title (GdkWindow *window, const gchar *title)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;

  g_return_if_fail (window != NULL);

  if (private->destroyed || !title)
    return;
  g_free (private->impl->title);
  private->impl->title = g_strdup (title);
  if (private->impl->xdg_toplevel)
    xdg_toplevel_set_title (private->impl->xdg_toplevel, title);
  _gdk_wayland_decor_changed (private);
}

void
_gdk_wayland_window_set_role (GdkWindow *window, const gchar *role)
{
}

/*
 * The window a toplevel belongs to, as a dialog does to the window it
 * was opened from. GTK+ says so when the dialog is realized, which is
 * before it is shown, and so before there is a toplevel to say it of:
 * it is kept, and said when the toplevel is made. A dialog the
 * compositor has not been told of this way is a window like any
 * other to it: a tiling one tiles it, and none keeps it over the
 * window it came from.
 */
static void
toplevel_send_parent (GdkWindowPrivate *private)
{
  GdkWindowPrivate *parent = (GdkWindowPrivate *) private->impl->transient_for;

  if (!private->impl->xdg_toplevel)
    return;
  if (parent && (parent->destroyed || !parent->impl->xdg_toplevel))
    parent = NULL;
  xdg_toplevel_set_parent (private->impl->xdg_toplevel,
                           parent ? parent->impl->xdg_toplevel : NULL);
}

void
_gdk_wayland_window_set_transient_for (GdkWindow *window, GdkWindow *leader)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;

  g_return_if_fail (window != NULL);

  if (private->destroyed)
    return;
  if (leader)
    gdk_window_ref (leader);
  if (private->impl->transient_for)
    gdk_window_unref (private->impl->transient_for);
  private->impl->transient_for = leader;
  toplevel_send_parent (private);
}

/* ---------------------------------------------------------------------
 * Cursors
 */

static GdkCursorImpl *
cursor_impl_ref (GdkCursorImpl *impl)
{
  impl->ref_count++;
  return impl;
}

static void
cursor_impl_unref (GdkCursorImpl *impl)
{
  if (--impl->ref_count > 0)
    return;
  _gdk_wayland_shm_buffer_free (&impl->own);
  g_free (impl);
}

/* The name the cursor theme knows an X cursor font glyph by. */
static const gchar *
cursor_name (GdkCursorType type)
{
  switch (type)
    {
    case GDK_X_CURSOR: return "X_cursor";
    case GDK_ARROW: return "arrow";
    case GDK_BOTTOM_LEFT_CORNER: return "bottom_left_corner";
    case GDK_BOTTOM_RIGHT_CORNER: return "bottom_right_corner";
    case GDK_BOTTOM_SIDE: return "bottom_side";
    case GDK_CROSS: return "cross";
    case GDK_CROSSHAIR: return "crosshair";
    case GDK_FLEUR: return "fleur";
    case GDK_HAND1: return "hand1";
    case GDK_HAND2: return "hand2";
    case GDK_LEFT_PTR: return "left_ptr";
    case GDK_LEFT_SIDE: return "left_side";
    case GDK_PENCIL: return "pencil";
    case GDK_PLUS: return "plus";
    case GDK_QUESTION_ARROW: return "question_arrow";
    case GDK_RIGHT_PTR: return "right_ptr";
    case GDK_RIGHT_SIDE: return "right_side";
    case GDK_SB_H_DOUBLE_ARROW: return "sb_h_double_arrow";
    case GDK_SB_V_DOUBLE_ARROW: return "sb_v_double_arrow";
    case GDK_TOP_LEFT_CORNER: return "top_left_corner";
    case GDK_TOP_RIGHT_CORNER: return "top_right_corner";
    case GDK_TOP_SIDE: return "top_side";
    case GDK_WATCH: return "watch";
    case GDK_XTERM: return "xterm";
    default: return "left_ptr";
    }
}

GdkCursor *
_gdk_wayland_cursor_new (GdkCursorType cursor_type)
{
  GdkCursorPrivate *private;
  GdkCursorImpl *impl;
  struct wl_cursor *cursor = NULL;

  impl = g_new0 (GdkCursorImpl, 1);
  impl->ref_count = 1;
  if (_gdk_wl_cursor_theme)
    {
      cursor = wl_cursor_theme_get_cursor (_gdk_wl_cursor_theme, cursor_name (cursor_type));
      if (!cursor)
        cursor = wl_cursor_theme_get_cursor (_gdk_wl_cursor_theme, "left_ptr");
    }
  if (cursor && cursor->image_count > 0)
    {
      struct wl_cursor_image *image = cursor->images[0];

      impl->buffer = wl_cursor_image_get_buffer (image);
      impl->width = image->width;
      impl->height = image->height;
      impl->hotspot_x = image->hotspot_x;
      impl->hotspot_y = image->hotspot_y;
      impl->scale = _gdk_wayland_output_cursor_scale ();  /* the theme was loaded for it */
      impl->zoomed = FALSE;
    }

  private = g_new (GdkCursorPrivate, 1);
  private->impl = impl;
  private->cursor.type = cursor_type;
  return (GdkCursor *) private;
}

GdkCursor *
_gdk_wayland_cursor_new_from_pixmap (GdkPixmap *source, GdkPixmap *mask,
                                     GdkColor *fg, GdkColor *bg, gint x, gint y)
{
  GdkCursorPrivate *private;
  GdkCursorImpl *impl;
  GdkFramebuffer *sfb = _gdk_drawable_fb (source);
  GdkFramebuffer *mfb = mask ? _gdk_drawable_fb (mask) : NULL;
  GdkWaylandBuffer *buffer;
  gint px, py, each;

  g_return_val_if_fail (source != NULL, NULL);

  impl = g_new0 (GdkCursorImpl, 1);
  impl->ref_count = 1;
  /*
   * The buffer is the pixmap's pixels, each as many times over as
   * brings the pixmap to the screen's scale: a compositor asked to
   * enlarge a small buffer smooths it, and the cursor is blurred. The
   * buffer is the cursor's, as the compositor says when it has done
   * with one and what it says goes to the buffer: on the stack it was
   * gone by then, and the first cursor to be changed for another wrote
   * over whatever was there.
   */
  buffer = &impl->own;
  each = sfb ? MAX (gdk_screen_scale_factor () / sfb->scale, 1) : 1;
  if (sfb && _gdk_wayland_shm_buffer_new (buffer, sfb->width * each, sfb->height * each, TRUE))
    {
      for (py = 0; py < buffer->height; py++)
        for (px = 0; px < buffer->width; px++)
          {
            gint sx = px / each, sy = py / each;
            gint mx = mfb ? sx * mfb->scale / sfb->scale : 0;
            gint my = mfb ? sy * mfb->scale / sfb->scale : 0;
            gboolean on = !mfb || (mx < mfb->width && my < mfb->height &&
                                   (mfb->pixels[(gsize) my * mfb->stride + mx] & 1));
            gboolean set = sfb->pixels[(gsize) sy * sfb->stride + sx] & 1;
            GdkColor *c = set ? fg : bg;

            buffer->data[(gsize) py * buffer->width + px] = on
              ? 0xff000000 | ((c->red >> 8) << 16) | ((c->green >> 8) << 8) | (c->blue >> 8)
              : 0;
          }
      impl->buffer = buffer->buffer;
      impl->width = buffer->width;
      impl->height = buffer->height;
      impl->scale = sfb->scale * each;
      impl->hotspot_x = x * impl->scale;
      impl->hotspot_y = y * impl->scale;
      impl->zoomed = TRUE;      /* the program's own: as big as its windows */
    }

  private = g_new (GdkCursorPrivate, 1);
  private->impl = impl;
  private->cursor.type = GDK_CURSOR_IS_PIXMAP;
  return (GdkCursor *) private;
}

void
_gdk_wayland_cursor_destroy (GdkCursor *cursor)
{
  g_return_if_fail (cursor != NULL);

  cursor_impl_unref (GDK_CURSOR_IMPL (cursor));
  g_free (cursor);
}

void
_gdk_wayland_window_set_cursor (GdkWindow *window, GdkCursor *cursor)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;

  g_return_if_fail (window != NULL);

  if (private->destroyed)
    return;
  if (private->impl->cursor)
    cursor_impl_unref (private->impl->cursor);
  private->impl->cursor = cursor ? cursor_impl_ref (GDK_CURSOR_IMPL (cursor)) : NULL;
  _gdk_wayland_cursor_changed ((GdkWindow *) _gdk_window_native (private, NULL, NULL));
}

/* The cursor the pointer shows: the pointer window's, or an ancestor's. */
static void show_cursor_impl (GdkCursorImpl *cursor);

void
_gdk_wayland_set_cursor_on_pointer (void)
{
  GdkWindowPrivate *native = _gdk_wayland_pointer_window;
  GdkWindowPrivate *w;
  GdkCursorImpl *cursor = NULL;

  if (!_gdk_wl_pointer || !native || native->destroyed)
    return;
  for (w = native->pointer_window ? native->pointer_window : native; w;
       w = (GdkWindowPrivate *) w->parent)
    {
      if (w->impl && w->impl->cursor)
        {
          cursor = w->impl->cursor;
          break;
        }
      if (w == native)
        break;
    }
  if (!cursor && _gdk_wl_cursor_theme)
    {
      static GdkCursor *arrow = NULL;

      if (!arrow)
        arrow = _gdk_wayland_cursor_new (GDK_LEFT_PTR);
      cursor = GDK_CURSOR_IMPL (arrow);
    }
  show_cursor_impl (cursor);
}

/* A cursor on the pointer, as from a title bar: whatever the window. */
void
_gdk_wayland_show_cursor (GdkCursor *cursor)
{
  if (_gdk_wl_pointer && cursor)
    show_cursor_impl (GDK_CURSOR_IMPL (cursor));
}

static void
show_cursor_impl (GdkCursorImpl *cursor)
{
  gint scale;

  if (!cursor || !cursor->buffer)
    {
      wl_pointer_set_cursor (_gdk_wl_pointer, _gdk_wayland_pointer_serial, NULL, 0, 0);
      return;
    }
  wl_surface_attach (_gdk_wl_cursor_surface, cursor->buffer, 0, 0);
  /*
   * The theme's cursors are the desktop's and stay its size under a
   * zoom; a program's own cursor is drawn with its windows, and zooms.
   */
  scale = cursor->zoomed ? MAX (cursor->scale / _gdk_wayland_zoom, 1)
                         : cursor->scale;
  if (wl_proxy_get_version ((struct wl_proxy *) _gdk_wl_cursor_surface) >= 3)
    wl_surface_set_buffer_scale (_gdk_wl_cursor_surface, scale);
  wl_surface_damage (_gdk_wl_cursor_surface, 0, 0, cursor->width, cursor->height);
  wl_surface_commit (_gdk_wl_cursor_surface);
  /* The hotspot is in the surface's pixels. */
  wl_pointer_set_cursor (_gdk_wl_pointer, _gdk_wayland_pointer_serial,
                         _gdk_wl_cursor_surface,
                         cursor->hotspot_x / scale, cursor->hotspot_y / scale);
}

void
_gdk_wayland_cursor_changed (GdkWindow *native)
{
  if ((GdkWindowPrivate *) native == _gdk_wayland_pointer_window)
    _gdk_wayland_set_cursor_on_pointer ();
}

/* ---------------------------------------------------------------------
 * Geometry and the pointer
 */

void
_gdk_wayland_window_set_colormap (GdkWindow *window, GdkColormap *colormap)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;

  g_return_if_fail (window != NULL);
  g_return_if_fail (colormap != NULL);

  if (private->destroyed)
    return;
  gdk_colormap_ref (colormap);
  if (private->colormap)
    gdk_colormap_unref (private->colormap);
  private->colormap = colormap;
}

void
_gdk_wayland_window_get_geometry (GdkWindow *window, gint *x, gint *y,
                                  gint *width, gint *height, gint *depth)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;

  if (!window)
    private = &gdk_root_parent;
  if (x) *x = private->x;
  if (y) *y = private->y;
  if (width) *width = private->width;
  if (height) *height = private->height;
  if (depth) *depth = private->depth;
}

/*
 * Root coordinates are the toplevel's: a toplevel is at (0, 0), a
 * popup at where it was put relative to its toplevel.
 */
static void
root_origin (GdkWindowPrivate *private, gint *x, gint *y)
{
  gint ox, oy;
  GdkWindowPrivate *native = _gdk_window_native (private, &ox, &oy);

  /* A native child: its offset in its own native ancestor's frame. */
  while (native->window_type == GDK_WINDOW_CHILD && native->parent)
    {
      gint px, py;

      ox += native->x;
      oy += native->y;
      native = _gdk_window_native ((GdkWindowPrivate *) native->parent, &px, &py);
      ox += px;
      oy += py;
    }
  if (native->window_type == GDK_WINDOW_TEMP ||
      (native->impl && native->impl->group))
    {
      ox += native->x;
      oy += native->y;
    }
  *x = ox;
  *y = oy;
}

gint
_gdk_wayland_window_get_origin (GdkWindow *window, gint *x, gint *y)
{
  gint ox = 0, oy = 0;

  g_return_val_if_fail (window != NULL, 0);

  if (!((GdkWindowPrivate *) window)->destroyed)
    root_origin ((GdkWindowPrivate *) window, &ox, &oy);
  if (x) *x = ox;
  if (y) *y = oy;
  return 1;
}

gboolean
_gdk_wayland_window_get_deskrelative_origin (GdkWindow *window, gint *x, gint *y)
{
  return _gdk_wayland_window_get_origin (window, x, y);
}

void
_gdk_wayland_window_get_root_origin (GdkWindow *window, gint *x, gint *y)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;

  g_return_if_fail (window != NULL);

  while (private->parent && ((GdkWindowPrivate *) private->parent)->parent)
    private = (GdkWindowPrivate *) private->parent;
  if (private->window_type == GDK_WINDOW_TEMP || (private->impl && private->impl->group))
    {
      if (x) *x = private->x;
      if (y) *y = private->y;
    }
  else
    {
      if (x) *x = 0;
      if (y) *y = 0;
    }
}

GdkWindow *
_gdk_wayland_window_get_pointer (GdkWindow *window, gint *x, gint *y,
                                 GdkModifierType *mask)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  GdkWindowPrivate *native, *below, *pw;
  GdkWindow *result = NULL;
  gint ox, oy, px = 0, py = 0, pwx = 0, pwy = 0;

  if (!window)
    private = &gdk_root_parent;
  /*
   * The window the pointer is in and where in it. During a drag over
   * a window of ours that is the drag's, as the pointer has left.
   */
  pw = _gdk_wayland_drop_pointer (&pwx, &pwy);
  if (!pw && (pw = _gdk_wayland_pointer_window) && pw->impl)
    {
      pwx = pw->impl->pointer_x;
      pwy = pw->impl->pointer_y;
    }
  if (!private->destroyed)
    {
      native = _gdk_window_native (private, &ox, &oy);
      if (native == &gdk_root_parent && pw)
        {
          /* "The screen": the pointer's window, at root coordinates. */
          native = pw;
          root_origin (native, &ox, &oy);
          ox = -ox;
          oy = -oy;
        }
      if (native->impl)
        {
          px = native->impl->pointer_x - ox;
          py = native->impl->pointer_y - oy;
          if (pw == native)
            {
              px = pwx - ox;
              py = pwy - oy;
            }
          else if (pw && !pw->destroyed && pw->impl &&
                   native != &gdk_root_parent &&
                   gdk_window_get_toplevel ((GdkWindow *) pw) ==
                   gdk_window_get_toplevel ((GdkWindow *) private))
            {
              /*
               * The pointer is in another surface of the same toplevel
               * -- a native window inside this one, or the one this is
               * inside: where it is here follows from where the two are.
               */
              gint rx, ry, wx, wy;

              root_origin (pw, &rx, &ry);
              root_origin (private, &wx, &wy);
              px = rx + pwx - wx;
              py = ry + pwy - wy;
            }
        }
      below = _gdk_window_child_at (private, px, py);
      while (below && below != private && below->parent != window)
        below = (GdkWindowPrivate *) below->parent;
      if (below && below != private)
        result = (GdkWindow *) below;
    }
  if (x) *x = px;
  if (y) *y = py;
  if (mask) *mask = _gdk_wayland_modifiers | (_gdk_wayland_buttons_down () ? _gdk_wayland_button_mask () : 0);
  return result;
}

GdkWindow *
_gdk_wayland_window_at_pointer (gint *win_x, gint *win_y)
{
  /* a popup the pointer was held over, or the surface it is on */
  GdkWindowPrivate *native = _gdk_wayland_pointer_native ();
  GdkWindowPrivate *below;
  gint ox, oy;

  if (!native || native->destroyed ||
      (native == _gdk_wayland_pointer_window && !native->impl->pointer_in))
    {
      if (win_x) *win_x = -1;
      if (win_y) *win_y = -1;
      return NULL;
    }
  below = _gdk_window_child_at (native, native->impl->pointer_x, native->impl->pointer_y);
  _gdk_window_native (below, &ox, &oy);
  if (win_x) *win_x = native->impl->pointer_x - ox;
  if (win_y) *win_y = native->impl->pointer_y - oy;
  return (GdkWindow *) below;
}

void
_gdk_wayland_window_set_events (GdkWindow *window, GdkEventMask event_mask)
{
  g_return_if_fail (window != NULL);

  ((GdkWindowPrivate *) window)->event_mask = event_mask;
}

/*
 * A shaped window: what the mask leaves out is transparent, and takes
 * no input.
 */
void
_gdk_wayland_window_shape_combine_mask (GdkWindow *window, GdkBitmap *shape_mask,
                                        gint offset_x, gint offset_y)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  GdkWindowImpl *impl;
  gint i;

  g_return_if_fail (window != NULL);

  impl = private->impl;
  if (private->destroyed || !private->native || !impl->surface)
    return;
  if (impl->shape_mask)
    gdk_pixmap_unref (impl->shape_mask);
  impl->shape_mask = shape_mask ? gdk_pixmap_ref (shape_mask) : NULL;
  if (impl->shape)
    wl_region_destroy (impl->shape);
  impl->shape = NULL;

  if (shape_mask)
    {
      GdkFramebuffer *mfb = _gdk_drawable_fb (shape_mask);
      gint x, y;

      /*
       * The region is in the surface's pixels, which are the window's:
       * a mask at a scale above one is read at every scale-th pixel.
       */
      impl->shape = wl_compositor_create_region (_gdk_wl_compositor);
      if (mfb)
        for (y = 0; y < mfb->height / mfb->scale; y++)
          {
            gint start = -1, w = mfb->width / mfb->scale, s = mfb->scale;

            for (x = 0; x <= w; x++)
              {
                gboolean on = x < w &&
                  (mfb->pixels[(gsize) (y * s) * mfb->stride + x * s] & 1);

                if (on && start < 0)
                  start = x;
                else if (!on && start >= 0)
                  {
                    wl_region_add (impl->shape, GDK_WL_TO (start + offset_x),
                                   GDK_WL_TO (y + offset_y), GDK_WL_TO (x - start),
                                   GDK_WL_TO (1));
                    start = -1;
                  }
              }
          }
    }
  wl_surface_set_input_region (impl->surface, impl->shape);
  wl_surface_set_opaque_region (impl->surface, impl->shape);

  /* The buffers must change format: made again at the next commit,
   * which sends the whole picture with the new shape cut into it. */
  for (i = 0; i < 2; i++)
    if (!impl->buffers[i].busy)
      _gdk_wayland_shm_buffer_free (&impl->buffers[i]);
  impl->damage_all = TRUE;
  impl->commit_pending = TRUE;
}

void
_gdk_wayland_window_set_override_redirect (GdkWindow *window, gboolean override_redirect)
{
}

void
_gdk_wayland_window_set_icon (GdkWindow *window, GdkWindow *icon_window,
                              GdkPixmap *pixmap, GdkBitmap *mask)
{
}

void
_gdk_wayland_window_set_icon_name (GdkWindow *window, const gchar *name)
{
}

/*
 * The window's icon as pictures, which are premultiplied; those that
 * are not square are left out. With none, or none left, the window
 * has the icon the compositor finds for it again.
 */
void
_gdk_wayland_window_set_icon_images (GdkWindow *window, const GdkIconImage *images,
                                     gint n_images)
{
#ifdef HAVE_XDG_TOPLEVEL_ICON
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  GdkWaylandIcon *icon = NULL;

  if (private->destroyed || !private->impl || !_gdk_xdg_toplevel_icon_manager)
    return;
  if (n_images > 0)
    {
      icon = wayland_icon_new (NULL, images, n_images, FALSE);
      if (icon->n_buffers == 0)
        {
          wayland_icon_free (icon);
          icon = NULL;
        }
    }
  window_set_icon (private, icon, TRUE);
#endif
}

/*
 * The icon the program is installed with: its name, for the compositor
 * to find among the icons it has, and the pictures the toolkit finds
 * by that name, for one that cannot. It is made once for each name and
 * is every window's that is given it. A name that is a whole path is
 * no name to a compositor, and is given as its pictures alone.
 */
void
_gdk_wayland_window_set_themed_icon (GdkWindow *window, const gchar *name)
{
#ifdef HAVE_XDG_TOPLEVEL_ICON
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  GdkThemedIcon *themed;

  if (private->destroyed || !private->impl || !_gdk_xdg_toplevel_icon_manager)
    return;
  themed = _gdk_themed_icon (name);
  if (!themed->backend_data)
    {
      gboolean path = g_path_is_absolute (name);

      if (path && themed->n_images == 0)
        return;
      themed->backend_data = wayland_icon_new (path ? NULL : name, themed->images,
                                               themed->n_images, TRUE);
    }
  window_set_icon (private, themed->backend_data, FALSE);
#endif
}

/*
 * A window put in a group -- its leader's, or its own if it is the
 * leader -- or, with no leader, taken out of one. A window already
 * shown changes its surface's role, so it is shown again.
 */
void
_gdk_wayland_window_set_group (GdkWindow *window, GdkWindow *leader)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  GdkWindowPrivate *lead = (GdkWindowPrivate *) leader;
  GdkWaylandGroup *group = NULL;
  gboolean was_mapped;
  gint x, y;

  g_return_if_fail (window != NULL);

  if (private->destroyed || !_gdk_wl_subcompositor ||
      (private->window_type != GDK_WINDOW_TOPLEVEL && private->window_type != GDK_WINDOW_DIALOG))
    return;
  if (lead && (lead->destroyed || !lead->impl))
    lead = NULL;
  if (lead)
    {
      group = group_find (lead);
      if (!group)
        {
          group = g_new0 (GdkWaylandGroup, 1);
          group->leader = lead;
          group->members = g_list_append (NULL, lead);
          lead->impl->group = group;
          lead->x = lead->impl->asked_x;
          lead->y = lead->impl->asked_y;
          groups = g_list_prepend (groups, group);
        }
    }
  if (private->impl->group == group)
    return;

  /* Its position among the group is what it last asked for. */
  x = private->impl->asked_x;
  y = private->impl->asked_y;

  was_mapped = private->mapped;
  if (was_mapped)
    {
      private->mapped = FALSE;
      window_unmap (private);
    }
  /* A surface's role is for good: the window gets a new surface for its new one. */
  if (private->impl->surface)
    {
      wl_surface_destroy (private->impl->surface);
      native_windows = g_list_remove (native_windows, private);
      window_make_surface (private);
      if (private->impl->shape)
        {
          wl_surface_set_input_region (private->impl->surface, private->impl->shape);
          wl_surface_set_opaque_region (private->impl->surface, private->impl->shape);
        }
    }
  if (private->impl->group)
    {
      GdkWaylandGroup *old = private->impl->group;

      old->members = g_list_remove (old->members, private);
      if (!old->members)
        group_free (old);
      else
        _gdk_wayland_group_layout (old);
    }
  private->impl->group = group;
  if (group)
    {
      if (!g_list_find (group->members, private))
        group->members = g_list_append (group->members, private);
      private->x = x;
      private->y = y;
    }
  else
    {
      private->x = private->y = 0;
      /*
       * Broken out while being dragged: the compositor takes the
       * drag up as soon as the new toplevel is shown, and the
       * program's own drag ends.
       */
      if (was_mapped && _gdk_wayland_buttons_down () && _gdk_wayland_pointer_window == private)
        private->impl->want_move = TRUE;
    }
  if (was_mapped)
    {
      private->mapped = TRUE;
      window_map (private);
    }
}

void
_gdk_wayland_window_set_group_resize (GdkWindow *window, gboolean horizontally,
                                      gboolean vertically)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;

  g_return_if_fail (window != NULL);

  if (private->destroyed || !private->impl)
    return;
  private->impl->group_resize_h = horizontally != FALSE;
  private->impl->group_resize_v = vertically != FALSE;
  if (private->impl->group)
    _gdk_wayland_group_layout (private->impl->group);
}

/* The compositor moves the window from here: the group's host, if it is in one. */
gboolean
_gdk_wayland_window_begin_move_drag (GdkWindow *window, gint button,
                                     gint x_root, gint y_root, guint32 time)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  GdkWindowPrivate *native;
  struct xdg_toplevel *toplevel = NULL;

  g_return_val_if_fail (window != NULL, FALSE);

  if (private->destroyed || !_gdk_wl_seat)
    return FALSE;
  native = _gdk_window_native (private, NULL, NULL);
  if (native->impl->group && native->impl->group->leader != native)
    return FALSE;     /* among its group, the program moves it */
  toplevel = native->impl->xdg_toplevel;
  if (!toplevel)
    return FALSE;
  xdg_toplevel_move (toplevel, _gdk_wl_seat, _gdk_wayland_press_serial);
  wl_display_flush (_gdk_wl_display);
  _gdk_wayland_release_buttons ();
  return TRUE;
}

void
_gdk_wayland_window_set_decorations (GdkWindow *window, GdkWMDecoration decorations)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;

  g_return_if_fail (window != NULL);

  if (private->destroyed)
    return;
  private->impl->decorated = decorations != 0;
  window_apply_decoration (private);
  _gdk_wayland_decor_changed (private);
}

void
_gdk_wayland_window_set_functions (GdkWindow *window, GdkWMFunction functions)
{
}

/* Kept for the title bar, if the window comes to have one of ours. */
void
_gdk_wayland_window_set_title_colors (GdkWindow *window, const GdkColor *colors)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  gint i;

  if (private->destroyed || !private->impl)
    return;
  for (i = 0; i < GDK_TITLE_N_COLORS; i++)
    private->impl->title_colors[i] = (colors[i].red >> 8) << 16 |
      (colors[i].green >> 8) << 8 | colors[i].blue >> 8;
  private->impl->title_colors_set = TRUE;
  _gdk_wayland_decor_changed (private);
}

/*
 * Minimised, a group goes as one: it is one toplevel, its leader's.
 * The compositor may not show it again until the user does.
 */
void
_gdk_wayland_window_iconify (GdkWindow *window)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  GdkWindowPrivate *native;

  if (private->destroyed)
    return;
  native = _gdk_window_native (private, NULL, NULL);
  if (native->impl->group)
    native = native->impl->group->leader;
  if (!native || native->destroyed || !native->impl->xdg_toplevel)
    return;

  xdg_toplevel_set_minimized (native->impl->xdg_toplevel);
  wl_display_flush (_gdk_wl_display);
}

/*
 * xdg-shell gives a client no say in stacking, desktops or the
 * taskbar: the compositor decides, so these ask for nothing.
 */
void
_gdk_wayland_window_set_sticky (GdkWindow *window, gboolean sticky)
{
}

void
_gdk_wayland_window_set_keep_above (GdkWindow *window, gboolean setting)
{
}

void
_gdk_wayland_window_set_skip_taskbar_hint (GdkWindow *window, gboolean setting)
{
}

void
_gdk_wayland_window_register_dnd (GdkWindow *window)
{
}

/* ---------------------------------------------------------------------
 * gdkwayland.h: for programs that talk to the compositor themselves.
 */

struct wl_display *
gdk_wayland_display (void)
{
  return _gdk_wl_display;
}

struct wl_surface *
gdk_wayland_window_surface (GdkWindow *window)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;

  g_return_val_if_fail (window != NULL, NULL);

  if (!_gdk_wl_display || private->destroyed ||
      private->window_type == GDK_WINDOW_PIXMAP)
    return NULL;
  private = _gdk_window_native (private, NULL, NULL);
  return private->impl ? private->impl->surface : NULL;
}

gint
gdk_wayland_window_buffer_scale (GdkWindow *window)
{
  g_return_val_if_fail (window != NULL, 1);

  return MAX (gdk_window_get_scale_factor (window) / _gdk_wayland_zoom, 1);
}
