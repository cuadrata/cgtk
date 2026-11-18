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
 * A title bar, for compositors that leave decorations to the program,
 * as GNOME's and Weston do. It is a subsurface over the top of the
 * window: the window's own surface, and what GTK sees, are as they were.
 * The window's geometry, as the compositor knows it, takes it in, so
 * the window is placed, maximized and tiled with its title bar.
 *
 * There is no border. Around the window, outside its geometry, four
 * strips no one can see take the pointer for resizing, by the sides or,
 * near their ends, by the corners.
 *
 * A window that asks for no decorations, as a skinned one does, gets
 * none; nor does a full-screen window, nor one in a group, whose
 * cluster is laid out as one.
 *
 * The colours are the program's, from gdk_window_set_title_colors(): GTK
 * gives its theme's, so that the bar of the window with the focus is in
 * the theme's selected colours and its buttons are the theme's buttons.
 * A program that gives none has a bar in greys.
 */

#include "config.h"
#include <string.h>
#include <linux/input-event-codes.h>
#include "gdk.h"
#include "gdkprivate-wayland.h"
#include "gdkbackend-wayland.h"

#define BAR_HEIGHT    22      /* the title bar, in GDK units */
#define MARGIN        6       /* the resize strips' width, likewise */
#define CORNER        16      /* how far along a strip a corner reaches */
#define BUTTON        16      /* the buttons' size */
#define BUTTON_GAP    2
#define DOUBLE_CLICK  400     /* ms */

enum { EDGE_TOP, EDGE_BOTTOM, EDGE_LEFT, EDGE_RIGHT, N_EDGES };
enum { PART_NONE, PART_BAR, PART_CLOSE, PART_MAXIMIZE, PART_MINIMIZE };

struct _GdkWaylandDecor
{
  GdkWindowPrivate *window;
  struct wl_surface *bar;
  struct wl_subsurface *bar_sub;
  GdkWaylandBuffer bar_buffers[2];
  struct wl_surface *edge[N_EDGES];
  struct wl_subsurface *edge_sub[N_EDGES];
  GdkWaylandBuffer edge_buffers[N_EDGES];
  gboolean shown;               /* attached, and the geometry takes it in */
  gboolean dirty;               /* to be drawn again */
  gint drawn_width;             /* the window's width it was drawn for */
  gint hover, pressed;          /* the parts the pointer is over, pressed in */
  guint32 last_press_time;
  gboolean minimized;           /* by its button, till it is seen again */
  gboolean hidden_for_minimize; /* the commit that took them off was made */
};

static GList *decors = NULL;

/* The surface the pointer is in, if one of ours. */
static GdkWaylandDecor *pointer_decor = NULL;
static struct wl_surface *pointer_surface = NULL;
static gint pointer_x, pointer_y;     /* in that surface, in GDK units */

static gboolean
decor_wanted (GdkWindowPrivate *private)
{
  GdkWindowImpl *impl = private->impl;

  return impl && impl->xdg_toplevel && impl->decorated && !impl->ssd &&
    !impl->group && !private->destroyed &&
    (private->window_type == GDK_WINDOW_TOPLEVEL ||
     private->window_type == GDK_WINDOW_DIALOG);
}

static gboolean
decor_shown (GdkWindowPrivate *private)
{
  return decor_wanted (private) && !private->impl->fullscreen &&
    !(private->impl->decor && private->impl->decor->minimized);
}

/* The title bar's height for a window: what sits above its contents. */
gint
_gdk_wayland_decor_height (GdkWindowPrivate *private)
{
  return decor_shown (private) ? BAR_HEIGHT : 0;
}

/* ---- drawing ---- */

static GdkFont *
title_font (void)
{
  static GdkFont *font = NULL;
  static gboolean tried = FALSE;

  if (!tried)
    {
      tried = TRUE;
      font = gdk_font_load ("-*-sans-bold-r-normal--12-*-*-*-*-*-iso10646-1");
      if (!font)
        font = gdk_font_load ("fixed");
    }
  return font;
}

static GdkGC *
gc_color (GdkGC *gc, guint32 rgb)
{
  GdkColor c;

  c.pixel = 0;
  c.red = ((rgb >> 16) & 0xff) * 0x101;
  c.green = ((rgb >> 8) & 0xff) * 0x101;
  c.blue = (rgb & 0xff) * 0x101;
  gdk_color_alloc (gdk_colormap_get_system (), &c);
  gdk_gc_set_foreground (gc, &c);
  return gc;
}

