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
 * The seat: what the pointer and the keyboard do arrives here, on a
 * native window, and is handed to the generic side to route to the
 * window inside that it belongs to, exactly as the X11 backend hands
 * on what the X server sends it. Keys are named by xkbcommon, whose
 * keysyms are X's, so GTK's key handling needs no translation. A held
 * key repeats here, since the compositor only says how fast.
 */

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <linux/input-event-codes.h>
#include "gdk.h"
#include "gdkprivate-wayland.h"
#include "gdkbackend-wayland.h"
#include "gdkkeysyms.h"

#define DOUBLE_CLICK_TIME 250
#define TRIPLE_CLICK_TIME 500

static guint buttons_down = 0;   /* as GDK button masks */
static GdkWindowPrivate *press_native = NULL;   /* where they went down */

gboolean
_gdk_wayland_buttons_down (void)
{
  return buttons_down != 0;
}

guint
_gdk_wayland_button_mask (void)
{
  return buttons_down;
}

static guint32 button_click_time[2];
static GdkWindow *button_window[2];
static guint button_number[2];

static gint repeat_rate = 25, repeat_delay = 600;   /* per second, ms */
static guint repeat_timeout = 0;
static guint32 repeat_key = 0;
static xkb_mod_index_t mod_shift, mod_caps, mod_ctrl, mod_alt, mod_num, mod_super;

/*
 * A configure tells the program the size its window has now. One
 * still queued from an earlier request would tell it a size it no
 * longer has, and a program that takes a configure for the window
 * system's wish would set that size again, and go back and forth for
 * ever between two of its own requests. So every configure still
 * queued for the window is made to say what this one says.
 */
void
_gdk_wayland_put_event (GdkEvent *event)
{
  if (event->any.window)
    gdk_window_ref (event->any.window);
  if (event->type == GDK_CONFIGURE)
    _gdk_event_queue_update_configures (&event->configure);
  gdk_event_put (event);
  if (event->any.window)
    gdk_window_unref (event->any.window);
}

/* ---------------------------------------------------------------------
 * The pointer
 */

static void
native_crossing (GdkWindowPrivate *native, GdkEventType type, gint x, gint y,
                 guint32 time)
{
  GdkEvent event;
  gint rx, ry;

  if (!(native->event_mask & (type == GDK_ENTER_NOTIFY
                              ? GDK_ENTER_NOTIFY_MASK : GDK_LEAVE_NOTIFY_MASK)))
    return;
  gdk_window_get_origin ((GdkWindow *) native, &rx, &ry);
  event.crossing.type = type;
  event.crossing.window = (GdkWindow *) native;
  event.crossing.send_event = FALSE;
  event.crossing.subwindow = NULL;
  event.crossing.time = time;
  event.crossing.x = x;
  event.crossing.y = y;
  event.crossing.x_root = rx + x;
  event.crossing.y_root = ry + y;
  event.crossing.mode = GDK_CROSSING_NORMAL;
  event.crossing.detail = GDK_NOTIFY_NONLINEAR;
  event.crossing.focus = FALSE;
  event.crossing.state = _gdk_wayland_modifiers | buttons_down;
  _gdk_wayland_put_event (&event);
}

static void
native_enter (GdkWindowPrivate *native, gint x, gint y)
{
  gint rx, ry;

  _gdk_wayland_pointer_window = native;
  native->impl->pointer_x = x;
  native->impl->pointer_y = y;
  native->impl->pointer_in = TRUE;
  /* Among a group, the keyboard follows the pointer from member to member. */
  if (native->impl->group)
    _gdk_wayland_group_set_active (native);
  native_crossing (native, GDK_ENTER_NOTIFY, x, y, _gdk_wayland_last_time);
  gdk_window_get_origin ((GdkWindow *) native, &rx, &ry);
  _gdk_window_pointer_moved (native, x, y, rx + x, ry + y, _gdk_wayland_last_time,
                             _gdk_wayland_modifiers | buttons_down, GDK_CROSSING_NORMAL, FALSE);
  _gdk_wayland_set_cursor_on_pointer ();
}

