/* GDK - The GIMP Drawing Kit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
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
 * Modified by the GTK+ Team and others 1997-1999. See the AUTHORS
 * file for a list of people on the GTK+ Team. See the ChangeLog
 * files for a list of changes. These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/.
 */

/*
 * The part of a window that is GDK's own: its framebuffer, its
 * background, what has been drawn and not yet shown, and the bits of
 * the API that only look at what GDK keeps. Making the window, moving
 * it, mapping it and so on is the backend's (gdk/x11/gdkwindow-x11.c).
 *
 * A toplevel owns a framebuffer; its children are rectangles of it.
 * A child draws into that framebuffer at its offset, clipped to what
 * of it can be seen: its ancestors, the siblings stacked above it and,
 * unless the GC includes inferiors, its own children. So the
 * framebuffer holds the composed picture of the whole toplevel, which
 * is what a screen wants.
 *
 * The framebuffer is made when something first draws into it or the
 * backend first needs it, filled with every mapped window's
 * background, and thrown away when the toplevel changes size, the way
 * the X server forgot a window's contents. When a child is mapped,
 * moved, resized, restacked or hidden, the parts that come into view
 * are painted with their window's background, as the server did before
 * sending the expose that has the program draw them. Drawing records
 * damage; the backend publishes the damaged parts when there is
 * nothing more pressing left to do -- no event to hand out, nothing
 * of GTK+'s to resize or redraw -- or when the program asks with
 * gdk_flush. So what goes to the screen is the picture as it stands
 * once everything has answered a change, not each stage on the way
 * there: a window is not seen blank between being resized and being
 * drawn again. See gdk/gdkevents.c for when exactly.
 *
 * A native window inside another's has a framebuffer of its own, and
 * hides whatever of its parent's picture it covers. An external
 * window is a native one with no framebuffer at all: something else
 * draws it -- GL, say -- straight into the window system's window,
 * and all GDK does for it is keep it in its place.
 */

#include <stdlib.h>
#include "gdk.h"
#include "gdkprivate.h"
#include "gdkframebuffer.h"

/* Device pixels for each of a new native window's. */
static gint gdk_window_scale = 1;

/* Native windows with damage the backend has not been given yet. */
static GSList *damaged_windows = NULL;
/* Whether the main loop has come round and found them there, and
 * when it first did. */
static gboolean damage_waiting = FALSE;
static GTimeVal damaged_since;

void
_gdk_window_private_init (GdkWindowPrivate *private)
{
  private->scale = gdk_window_scale;
  private->fb = NULL;
  private->damage = NULL;
  private->bg_color.pixel = 0;
  private->bg_color.red = private->bg_color.green = private->bg_color.blue = 0;
  private->bg_pixmap = NULL;
  private->bg_parent_relative = FALSE;
  private->native = FALSE;
  private->external = FALSE;
  private->external_release = NULL;
  private->external_data = NULL;
  private->event_mask = 0;
  private->pointer_window = NULL;
  private->impl = NULL;
}

void
_gdk_window_external_release (GdkWindowPrivate *private)
{
  GDestroyNotify release = private->external_release;
  gpointer data = private->external_data;

  private->external_release = NULL;
  private->external_data = NULL;
  if (release)
    release (data);
}

void
_gdk_window_private_free (GdkWindowPrivate *private)
{
  _gdk_window_external_release (private);
  damaged_windows = g_slist_remove (damaged_windows, private);
  _gdk_framebuffer_free (private->fb);
  private->fb = NULL;
  if (private->damage)
    gdk_region_destroy (private->damage);
  private->damage = NULL;
  if (private->bg_pixmap)
    gdk_pixmap_unref (private->bg_pixmap);
  private->bg_pixmap = NULL;
  if (private->pointer_window)
    gdk_window_unref ((GdkWindow *) private->pointer_window);
  private->pointer_window = NULL;
}

/* A window that is a rectangle of another's framebuffer, not its own. */
#define IS_CHILD(p) (!(p)->native && (p)->parent)

/* A native window inside another window: it covers part of that one's
 * picture, with a picture of its own. */
#define IS_NATIVE_CHILD(p) \
  ((p)->native && (p)->window_type == GDK_WINDOW_CHILD && (p)->parent && \
   ((GdkWindowPrivate *) (p)->parent)->window_type != GDK_WINDOW_ROOT)

/* Whether a window has pixels: mapped, as are all its ancestors. */
static gboolean
window_viewable (GdkWindowPrivate *private)
{
  while (private && IS_CHILD (private))
    {
      if (!private->mapped)
        return FALSE;
      private = (GdkWindowPrivate *) private->parent;
    }
  return private && private->mapped;
}

/*
 * The window whose framebuffer this window draws into, and where this
 * window's origin is in it, in window pixels.
 */
GdkWindowPrivate *
_gdk_window_native (GdkWindowPrivate *private, gint *x, gint *y)
{
  gint ox = 0, oy = 0;

  while (IS_CHILD (private))
    {
      ox += private->x;
      oy += private->y;
      private = (GdkWindowPrivate *) private->parent;
    }
  if (x)
    *x = ox;
  if (y)
    *y = oy;
  return private;
}