/* What a title bar has when the program gave no colours: greys. */
static const guint32 plain_colors[GDK_TITLE_N_COLORS] = {
  0xc4c4c4, 0x000000, 0xffffff, 0x8a8a8a,       /* with the focus: bar, title, edges */
  0xdedede, 0x8a8a8a, 0xffffff, 0x8a8a8a,       /* without */
  0xd6d6d6, 0x202020, 0xffffff, 0x808080,       /* a button, its symbol, its edges */
  0x808080,                                     /* its symbol without the focus */
  0xe8e8e8, 0x202020,                           /* under the pointer */
  0xc0c0c0, 0x202020                            /* held down */
};

static const guint32 *
decor_colors (GdkWaylandDecor *decor)
{
  GdkWindowImpl *impl = decor->window->impl;

  return impl->title_colors_set ? impl->title_colors : plain_colors;
}

/* Where a button is, from the right. */
static void
button_rect (gint width, gint part, gint *x, gint *y)
{
  gint index = part == PART_CLOSE ? 0 : part == PART_MAXIMIZE ? 1 : 2;

  *x = width - (BAR_HEIGHT - BUTTON) / 2 - BUTTON - index * (BUTTON + BUTTON_GAP);
  *y = (BAR_HEIGHT - BUTTON) / 2;
}

static gint
part_at (GdkWaylandDecor *decor, gint x, gint y)
{
  gint parts[] = { PART_CLOSE, PART_MAXIMIZE, PART_MINIMIZE };
  guint i;

  for (i = 0; i < sizeof parts / sizeof parts[0]; i++)
    {
      gint bx, by;

      button_rect (decor->window->width, parts[i], &bx, &by);
      if (x >= bx && x < bx + BUTTON && y >= by && y < by + BUTTON)
        return parts[i];
    }
  return PART_BAR;
}

/*
 * A button, as the theme's buttons are: raised at rest; under the
 * pointer, in the prelight colour; held down, in the active colour and
 * sunken, its symbol a step down and to the right. Under the pointer it
 * has a frame round it as well, in the colour of its symbol: there are
 * themes with one colour for all three, and the frame is seen in any.
 * A button that was pressed and then left is at rest, and springs back
 * if the pointer returns; while one is held no other answers.
 */
