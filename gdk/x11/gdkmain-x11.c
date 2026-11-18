/* GDK - The GIMP Drawing Kit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
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

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include <sys/select.h>

#define XLIB_ILLEGAL_ACCESS
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xos.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include "gdk.h"
#include "gdkprivate-x11.h"
#include "gdkx.h"
#include "gdki18n.h"
#include "gdkkeysyms.h"
#include "gdkbackend-x11.h"

#ifndef X_GETTIMEOFDAY
#define X_GETTIMEOFDAY(tv)  gettimeofday (tv, NULL)
#endif /* X_GETTIMEOFDAY */


typedef struct _GdkErrorTrap  GdkErrorTrap;

struct _GdkErrorTrap
{
  gint error_warnings;
  gint error_code;
};

/*
 * Private function declarations
 */


static int          gdk_x_error                  (Display     *display,
                                                  XErrorEvent *error);
static int          gdk_x_io_error               (Display     *display);

/* Private variable declarations
 */
static gint autorepeat;

static GSList *gdk_error_traps = NULL;               /* List of error traps */
static GSList *gdk_error_trap_free_list = NULL;      /* Free list */


/*
 *--------------------------------------------------------------
 * _gdk_x11_pointer_grab
 *
 *   Grabs the pointer to a specific window
 *
 * Arguments:
 *   "window" is the window which will receive the grab
 *   "owner_events" specifies whether events will be reported as is,
 *     or relative to "window"
 *   "event_mask" masks only interesting events
 *   "confine_to" limits the cursor movement to the specified window
 *   "cursor" changes the cursor for the duration of the grab
 *   "time" specifies the time
 *
 * Results:
 *
 * Side effects:
 *   requires a corresponding call to _gdk_x11_pointer_ungrab
 *
 *--------------------------------------------------------------
 */

/* ---------------------------------------------------------------------
 * Scale: X pixels to the toolkit's and back.
 */

gint
_gdk_x11_from_x (gint v)
{
  gint s = _gdk_x11_scale;

  if (s == 1)
    return v;
  /* Towards minus infinity, so that -1 is still to the left of 0. */
  return v >= 0 ? v / s : -((-v + s - 1) / s);
}

void
_gdk_x11_rect_from_x (GdkRectangle *rect)
{
  gint s = _gdk_x11_scale;
  gint x1, y1, x2, y2;

  if (s == 1)
    return;
  x1 = _gdk_x11_from_x (rect->x);
  y1 = _gdk_x11_from_x (rect->y);
  x2 = _gdk_x11_from_x (rect->x + rect->width + s - 1);
  y2 = _gdk_x11_from_x (rect->y + rect->height + s - 1);
  rect->x = x1;
  rect->y = y1;
  rect->width = x2 - x1;
  rect->height = y2 - y1;
}

/* The crossing events a grab makes for the windows inside the native one. */
static void
gdk_x11_grab_crossing (GdkWindowPrivate *window, gboolean grabbed, guint32 time)
{
  GdkWindowPrivate *native = _gdk_window_native (window, NULL, NULL);
  Window root, child;
  int x_root, y_root, x, y;
  unsigned int mask;

  if (native->destroyed || !native->impl || native->impl->xwindow == None)
    return;

  if (!XQueryPointer (gdk_display, native->impl->xwindow, &root, &child,
                      &x_root, &y_root, &x, &y, &mask))
    return;
  x_root = GDK_X11_FROM_X (x_root);
  y_root = GDK_X11_FROM_X (y_root);
  x = GDK_X11_FROM_X (x);
  y = GDK_X11_FROM_X (y);

  /*
   * The round trip brought whatever the server had to say first -- an
   * enter for a window just mapped under the pointer, say -- and that
   * comes before the grab's own crossing, as it did for the server.
   */
  _gdk_x11_events_queue ();

  _gdk_window_grab_crossing (window, grabbed, x, y, x_root, y_root, time, mask);
}

