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
 * The Wayland input context. Input methods come by the text input
 * protocol, when the compositor has it: the compositor passes the
 * input method's text to the focused text field, and the keys the input
 * method doesn't take reach the program as ever. Nothing on Wayland does
 * dead keys and compose sequences for a program, as X did: xkbcommon's
 * compose tables do them here, the same tables X used.
 */

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon-compose.h>
#include "gdk.h"
#include "gdkbackend.h"
#include "gdkprivate-wayland.h"
#include "gdkbackend-wayland.h"

typedef struct
{
  GdkInputContext base;
  GdkWindow *window;
  struct xkb_compose_state *compose;
  gboolean password;
  gchar *preedit;                       /* from the input method, UTF-8 */
  gint preedit_cursor, mark_start, mark_end;
  gboolean have_rect;                   /* the cursor, in surface coordinates */
  gint32 rect_x, rect_y, rect_width, rect_height;
} WaylandInputContext;

/*
 * The seat's one text input, which follows the keyboard focus: the
 * compositor says which surface it is on, and it is enabled while the
 * focused context's field is on that surface.
 */
static struct zwp_text_input_v3 *text_input = NULL;
static struct wl_surface *text_input_surface = NULL;
static WaylandInputContext *focused = NULL;
static gboolean enabled = FALSE;

/* What the input method sends, applied together on done. */
static gchar *pending_preedit = NULL;
static gint32 pending_begin, pending_end;
static gchar *pending_commit = NULL;

static struct xkb_compose_table *compose_table = NULL;
static gboolean compose_tried = FALSE;

/* The locale whose compose table applies, as xkbcommon asks it found. */
static const char *
compose_locale (void)
{
  const char *locale = getenv ("LC_ALL");

  if (!locale || !*locale)
    locale = getenv ("LC_CTYPE");
  if (!locale || !*locale)
    locale = getenv ("LANG");
  return locale && *locale ? locale : "C";
}

/* Where byte offset bytes into UTF-8 text falls, in characters. */
static gint
utf8_chars (const gchar *utf8, gint bytes)
{
  gint i, n = 0;

  for (i = 0; i < bytes && utf8[i]; i++)
    if (((guchar) utf8[i] & 0xc0) != 0x80)
      n++;
  return n;
}

static void
preedit_set (WaylandInputContext *ic, const gchar *utf8, gint32 begin, gint32 end)
{
  gint len = utf8 ? strlen (utf8) : 0;

  if (!len && !ic->preedit)
    return;
  g_free (ic->preedit);
  ic->preedit = len ? g_strdup (utf8) : NULL;
  ic->mark_start = ic->mark_end = 0;
  if (!len)
    ic->preedit_cursor = 0;
  else if (begin < 0 || end < 0)
    ic->preedit_cursor = utf8_chars (utf8, len);      /* no cursor shown: at the end */
  else
    {
      ic->preedit_cursor = utf8_chars (utf8, MIN (end, len));
      if (begin != end)
        {
          ic->mark_start = utf8_chars (utf8, MIN (MIN (begin, end), len));
          ic->mark_end = utf8_chars (utf8, MIN (MAX (begin, end), len));
        }
    }
  _gdk_input_context_preedit_changed (&ic->base);
}

static struct wl_surface *
context_surface (WaylandInputContext *ic)
{
  GdkWindowPrivate *native;

  if (!ic->window || ((GdkWindowPrivate *) ic->window)->destroyed)
    return NULL;
  native = _gdk_window_native ((GdkWindowPrivate *) ic->window, NULL, NULL);
  return native && native->impl ? native->impl->surface : NULL;
}