static void
draw_button (GdkWaylandDecor *decor, GdkDrawable *d, GdkGC *gc, gint part, gboolean active)
{
  GdkWindowImpl *impl = decor->window->impl;
  const guint32 *col = decor_colors (decor);
  gboolean over = decor->hover == part &&
    (decor->pressed == PART_NONE || decor->pressed == part);
  gboolean down = over && decor->pressed == part;
  guint32 fill = col[down ? GDK_TITLE_BUTTON_ACTIVE_BG
                     : over ? GDK_TITLE_BUTTON_PRELIGHT_BG : GDK_TITLE_BUTTON_BG];
  guint32 ink = col[down ? GDK_TITLE_BUTTON_ACTIVE_FG
                    : over ? GDK_TITLE_BUTTON_PRELIGHT_FG
                    : active ? GDK_TITLE_BUTTON_FG : GDK_TITLE_BUTTON_UNFOCUSED_FG];
  guint32 light = col[GDK_TITLE_BUTTON_LIGHT], dark = col[GDK_TITLE_BUTTON_DARK];
  gint x, y, m;

  button_rect (decor->window->width, part, &x, &y);
  gdk_draw_rectangle (d, gc_color (gc, fill), TRUE, x, y, BUTTON, BUTTON);
  gdk_draw_line (d, gc_color (gc, down ? dark : light), x, y, x + BUTTON - 1, y);
  gdk_draw_line (d, gc, x, y, x, y + BUTTON - 1);
  gdk_draw_line (d, gc_color (gc, down ? light : dark),
                 x, y + BUTTON - 1, x + BUTTON - 1, y + BUTTON - 1);
  gdk_draw_line (d, gc, x + BUTTON - 1, y, x + BUTTON - 1, y + BUTTON - 1);
  if (over)
    gdk_draw_rectangle (d, gc_color (gc, ink), FALSE, x - 1, y - 1, BUTTON + 1, BUTTON + 1);
  if (down)
    x++, y++;

  gc_color (gc, ink);
  m = 4;
  switch (part)
    {
    case PART_CLOSE:
      gdk_draw_line (d, gc, x + m, y + m, x + BUTTON - 1 - m, y + BUTTON - 1 - m);
      gdk_draw_line (d, gc, x + m + 1, y + m, x + BUTTON - m, y + BUTTON - 1 - m);
      gdk_draw_line (d, gc, x + BUTTON - 1 - m, y + m, x + m, y + BUTTON - 1 - m);
      gdk_draw_line (d, gc, x + BUTTON - m, y + m, x + m + 1, y + BUTTON - 1 - m);
      break;
    case PART_MAXIMIZE:
      if (impl->maximized)
        {
          /* Two windows, one behind: restore. */
          gdk_draw_rectangle (d, gc, FALSE, x + m + 2, y + m, BUTTON - 2 * m - 3, BUTTON - 2 * m - 3);
          gdk_draw_rectangle (d, gc_color (gc, fill), TRUE,
                              x + m, y + m + 2, BUTTON - 2 * m - 2, BUTTON - 2 * m - 2);
          gc_color (gc, ink);
          gdk_draw_rectangle (d, gc, FALSE, x + m, y + m + 2, BUTTON - 2 * m - 3, BUTTON - 2 * m - 3);
          gdk_draw_line (d, gc, x + m, y + m + 3, x + BUTTON - m - 4, y + m + 3);
        }
      else
        {
          gdk_draw_rectangle (d, gc, FALSE, x + m, y + m, BUTTON - 2 * m - 1, BUTTON - 2 * m - 1);
          gdk_draw_line (d, gc, x + m, y + m + 1, x + BUTTON - m - 1, y + m + 1);
        }
      break;
    case PART_MINIMIZE:
      gdk_draw_rectangle (d, gc, TRUE, x + m, y + BUTTON - m - 2, BUTTON - 2 * m, 2);
      break;
    }
}

/* The title, cut to what fits before the buttons. */
static gchar *
fitted_title (GdkFont *font, const gchar *title, gint room)
{
  gint len = strlen (title);
  gchar *s, *t;

  if (gdk_string_width (font, title) <= room)
    return g_strdup (title);
  for (; len > 0; len--)
    {
      /* Not in the middle of a character. */
      if (((guchar) title[len] & 0xc0) == 0x80)
        continue;
      s = g_strndup (title, len);
      t = g_strconcat (s, "...", NULL);
      g_free (s);
      if (gdk_string_width (font, t) <= room)
        return t;
      g_free (t);
    }
  return g_strdup ("");
}

/* The title bar into a pixmap at the window's scale, then its pixels
 * into a buffer for the bar's surface. */