/* A window's rectangle in the device pixels of its native framebuffer. */
static void
window_rect (GdkWindowPrivate *private, GdkRectangle *rect)
{
  gint x, y;
  GdkWindowPrivate *native = _gdk_window_native (private, &x, &y);
  gint scale = native->scale;

  rect->x = x * scale;
  rect->y = y * scale;
  rect->width = private->width * scale;
  rect->height = private->height * scale;
}

/*
 * A window's rectangle in the device pixels of its parent's native
 * framebuffer: what it hides of its parent. The same as window_rect
 * unless the window is native, when its own framebuffer starts at its
 * own corner.
 */
static void
window_rect_in_parent (GdkWindowPrivate *private, GdkRectangle *rect)
{
  gint x, y;
  GdkWindowPrivate *native =
    _gdk_window_native ((GdkWindowPrivate *) private->parent, &x, &y);
  gint scale = native->scale;

  rect->x = (x + private->x) * scale;
  rect->y = (y + private->y) * scale;
  rect->width = private->width * scale;
  rect->height = private->height * scale;
}

/*
 * The part of a window that can be seen, in device pixels: what its
 * ancestors show of it, less the mapped siblings stacked above it at
 * every level, less its own mapped children unless they are included.
 * A child with no pixels of its own (input only) hides nothing.
 */
GdkRegion *
_gdk_window_visible_region (GdkWindowPrivate *private,
                            gboolean          include_inferiors)
{
  GdkRegion *region = gdk_region_new ();
  GdkWindowPrivate *w, *parent;
  GdkRectangle rect;
  GList *l;

  if (private->destroyed || private->depth == 0 || !window_viewable (private))
    return region;

  window_rect (private, &rect);
  _gdk_region_add_rect (region, &rect);

  for (w = private; IS_CHILD (w); w = parent)
    {
      parent = (GdkWindowPrivate *) w->parent;
      window_rect (parent, &rect);
      _gdk_region_intersect_rect (region, &rect);

      /* Siblings before w in the list are above it. */
      for (l = parent->children; l && l->data != w; l = l->next)
        {
          GdkWindowPrivate *sibling = l->data;

          if (sibling->mapped && !sibling->destroyed && sibling->depth != 0)
            {
              window_rect_in_parent (sibling, &rect);
              _gdk_region_subtract_rect (region, &rect);
            }
        }
    }

  if (!include_inferiors)
    for (l = private->children; l; l = l->next)
      {
        GdkWindowPrivate *child = l->data;

        if (child->mapped && !child->destroyed && child->depth != 0)
          {
            window_rect_in_parent (child, &rect);
            _gdk_region_subtract_rect (region, &rect);
          }
      }

  return region;
}

/*
 * The background a window shows: its own, or for a parent relative
 * background the nearest ancestor's that has one, tiled from that
 * window's origin. Painted over region, in device pixels of the
 * native framebuffer.
 */
static void
paint_background_region (GdkWindowPrivate *private, GdkRegion *region)
{
  GdkWindowPrivate *native, *source = private;
  GdkFramebuffer *fb;
  GdkPaint paint;
  gint x, y;

  native = _gdk_window_native (private, NULL, NULL);
  fb = native->fb;
  if (!fb || gdk_region_empty (region))
    return;

  while (source->bg_parent_relative && IS_CHILD (source) && source->parent)
    source = (GdkWindowPrivate *) source->parent;
  _gdk_window_native (source, &x, &y);

  if (!_gdk_paint_init (&paint, fb, NULL, 0, 0, region))
    return;
  if (source->bg_pixmap)
    {
      paint.fill = GDK_TILED;
      paint.tile = _gdk_drawable_fb (source->bg_pixmap);
      paint.ts_x = x * fb->scale;
      paint.ts_y = y * fb->scale;
    }
  else
    paint.fg = GDK_FB_PIXEL (fb, source->bg_color.pixel);
  _gdk_paint_rectangle (&paint, TRUE,
                        GDK_REGION_EXTENTS (paint.clip).x / fb->scale,
                        GDK_REGION_EXTENTS (paint.clip).y / fb->scale,
                        (GDK_REGION_EXTENTS (paint.clip).width + fb->scale - 1) / fb->scale,
                        (GDK_REGION_EXTENTS (paint.clip).height + fb->scale - 1) / fb->scale);
  _gdk_window_damage (native, &paint.damage);
  _gdk_paint_finish (&paint);
}

/*
 * The backgrounds of a window and its mapped children, bottom up, but
 * only within limit (device pixels; NULL for everywhere). A native
 * child is another framebuffer, painted when that one is made.
 */
static void
paint_backgrounds (GdkWindowPrivate *private, GdkRegion *limit, gboolean expose)
{
  GdkRegion *region;
  GList *l;

  if (private->destroyed || private->depth == 0 || !private->mapped)
    return;

  region = _gdk_window_visible_region (private, FALSE);
  if (limit)
    {
      GdkRegion *both = gdk_regions_intersect (region, limit);
      gdk_region_destroy (region);
      region = both;
    }
  paint_background_region (private, region);
  if (expose)
    _gdk_window_expose_region (private, region);
  gdk_region_destroy (region);

  for (l = g_list_last (private->children); l; l = l->prev)
    if (!((GdkWindowPrivate *) l->data)->native)
      paint_backgrounds (l->data, limit, expose);
}