static void
native_leave (GdkWindowPrivate *native)
{
  gint rx, ry;

  if (!native || native->destroyed)
    return;
  gdk_window_get_origin ((GdkWindow *) native, &rx, &ry);
  _gdk_window_pointer_moved (native, native->impl->pointer_x, native->impl->pointer_y,
                             rx + native->impl->pointer_x, ry + native->impl->pointer_y,
                             _gdk_wayland_last_time, _gdk_wayland_modifiers | buttons_down,
                             GDK_CROSSING_NORMAL, TRUE);
  native_crossing (native, GDK_LEAVE_NOTIFY, native->impl->pointer_x,
                   native->impl->pointer_y, _gdk_wayland_last_time);
  native->impl->pointer_in = FALSE;
  if (_gdk_wayland_pointer_window == native)
    _gdk_wayland_pointer_window = NULL;
}

/*
 * While a button is held the compositor keeps the pointer on the
 * surface it went down on, wherever it goes. The X server did the
 * same until the program grabbed the pointer, and a menu popped up by
 * the press -- an option menu's, a menu bar's, a combo's list -- does
 * grab it, owner_events and all, so under X the menu got the motion and
 * the release and the press could be dragged to an item. Here the
 * popup would never hear of the pointer. So once the pointer, held,
 * is over one of the program's own popups, GDK has it there: the
 * surface it went down on hears it leave, the popup hears it enter,
 * and gets the motion and the release. That lasts until the
 * compositor says where the pointer is again, or the popup goes.
 */
static GdkWindowPrivate *over_native = NULL;   /* where GDK has the pointer instead */

/* The popup with the grab, or one it hangs from, that the point at
 * root coordinates is in, if any. */
static GdkWindowPrivate *
popup_at (gint x_root, gint y_root)
{
  GdkWindowPrivate *w = _gdk_wayland_grab_popup;

  while (w && !w->destroyed && w->mapped && w->impl && w->impl->xdg_popup)
    {
      gint ox, oy;

      gdk_window_get_origin ((GdkWindow *) w, &ox, &oy);
      if (x_root >= ox && x_root < ox + w->width &&
          y_root >= oy && y_root < oy + w->height)
        return w;
      w = (GdkWindowPrivate *) w->impl->popup_parent;
    }
  return NULL;
}

/* The pointer goes into a native window, or out of it, as far as the
 * program is told: the surface the compositor has it on is left as it
 * is. */
static void
virtual_cross (GdkWindowPrivate *native, gboolean in, gint x, gint y)
{
  gint rx, ry;

  if (!native || native->destroyed)
    return;
  gdk_window_get_origin ((GdkWindow *) native, &rx, &ry);
  if (in)
    {
      native->impl->pointer_x = x;
      native->impl->pointer_y = y;
      native_crossing (native, GDK_ENTER_NOTIFY, x, y, _gdk_wayland_last_time);
    }
  _gdk_window_pointer_moved (native, x, y, rx + x, ry + y, _gdk_wayland_last_time,
                             _gdk_wayland_modifiers | buttons_down, GDK_CROSSING_NORMAL, !in);
  if (!in)
    native_crossing (native, GDK_LEAVE_NOTIFY, x, y, _gdk_wayland_last_time);
}

/* Where (x, y) in native, the surface the compositor has the pointer
 * on, is in the native window GDK has it in: into *x, *y. */
static GdkWindowPrivate *
pointer_native (GdkWindowPrivate *native, gint *x, gint *y)
{
  gint rx, ry, ox, oy;

  if (!over_native)
    return native;
  gdk_window_get_origin ((GdkWindow *) native, &rx, &ry);
  gdk_window_get_origin ((GdkWindow *) over_native, &ox, &oy);
  *x += rx - ox;
  *y += ry - oy;
  return over_native;
}

/* The pointer moved to (x, y) in native: over a popup, held, or back
 * off it. */
static void
pointer_follow (GdkWindowPrivate *native, gint x, gint y)
{
  GdkWindowPrivate *over = NULL, *was = over_native;
  gint rx, ry, ox, oy;

  if (!buttons_down && !over_native)
    return;
  gdk_window_get_origin ((GdkWindow *) native, &rx, &ry);
  over = popup_at (rx + x, ry + y);
  if (over == native)
    over = NULL;
  if (over == over_native)
    return;

  /* out of where it was */
  if (was)
    {
      gdk_window_get_origin ((GdkWindow *) was, &ox, &oy);
      virtual_cross (was, FALSE, rx + x - ox, ry + y - oy);
    }
  else
    virtual_cross (native, FALSE, x, y);

  over_native = over;
  if (over)
    {
      gdk_window_get_origin ((GdkWindow *) over, &ox, &oy);
      virtual_cross (over, TRUE, rx + x - ox, ry + y - oy);
    }
  else
    virtual_cross (native, TRUE, x, y);
}