static void
draw_bar (GdkWaylandDecor *decor)
{
  GdkWindowPrivate *private = decor->window;
  GdkWindowImpl *impl = private->impl;
  gboolean active = impl->activated;
  /* The bar's four colours: with the focus, or without. */
  const guint32 *col = decor_colors (decor) + (active ? GDK_TITLE_BG : GDK_TITLE_UNFOCUSED_BG);
  gint width = private->width;
  GdkPixmap *pixmap;
  GdkWindowPrivate *pm;
  GdkGC *gc;
  GdkFont *font = title_font ();
  GdkWaylandBuffer *buffer = NULL;
  gint i, row, buffer_scale, bx, by;

  if (width < 1)
    return;
  pixmap = gdk_pixmap_new ((GdkWindow *) private, width, BAR_HEIGHT, -1);
  pm = (GdkWindowPrivate *) pixmap;
  gc = gdk_gc_new (pixmap);

  gdk_draw_rectangle (pixmap, gc_color (gc, col[GDK_TITLE_BG]), TRUE,
                      0, 0, width, BAR_HEIGHT);
  gdk_draw_line (pixmap, gc_color (gc, col[GDK_TITLE_LIGHT]), 0, 0, width - 1, 0);
  gdk_draw_line (pixmap, gc_color (gc, col[GDK_TITLE_DARK]),
                 0, BAR_HEIGHT - 1, width - 1, BAR_HEIGHT - 1);

  if (font && impl->title)
    {
      gint room;
      gchar *text;

      button_rect (width, PART_MINIMIZE, &bx, &by);
      room = bx - 8 - 8;
      text = fitted_title (font, impl->title, room);
      gdk_draw_string (pixmap, font, gc_color (gc, col[GDK_TITLE_FG]), 8,
                       (BAR_HEIGHT + font->ascent - font->descent) / 2, text);
      g_free (text);
    }
  draw_button (decor, pixmap, gc, PART_MINIMIZE, active);
  draw_button (decor, pixmap, gc, PART_MAXIMIZE, active);
  draw_button (decor, pixmap, gc, PART_CLOSE, active);
  gdk_gc_unref (gc);

  for (i = 0; i < 2; i++)
    if (!decor->bar_buffers[i].busy)
      {
        buffer = &decor->bar_buffers[i];
        break;
      }
  if (buffer)
    {
      if (buffer->buffer && (buffer->width != pm->fb->width || buffer->height != pm->fb->height))
        _gdk_wayland_shm_buffer_free (buffer);
      if (buffer->buffer || _gdk_wayland_shm_buffer_new (buffer, pm->fb->width, pm->fb->height, FALSE))
        {
          buffer->window = private;
          for (row = 0; row < pm->fb->height; row++)
            memcpy (buffer->data + row * pm->fb->width, pm->fb->pixels + row * pm->fb->stride,
                    pm->fb->width * sizeof (guint32));
          buffer_scale = MAX (pm->fb->scale / _gdk_wayland_zoom, 1);
          wl_surface_attach (decor->bar, buffer->buffer, 0, 0);
          wl_surface_damage_buffer (decor->bar, 0, 0, buffer->width, buffer->height);
          if (wl_proxy_get_version ((struct wl_proxy *) decor->bar) >= 3)
            wl_surface_set_buffer_scale (decor->bar, buffer_scale);
          buffer->busy = TRUE;
          wl_surface_commit (decor->bar);
          decor->dirty = FALSE;
          decor->drawn_width = width;
        }
    }
  gdk_pixmap_unref (pixmap);
}

/* The strips around the window: transparent, sized to it. */
static void
place_edges (GdkWaylandDecor *decor)
{
  GdkWindowPrivate *private = decor->window;
  gint w = GDK_WL_TO (private->width), h = GDK_WL_TO (private->height);
  gint bar = GDK_WL_TO (BAR_HEIGHT), m = GDK_WL_TO (MARGIN);
  gboolean resizable = !private->impl->maximized;
  gint e;

  for (e = 0; e < N_EDGES; e++)
    {
      gint x, y, bw, bh;
      GdkWaylandBuffer *buffer = &decor->edge_buffers[e];

      switch (e)
        {
        case EDGE_TOP:    x = -m; y = -bar - m; bw = w + 2 * m; bh = m; break;
        case EDGE_BOTTOM: x = -m; y = h;        bw = w + 2 * m; bh = m; break;
        case EDGE_LEFT:   x = -m; y = -bar;     bw = m; bh = bar + h; break;
        default:          x = w;  y = -bar;     bw = m; bh = bar + h; break;
        }
      wl_subsurface_set_position (decor->edge_sub[e], x, y);
      if (!resizable)
        {
          wl_surface_attach (decor->edge[e], NULL, 0, 0);
          wl_surface_commit (decor->edge[e]);
          continue;
        }
      if (buffer->buffer && (buffer->width != bw || buffer->height != bh))
        _gdk_wayland_shm_buffer_free (buffer);
      if (!buffer->buffer)
        {
          /* Never drawn on, nor released: it stays attached. */
          if (!_gdk_wayland_shm_buffer_new (buffer, MAX (bw, 1), MAX (bh, 1), TRUE))
            continue;
          buffer->window = private;
          memset (buffer->data, 0, buffer->size);
        }
      wl_surface_attach (decor->edge[e], buffer->buffer, 0, 0);
      wl_surface_damage_buffer (decor->edge[e], 0, 0, buffer->width, buffer->height);
      wl_surface_commit (decor->edge[e]);
    }
}

/* ---- life ---- */

static struct wl_surface *
decor_surface (GdkWindowPrivate *private, struct wl_subsurface **sub)
{
  struct wl_surface *surface = wl_compositor_create_surface (_gdk_wl_compositor);

  *sub = wl_subcompositor_get_subsurface (_gdk_wl_subcompositor, surface,
                                          private->impl->surface);
  return surface;
}