gboolean
_gdk_window_fb_ensure (GdkWindowPrivate *private)
{
  GdkWindowPrivate *native;

  if (private->destroyed || private->depth == 0)
    return FALSE;

  native = _gdk_window_native (private, NULL, NULL);
  if (native->fb)
    return TRUE;
  if (native->destroyed || native->external)
    return FALSE;

  native->fb = _gdk_framebuffer_new (native->width, native->height,
                                     native->depth, native->scale);

  if (native->window_type != GDK_WINDOW_PIXMAP)
    {
      if (native->mapped)
        paint_backgrounds (native, NULL, FALSE);
      else
        {
          GdkRegion *all = gdk_region_new ();
          GdkRectangle rect;

          rect.x = rect.y = 0;
          rect.width = native->fb->width;
          rect.height = native->fb->height;
          _gdk_region_add_rect (all, &rect);
          paint_background_region (native, all);
          gdk_region_destroy (all);
        }
    }

  return TRUE;
}

GdkFramebuffer *
_gdk_drawable_fb (GdkDrawable *drawable)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) drawable;

  if (!_gdk_window_fb_ensure (private))
    return NULL;
  return _gdk_window_native (private, NULL, NULL)->fb;
}

/*
 * The backend says a native window is another size. The window starts
 * again from its background, but keeps what it had drawn where that
 * still fits: a program may have drawn at the new size before the
 * system said the window had it, and the system's exposes, which have
 * the program draw the rest, may never reach a program that is busy
 * resizing.
 */
void
_gdk_window_resized (GdkWindow *window)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  GdkFramebuffer *old;
  GdkRegion *clip;
  GdkRectangle keep;
  GdkPaint paint;

  old = private->fb;
  if (!old ||
      (old->width == private->width * old->scale &&
       old->height == private->height * old->scale))
    return;

  private->fb = NULL;
  if (private->damage)
    _gdk_region_clear (private->damage);

  if (_gdk_window_fb_ensure (private) && private->fb->scale == old->scale)
    {
      keep.x = keep.y = 0;
      keep.width = MIN (old->width, private->fb->width);
      keep.height = MIN (old->height, private->fb->height);
      clip = _gdk_region_from_rect (&keep);
      if (_gdk_paint_init (&paint, private->fb, NULL, 0, 0, clip))
        {
          _gdk_paint_copy (&paint, old, 0, 0, 0, 0,
                           keep.width / old->scale, keep.height / old->scale);
          _gdk_window_damage (private, &paint.damage);
          _gdk_paint_finish (&paint);
        }
      gdk_region_destroy (clip);
    }

  _gdk_framebuffer_free (old);
}

/* ---------------------------------------------------------------------
 * Scale
 */

gint
_gdk_scale_override (void)
{
  static gint override = -1;

  if (override < 0)
    {
      const gchar *env = getenv ("GDK_SCALE");

      override = env ? atoi (env) : 0;
      if (override < 1 || override > 16)
        override = 0;
    }
  return override;
}

void
_gdk_window_set_default_scale (gint scale)
{
  if (_gdk_scale_override ())
    scale = _gdk_scale_override ();
  gdk_window_scale = CLAMP (scale, 1, 16);
}

gint
gdk_screen_scale_factor (void)
{
  return gdk_window_scale;
}

gint
gdk_window_get_scale_factor (GdkWindow *window)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;

  g_return_val_if_fail (window != NULL, 1);

  if (private->window_type == GDK_WINDOW_PIXMAP)
    return private->fb ? private->fb->scale : 1;
  return _gdk_window_native (private, NULL, NULL)->scale;
}

/*
 * The backend says a native window is at another scale now. Nothing
 * drawn survives that -- the text has to be rasterised again -- so the
 * framebuffer starts again from the backgrounds and the program is
 * asked to draw everything.
 */
void
_gdk_window_set_scale (GdkWindow *window,
                       gint       scale)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  GdkRectangle all;

  g_return_if_fail (window != NULL);
  g_return_if_fail (scale >= 1);

  if (private->destroyed || !private->native || private->scale == scale)
    return;

  private->scale = scale;
  if (private->external)
    {
      GdkEvent event;

      /*
       * Nothing of GDK's to remake. Its owner has another number of
       * pixels to draw, and hears of that as of any change of size,
       * before being asked to draw them.
       */
      event.configure.type = GDK_CONFIGURE;
      event.configure.window = window;
      event.configure.send_event = FALSE;
      event.configure.x = private->x;
      event.configure.y = private->y;
      event.configure.width = private->width;
      event.configure.height = private->height;
      gdk_window_ref (window);
      gdk_event_put (&event);
      gdk_window_unref (window);

      all.x = all.y = 0;
      all.width = private->width;
      all.height = private->height;
      if (private->mapped)
        _gdk_window_expose_split (private, &all);
      return;
    }
  if (!private->fb)
    return;

  _gdk_framebuffer_free (private->fb);
  private->fb = NULL;
  if (private->damage)
    _gdk_region_clear (private->damage);
  if (!_gdk_window_fb_ensure (private))
    return;

  all.x = all.y = 0;
  all.width = private->width;
  all.height = private->height;
  if (private->mapped)
    _gdk_window_expose_split (private, &all);
}