gint
_gdk_x11_pointer_grab (GdkWindow *        window,
                  gint            owner_events,
                  GdkEventMask    event_mask,
                  GdkWindow *     confine_to,
                  GdkCursor *     cursor,
                  guint32         time)
{
  /*  From gdkwindow.c  */
  gint return_val;
  GdkWindowPrivate *window_private;
  GdkWindowPrivate *confine_to_private;
  GdkCursorPrivate *cursor_private;
  guint xevent_mask;
  Window xwindow;
  Window xconfine_to;
  Cursor xcursor;
  int i;

  g_return_val_if_fail (window != NULL, 0);

  window_private = (GdkWindowPrivate*) window;
  confine_to_private = (GdkWindowPrivate*) confine_to;
  cursor_private = (GdkCursorPrivate*) cursor;

  /* The grab is on the native window; the window inside gets the events. */
  xwindow = gdk_x11_window_xid (window);

  if (!confine_to || confine_to_private->destroyed)
    xconfine_to = None;
  else
    xconfine_to = gdk_x11_window_xid (confine_to);

  if (!cursor)
    xcursor = None;
  else
    xcursor = gdk_x11_cursor_xcursor ((GdkCursor *) cursor_private);


  xevent_mask = 0;
  for (i = 0; i < gdk_nevent_masks; i++)
    {
      if (event_mask & (1 << (i + 1)))
        xevent_mask |= gdk_event_mask_table[i];
    }

  if (!window_private->destroyed)
    return_val = XGrabPointer (gdk_display,
                               xwindow,
                               owner_events,
                               xevent_mask | ButtonPressMask | ButtonReleaseMask |
                               PointerMotionMask | EnterWindowMask | LeaveWindowMask,
                               GrabModeAsync, GrabModeAsync,
                               xconfine_to,
                               xcursor,
                               time);
  else
    return_val = AlreadyGrabbed;

  if (return_val == GrabSuccess)
    {
      gdk_xgrab_window = window_private;
      _gdk_pointer_grab_owner_events = owner_events;
      _gdk_pointer_grab_set (window_private, owner_events, event_mask, FALSE);
      gdk_x11_grab_crossing (window_private, TRUE, time);
    }

  return return_val;
}

/*
 *--------------------------------------------------------------
 * _gdk_x11_pointer_ungrab
 *
 *   Releases any pointer grab
 *
 * Arguments:
 *
 * Results:
 *
 * Side effects:
 *
 *--------------------------------------------------------------
 */

void
_gdk_x11_pointer_ungrab (guint32 time)
{
  if (gdk_xgrab_window && !gdk_xgrab_window->destroyed)
    gdk_x11_grab_crossing (gdk_xgrab_window, FALSE, time);
  XUngrabPointer (gdk_display, time);
  gdk_xgrab_window = NULL;
  _gdk_pointer_grab_clear (FALSE);
}

/*
 *--------------------------------------------------------------
 * _gdk_x11_pointer_is_grabbed
 *
 *   Tell wether there is an active x pointer grab in effect
 *
 * Arguments:
 *
 * Results:
 *
 * Side effects:
 *
 *--------------------------------------------------------------
 */

gboolean
_gdk_x11_pointer_is_grabbed (void)
{
  return gdk_xgrab_window != NULL;
}

/*
 *--------------------------------------------------------------
 * _gdk_x11_keyboard_grab
 *
 *   Grabs the keyboard to a specific window
 *
 * Arguments:
 *   "window" is the window which will receive the grab
 *   "owner_events" specifies whether events will be reported as is,
 *     or relative to "window"
 *   "time" specifies the time
 *
 * Results:
 *
 * Side effects:
 *   requires a corresponding call to _gdk_x11_keyboard_ungrab
 *
 *--------------------------------------------------------------
 */

gint
_gdk_x11_keyboard_grab (GdkWindow *        window,
                   gint            owner_events,
                   guint32         time)
{
  GdkWindowPrivate *window_private;
  Window xwindow;

  g_return_val_if_fail (window != NULL, 0);

  window_private = (GdkWindowPrivate*) window;
  xwindow = gdk_x11_window_xid (window);

  if (!window_private->destroyed)
    return XGrabKeyboard (window_private->impl->xdisplay,
                          xwindow,
                          owner_events,
                          GrabModeAsync, GrabModeAsync,
                          time);
  else
    return AlreadyGrabbed;
}

/*
 *--------------------------------------------------------------
 * _gdk_x11_keyboard_ungrab
 *
 *   Releases any keyboard grab
 *
 * Arguments:
 *
 * Results:
 *
 * Side effects:
 *
 *--------------------------------------------------------------
 */

void
_gdk_x11_keyboard_ungrab (guint32 time)
{
  XUngrabKeyboard (gdk_display, time);
}

/*
 *--------------------------------------------------------------
 * _gdk_x11_screen_width
 *
 *   Return the width of the screen.
 *
 * Arguments:
 *
 * Results:
 *
 * Side effects:
 *
 *--------------------------------------------------------------
 */