/* The native window GDK has the pointer in. */
GdkWindowPrivate *
_gdk_wayland_pointer_native (void)
{
  return over_native ? over_native : _gdk_wayland_pointer_window;
}

/* A native window went away: if GDK had the pointer in it, the pointer
 * is back on the surface the compositor has it on. */
void
_gdk_wayland_pointer_native_gone (GdkWindowPrivate *native)
{
  GdkWindowPrivate *real = _gdk_wayland_pointer_window;

  if (native != over_native)
    return;
  over_native = NULL;
  _gdk_window_pointer_moved (native, 0, 0, 0, 0, _gdk_wayland_last_time,
                             _gdk_wayland_modifiers | buttons_down, GDK_CROSSING_NORMAL, TRUE);
  if (real && real != native && !real->destroyed && real->impl->pointer_in)
    virtual_cross (real, TRUE, real->impl->pointer_x, real->impl->pointer_y);
}

static void
pointer_enter (void *data, struct wl_pointer *pointer, uint32_t serial,
               struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy)
{
  GdkWindowPrivate *native = _gdk_wayland_window_from_surface (surface);
  gint x = GDK_WL_FROM (wl_fixed_to_int (sx)), y = GDK_WL_FROM (wl_fixed_to_int (sy));

  _gdk_wayland_pointer_serial = serial;
  _gdk_wayland_last_serial = serial;
  /* A title bar, or the strips for resizing: not the program's. */
  if (_gdk_wayland_decor_pointer_enter (surface, x, y))
    return;
  if (!native || native->destroyed)
    return;
  if (over_native)
    {
      GdkWindowPrivate *was = over_native;

      /* GDK had it here already: the compositor has caught up. */
      over_native = NULL;
      if (was == native)
        {
          _gdk_wayland_pointer_window = native;
          native->impl->pointer_x = x;
          native->impl->pointer_y = y;
          native->impl->pointer_in = TRUE;
          if (native->impl->group)
            _gdk_wayland_group_set_active (native);
          _gdk_wayland_set_cursor_on_pointer ();
          return;
        }
      virtual_cross (was, FALSE, was->impl->pointer_x, was->impl->pointer_y);
    }
  native_enter (native, x, y);
}

static void
pointer_leave (void *data, struct wl_pointer *pointer, uint32_t serial,
               struct wl_surface *surface)
{
  GdkWindowPrivate *native = _gdk_wayland_window_from_surface (surface);

  _gdk_wayland_last_serial = serial;
  if (_gdk_wayland_decor_pointer_leave (surface))
    return;
  if (over_native && native && native != over_native && !native->destroyed)
    {
      /* The program was told it left when GDK took it to the popup. */
      native->impl->pointer_in = FALSE;
      if (_gdk_wayland_pointer_window == native)
        _gdk_wayland_pointer_window = NULL;
      return;
    }
  native_leave (native);
}

/*
 * A pointer event routed to its window inside the native one: to the
 * deepest window under it that asked, or the grab window.
 */