/*
 * A child has moved or changed size. What it uncovered shows its
 * parent's background; what it newly covers shows its own; a moved
 * window takes its pixels with it, and a resized one keeps them in
 * place if it asked for static bit gravity, else starts again from
 * its background, as the server did.
 */
void
_gdk_window_geometry_changed (GdkWindow *window,
                              gint old_x, gint old_y,
                              gint old_width, gint old_height)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  GdkWindowPrivate *native, *parent;
  GdkRegion *old_region, *new_region, *vacated;
  GdkRectangle old_rect, new_rect;
  gint ox, oy, scale;
  gboolean moved = old_x != private->x || old_y != private->y;
  gboolean resized = old_width != private->width || old_height != private->height;
  gboolean expose = !private->native;   /* else the system exposes */

  _gdk_window_pointer_stale (private);
  if (IS_NATIVE_CHILD (private))
    {
      /*
       * Its own picture is its own affair; what it no longer covers is
       * its parent's to show again.
       */
      if (private->destroyed || !private->mapped || (!moved && !resized))
        return;
      parent = (GdkWindowPrivate *) private->parent;
      native = _gdk_window_native (parent, &ox, &oy);
      if (!native->fb)
        return;
      scale = native->fb->scale;
      old_rect.x = (ox + old_x) * scale;
      old_rect.y = (oy + old_y) * scale;
      old_rect.width = old_width * scale;
      old_rect.height = old_height * scale;
      window_rect_in_parent (private, &new_rect);
      old_region = _gdk_region_from_rect (&old_rect);
      new_region = _gdk_region_from_rect (&new_rect);
      vacated = gdk_regions_subtract (old_region, new_region);
      paint_backgrounds (parent, vacated, TRUE);
      gdk_region_destroy (vacated);
      gdk_region_destroy (old_region);
      gdk_region_destroy (new_region);
      return;
    }

  if (!IS_CHILD (private) || private->destroyed || !private->mapped)
    return;
  native = _gdk_window_native (private, &ox, &oy);
  if (!native->fb)
    return;
  scale = native->fb->scale;
  if (!moved && !resized)
    return;

  parent = (GdkWindowPrivate *) private->parent;
  old_rect.x = (ox - private->x + old_x) * scale;
  old_rect.y = (oy - private->y + old_y) * scale;
  old_rect.width = old_width * scale;
  old_rect.height = old_height * scale;
  window_rect (private, &new_rect);

  new_region = _gdk_window_visible_region (private, TRUE);

  if (moved && !resized)
    {
      GdkRegion *old_visible, *copied, *rest;
      GdkPaint paint;
      gint w = private->width, h = private->height;

      /*
       * The pixels go with the window -- those that were its: what the
       * old position showed of it, as the server copied. Whatever was
       * hidden then is painted afresh, as the server exposed it.
       */
      private->x = old_x;
      private->y = old_y;
      old_visible = _gdk_window_visible_region (private, TRUE);
      private->x = old_x + (new_rect.x - old_rect.x) / scale;
      private->y = old_y + (new_rect.y - old_rect.y) / scale;
      gdk_region_offset (old_visible, new_rect.x - old_rect.x, new_rect.y - old_rect.y);
      copied = gdk_regions_intersect (new_region, old_visible);
      rest = gdk_regions_subtract (new_region, copied);

      if (_gdk_paint_init (&paint, native->fb, NULL, 0, 0, copied))
        {
          _gdk_paint_copy (&paint, native->fb,
                           old_rect.x / scale, old_rect.y / scale,
                           new_rect.x / scale, new_rect.y / scale, w, h);
          _gdk_window_damage (native, &paint.damage);
          _gdk_paint_finish (&paint);
        }
      paint_backgrounds (private, rest, expose);
      gdk_region_destroy (rest);
      gdk_region_destroy (copied);
      gdk_region_destroy (old_visible);
    }
  else
    paint_backgrounds (private, new_region, expose);

  /* What the window left behind belongs to the parent's picture now. */
  old_region = _gdk_region_from_rect (&old_rect);
  vacated = gdk_regions_subtract (old_region, new_region);
  paint_backgrounds (parent, vacated, expose);
  gdk_region_destroy (vacated);
  gdk_region_destroy (old_region);
  gdk_region_destroy (new_region);
}