gint
_gdk_x11_screen_width (void)
{
  gint return_val;

  return_val = GDK_X11_FROM_X (DisplayWidth (gdk_display, gdk_screen));

  return return_val;
}

/*
 *--------------------------------------------------------------
 * _gdk_x11_screen_height
 *
 *   Return the height of the screen.
 *
 * Arguments:
 *
 * Results:
 *
 * Side effects:
 *
 *--------------------------------------------------------------
 */

gint
_gdk_x11_screen_height (void)
{
  gint return_val;

  return_val = GDK_X11_FROM_X (DisplayHeight (gdk_display, gdk_screen));

  return return_val;
}

/*
 *--------------------------------------------------------------
 * _gdk_x11_screen_width_mm
 *
 *   Return the width of the screen in millimeters.
 *
 * Arguments:
 *
 * Results:
 *
 * Side effects:
 *
 *--------------------------------------------------------------
 */

gint
_gdk_x11_screen_width_mm (void)
{
  gint return_val;

  return_val = DisplayWidthMM (gdk_display, gdk_screen);

  return return_val;
}

/*
 *--------------------------------------------------------------
 * _gdk_x11_screen_height
 *
 *   Return the height of the screen in millimeters.
 *
 * Arguments:
 *
 * Results:
 *
 * Side effects:
 *
 *--------------------------------------------------------------
 */

gint
_gdk_x11_screen_height_mm (void)
{
  gint return_val;

  return_val = DisplayHeightMM (gdk_display, gdk_screen);

  return return_val;
}

/* The window manager may move a window, but where a program puts it is
 * where it goes. */
gboolean
_gdk_x11_can_place_toplevels (void)
{
  return TRUE;
}

/*
 *--------------------------------------------------------------
 * _gdk_x11_set_sm_client_id
 *
 *   Set the SM_CLIENT_ID property on the WM_CLIENT_LEADER window
 *   so that the window manager can save our state using the
 *   X11R6 ICCCM session management protocol. A NULL value should
 *   be set following disconnection from the session manager to
 *   remove the SM_CLIENT_ID property.
 *
 * Arguments:
 *
 *   "sm_client_id" specifies the client id assigned to us by the
 *   session manager or NULL to remove the property.
 *
 * Results:
 *
 * Side effects:
 *
 *--------------------------------------------------------------
 */

void
_gdk_x11_set_sm_client_id (const gchar* sm_client_id)
{
  if (sm_client_id && strcmp (sm_client_id, ""))
    {
      XChangeProperty (gdk_display, gdk_leader_window,
                       _gdk_x11_atom_intern ("SM_CLIENT_ID", FALSE),
                       XA_STRING, 8, PropModeReplace,
                       (unsigned char *) sm_client_id, strlen(sm_client_id));
    }
  else
     XDeleteProperty (gdk_display, gdk_leader_window,
                      _gdk_x11_atom_intern ("SM_CLIENT_ID", FALSE));
}

void
_gdk_x11_key_repeat_disable (void)
{
  XAutoRepeatOff (gdk_display);
}

void
_gdk_x11_key_repeat_restore (void)
{
  if (autorepeat)
    XAutoRepeatOn (gdk_display);
  else
    XAutoRepeatOff (gdk_display);
}


void
_gdk_x11_beep (void)
{
  XBell(gdk_display, 100);
}

/*
 *--------------------------------------------------------------
 * gdk_x_error
 *
 *   The X error handling routine.
 *
 * Arguments:
 *   "display" is the X display the error orignated from.
 *   "error" is the XErrorEvent that we are handling.
 *
 * Results:
 *   Either we were expecting some sort of error to occur,
 *   in which case we set the "gdk_error_code" flag, or this
 *   error was unexpected, in which case we will print an
 *   error message and exit. (Since trying to continue will
 *   most likely simply lead to more errors).
 *
 * Side effects:
 *
 *--------------------------------------------------------------
 */