static void
deliver_pointer_event (GdkWindowPrivate *native, GdkEvent *event, gint x, gint y)
{
  GdkWindowPrivate *target, *grab_window;
  GdkEventMask target_mask;
  GdkEventMask needed;
  gboolean implicit;
  gint tx = 0, ty = 0, rx, ry;
  guint state = event->type == GDK_MOTION_NOTIFY ? event->motion.state : event->button.state;

  switch (event->type)
    {
    case GDK_BUTTON_PRESS:
      needed = GDK_BUTTON_PRESS_MASK;
      break;
    case GDK_BUTTON_RELEASE:
      needed = GDK_BUTTON_RELEASE_MASK;
      break;
    default:
      needed = GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK;
      if (state & (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK | GDK_BUTTON3_MASK |
                   GDK_BUTTON4_MASK | GDK_BUTTON5_MASK))
        needed |= GDK_BUTTON_MOTION_MASK;
      if (state & GDK_BUTTON1_MASK)
        needed |= GDK_BUTTON1_MOTION_MASK;
      if (state & GDK_BUTTON2_MASK)
        needed |= GDK_BUTTON2_MOTION_MASK;
      if (state & GDK_BUTTON3_MASK)
        needed |= GDK_BUTTON3_MOTION_MASK;
      break;
    }

  gdk_window_get_origin ((GdkWindow *) native, &rx, &ry);
  target = _gdk_window_event_target (native, x, y, needed, &tx, &ty);
  target_mask = target ? target->event_mask : 0;

  grab_window = _gdk_pointer_grab_window (&implicit);
  if (grab_window && grab_window->destroyed)
    {
      _gdk_pointer_grab_clear (FALSE);
      grab_window = NULL;
    }
  if (grab_window)
    {
      gboolean owner_events = implicit || _gdk_pointer_grab_owner_events;

      if (!(owner_events && target))
        {
          if (!(_gdk_pointer_grab_mask () & needed) && !implicit)
            target = NULL;
          else
            {
              gint gx, gy;

              target = grab_window;
              target_mask = _gdk_pointer_grab_mask ();
              gdk_window_get_origin ((GdkWindow *) target, &gx, &gy);
              tx = rx + x - gx;
              ty = ry + y - gy;
            }
        }
    }
  if (!target)
    return;

  event->any.window = (GdkWindow *) target;
  if (event->type == GDK_MOTION_NOTIFY)
    {
      event->motion.x = tx;
      event->motion.y = ty;
      event->motion.x_root = rx + x;
      event->motion.y_root = ry + y;
      /* A window that asked for hints gets one, and asks the pointer
       * where it is, as the server had it do. */
      event->motion.is_hint = (target_mask & GDK_POINTER_MOTION_HINT_MASK) != 0;
    }
  else
    {
      event->button.x = tx;
      event->button.y = ty;
      event->button.x_root = rx + x;
      event->button.y_root = ry + y;
    }
  _gdk_wayland_put_event (event);
}


static void
pointer_motion (void *data, struct wl_pointer *pointer, uint32_t time,
                wl_fixed_t sx, wl_fixed_t sy)
{
  GdkWindowPrivate *native;
  gint x = GDK_WL_FROM (wl_fixed_to_int (sx)), y = GDK_WL_FROM (wl_fixed_to_int (sy));
  gint rx, ry;
  GdkEvent event;

  _gdk_wayland_last_time = time;
  if (_gdk_wayland_decor_pointer_motion (x, y))
    return;
  native = _gdk_wayland_pointer_window;
  if (!native || native->destroyed)
    return;
  native->impl->pointer_x = x;
  native->impl->pointer_y = y;
  pointer_follow (native, x, y);
  native = pointer_native (native, &x, &y);
  native->impl->pointer_x = x;
  native->impl->pointer_y = y;
  gdk_window_get_origin ((GdkWindow *) native, &rx, &ry);
  _gdk_window_pointer_moved (native, x, y, rx + x, ry + y, time,
                             _gdk_wayland_modifiers | buttons_down, GDK_CROSSING_NORMAL, FALSE);

  memset (&event, 0, sizeof event);
  event.motion.type = GDK_MOTION_NOTIFY;
  event.motion.time = time;
  event.motion.pressure = 0.5;
  event.motion.state = _gdk_wayland_modifiers | buttons_down;
  event.motion.is_hint = FALSE;
  event.motion.source = GDK_SOURCE_MOUSE;
  event.motion.deviceid = GDK_CORE_POINTER;
  deliver_pointer_event (native, &event, x, y);
}

static void
synthesize_click (GdkEvent *event, gint nclicks)
{
  GdkEvent temp_event = *event;

  temp_event.type = (nclicks == 2) ? GDK_2BUTTON_PRESS : GDK_3BUTTON_PRESS;
  _gdk_wayland_put_event (&temp_event);
}