/* A child was mapped or unmapped: its area shows it, or its parent. */
void
_gdk_window_mapped (GdkWindow *window, gboolean mapped)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  GdkWindowPrivate *native;
  GdkRegion *region;
  GdkRectangle rect;

  _gdk_window_pointer_stale (private);
  if (private->destroyed)
    return;
  if (!mapped)
    _gdk_pointer_grab_window_hidden (private);

  if (mapped)
    {
      native = _gdk_window_native (private, NULL, NULL);
      if (native->fb)
        paint_backgrounds (private, NULL, !private->native);
      return;
    }

  /* Hidden: where it was shows its parent again, native or not. */
  if (!IS_CHILD (private) && !IS_NATIVE_CHILD (private))
    return;
  native = _gdk_window_native ((GdkWindowPrivate *) private->parent, NULL, NULL);
  if (!native->fb)
    return;
  window_rect_in_parent (private, &rect);
  region = _gdk_region_from_rect (&rect);
  paint_backgrounds ((GdkWindowPrivate *) private->parent, region, TRUE);
  gdk_region_destroy (region);
}

/*
 * A child was raised or lowered: whatever of it and its siblings is
 * newly in view starts from its background, and the server's exposes
 * have the program draw it.
 */
void
_gdk_window_restacked (GdkWindow *window)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  GdkWindowPrivate *native;
  GdkRegion *region;
  GdkRectangle rect;

  _gdk_window_pointer_stale (private);
  if (!IS_CHILD (private) || private->destroyed || !private->mapped)
    return;
  native = _gdk_window_native (private, NULL, NULL);
  if (!native->fb)
    return;

  window_rect (private, &rect);
  region = _gdk_region_from_rect (&rect);
  paint_backgrounds ((GdkWindowPrivate *) private->parent, region,
                     !private->native);
  gdk_region_destroy (region);
}

/* A native window is among those with something to publish. */
static void
window_wants_publish (GdkWindowPrivate *private)
{
  if (!private->damage)
    private->damage = gdk_region_new ();
  if (!g_slist_find (damaged_windows, private))
    damaged_windows = g_slist_prepend (damaged_windows, private);
}

/*
 * The backend has something of a window's to show that no drawing
 * brought about, and would show it together with the window's picture:
 * the window is published with the next that are, though nothing of
 * its picture may have changed.
 */
void
_gdk_window_publish_soon (GdkWindowPrivate *private)
{
  private = _gdk_window_native (private, NULL, NULL);
  if (private->window_type != GDK_WINDOW_PIXMAP && !private->destroyed)
    window_wants_publish (private);
}

/*
 * For how many milliseconds the main loop, which asks every time
 * round, has kept something waiting to be published; less than
 * nothing if nothing is waiting. The time a program takes over its
 * drawing is not the main loop's, and is not counted.
 */
glong
_gdk_windows_waiting (const GTimeVal *now)
{
  glong waited;

  if (!damaged_windows)
    return -1;
  if (!damage_waiting)
    {
      damage_waiting = TRUE;
      damaged_since = *now;
    }
  waited = (now->tv_sec - damaged_since.tv_sec) * 1000 +
    (now->tv_usec - damaged_since.tv_usec) / 1000;
  if (waited < 0)
    {
      /* The clock was put back. */
      damaged_since = *now;
      waited = 0;
    }
  return waited;
}

/* rect is in device pixels of the native framebuffer. */
void
_gdk_window_damage (GdkWindowPrivate *private, const GdkRectangle *rect)
{
  private = _gdk_window_native (private, NULL, NULL);

  if (private->window_type == GDK_WINDOW_PIXMAP)
    {
      /* Only whether it changed matters, for a copy the backend keeps. */
      if (!private->damage)
        private->damage = gdk_region_new ();
      if (gdk_region_empty (private->damage))
        {
          GdkRectangle whole;

          whole.x = whole.y = 0;
          whole.width = private->fb ? private->fb->width : private->width;
          whole.height = private->fb ? private->fb->height : private->height;
          _gdk_region_add_rect (private->damage, &whole);
        }
      return;
    }

  if (!private->damage || gdk_region_empty (private->damage))
    window_wants_publish (private);
  _gdk_region_add_rect (private->damage, rect);

  /* Too many pieces cost more than the pixels between them. */
  if (GDK_REGION_N_RECTS (private->damage) > 32)
    {
      GdkRectangle extents = GDK_REGION_EXTENTS (private->damage);

      _gdk_region_clear (private->damage);
      _gdk_region_add_rect (private->damage, &extents);
    }
}

/*
 * A rectangle of the window, in its own pixels, to be put on the
 * screen again though nothing has drawn in it: for an expose. It
 * goes with whatever else is waiting to, not before: the window
 * system exposes what a change has brought into view, and the program
 * may not have drawn that yet.
 */
void
_gdk_window_publish (GdkWindow *window, const GdkRectangle *rect)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  GdkWindowPrivate *native;
  GdkRectangle dev, bounds;
  gint ox, oy;

  if (private->window_type == GDK_WINDOW_PIXMAP || !window_viewable (private))
    return;
  if (!_gdk_window_fb_ensure (private))
    return;
  native = _gdk_window_native (private, &ox, &oy);

  dev.x = (rect->x + ox) * native->fb->scale;
  dev.y = (rect->y + oy) * native->fb->scale;
  dev.width = rect->width * native->fb->scale;
  dev.height = rect->height * native->fb->scale;
  bounds.x = bounds.y = 0;
  bounds.width = native->fb->width;
  bounds.height = native->fb->height;
  if (!gdk_rectangle_intersect (&dev, &bounds, &dev))
    return;

  _gdk_window_damage (native, &dev);
}