static GdkWaylandDecor *
decor_new (GdkWindowPrivate *private)
{
  GdkWaylandDecor *decor = g_new0 (GdkWaylandDecor, 1);
  gint e;

  decor->window = private;
  decor->bar = decor_surface (private, &decor->bar_sub);
  for (e = 0; e < N_EDGES; e++)
    {
      decor->edge[e] = decor_surface (private, &decor->edge_sub[e]);
      wl_subsurface_place_below (decor->edge_sub[e], private->impl->surface);
    }
  decor->hover = decor->pressed = PART_NONE;
  decor->dirty = TRUE;
  decors = g_list_prepend (decors, decor);
  return decor;
}

static void
decor_hide (GdkWaylandDecor *decor)
{
  gint e;

  if (!decor->shown)
    return;
  wl_surface_attach (decor->bar, NULL, 0, 0);
  wl_surface_commit (decor->bar);
  for (e = 0; e < N_EDGES; e++)
    {
      wl_surface_attach (decor->edge[e], NULL, 0, 0);
      wl_surface_commit (decor->edge[e]);
    }
  decor->shown = FALSE;
  decor->dirty = TRUE;
}

void
_gdk_wayland_decor_free (GdkWindowPrivate *private)
{
  GdkWaylandDecor *decor = private->impl ? private->impl->decor : NULL;
  gint e, i;

  if (!decor)
    return;
  if (pointer_decor == decor)
    {
      pointer_decor = NULL;
      pointer_surface = NULL;
    }
  decors = g_list_remove (decors, decor);
  for (i = 0; i < 2; i++)
    _gdk_wayland_shm_buffer_free (&decor->bar_buffers[i]);
  wl_subsurface_destroy (decor->bar_sub);
  wl_surface_destroy (decor->bar);
  for (e = 0; e < N_EDGES; e++)
    {
      _gdk_wayland_shm_buffer_free (&decor->edge_buffers[e]);
      wl_subsurface_destroy (decor->edge_sub[e]);
      wl_surface_destroy (decor->edge[e]);
    }
  g_free (decor);
  private->impl->decor = NULL;
}

/* Something about the window changed: its decorations are drawn again
 * with its next commit. */
void
_gdk_wayland_decor_changed (GdkWindowPrivate *private)
{
  if (private->impl && private->impl->decor)
    private->impl->decor->dirty = TRUE;
  if (private->impl)
    private->impl->commit_pending = TRUE;
}

/*
 * Before the window commits: its title bar and strips made, drawn and
 * placed, or taken away, and its geometry set to what the compositor is
 * to treat as the window. The subsurfaces are synchronized: what they
 * get now shows with this commit, at the window's new size.
 */
void
_gdk_wayland_decor_before_commit (GdkWindowPrivate *private)
{
  GdkWindowImpl *impl = private->impl;
  GdkWaylandDecor *decor = impl->decor;
  gint w = GDK_WL_TO (private->width), h = GDK_WL_TO (private->height);

  if (!decor_shown (private))
    {
      if (decor && decor->shown)
        {
          decor_hide (decor);
          if (decor->minimized)
            decor->hidden_for_minimize = TRUE;
          xdg_surface_set_window_geometry (impl->xdg_surface, 0, 0, w, h);
          _gdk_wayland_window_send_size_limits (private);
        }
      return;
    }
  if (!decor)
    decor = impl->decor = decor_new (private);
  if (!decor->shown)
    _gdk_wayland_window_send_size_limits (private);
  if (decor->dirty || !decor->shown || decor->drawn_width != private->width)
    {
      wl_subsurface_set_position (decor->bar_sub, 0, -GDK_WL_TO (BAR_HEIGHT));
      draw_bar (decor);
      place_edges (decor);
    }
  decor->shown = TRUE;
  xdg_surface_set_window_geometry (impl->xdg_surface, 0, -GDK_WL_TO (BAR_HEIGHT),
                                   w, h + GDK_WL_TO (BAR_HEIGHT));
}

/*
 * A frame was shown. A compositor sends none to a window it isn't
 * showing: the one for the commit that took the title bar off comes when
 * the window is back from being minimized, and the title bar with it.
 */
void
_gdk_wayland_decor_frame_done (GdkWindowPrivate *private)
{
  GdkWaylandDecor *decor = private->impl ? private->impl->decor : NULL;

  if (decor && decor->minimized && decor->hidden_for_minimize)
    {
      decor->minimized = decor->hidden_for_minimize = FALSE;
      _gdk_wayland_decor_changed (private);
    }
}