/* The field's state, sent with the commit that applies it. */
static void
text_input_commit_state (void)
{
  zwp_text_input_v3_set_content_type (text_input, ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE,
                                      ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
  if (focused->have_rect)
    zwp_text_input_v3_set_cursor_rectangle (text_input, focused->rect_x, focused->rect_y,
                                            focused->rect_width, focused->rect_height);
  zwp_text_input_v3_commit (text_input);
}

/* Enabled while the focused context's field, not a password, is where
 * the compositor has the text input. */
static void
text_input_update (void)
{
  gboolean want = text_input && focused && !focused->password && text_input_surface &&
    text_input_surface == context_surface (focused);

  if (want && !enabled)
    {
      zwp_text_input_v3_enable (text_input);
      text_input_commit_state ();
      enabled = TRUE;
    }
  else if (!want && enabled)
    {
      if (text_input_surface)
        {
          zwp_text_input_v3_disable (text_input);
          zwp_text_input_v3_commit (text_input);
        }
      enabled = FALSE;
      /* What it was composing is gone with it. */
      if (focused)
        preedit_set (focused, NULL, 0, 0);
    }
}

static void
text_input_enter (void *data, struct zwp_text_input_v3 *ti, struct wl_surface *surface)
{
  text_input_surface = surface;
  text_input_update ();
}

static void
text_input_leave (void *data, struct zwp_text_input_v3 *ti, struct wl_surface *surface)
{
  /* Leaving clears the text input's state: it is disabled already. */
  if (surface != text_input_surface)
    return;
  text_input_surface = NULL;
  enabled = FALSE;
  if (focused)
    preedit_set (focused, NULL, 0, 0);
}

static void
text_input_preedit_string (void *data, struct zwp_text_input_v3 *ti, const char *text,
                           int32_t cursor_begin, int32_t cursor_end)
{
  g_free (pending_preedit);
  pending_preedit = g_strdup (text ? text : "");
  pending_begin = cursor_begin;
  pending_end = cursor_end;
}

static void
text_input_commit_string (void *data, struct zwp_text_input_v3 *ti, const char *text)
{
  g_free (pending_commit);
  pending_commit = g_strdup (text ? text : "");
}

/* Only asked for of text we told it of, which we don't, yet. */
static void
text_input_delete_surrounding_text (void *data, struct zwp_text_input_v3 *ti,
                                    uint32_t before_length, uint32_t after_length)
{
}

/*
 * The input method's changes, in the protocol's order: the old preedit
 * goes, the committed text goes in, and the new preedit is shown. A
 * serial behind ours still has its text applied.
 */
static void
text_input_done (void *data, struct zwp_text_input_v3 *ti, uint32_t serial)
{
  if (focused && enabled)
    {
      if (pending_commit && *pending_commit)
        {
          preedit_set (focused, NULL, 0, 0);
          _gdk_input_context_commit (&focused->base, pending_commit);
        }
      preedit_set (focused, pending_preedit, pending_begin, pending_end);
    }
  g_free (pending_preedit);
  g_free (pending_commit);
  pending_preedit = pending_commit = NULL;
}

static const struct zwp_text_input_v3_listener text_input_listener = {
  text_input_enter,
  text_input_leave,
  text_input_preedit_string,
  text_input_commit_string,
  text_input_delete_surrounding_text,
  text_input_done,
};

static void
text_input_ensure (void)
{
  if (text_input || !_gdk_wl_text_input_manager || !_gdk_wl_seat)
    return;
  text_input = zwp_text_input_manager_v3_get_text_input (_gdk_wl_text_input_manager,
                                                         _gdk_wl_seat);
  zwp_text_input_v3_add_listener (text_input, &text_input_listener, NULL);
}

/* From the start, as the compositor may tell only those there already
 * where the keyboard focus is. */
void
_gdk_wayland_input_context_init (void)
{
  text_input_ensure ();
}

void
_gdk_wayland_input_context_exit (void)
{
  if (text_input)
    zwp_text_input_v3_destroy (text_input);
  text_input = NULL;
  text_input_surface = NULL;
  focused = NULL;
  enabled = FALSE;
}

static GdkInputContext *
wl_create (void)
{
  WaylandInputContext *ic = g_new0 (WaylandInputContext, 1);

  if (!compose_tried && _gdk_xkb_context)
    {
      compose_tried = TRUE;
      compose_table = xkb_compose_table_new_from_locale (_gdk_xkb_context, compose_locale (),
                                                         XKB_COMPOSE_COMPILE_NO_FLAGS);
    }
  if (compose_table)
    ic->compose = xkb_compose_state_new (compose_table, XKB_COMPOSE_STATE_NO_FLAGS);
  return &ic->base;
}

static void
wl_destroy (GdkInputContext *context)
{
  WaylandInputContext *ic = (WaylandInputContext *) context;

  if (focused == ic)
    {
      focused = NULL;
      text_input_update ();
    }
  g_free (ic->preedit);

  if (ic->compose)
    xkb_compose_state_unref (ic->compose);
  if (ic->window)
    gdk_window_unref (ic->window);
  g_free (ic);
}

static void
wl_set_window (GdkInputContext *context, GdkWindow *window)
{
  WaylandInputContext *ic = (WaylandInputContext *) context;

  if (window)
    gdk_window_ref (window);
  if (ic->window)
    gdk_window_unref (ic->window);
  ic->window = window;
  preedit_set (ic, NULL, 0, 0);
  if (focused == ic)
    {
      /* Another surface, perhaps: start over on it. */
      focused = NULL;
      text_input_update ();
      focused = ic;
      text_input_update ();
    }
}

/* Whatever was being composed is dropped, and the input method starts
 * again: the protocol has no other way to say so. */
static void
wl_reset (GdkInputContext *context)
{
  WaylandInputContext *ic = (WaylandInputContext *) context;

  if (ic->compose)
    xkb_compose_state_reset (ic->compose);
  preedit_set (ic, NULL, 0, 0);
  if (focused == ic && enabled)
    {
      focused = NULL;
      text_input_update ();
      focused = ic;
      text_input_update ();
    }
}

static void
wl_focus_in (GdkInputContext *context)
{
  focused = (WaylandInputContext *) context;
  text_input_ensure ();
  text_input_update ();
}

static void
wl_focus_out (GdkInputContext *context)
{
  WaylandInputContext *ic = (WaylandInputContext *) context;

  if (ic->compose)
    xkb_compose_state_reset (ic->compose);
  preedit_set (ic, NULL, 0, 0);
  if (focused == ic)
    {
      focused = NULL;
      text_input_update ();
    }
}

/* Text, not a control character, and typed without Control or Alt. */
static gboolean
is_typed_text (GdkEventKey *event)
{
  return event->length > 0 && (guchar) event->string[0] >= 0x20 &&
    event->string[0] != 0x7f && !(event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK));
}