/* Everything that waits to be published is; whether anything did. */
gboolean
_gdk_windows_flush (void)
{
  gboolean any = damaged_windows != NULL;

  damage_waiting = FALSE;
  while (damaged_windows)
    {
      GdkWindowPrivate *private = damaged_windows->data;
      GdkRegion *damage;
      gint i;

      damaged_windows = g_slist_remove (damaged_windows, private);
      damage = private->damage;
      private->damage = NULL;
      if (!damage)
        continue;

      if (!private->destroyed && private->mapped && private->fb)
        for (i = 0; i < GDK_REGION_N_RECTS (damage); i++)
          _gdk_backend_publish ((GdkWindow *) private,
                                &GDK_REGION_RECTS (damage)[i]);
      /* That was all of this window: it can be shown. */
      if (!private->destroyed)
        _gdk_backend_publish ((GdkWindow *) private, NULL);

      gdk_region_destroy (damage);
    }

  return any;
}

static void
_gdk_window_clear_rect (GdkWindowPrivate *private,
                        gint x, gint y, gint width, gint height)
{
  GdkWindowPrivate *native;
  GdkRegion *region;
  GdkRectangle rect;
  gint ox, oy;

  if (private->window_type == GDK_WINDOW_PIXMAP)
    return;
  if (!_gdk_window_fb_ensure (private))
    return;
  native = _gdk_window_native (private, &ox, &oy);

  /* As for X, a zero width or height means to the far edge. */
  if (width <= 0)
    width = private->width - x;
  if (height <= 0)
    height = private->height - y;

  rect.x = (x + ox) * native->fb->scale;
  rect.y = (y + oy) * native->fb->scale;
  rect.width = width * native->fb->scale;
  rect.height = height * native->fb->scale;

  region = _gdk_window_visible_region (private, FALSE);
  _gdk_region_intersect_rect (region, &rect);
  paint_background_region (private, region);
  gdk_region_destroy (region);
}

/* ---------------------------------------------------------------------
 * External windows
 */

void
gdk_window_set_external (GdkWindow      *window,
                         GDestroyNotify  release,
                         gpointer        data)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;

  g_return_if_fail (window != NULL);
  /* A toplevel is the window system's and GDK's to agree about. */
  g_return_if_fail (private->window_type == GDK_WINDOW_CHILD);

  if (private->destroyed)
    return;
  gdk_window_ensure_native (window);
  if (!private->native)
    return;

  /* Whoever had it before lets go. */
  _gdk_window_external_release (private);
  private->external_release = release;
  private->external_data = data;
  if (private->external)
    return;

  private->external = TRUE;
  damaged_windows = g_slist_remove (damaged_windows, private);
  _gdk_framebuffer_free (private->fb);
  private->fb = NULL;
  if (private->damage)
    gdk_region_destroy (private->damage);
  private->damage = NULL;
}

void
gdk_window_unset_external (GdkWindow *window)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  GdkRectangle all;

  g_return_if_fail (window != NULL);

  if (!private->external)
    return;
  _gdk_window_external_release (private);
  private->external = FALSE;

  /* GDK's again: the program is asked to draw it. */
  if (!private->destroyed && private->mapped)
    {
      all.x = all.y = 0;
      all.width = private->width;
      all.height = private->height;
      _gdk_window_expose_split (private, &all);
    }
}

gboolean
gdk_window_is_external (GdkWindow *window)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;

  g_return_val_if_fail (window != NULL, FALSE);

  return private->external && !private->destroyed;
}

/* ---------------------------------------------------------------------- */

GdkWindow*
gdk_window_ref (GdkWindow *window)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *)window;
  g_return_val_if_fail (window != NULL, NULL);

  private->ref_count += 1;
  return window;
}

void
gdk_window_unref (GdkWindow *window)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *)window;
  g_return_if_fail (window != NULL);
  g_return_if_fail (private->ref_count > 0);

  private->ref_count -= 1;
  if (private->ref_count == 0)
    {
      if (!private->destroyed && private->window_type != GDK_WINDOW_FOREIGN)
        g_warning ("losing last reference to undestroyed window\n");
      _gdk_window_private_free (private);
      _gdk_backend_window_free (private);
      g_dataset_destroy (window);
      g_free (window);
    }
}

void
gdk_window_clear (GdkWindow *window)
{
  GdkWindowPrivate *private;

  g_return_if_fail (window != NULL);

  private = (GdkWindowPrivate*) window;

  if (!private->destroyed)
    _gdk_window_clear_rect (private, 0, 0, 0, 0);
}