/* Whether the window is to wait for its frame, however long: the one
 * that tells the title bar to come back when it is no longer minimized. */
gboolean
_gdk_wayland_decor_needs_frame (GdkWindowPrivate *private)
{
  GdkWaylandDecor *decor = private->impl ? private->impl->decor : NULL;

  return decor && decor->minimized && decor->hidden_for_minimize;
}

/* The compositor's word on decorations: its own, or ours. */
static void
decoration_configure (void *data, struct zxdg_toplevel_decoration_v1 *decoration, uint32_t mode)
{
  GdkWindowPrivate *private = data;

  if (private->destroyed || !private->impl)
    return;
  private->impl->ssd = mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
  _gdk_wayland_decor_changed (private);
}

static const struct zxdg_toplevel_decoration_v1_listener decoration_listener = {
  decoration_configure
};

void
_gdk_wayland_decor_listen (GdkWindowPrivate *private)
{
  zxdg_toplevel_decoration_v1_add_listener (private->impl->decoration,
                                            &decoration_listener, private);
}

/* ---- the pointer ---- */

static GdkWaylandDecor *
decor_of_surface (struct wl_surface *surface, gint *edge)
{
  GList *l;
  gint e;

  for (l = decors; l; l = l->next)
    {
      GdkWaylandDecor *decor = l->data;

      if (decor->bar == surface)
        {
          *edge = -1;
          return decor;
        }
      for (e = 0; e < N_EDGES; e++)
        if (decor->edge[e] == surface)
          {
            *edge = e;
            return decor;
          }
    }
  return NULL;
}

/* Which way a press in a strip resizes: by its side, or a corner. */
static enum xdg_toplevel_resize_edge
resize_edge (GdkWaylandDecor *decor, gint edge, gint x, gint y)
{
  gint w = decor->window->width + 2 * MARGIN;
  gint h = decor->window->height + BAR_HEIGHT;

  switch (edge)
    {
    case EDGE_TOP:
      return x < CORNER ? XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT
        : x >= w - CORNER ? XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT
        : XDG_TOPLEVEL_RESIZE_EDGE_TOP;
    case EDGE_BOTTOM:
      return x < CORNER ? XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT
        : x >= w - CORNER ? XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT
        : XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
    case EDGE_LEFT:
      return y < CORNER - MARGIN ? XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT
        : y >= h - (CORNER - MARGIN) ? XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT
        : XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
    default:
      return y < CORNER - MARGIN ? XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT
        : y >= h - (CORNER - MARGIN) ? XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT
        : XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
    }
}

static GdkCursorType
edge_cursor (enum xdg_toplevel_resize_edge edge)
{
  switch (edge)
    {
    case XDG_TOPLEVEL_RESIZE_EDGE_TOP:          return GDK_TOP_SIDE;
    case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM:       return GDK_BOTTOM_SIDE;
    case XDG_TOPLEVEL_RESIZE_EDGE_LEFT:         return GDK_LEFT_SIDE;
    case XDG_TOPLEVEL_RESIZE_EDGE_RIGHT:        return GDK_RIGHT_SIDE;
    case XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT:     return GDK_TOP_LEFT_CORNER;
    case XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT:    return GDK_TOP_RIGHT_CORNER;
    case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT:  return GDK_BOTTOM_LEFT_CORNER;
    default:                                    return GDK_BOTTOM_RIGHT_CORNER;
    }
}

static void
show_cursor (GdkCursorType type)
{
  static GdkCursor *cursors[GDK_LAST_CURSOR];

  if (type < 0 || type >= GDK_LAST_CURSOR)
    return;
  if (!cursors[type])
    cursors[type] = _gdk_wayland_cursor_new (type);
  _gdk_wayland_show_cursor (cursors[type]);
}

static void
set_hover (GdkWaylandDecor *decor, gint part)
{
  if (decor->hover == part)
    return;
  decor->hover = part;
  _gdk_wayland_decor_changed (decor->window);
}

/* The pointer came into a surface: TRUE if one of ours, and dealt with. */
gboolean
_gdk_wayland_decor_pointer_enter (struct wl_surface *surface, gint x, gint y)
{
  gint edge;
  GdkWaylandDecor *decor = decor_of_surface (surface, &edge);

  if (!decor)
    return FALSE;
  pointer_decor = decor;
  pointer_surface = surface;
  _gdk_wayland_decor_pointer_motion (x, y);
  return TRUE;
}