static void
button_event (GdkWindowPrivate *native, guint button, gboolean down, guint32 time)
{
  GdkEvent event;
  guint mask = GDK_BUTTON1_MASK << (button - 1);
  GdkWindowPrivate *at;

  /* Where GDK has the pointer: a popup it was held over, if so; one
   * that came up under it while it stood still is over it as well. */
  if (!down)
    pointer_follow (native, native->impl->pointer_x, native->impl->pointer_y);
  at = over_native ? over_native : native;

  memset (&event, 0, sizeof event);
  event.button.type = down ? GDK_BUTTON_PRESS : GDK_BUTTON_RELEASE;
  event.button.time = time;
  event.button.pressure = 0.5;
  event.button.state = _gdk_wayland_modifiers | buttons_down;
  event.button.button = button;
  event.button.source = GDK_SOURCE_MOUSE;
  event.button.deviceid = GDK_CORE_POINTER;

  if (down)
    {
      GdkWindowPrivate *grab;

      deliver_pointer_event (at, &event, at->impl->pointer_x, at->impl->pointer_y);
      if (!event.any.window)
        return;
      buttons_down |= mask;
      press_native = native;
      /* The press starts an implicit grab, until the buttons are up. */
      _gdk_pointer_grab_set ((GdkWindowPrivate *) event.button.window, TRUE,
                             ((GdkWindowPrivate *) event.button.window)->event_mask, TRUE);
      grab = (GdkWindowPrivate *) event.button.window;
      (void) grab;

      if ((time < button_click_time[1] + TRIPLE_CLICK_TIME) &&
          (event.button.window == button_window[1]) && (button == button_number[1]))
        {
          synthesize_click (&event, 3);
          button_click_time[1] = button_click_time[0] = 0;
          button_window[1] = button_window[0] = NULL;
          button_number[1] = button_number[0] = -1;
        }
      else if ((time < button_click_time[0] + DOUBLE_CLICK_TIME) &&
               (event.button.window == button_window[0]) && (button == button_number[0]))
        {
          synthesize_click (&event, 2);
          button_click_time[1] = button_click_time[0];
          button_click_time[0] = time;
          button_window[1] = button_window[0];
          button_window[0] = event.button.window;
          button_number[1] = button_number[0];
          button_number[0] = button;
        }
      else
        {
          button_click_time[1] = 0;
          button_click_time[0] = time;
          button_window[1] = NULL;
          button_window[0] = event.button.window;
          button_number[1] = -1;
          button_number[0] = button;
        }
    }
  else
    {
      deliver_pointer_event (at, &event, at->impl->pointer_x, at->impl->pointer_y);
      buttons_down &= ~mask;
      if (!buttons_down)
        press_native = NULL;
      if (!buttons_down)
        _gdk_window_implicit_grab_ended (native, native->impl->pointer_x,
                                         native->impl->pointer_y,
                                         event.button.x_root, event.button.y_root,
                                         time, event.button.state);
    }
}

static void
pointer_button (void *data, struct wl_pointer *pointer, uint32_t serial,
                uint32_t time, uint32_t button, uint32_t state)
{
  GdkWindowPrivate *native = _gdk_wayland_pointer_window;
  guint gdk_button;

  _gdk_wayland_last_serial = serial;
  _gdk_wayland_last_time = time;
  if (state == WL_POINTER_BUTTON_STATE_PRESSED)
    _gdk_wayland_press_serial = _gdk_wayland_input_serial = serial;
  if (_gdk_wayland_decor_pointer_button (serial, time, button,
                                         state == WL_POINTER_BUTTON_STATE_PRESSED))
    return;
  if (!native || native->destroyed)
    return;
  switch (button)
    {
    case BTN_LEFT: gdk_button = 1; break;
    case BTN_MIDDLE: gdk_button = 2; break;
    case BTN_RIGHT: gdk_button = 3; break;
    default: return;
    }
  button_event (native, gdk_button, state == WL_POINTER_BUTTON_STATE_PRESSED, time);
}

/*
 * The compositor took the pointer to move the window: the program
 * sees the button go up, so its own drag ends, as the release the
 * compositor keeps would have ended it.
 */
void
_gdk_wayland_release_buttons (void)
{
  /* A pointer that went away left its window first: where they went down. */
  GdkWindowPrivate *native = _gdk_wayland_pointer_window ? _gdk_wayland_pointer_window
    : press_native;
  guint button;

  if (!native || native->destroyed)
    {
      buttons_down = 0;
      _gdk_pointer_grab_clear (TRUE);
      return;
    }
  for (button = 1; button <= 5; button++)
    if (buttons_down & (GDK_BUTTON1_MASK << (button - 1)))
      button_event (native, button, FALSE, _gdk_wayland_last_time);
}

/*
 * A scroll is a press and release of button 4, 5, 6 or 7, as X sent it:
 * one per notch of a wheel. From version 5 a frame gathers a scroll's
 * parts: a wheel's notches come counted, and a touchpad's motion comes
 * in small amounts that make a step for each 10, as GTK 3 has it.
 * Before version 5 each axis event is a step.
 */
#define SCROLL_STEP 10.0