void
gdk_window_clear_area (GdkWindow *window,
                       gint       x,
                       gint       y,
                       gint       width,
                       gint       height)
{
  GdkWindowPrivate *private;

  g_return_if_fail (window != NULL);

  private = (GdkWindowPrivate*) window;

  if (!private->destroyed)
    _gdk_window_clear_rect (private, x, y, width, height);
}

void
gdk_window_clear_area_e (GdkWindow *window,
                         gint       x,
                         gint       y,
                         gint       width,
                         gint       height)
{
  GdkWindowPrivate *private;

  g_return_if_fail (window != NULL);

  private = (GdkWindowPrivate*) window;

  if (!private->destroyed)
    {
      GdkWindowPrivate *native;
      GdkRegion *region;
      GdkRectangle rect;
      gint ox, oy, scale;

      _gdk_window_clear_rect (private, x, y, width, height);

      /*
       * And an expose for what was cleared, as the server sent one.
       * An external window has nothing to clear and is exposed all
       * the same: it is how its owner is asked to draw it again.
       */
      native = _gdk_window_native (private, &ox, &oy);
      if (!native->external && !_gdk_window_fb_ensure (private))
        return;
      scale = native->scale;
      if (width <= 0)
        width = private->width - x;
      if (height <= 0)
        height = private->height - y;
      rect.x = (x + ox) * scale;
      rect.y = (y + oy) * scale;
      rect.width = width * scale;
      rect.height = height * scale;
      region = _gdk_window_visible_region (private, FALSE);
      _gdk_region_intersect_rect (region, &rect);
      _gdk_window_expose_region (private, region);
      gdk_region_destroy (region);
    }
}

void
gdk_window_copy_area (GdkWindow    *window,
                      GdkGC        *gc,
                      gint          x,
                      gint          y,
                      GdkWindow    *source_window,
                      gint          source_x,
                      gint          source_y,
                      gint          width,
                      gint          height)
{
  g_return_if_fail (window != NULL);
  g_return_if_fail (gc != NULL);

  if (source_window == NULL)
    source_window = window;

  gdk_draw_pixmap (window, gc, source_window, source_x, source_y,
                   x, y, width, height);
}

void
gdk_window_set_user_data (GdkWindow *window,
                          gpointer   user_data)
{
  g_return_if_fail (window != NULL);

  window->user_data = user_data;
}

void
gdk_window_set_background (GdkWindow *window,
                           GdkColor  *color)
{
  GdkWindowPrivate *private;

  g_return_if_fail (window != NULL);
  g_return_if_fail (color != NULL);

  private = (GdkWindowPrivate*) window;
  if (private->destroyed)
    return;

  private->bg_color = *color;
  private->bg_parent_relative = FALSE;
  if (private->bg_pixmap)
    gdk_pixmap_unref (private->bg_pixmap);
  private->bg_pixmap = NULL;
}

void
gdk_window_set_back_pixmap (GdkWindow *window,
                            GdkPixmap *pixmap,
                            gboolean   parent_relative)
{
  GdkWindowPrivate *private;

  g_return_if_fail (window != NULL);

  private = (GdkWindowPrivate*) window;
  if (private->destroyed)
    return;

  if (pixmap && !parent_relative)
    gdk_pixmap_ref (pixmap);
  if (private->bg_pixmap)
    gdk_pixmap_unref (private->bg_pixmap);
  private->bg_pixmap = parent_relative ? NULL : pixmap;
  private->bg_parent_relative = parent_relative;
}

void
gdk_window_get_user_data (GdkWindow *window,
                          gpointer  *data)
{
  g_return_if_fail (window != NULL);

  *data = window->user_data;
}

void
gdk_window_get_size (GdkWindow *window,
                     gint       *width,
                     gint       *height)
{
  GdkWindowPrivate *window_private;

  g_return_if_fail (window != NULL);

  window_private = (GdkWindowPrivate*) window;

  if (width)
    *width = window_private->width;
  if (height)
    *height = window_private->height;
}

void
gdk_window_get_position (GdkWindow *window,
                         gint      *x,
                         gint      *y)
{
  GdkWindowPrivate *window_private;

  g_return_if_fail (window != NULL);

  window_private = (GdkWindowPrivate*) window;

  if (x)
    *x = window_private->x;
  if (y)
    *y = window_private->y;
}

GdkVisual*
gdk_window_get_visual (GdkWindow *window)
{
  g_return_val_if_fail (window != NULL, NULL);

  return gdk_visual_get_system ();
}

GdkColormap*
gdk_window_get_colormap (GdkWindow *window)
{
  GdkWindowPrivate *window_private;

  g_return_val_if_fail (window != NULL, NULL);
  window_private = (GdkWindowPrivate*) window;

  g_return_val_if_fail (window_private->window_type != GDK_WINDOW_PIXMAP, NULL);
  if (window_private->destroyed)
    return NULL;

  if (window_private->colormap)
    return window_private->colormap;
  return gdk_colormap_get_system ();
}

GdkWindowType
gdk_window_get_type (GdkWindow *window)
{
  GdkWindowPrivate *window_private;

  g_return_val_if_fail (window != NULL, (GdkWindowType) -1);

  window_private = (GdkWindowPrivate*) window;
  return window_private->window_type;
}