gboolean
_gdk_wayland_decor_pointer_leave (struct wl_surface *surface)
{
  gint edge;
  GdkWaylandDecor *decor = decor_of_surface (surface, &edge);

  if (!decor)
    return FALSE;
  if (pointer_decor == decor)
    {
      set_hover (decor, PART_NONE);
      pointer_decor = NULL;
      pointer_surface = NULL;
    }
  return TRUE;
}

gboolean
_gdk_wayland_decor_pointer_motion (gint x, gint y)
{
  gint edge;
  GdkWaylandDecor *decor = pointer_surface ? decor_of_surface (pointer_surface, &edge) : NULL;

  if (!decor)
    return FALSE;
  pointer_x = x;
  pointer_y = y;
  if (edge < 0)
    {
      set_hover (decor, part_at (decor, x, y));
      show_cursor (GDK_LEFT_PTR);
    }
  else
    show_cursor (edge_cursor (resize_edge (decor, edge, x, y)));
  return TRUE;
}

static void
toggle_maximized (GdkWindowPrivate *private)
{
  if (private->impl->maximized)
    xdg_toplevel_unset_maximized (private->impl->xdg_toplevel);
  else
    xdg_toplevel_set_maximized (private->impl->xdg_toplevel);
}

static void
close_window (GdkWindowPrivate *private)
{
  GdkEvent event;

  /* As if the compositor had asked: the program decides. */
  event.any.type = GDK_DELETE;
  event.any.window = (GdkWindow *) private;
  event.any.send_event = FALSE;
  _gdk_wayland_put_event (&event);
}

gboolean
_gdk_wayland_decor_pointer_button (guint32 serial, guint32 time, guint32 button, gboolean press)
{
  gint edge;
  GdkWaylandDecor *decor = pointer_surface ? decor_of_surface (pointer_surface, &edge) : NULL;
  GdkWindowPrivate *private;
  struct xdg_toplevel *toplevel;

  if (!decor)
    return FALSE;
  private = decor->window;
  toplevel = private->impl->xdg_toplevel;
  if (!toplevel || !_gdk_wl_seat)
    return TRUE;

  if (edge >= 0)
    {
      if (press && button == BTN_LEFT)
        xdg_toplevel_resize (toplevel, _gdk_wl_seat, serial,
                             resize_edge (decor, edge, pointer_x, pointer_y));
      return TRUE;
    }

  if (!press)
    {
      gint pressed = decor->pressed;

      decor->pressed = PART_NONE;
      _gdk_wayland_decor_changed (private);
      if (button != BTN_LEFT || pressed != part_at (decor, pointer_x, pointer_y))
        return TRUE;
      switch (pressed)
        {
        case PART_CLOSE:    close_window (private); break;
        case PART_MAXIMIZE: toggle_maximized (private); break;
        case PART_MINIMIZE:
          /*
           * Taken off with the window's next commit, before it goes: some
           * compositors, Weston's for one, hide a minimized window's own
           * surface and leave the ones on it. Shown again, it has them
           * back.
           */
          decor->minimized = TRUE;
          _gdk_wayland_decor_changed (private);
          xdg_toplevel_set_minimized (toplevel);
          break;
        default: break;
        }
      return TRUE;
    }

  if (button == BTN_RIGHT)
    {
      xdg_toplevel_show_window_menu (toplevel, _gdk_wl_seat, serial,
                                     GDK_WL_TO (pointer_x),
                                     GDK_WL_TO (pointer_y - BAR_HEIGHT));
      return TRUE;
    }
  if (button != BTN_LEFT)
    return TRUE;

  decor->pressed = part_at (decor, pointer_x, pointer_y);
  if (decor->pressed != PART_BAR)
    {
      _gdk_wayland_decor_changed (private);
      return TRUE;
    }
  decor->pressed = PART_NONE;
  if (time - decor->last_press_time < DOUBLE_CLICK)
    {
      decor->last_press_time = 0;
      toggle_maximized (private);
    }
  else
    {
      decor->last_press_time = time;
      xdg_toplevel_move (toplevel, _gdk_wl_seat, serial);
    }
  return TRUE;
}