static gdouble scroll_value[2];      /* this frame's, per axis */
static gint scroll_discrete[2];
static gdouble scroll_rest[2];       /* motion short of a whole step */
static guint32 scroll_source = WL_POINTER_AXIS_SOURCE_WHEEL;
static gboolean scroll_pending = FALSE;

static void
scroll_steps (guint axis, gint steps, guint32 time)
{
  GdkWindowPrivate *native = _gdk_wayland_pointer_window;
  guint button;

  if (!native || native->destroyed || steps == 0)
    return;
  if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
    button = steps < 0 ? 4 : 5;
  else
    button = steps < 0 ? 6 : 7;
  for (steps = ABS (steps); steps > 0; steps--)
    {
      button_event (native, button, TRUE, time);
      button_event (native, button, FALSE, time);
    }
}

static void
pointer_axis (void *data, struct wl_pointer *pointer, uint32_t time,
              uint32_t axis, wl_fixed_t value)
{
  gdouble v = wl_fixed_to_double (value);

  _gdk_wayland_last_time = time;
  if (axis > WL_POINTER_AXIS_HORIZONTAL_SCROLL || v == 0)
    return;
  if (wl_pointer_get_version (pointer) < WL_POINTER_FRAME_SINCE_VERSION)
    {
      scroll_steps (axis, v < 0 ? -1 : 1, time);
      return;
    }
  scroll_value[axis] += v;
  scroll_pending = TRUE;
}

static void
pointer_frame (void *data, struct wl_pointer *pointer)
{
  guint axis;

  if (!scroll_pending)
    return;
  for (axis = 0; axis < 2; axis++)
    {
      gdouble v = scroll_value[axis];
      gint steps;

      if (scroll_discrete[axis])
        steps = scroll_discrete[axis];
      else if (scroll_source == WL_POINTER_AXIS_SOURCE_WHEEL ||
               scroll_source == WL_POINTER_AXIS_SOURCE_WHEEL_TILT)
        steps = (v > 0) - (v < 0);
      else
        {
          scroll_rest[axis] += v;
          steps = (gint) (scroll_rest[axis] / SCROLL_STEP);
          scroll_rest[axis] -= steps * SCROLL_STEP;
        }
      scroll_steps (axis, steps, _gdk_wayland_last_time);
      scroll_value[axis] = 0;
      scroll_discrete[axis] = 0;
    }
  scroll_source = WL_POINTER_AXIS_SOURCE_WHEEL;
  scroll_pending = FALSE;
}

static void
pointer_axis_source (void *data, struct wl_pointer *pointer, uint32_t source)
{
  scroll_source = source;
}

/* A touchpad's scroll ended: what was short of a step goes. */
static void
pointer_axis_stop (void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis)
{
  if (axis <= WL_POINTER_AXIS_HORIZONTAL_SCROLL)
    scroll_rest[axis] = 0;
}

static void
pointer_axis_discrete (void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete)
{
  if (axis <= WL_POINTER_AXIS_HORIZONTAL_SCROLL)
    scroll_discrete[axis] += discrete;
}

/* Named, so that newer headers' later events are left NULL without
 * warning. The seat is bound at version 5 or lower, whose pointer
 * sends none of them; binding a later version needs handlers here. */
static const struct wl_pointer_listener pointer_listener = {
  .enter = pointer_enter,
  .leave = pointer_leave,
  .motion = pointer_motion,
  .button = pointer_button,
  .axis = pointer_axis,
  .frame = pointer_frame,
  .axis_source = pointer_axis_source,
  .axis_stop = pointer_axis_stop,
  .axis_discrete = pointer_axis_discrete,
};

/* ---------------------------------------------------------------------
 * The keyboard
 */