GdkWindow*
gdk_window_get_parent (GdkWindow *window)
{
  g_return_val_if_fail (window != NULL, NULL);

  return ((GdkWindowPrivate*) window)->parent;
}

GdkWindow*
gdk_window_get_toplevel (GdkWindow *window)
{
  GdkWindowPrivate *private;

  g_return_val_if_fail (window != NULL, NULL);

  private = (GdkWindowPrivate*) window;

  while (private->window_type == GDK_WINDOW_CHILD)
    {
      window = ((GdkWindowPrivate*) window)->parent;
      private = (GdkWindowPrivate*) window;
    }

  return window;
}

void
gdk_window_add_filter (GdkWindow     *window,
                       GdkFilterFunc  function,
                       gpointer       data)
{
  GdkWindowPrivate *private;
  GList *tmp_list;
  GdkEventFilter *filter;

  private = (GdkWindowPrivate*) window;
  if (private && private->destroyed)
    return;

  if (private)
    tmp_list = private->filters;
  else
    tmp_list = gdk_default_filters;

  while (tmp_list)
    {
      filter = (GdkEventFilter *)tmp_list->data;
      if ((filter->function == function) && (filter->data == data))
        return;
      tmp_list = tmp_list->next;
    }

  filter = g_new (GdkEventFilter, 1);
  filter->function = function;
  filter->data = data;

  if (private)
    private->filters = g_list_append (private->filters, filter);
  else
    gdk_default_filters = g_list_append (gdk_default_filters, filter);
}

void
gdk_window_remove_filter (GdkWindow     *window,
                          GdkFilterFunc  function,
                          gpointer       data)
{
  GdkWindowPrivate *private;
  GList *tmp_list, *node;
  GdkEventFilter *filter;

  private = (GdkWindowPrivate*) window;

  if (private)
    tmp_list = private->filters;
  else
    tmp_list = gdk_default_filters;

  while (tmp_list)
    {
      filter = (GdkEventFilter *)tmp_list->data;
      node = tmp_list;
      tmp_list = tmp_list->next;

      if ((filter->function == function) && (filter->data == data))
        {
          if (private)
            private->filters = g_list_remove_link (private->filters, node);
          else
            gdk_default_filters = g_list_remove_link (gdk_default_filters, node);
          g_list_free_1 (node);
          g_free (filter);

          return;
        }
    }
}

/*************************************************************
 * gdk_window_is_visible:
 *     Check if the given window is mapped.
 *   arguments:
 *     window:
 *   results:
 *     is the window mapped
 *************************************************************/

gboolean
gdk_window_is_visible (GdkWindow *window)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *)window;

  g_return_val_if_fail (window != NULL, FALSE);

  return private->mapped;
}

/*************************************************************
 * gdk_window_is_viewable:
 *     Check if the window and all ancestors of the window
 *     are mapped. (This is not necessarily "viewable" in
 *     the X sense, since we only check as far as we have
 *     GDK window parents, not to the root window)
 *   arguments:
 *     window:
 *   results:
 *     is the window viewable
 *************************************************************/

gboolean
gdk_window_is_viewable (GdkWindow *window)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *)window;

  g_return_val_if_fail (window != NULL, FALSE);

  while (private &&
         (private != &gdk_root_parent) &&
         (private->window_type != GDK_WINDOW_FOREIGN))
    {
      if (!private->mapped)
        return FALSE;

      private = (GdkWindowPrivate *)private->parent;
    }

  return TRUE;
}

GdkEventMask
gdk_window_get_events (GdkWindow *window)
{
  GdkWindowPrivate *private;

  g_return_val_if_fail (window != NULL, 0);

  private = (GdkWindowPrivate*) window;
  if (private->destroyed)
    return 0;

  return private->event_mask;
}

/*
 * The window's children that GDK knows, topmost first, as X's
 * XQueryTree order once gave them. GDK's own list: it has the
 * windows drawn inside their parent, which the window system does not
 * know of, and it is the same on every backend.
 */
GList*
gdk_window_get_children (GdkWindow *window)
{
  GdkWindowPrivate *private;
  GList *children = NULL;
  GList *l;

  g_return_val_if_fail (window != NULL, NULL);

  private = (GdkWindowPrivate *) window;
  if (private->destroyed)
    return NULL;
  for (l = g_list_last (private->children); l; l = l->prev)
    if (!((GdkWindowPrivate *) l->data)->destroyed)
      children = g_list_prepend (children, l->data);
  return children;
}

GList *
gdk_window_get_toplevels (void)
{
  GList *new_list = NULL;
  GList *tmp_list;

  tmp_list = gdk_root_parent.children;
  while (tmp_list)
    {
      new_list = g_list_prepend (new_list, tmp_list->data);
      tmp_list = tmp_list->next;
    }

  return new_list;
}

void
gdk_drawable_set_data (GdkDrawable   *drawable,
                       const gchar   *key,
                       gpointer       data,
                       GDestroyNotify destroy_func)
{
  g_dataset_set_data_full (drawable, key, data, destroy_func);
}