static gboolean
wl_filter_key (GdkInputContext *context, GdkEventKey *event)
{
  WaylandInputContext *ic = (WaylandInputContext *) context;

  if (event->type != GDK_KEY_PRESS)
    return FALSE;

  if (ic->compose &&
      xkb_compose_state_feed (ic->compose, event->keyval) == XKB_COMPOSE_FEED_ACCEPTED)
    {
      gchar buf[64];

      switch (xkb_compose_state_get_status (ic->compose))
        {
        case XKB_COMPOSE_COMPOSING:
          return TRUE;
        case XKB_COMPOSE_COMPOSED:
          if (xkb_compose_state_get_utf8 (ic->compose, buf, sizeof buf) <= 0)
            xkb_keysym_to_utf8 (xkb_compose_state_get_one_sym (ic->compose), buf, sizeof buf);
          xkb_compose_state_reset (ic->compose);
          _gdk_input_context_commit (context, buf);
          return TRUE;
        case XKB_COMPOSE_CANCELLED:
          xkb_compose_state_reset (ic->compose);
          return TRUE;
        case XKB_COMPOSE_NOTHING:
          break;
        }
    }

  /* Key events' text is UTF-8 here already. */
  if (is_typed_text (event))
    {
      _gdk_input_context_commit (context, event->string);
      return TRUE;
    }
  return FALSE;
}

static void
wl_get_preedit (GdkInputContext *context, gchar **utf8, gint *cursor,
                gint *mark_start, gint *mark_end)
{
  WaylandInputContext *ic = (WaylandInputContext *) context;

  *utf8 = ic->preedit ? g_strdup (ic->preedit) : NULL;
  *cursor = ic->preedit_cursor;
  *mark_start = ic->mark_start;
  *mark_end = ic->mark_end;
}

/* In the client window's coordinates, to the surface's. */
static void
wl_set_cursor_location (GdkInputContext *context, GdkRectangle *area)
{
  WaylandInputContext *ic = (WaylandInputContext *) context;
  gint x = 0, y = 0;
  gint32 rx, ry, rw, rh;

  if (!ic->window)
    return;
  _gdk_window_native ((GdkWindowPrivate *) ic->window, &x, &y);
  rx = GDK_WL_TO (x + area->x);
  ry = GDK_WL_TO (y + area->y);
  rw = GDK_WL_TO (area->width);
  rh = GDK_WL_TO (area->height);
  if (ic->have_rect && rx == ic->rect_x && ry == ic->rect_y &&
      rw == ic->rect_width && rh == ic->rect_height)
    return;
  ic->have_rect = TRUE;
  ic->rect_x = rx;
  ic->rect_y = ry;
  ic->rect_width = rw;
  ic->rect_height = rh;
  if (focused == ic && enabled)
    {
      zwp_text_input_v3_set_cursor_rectangle (text_input, rx, ry, rw, rh);
      zwp_text_input_v3_commit (text_input);
    }
}

static void
wl_set_surrounding (GdkInputContext *context, const gchar *utf8, gint length,
                    gint cursor_index)
{
}

/* No input method sees a password field. */
static void
wl_set_purpose (GdkInputContext *context, GdkInputPurpose purpose)
{
  WaylandInputContext *ic = (WaylandInputContext *) context;

  ic->password = purpose == GDK_INPUT_PURPOSE_PASSWORD;
  if (focused == ic)
    text_input_update ();
}

const GdkInputContextClass _gdk_wayland_input_context_class = {
  wl_create,
  wl_destroy,
  wl_set_window,
  wl_focus_in,
  wl_focus_out,
  wl_reset,
  wl_filter_key,
  wl_get_preedit,
  wl_set_cursor_location,
  wl_set_surrounding,
  wl_set_purpose,
};