static int
gdk_x_error (Display     *display,
             XErrorEvent *error)
{
  if (error->error_code)
    {
      if (gdk_error_warnings)
        {
          char buf[64];

          XGetErrorText (display, error->error_code, buf, 63);

#ifdef G_ENABLE_DEBUG
          g_error ("%s\n  serial %ld error_code %d request_code %d minor_code %d\n",
                   buf,
                   error->serial,
                   error->error_code,
                   error->request_code,
                   error->minor_code);
#else /* !G_ENABLE_DEBUG */
          fprintf (stderr, "Gdk-ERROR **: %s\n  serial %ld error_code %d request_code %d minor_code %d\n",
                   buf,
                   error->serial,
                   error->error_code,
                   error->request_code,
                   error->minor_code);

          exit(1);
#endif /* G_ENABLE_DEBUG */
        }
      gdk_error_code = error->error_code;
    }

  return 0;
}

/*
 *--------------------------------------------------------------
 * gdk_x_io_error
 *
 *   The X I/O error handling routine.
 *
 * Arguments:
 *   "display" is the X display the error orignated from.
 *
 * Results:
 *   An X I/O error basically means we lost our connection
 *   to the X server. There is not much we can do to
 *   continue, so simply print an error message and exit.
 *
 * Side effects:
 *
 *--------------------------------------------------------------
 */

static int
gdk_x_io_error (Display *display)
{
  /* This is basically modelled after the code in XLib. We need
   * an explicit error handler here, so we can disable our atexit()
   * which would otherwise cause a nice segfault.
   * We fprintf(stderr, instead of g_warning() because g_warning()
   * could possibly be redirected to a dialog
   */
  if (errno == EPIPE)
    {
      fprintf (stderr, "Gdk-ERROR **: X connection to %s broken (explicit kill or server shutdown).\n", gdk_display ? DisplayString (gdk_display) : gdk_get_display());
    }
  else
    {
      fprintf (stderr, "Gdk-ERROR **: Fatal IO error %d (%s) on X server %s.\n",
               errno, g_strerror (errno),
               gdk_display ? DisplayString (gdk_display) : gdk_get_display());
    }

  /* Nothing more can be said to the server; leave without closing it. */
  gdk_display = NULL;
  exit(1);
}


/*************************************************************
 * _gdk_x11_error_trap_push:
 *     Push an error trap. X errors will be trapped until
 *     the corresponding gdk_error_pop(), which will return
 *     the error code, if any.
 *   arguments:
 *
 *   results:
 *************************************************************/

void
_gdk_x11_error_trap_push (void)
{
  GSList *node;
  GdkErrorTrap *trap;

  if (gdk_error_trap_free_list)
    {
      node = gdk_error_trap_free_list;
      gdk_error_trap_free_list = gdk_error_trap_free_list->next;
    }
  else
    {
      node = g_slist_alloc ();
      node->data = g_new (GdkErrorTrap, 1);
    }

  node->next = gdk_error_traps;
  gdk_error_traps = node;

  trap = node->data;
  trap->error_code = gdk_error_code;
  trap->error_warnings = gdk_error_warnings;

  gdk_error_code = 0;
  gdk_error_warnings = 0;
}

/*************************************************************
 * _gdk_x11_error_trap_pop:
 *     Pop an error trap added with gdk_error_push()
 *   arguments:
 *
 *   results:
 *     0, if no error occured, otherwise the error code.
 *************************************************************/

gint
_gdk_x11_error_trap_pop (void)
{
  GSList *node;
  GdkErrorTrap *trap;
  gint result;

  g_return_val_if_fail (gdk_error_traps != NULL, 0);

  node = gdk_error_traps;
  gdk_error_traps = gdk_error_traps->next;

  node->next = gdk_error_trap_free_list;
  gdk_error_trap_free_list = node;

  result = gdk_error_code;

  trap = node->data;
  gdk_error_code = trap->error_code;
  gdk_error_warnings = trap->error_warnings;

  return result;
}

gint
gdk_send_xevent (Window window, gboolean propagate, glong event_mask,
                 XEvent *event_send)
{
  Status result;
  gint old_warnings = gdk_error_warnings;

  gdk_error_code = 0;

  gdk_error_warnings = 0;
  result = XSendEvent (gdk_display, window, propagate, event_mask, event_send);
  XSync (gdk_display, False);
  gdk_error_warnings = old_warnings;

  return result && !gdk_error_code;
}


gchar*
_gdk_x11_keyval_name (guint           keyval)
{
  return XKeysymToString (keyval);
}

guint
_gdk_x11_keyval_from_name (const gchar *keyval_name)
{
  g_return_val_if_fail (keyval_name != NULL, 0);

  return XStringToKeysym (keyval_name);
}