static void
keyboard_keymap (void *data, struct wl_keyboard *keyboard, uint32_t format,
                 int32_t fd, uint32_t size)
{
  char *map;

  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
    {
      close (fd);
      return;
    }
  map = mmap (NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  close (fd);
  if (map == MAP_FAILED)
    return;
  if (_gdk_xkb_state)
    xkb_state_unref (_gdk_xkb_state);
  if (_gdk_xkb_keymap)
    xkb_keymap_unref (_gdk_xkb_keymap);
  _gdk_xkb_keymap = xkb_keymap_new_from_string (_gdk_xkb_context, map,
                                                XKB_KEYMAP_FORMAT_TEXT_V1,
                                                XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap (map, size);
  if (!_gdk_xkb_keymap)
    return;
  _gdk_xkb_state = xkb_state_new (_gdk_xkb_keymap);
  mod_shift = xkb_keymap_mod_get_index (_gdk_xkb_keymap, XKB_MOD_NAME_SHIFT);
  mod_caps = xkb_keymap_mod_get_index (_gdk_xkb_keymap, XKB_MOD_NAME_CAPS);
  mod_ctrl = xkb_keymap_mod_get_index (_gdk_xkb_keymap, XKB_MOD_NAME_CTRL);
  mod_alt = xkb_keymap_mod_get_index (_gdk_xkb_keymap, XKB_MOD_NAME_ALT);
  mod_num = xkb_keymap_mod_get_index (_gdk_xkb_keymap, XKB_MOD_NAME_NUM);
  mod_super = xkb_keymap_mod_get_index (_gdk_xkb_keymap, XKB_MOD_NAME_LOGO);
}

/*
 * The keyboard is the compositor's to give a surface; among a group it
 * gives the leader's, and the events are for the group's active member.
 */
static void
focus_event (GdkWindowPrivate *native, gboolean in)
{
  _gdk_wayland_focus_event (_gdk_wayland_keyboard_target (native), in);
}

static void
repeat_stop (void)
{
  if (repeat_timeout)
    g_source_remove (repeat_timeout);
  repeat_timeout = 0;
  repeat_key = 0;
}

static void
keyboard_enter (void *data, struct wl_keyboard *keyboard, uint32_t serial,
                struct wl_surface *surface, struct wl_array *keys)
{
  GdkWindowPrivate *native = _gdk_wayland_window_from_surface (surface);

  _gdk_wayland_last_serial = serial;
  if (!native || native->destroyed)
    return;
  _gdk_wayland_keyboard_window = native;
  focus_event (native, TRUE);
}

static void
keyboard_leave (void *data, struct wl_keyboard *keyboard, uint32_t serial,
                struct wl_surface *surface)
{
  GdkWindowPrivate *native = _gdk_wayland_window_from_surface (surface);

  _gdk_wayland_last_serial = serial;
  repeat_stop ();
  if (!native || native->destroyed)
    return;
  focus_event (native, FALSE);
  if (_gdk_wayland_keyboard_window == native)
    _gdk_wayland_keyboard_window = NULL;
}

static void
key_event (guint32 key, gboolean down, guint32 time)
{
  GdkWindowPrivate *native = _gdk_wayland_keyboard_target (_gdk_wayland_keyboard_window);
  GdkEvent event;
  xkb_keycode_t code = key + 8;
  xkb_keysym_t sym;
  gchar buf[16];
  gint n;

  if (!native || native->destroyed || !_gdk_xkb_state)
    return;
  if (!(native->event_mask & (down ? GDK_KEY_PRESS_MASK : GDK_KEY_RELEASE_MASK)))
    return;

  sym = xkb_state_key_get_one_sym (_gdk_xkb_state, code);
  n = xkb_state_key_get_utf8 (_gdk_xkb_state, code, buf, sizeof buf);
  if (n > 0 && (guchar) buf[0] < 0x20)
    {
      /* Control characters as X gave them: the key's own. */
    }

  memset (&event, 0, sizeof event);
  event.key.type = down ? GDK_KEY_PRESS : GDK_KEY_RELEASE;
  event.key.window = (GdkWindow *) native;
  event.key.time = time;
  event.key.state = _gdk_wayland_modifiers | buttons_down;
  event.key.keyval = sym;
  event.key.length = n > 0 ? n : 0;
  event.key.string = n > 0 ? g_strndup (buf, n) : g_strdup ("");
  _gdk_wayland_put_event (&event);
  g_free (event.key.string);
}

static gint
repeat_tick (gpointer data)
{
  key_event (repeat_key, TRUE, _gdk_wayland_last_time);
  key_event (repeat_key, FALSE, _gdk_wayland_last_time);
  repeat_timeout = g_timeout_add (1000 / MAX (repeat_rate, 1), repeat_tick, NULL);
  return FALSE;
}

static void
keyboard_key (void *data, struct wl_keyboard *keyboard, uint32_t serial,
              uint32_t time, uint32_t key, uint32_t state)
{
  gboolean down = state == WL_KEYBOARD_KEY_STATE_PRESSED;

  _gdk_wayland_last_serial = serial;
  if (down)
    _gdk_wayland_input_serial = serial;
  _gdk_wayland_last_time = time;
  key_event (key, down, time);

  if (down)
    {
      repeat_stop ();
      if (_gdk_xkb_keymap && repeat_rate > 0 &&
          xkb_keymap_key_repeats (_gdk_xkb_keymap, key + 8))
        {
          repeat_key = key;
          repeat_timeout = g_timeout_add (repeat_delay, repeat_tick, NULL);
        }
    }
  else if (key == repeat_key)
    repeat_stop ();
}

static void
keyboard_modifiers (void *data, struct wl_keyboard *keyboard, uint32_t serial,
                    uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group)
{
  guint mods = 0;

  _gdk_wayland_last_serial = serial;
  if (!_gdk_xkb_state)
    return;
  xkb_state_update_mask (_gdk_xkb_state, depressed, latched, locked, 0, 0, group);

#define ACTIVE(m) (m != XKB_MOD_INVALID && \
                   xkb_state_mod_index_is_active (_gdk_xkb_state, m, XKB_STATE_MODS_EFFECTIVE) > 0)
  if (ACTIVE (mod_shift)) mods |= GDK_SHIFT_MASK;
  if (ACTIVE (mod_caps)) mods |= GDK_LOCK_MASK;
  if (ACTIVE (mod_ctrl)) mods |= GDK_CONTROL_MASK;
  if (ACTIVE (mod_alt)) mods |= GDK_MOD1_MASK;
  if (ACTIVE (mod_num)) mods |= GDK_MOD2_MASK;
  if (ACTIVE (mod_super)) mods |= GDK_MOD4_MASK;
#undef ACTIVE
  _gdk_wayland_modifiers = mods;
}

static void
keyboard_repeat_info (void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay)
{
  repeat_rate = rate;
  repeat_delay = delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
  keyboard_keymap, keyboard_enter, keyboard_leave, keyboard_key,
  keyboard_modifiers, keyboard_repeat_info
};

/* ---------------------------------------------------------------------
 * The seat
 */

void
_gdk_wayland_seat_capabilities (uint32_t capabilities)
{
  if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !_gdk_wl_pointer)
    {
      _gdk_wl_pointer = wl_seat_get_pointer (_gdk_wl_seat);
      wl_pointer_add_listener (_gdk_wl_pointer, &pointer_listener, NULL);
    }
  else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && _gdk_wl_pointer)
    {
      /* The pointer went: it left its window, holding nothing. */
      _gdk_wayland_release_buttons ();
      native_leave (_gdk_wayland_pointer_window);
      _gdk_wayland_pointer_window = NULL;
      if (wl_pointer_get_version (_gdk_wl_pointer) >= WL_POINTER_RELEASE_SINCE_VERSION)
        wl_pointer_release (_gdk_wl_pointer);
      else
        wl_pointer_destroy (_gdk_wl_pointer);
      _gdk_wl_pointer = NULL;
    }
  if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !_gdk_wl_keyboard)
    {
      _gdk_wl_keyboard = wl_seat_get_keyboard (_gdk_wl_seat);
      wl_keyboard_add_listener (_gdk_wl_keyboard, &keyboard_listener, NULL);
    }
  else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && _gdk_wl_keyboard)
    {
      /* The keyboard went: no key repeats, and its window loses focus. */
      repeat_stop ();
      if (_gdk_wayland_keyboard_window && !_gdk_wayland_keyboard_window->destroyed)
        focus_event (_gdk_wayland_keyboard_window, FALSE);
      _gdk_wayland_keyboard_window = NULL;
      if (wl_keyboard_get_version (_gdk_wl_keyboard) >= WL_KEYBOARD_RELEASE_SINCE_VERSION)
        wl_keyboard_release (_gdk_wl_keyboard);
      else
        wl_keyboard_destroy (_gdk_wl_keyboard);
      _gdk_wl_keyboard = NULL;
    }
}

void
_gdk_wayland_events_init (void)
{
  button_click_time[0] = button_click_time[1] = 0;
  button_window[0] = button_window[1] = NULL;
  button_number[0] = button_number[1] = -1;
}

void
_gdk_wayland_events_exit (void)
{
  repeat_stop ();
  if (_gdk_wl_pointer)
    wl_pointer_destroy (_gdk_wl_pointer);
  if (_gdk_wl_keyboard)
    wl_keyboard_destroy (_gdk_wl_keyboard);
  _gdk_wl_pointer = NULL;
  _gdk_wl_keyboard = NULL;
}