guint
_gdk_x11_keyval_to_upper (guint   keyval)
{
  if (keyval)
    {
      KeySym lower_val = 0;
      KeySym upper_val = 0;

      XConvertCase (keyval, &lower_val, &upper_val);
      return upper_val;
    }
  return 0;
}

guint
_gdk_x11_keyval_to_lower (guint   keyval)
{
  if (keyval)
    {
      KeySym lower_val = 0;
      KeySym upper_val = 0;

      XConvertCase (keyval, &lower_val, &upper_val);
      return lower_val;
    }
  return 0;
}

/* ---------------------------------------------------------------------
 * XSETTINGS: the desktop's settings, as the settings daemon publishes
 * them on the window that owns the _XSETTINGS_S<screen> selection, in
 * its _XSETTINGS_SETTINGS property. Read once, at init, for the two
 * or three numbers that matter to a toolkit that draws its own
 * windows: no daemon means no settings, and that is fine.
 */

static guchar *xsettings_data = NULL;
static gulong  xsettings_length = 0;

static void
xsettings_read (void)
{
  gchar name[32];
  Atom selection, property, type;
  Window owner;
  gint format;
  gulong nitems, left;
  guchar *data = NULL;

  g_snprintf (name, sizeof name, "_XSETTINGS_S%d", gdk_screen);
  selection = XInternAtom (gdk_display, name, False);
  property = XInternAtom (gdk_display, "_XSETTINGS_SETTINGS", False);
  owner = XGetSelectionOwner (gdk_display, selection);
  if (owner == None)
    return;
  _gdk_x11_error_trap_push ();
  if (XGetWindowProperty (gdk_display, owner, property, 0, 0x10000, False,
                          property, &type, &format, &nitems, &left, &data) == Success
      && type == property && format == 8 && nitems >= 12)
    {
      xsettings_data = data;
      xsettings_length = nitems;
    }
  else if (data)
    XFree (data);
  _gdk_x11_error_trap_pop ();
}

/* One 32 bit number of the settings, in the byte order they came in. */
static guint32
xsettings_card32 (const guchar *p)
{
  if (xsettings_data[0] == MSBFirst)
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
  return p[0] | (p[1] << 8) | (p[2] << 16) | ((guint32) p[3] << 24);
}

/* An integer setting by name: TRUE and its value if the desktop set it. */
static gboolean
xsettings_int (const gchar *name, gint *value)
{
  const guchar *p, *end;
  guint32 n, i;
  gsize name_len = strlen (name);

  if (!xsettings_data)
    return FALSE;
  p = xsettings_data;
  end = p + xsettings_length;
  n = xsettings_card32 (p + 8);
  p += 12;
  for (i = 0; i < n && p + 4 <= end; i++)
    {
      guint8 type = p[0];
      guint32 len = (xsettings_data[0] == MSBFirst) ? (p[2] << 8) | p[3]
                                                     : p[2] | (p[3] << 8);
      const guchar *setting_name = p + 4;

      p += 4 + ((len + 3) & ~3);
      if (p + 4 > end)
        break;
      p += 4;                       /* last-change serial */
      switch (type)
        {
        case 0:                     /* integer */
          if (p + 4 > end)
            return FALSE;
          if (len == name_len && memcmp (setting_name, name, len) == 0)
            {
              *value = (gint32) xsettings_card32 (p);
              return TRUE;
            }
          p += 4;
          break;
        case 1:                     /* string */
          if (p + 4 > end)
            return FALSE;
          {
            guint32 slen = xsettings_card32 (p);
            p += 4 + ((slen + 3) & ~3);
          }
          break;
        case 2:                     /* colour */
          p += 8;
          break;
        default:
          return FALSE;
        }
    }
  return FALSE;
}

/*
 * The scale the desktop wants its windows at, and the resolution to
 * size fonts by, the way GTK 3 reads them. Gdk/WindowScalingFactor is
 * what a settings daemon publishes when the user asks for 2x; with no
 * daemon, Xft.dpi is what they set with xrdb, and 192 means 2x. When
 * the scale comes from the desktop, the Xft.dpi it publishes is for
 * clients that scale by resolution, so it is that many times too big
 * for the points our fonts are sized in: Gdk/UnscaledDPI says the
 * real one, else it is divided back. GDK_SCALE in the environment
 * is a plain multiplier and leaves the resolution alone, as in GTK 3.
 */
static gboolean scale_from_desktop = FALSE;

static gint
scale_from_display (void)
{
  gint scale;
  const char *value;

  if (xsettings_int ("Gdk/WindowScalingFactor", &scale) && scale >= 1)
    {
      scale_from_desktop = TRUE;
      return scale;
    }
  value = XGetDefault (gdk_display, "Xft", "dpi");
  if (value && atof (value) >= 2 * 96)
    {
      scale_from_desktop = TRUE;
      return (gint) (atof (value) / 96);
    }
  return 1;
}

gdouble
_gdk_x11_dpi (void)
{
  gint setting;
  const char *value;
  gdouble dpi;

  if (xsettings_int ("Gdk/UnscaledDPI", &setting) && setting > 0)
    return setting / 1024.0;
  if (xsettings_int ("Xft/DPI", &setting) && setting > 0)
    dpi = setting / 1024.0;
  else
    {
      value = XGetDefault (gdk_display, "Xft", "dpi");
      if (!value)
        return 0;
      dpi = atof (value);
    }
  if (scale_from_desktop && _gdk_x11_scale > 1)
    dpi /= _gdk_x11_scale;
  return dpi;
}

/*
 * The backend's start and end: the display opened, the leader window
 * and the atoms every window needs, and each part of the backend
 * started; then, at the end, closed again.
 */
gboolean
_gdk_x11_init (const gchar *display_name,
               gboolean     synchronize,
               const gchar *program_class)
{
  XKeyboardState keyboard_state;
  XClassHint *class_hint;

  XSetErrorHandler (gdk_x_error);
  XSetIOErrorHandler (gdk_x_io_error);

  gdk_display = XOpenDisplay (display_name);
  if (!gdk_display)
    return FALSE;

  if (synchronize)
    XSynchronize (gdk_display, True);

  gdk_screen = DefaultScreen (gdk_display);
  gdk_root_window = RootWindow (gdk_display, gdk_screen);

  /*
   * The scale: what the desktop's settings say, unless GDK_SCALE says
   * otherwise. Every window on the display is at this scale from the
   * start.
   */
  xsettings_read ();
  _gdk_window_set_default_scale (scale_from_display ());
  _gdk_x11_scale = gdk_screen_scale_factor ();

  gdk_leader_window = XCreateSimpleWindow(gdk_display, gdk_root_window,
                                          10, 10, 10, 10, 0, 0 , 0);
  class_hint = XAllocClassHint();
  class_hint->res_name = g_get_prgname ();
  class_hint->res_class = (char *) program_class;
  XmbSetWMProperties (gdk_display, gdk_leader_window,
                      NULL, NULL, NULL, 0,
                      NULL, NULL, class_hint);
  XFree (class_hint);

  gdk_wm_delete_window = XInternAtom (gdk_display, "WM_DELETE_WINDOW", False);
  gdk_wm_take_focus = XInternAtom (gdk_display, "WM_TAKE_FOCUS", False);
  gdk_wm_protocols = XInternAtom (gdk_display, "WM_PROTOCOLS", False);
  gdk_wm_window_protocols[0] = gdk_wm_delete_window;
  gdk_wm_window_protocols[1] = gdk_wm_take_focus;
  gdk_selection_property = XInternAtom (gdk_display, "GDK_SELECTION", False);

  XGetKeyboardControl (gdk_display, &keyboard_state);
  autorepeat = keyboard_state.global_auto_repeat;

  gdk_events_init ();
  _gdk_x11_publish_init ();
  gdk_window_init ();
  gdk_image_init ();
  gdk_dnd_init ();

#ifdef USE_XIM
  gdk_im_open ();
#endif

  return TRUE;
}

void
_gdk_x11_exit (void)
{
  if (!gdk_display)
    return;
#ifdef USE_XIM
  gdk_im_close ();
#endif
  _gdk_x11_key_repeat_restore ();
  XCloseDisplay (gdk_display);
  gdk_display = NULL;
}

/* The last reference to a window is gone: what X knew it as goes too. */
void
_gdk_x11_window_free (GdkWindowPrivate *private)
{
  if (!private->destroyed && private->window_type == GDK_WINDOW_FOREIGN)
    gdk_xid_table_remove (private->impl->xwindow);
  if (private->impl->cursor)
    _gdk_x11_cursor_impl_unref (private->impl->cursor);
  if (private->impl->xcolormap != None && gdk_display)
    XFreeColormap (gdk_display, private->impl->xcolormap);
  g_free (private->impl);
  private->impl = NULL;
}
