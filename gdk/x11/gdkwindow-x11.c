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

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <netinet/in.h>
#include "gdk.h"
#include "config.h"
#include "gdkprivate-x11.h"
#include "gdkbackend-x11.h"
#include "MwmUtil.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>


#ifdef HAVE_SHAPE_EXT
#include <X11/extensions/shape.h>
#endif
#ifdef HAVE_XRANDR
#include <X11/extensions/Xrandr.h>
#endif

const int gdk_event_mask_table[20] =
{
  ExposureMask,
  PointerMotionMask,
  PointerMotionHintMask,
  ButtonMotionMask,
  Button1MotionMask,
  Button2MotionMask,
  Button3MotionMask,
  ButtonPressMask | OwnerGrabButtonMask,
  ButtonReleaseMask | OwnerGrabButtonMask,
  KeyPressMask,
  KeyReleaseMask,
  EnterWindowMask,
  LeaveWindowMask,
  FocusChangeMask,
  StructureNotifyMask,
  PropertyChangeMask,
  VisibilityChangeMask,
  0,                            /* PROXIMITY_IN */
  0,                            /* PROXIMTY_OUT */
  SubstructureNotifyMask
};
const int gdk_nevent_masks = sizeof (gdk_event_mask_table) / sizeof (int);

/* Forward declarations */
static gboolean gdk_window_have_shape_ext (void);

void
gdk_window_init (void)
{
  XWindowAttributes xattributes;
  unsigned int width;
  unsigned int height;
  unsigned int border_width;
  unsigned int depth;
  int x, y;

  XGetGeometry (gdk_display, gdk_root_window, &gdk_root_window,
                &x, &y, &width, &height, &border_width, &depth);
  XGetWindowAttributes (gdk_display, gdk_root_window, &xattributes);

  _gdk_window_private_init (&gdk_root_parent);
  gdk_root_parent.impl = _gdk_x11_impl_new (gdk_root_window);
  gdk_root_parent.native = TRUE;
  gdk_root_parent.window_type = GDK_WINDOW_ROOT;
  gdk_root_parent.window.user_data = NULL;
  gdk_root_parent.width = GDK_X11_FROM_X (width);
  gdk_root_parent.height = GDK_X11_FROM_X (height);
  gdk_root_parent.children = NULL;
  gdk_root_parent.colormap = NULL;
  gdk_root_parent.ref_count = 1;

  gdk_xid_table_insert (&gdk_root_window, &gdk_root_parent);
}

static GdkAtom wm_client_leader_atom = GDK_NONE;

/* EWMH window managers and compositors -- including the X window managers
 * built into Wayland compositors for XWayland -- read titles from the UTF-8
 * _NET_WM_NAME and _NET_WM_ICON_NAME properties and may not decode the
 * ICCCM encodings (STRING, COMPOUND_TEXT) used for WM_NAME and
 * WM_ICON_NAME. Set the UTF-8 properties as well; Xlib converts from the
 * locale's encoding.
 */
static void
gdk_window_set_utf8_name (GdkWindowPrivate *private,
                          const char       *property,
                          const gchar      *text)
{
#ifdef X_HAVE_UTF8_STRING
  XTextProperty prop;
  char *list = (char *) text;   /* Xlib's prototype predates const */

  if (XmbTextListToTextProperty (private->impl->xdisplay, &list, 1,
                                 XUTF8StringStyle, &prop) >= 0)
    {
      XSetTextProperty (private->impl->xdisplay, GDK_WINDOW_XID (private), &prop,
                        _gdk_x11_atom_intern (property, FALSE));
      if (prop.value)
        XFree (prop.value);
    }
#else
  (void) private; (void) property; (void) text;
#endif
}

GdkWindow*
_gdk_x11_window_new (GdkWindow     *parent,
                GdkWindowAttr *attributes,
                gint           attributes_mask)
{
  GdkWindow *window;
  GdkWindowPrivate *private;
  GdkWindowPrivate *parent_private;
  GdkVisual *visual;
  Display *parent_display;
  Window xparent;
  Visual *xvisual;
  XSetWindowAttributes xattributes;
  long xattributes_mask;
  XSizeHints size_hints;
  XWMHints wm_hints;
  XClassHint *class_hint;
  int x, y, depth;
  unsigned int class;
  char *title;
  int i;

  g_return_val_if_fail (attributes != NULL, NULL);

  if (!parent)
    parent = (GdkWindow*) &gdk_root_parent;

  parent_private = (GdkWindowPrivate*) parent;
  if (parent_private->destroyed)
    return NULL;

  xparent = parent_private->impl->xwindow;
  parent_display = parent_private->impl->xdisplay;

  private = g_new0 (GdkWindowPrivate, 1);
  window = (GdkWindow*) private;
  _gdk_window_private_init (private);
  private->impl = _gdk_x11_impl_new (None);

  private->parent = parent;

  private->impl->xdisplay = parent_display;
  private->destroyed = FALSE;
  private->mapped = FALSE;
  private->resize_count = 0;
  private->ref_count = 1;
  xattributes_mask = 0;

  if (attributes_mask & GDK_WA_X)
    x = attributes->x;
  else
    x = 0;

  if (attributes_mask & GDK_WA_Y)
    y = attributes->y;
  else
    y = 0;

  private->x = x;
  private->y = y;
  private->width = (attributes->width > 1) ? (attributes->width) : (1);
  private->height = (attributes->height > 1) ? (attributes->height) : (1);
  private->window_type = attributes->window_type;
  private->extension_events = FALSE;

  private->filters = NULL;
  private->children = NULL;

  window->user_data = NULL;

  if (attributes_mask & GDK_WA_VISUAL)
    visual = attributes->visual;
  else
    visual = gdk_visual_get_system ();
  xvisual = _gdk_x11_visual ();

  private->event_mask = attributes->event_mask;

  /*
   * A child is a rectangle of its toplevel unless it asks to be a
   * window of X's own. A native window gets every pointer, key and
   * crossing event whether it asked or not, since the windows inside
   * it can only be served through it.
   */
  private->native = private->window_type != GDK_WINDOW_CHILD ||
    (attributes_mask & GDK_WA_NATIVE) != 0;

  xattributes.event_mask = StructureNotifyMask | ExposureMask |
    ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
    EnterWindowMask | LeaveWindowMask | KeyPressMask | KeyReleaseMask |
    FocusChangeMask;
  for (i = 0; i < gdk_nevent_masks; i++)
    {
      if (attributes->event_mask & (1 << (i + 1)))
        xattributes.event_mask |= gdk_event_mask_table[i];
    }
  xattributes.event_mask &= ~PointerMotionHintMask;

  if (xattributes.event_mask)
    xattributes_mask |= CWEventMask;

  if (attributes_mask & GDK_WA_NOREDIR)
    {
      xattributes.override_redirect =
        (attributes->override_redirect == FALSE)?False:True;
      xattributes_mask |= CWOverrideRedirect;
    }
  else
    xattributes.override_redirect = False;

  if (attributes->wclass == GDK_INPUT_OUTPUT)
    {
      class = InputOutput;
      depth = _gdk_x11_depth ();
      private->depth = visual->depth;

      if (attributes_mask & GDK_WA_COLORMAP)
        private->colormap = attributes->colormap;
      else
        private->colormap = gdk_colormap_get_system ();

      /*
       * The window shows nothing but what GDK publishes into it: no
       * background for the server to paint, so nothing flashes before
       * the framebuffer arrives.
       */
      xattributes.background_pixmap = None;
      xattributes.border_pixel = BlackPixel (gdk_display, gdk_screen);
      xattributes_mask |= CWBorderPixel | CWBackPixmap;

      switch (private->window_type)
        {
        case GDK_WINDOW_TOPLEVEL:
          xattributes.colormap = _gdk_x11_colormap ();
          xattributes_mask |= CWColormap;

          xparent = gdk_root_window;
          break;

        case GDK_WINDOW_CHILD:
          xattributes.colormap = _gdk_x11_colormap ();
          xattributes_mask |= CWColormap;
          break;

        case GDK_WINDOW_DIALOG:
          xattributes.colormap = _gdk_x11_colormap ();
          xattributes_mask |= CWColormap;

          xparent = gdk_root_window;
          break;

        case GDK_WINDOW_TEMP:
          xattributes.colormap = _gdk_x11_colormap ();
          xattributes_mask |= CWColormap;

          xparent = gdk_root_window;

          xattributes.save_under = True;
          xattributes.override_redirect = True;
          xattributes.cursor = None;
          xattributes_mask |= CWSaveUnder | CWOverrideRedirect;
          break;
        case GDK_WINDOW_ROOT:
          g_error ("cannot make windows of type GDK_WINDOW_ROOT");
          break;
        case GDK_WINDOW_PIXMAP:
          g_error ("cannot make windows of type GDK_WINDOW_PIXMAP (use gdk_pixmap_new)");
          break;
        }
    }
  else
    {
      depth = 0;
      private->depth = 0;
      class = InputOnly;
      private->colormap = gdk_colormap_get_system ();
    }

  if (private->native)
    {
      /*
       * Under the nearest X window: the native ancestor's, which is
       * where the parent is only if the parent is native itself.
       */
      if (private->window_type == GDK_WINDOW_CHILD)
        {
          gint ox, oy;

          xparent = gdk_x11_window_xid (parent);
          _gdk_window_native (parent_private, &ox, &oy);
          x += ox;
          y += oy;
        }
      private->impl->xwindow = XCreateWindow (private->impl->xdisplay, xparent,
                                              GDK_X11_TO_X (x), GDK_X11_TO_X (y),
                                              GDK_X11_TO_X (private->width),
                                              GDK_X11_TO_X (private->height),
                                              0, depth, class, xvisual,
                                              xattributes_mask, &xattributes);
      gdk_xid_table_insert (&private->impl->xwindow, window);
      /* The table's, dropped when the X window's destruction is heard of:
       * a window with no X window of its own is never in it. */
      gdk_window_ref (window);
    }

  if (private->colormap)
    gdk_colormap_ref (private->colormap);

  if (parent_private)
    parent_private->children = g_list_prepend (parent_private->children, window);

  _gdk_x11_window_set_cursor (window, ((attributes_mask & GDK_WA_CURSOR) ?
                                  (attributes->cursor) :
                                  NULL));

  if (!private->native)
    return window;

  switch (private->window_type)
    {
    case GDK_WINDOW_DIALOG:
      XSetTransientForHint (private->impl->xdisplay, private->impl->xwindow, xparent);
    case GDK_WINDOW_TOPLEVEL:
    case GDK_WINDOW_TEMP:
      XSetWMProtocols (private->impl->xdisplay, private->impl->xwindow, gdk_wm_window_protocols, 2);
      break;
    case GDK_WINDOW_CHILD:
      return window;
    default:

      return window;
    }

  size_hints.flags = PSize;
  size_hints.width = GDK_X11_TO_X (private->width);
  size_hints.height = GDK_X11_TO_X (private->height);

  wm_hints.flags = InputHint | StateHint | WindowGroupHint;
  wm_hints.window_group = gdk_leader_window;
  wm_hints.input = True;
  wm_hints.initial_state = NormalState;

  /* FIXME: Is there any point in doing this? Do any WM's pay
   * attention to PSize, and even if they do, is this the
   * correct value???
   */
  XSetWMNormalHints (private->impl->xdisplay, private->impl->xwindow, &size_hints);

  XSetWMHints (private->impl->xdisplay, private->impl->xwindow, &wm_hints);

  if (!wm_client_leader_atom)
    wm_client_leader_atom = _gdk_x11_atom_intern ("WM_CLIENT_LEADER", FALSE);

  XChangeProperty (private->impl->xdisplay, private->impl->xwindow,
                   wm_client_leader_atom,
                   XA_WINDOW, 32, PropModeReplace,
                   (guchar*) &gdk_leader_window, 1);

  if (attributes_mask & GDK_WA_TITLE)
    title = attributes->title;
  else
    title = g_get_prgname ();

  XmbSetWMProperties (private->impl->xdisplay, private->impl->xwindow,
                      title, title,
                      NULL, 0,
                      NULL, NULL, NULL);
  gdk_window_set_utf8_name (private, "_NET_WM_NAME", title);
  gdk_window_set_utf8_name (private, "_NET_WM_ICON_NAME", title);

  if (attributes_mask & GDK_WA_WMCLASS)
    {
      class_hint = XAllocClassHint ();
      class_hint->res_name = attributes->wmclass_name;
      class_hint->res_class = attributes->wmclass_class;
      XSetClassHint (private->impl->xdisplay, private->impl->xwindow, class_hint);
      XFree (class_hint);
    }


  return window;
}

GdkWindow *
_gdk_x11_window_foreign_new (guint32 anid)
{
  GdkWindow *window;
  GdkWindowPrivate *private;
  GdkWindowPrivate *parent_private;
  XWindowAttributes attrs;
  Window root, parent;
  Window *children = NULL;
  guint nchildren;
  gboolean result;

  _gdk_x11_error_trap_push ();
  result = XGetWindowAttributes (gdk_display, anid, &attrs);
  if (_gdk_x11_error_trap_pop () || !result)
    return NULL;

  /* FIXME: This is pretty expensive. Maybe the caller should supply
   *        the parent */
  _gdk_x11_error_trap_push ();
  result = XQueryTree (gdk_display, anid, &root, &parent, &children, &nchildren);
  if (_gdk_x11_error_trap_pop () || !result)
    return NULL;

  private = g_new0 (GdkWindowPrivate, 1);
  window = (GdkWindow*) private;
  _gdk_window_private_init (private);
  private->impl = _gdk_x11_impl_new (anid);

  if (children)
    XFree (children);
  private->parent = gdk_xid_table_lookup (parent);

  parent_private = (GdkWindowPrivate *)private->parent;

  if (parent_private)
    parent_private->children = g_list_prepend (parent_private->children, window);

  private->impl->xwindow = anid;
  private->impl->xdisplay = gdk_display;
  private->x = GDK_X11_FROM_X (attrs.x);
  private->y = GDK_X11_FROM_X (attrs.y);
  private->width = GDK_X11_FROM_X (attrs.width);
  private->height = GDK_X11_FROM_X (attrs.height);
  private->resize_count = 0;
  private->ref_count = 1;
  private->window_type = GDK_WINDOW_FOREIGN;
  private->native = TRUE;
  private->destroyed = FALSE;
  private->mapped = (attrs.map_state != IsUnmapped);
  private->extension_events = 0;

  private->colormap = NULL;

  private->filters = NULL;
  private->children = NULL;

  window->user_data = NULL;

  gdk_window_ref (window);
  gdk_xid_table_insert (&private->impl->xwindow, window);

  return window;
}

/* Call this function when you want a window and all its children to
 * disappear. When xdestroy is true, a request to destroy the XWindow
 * is sent out. When it is false, it is assumed that the XWindow has
 * been or will be destroyed by destroying some ancestor of this
 * window.
 */
static void
gdk_window_internal_destroy (GdkWindow *window,
                             gboolean   xdestroy,
                             gboolean   our_destroy)
{
  GdkWindowPrivate *private;
  GdkWindowPrivate *temp_private;
  GdkWindow *temp_window;
  GList *children;
  GList *tmp;

  g_return_if_fail (window != NULL);

  private = (GdkWindowPrivate*) window;

  switch (private->window_type)
    {
    case GDK_WINDOW_TOPLEVEL:
    case GDK_WINDOW_CHILD:
    case GDK_WINDOW_DIALOG:
    case GDK_WINDOW_TEMP:
    case GDK_WINDOW_FOREIGN:
      if (!private->destroyed)
        {
          /* Whatever draws an external window lets go while X still has it. */
          _gdk_window_external_release (private);

          if (private->mapped)
            {
              private->mapped = FALSE;
              _gdk_window_mapped (window, FALSE);
            }
          if (private->parent)
            {
              GdkWindowPrivate *parent_private = (GdkWindowPrivate *)private->parent;
              if (parent_private->children)
                parent_private->children = g_list_remove (parent_private->children, window);
            }

          if (private->window_type != GDK_WINDOW_FOREIGN)
            {
              children = tmp = private->children;
              private->children = NULL;

              while (tmp)
                {
                  temp_window = tmp->data;
                  tmp = tmp->next;

                  /*
                   * X destroys the windows inside a window it destroys;
                   * a window with no X window of its own takes nothing
                   * with it, so the native windows inside it go one by one.
                   */
                  temp_private = (GdkWindowPrivate*) temp_window;
                  if (temp_private)
                    gdk_window_internal_destroy (temp_window,
                                                 xdestroy && !private->native,
                                                 our_destroy);
                }

              g_list_free (children);
            }

          if (private->filters)
            {
              tmp = private->filters;

              while (tmp)
                {
                  g_free (tmp->data);
                  tmp = tmp->next;
                }

              g_list_free (private->filters);
              private->filters = NULL;
            }

          if (private->window_type == GDK_WINDOW_FOREIGN)
            {
              if (our_destroy && (private->parent != NULL))
                {
                  /* It's somebody elses window, but in our heirarchy,
                   * so reparent it to the root window, and then send
                   * it a delete event, as if we were a WM
                   */
                  XClientMessageEvent xevent;

                  _gdk_x11_error_trap_push ();
                  _gdk_x11_window_hide (window);
                  _gdk_x11_window_reparent (window, NULL, 0, 0);

                  xevent.type = ClientMessage;
                  xevent.window = private->impl->xwindow;
                  xevent.message_type = gdk_wm_protocols;
                  xevent.format = 32;
                  xevent.data.l[0] = gdk_wm_delete_window;
                  xevent.data.l[1] = CurrentTime;

                  XSendEvent (private->impl->xdisplay, private->impl->xwindow,
                              False, 0, (XEvent *)&xevent);
                  gdk_flush ();
                  _gdk_x11_error_trap_pop ();
                }
            }
          else if (xdestroy && private->native)
            XDestroyWindow (private->impl->xdisplay, private->impl->xwindow);

          if (private->colormap)
            gdk_colormap_unref (private->colormap);
          private->colormap = NULL;
          _gdk_window_private_free (private);

          private->mapped = FALSE;
          private->destroyed = TRUE;
        }
      break;

    case GDK_WINDOW_ROOT:
      g_error ("attempted to destroy root window");
      break;

    case GDK_WINDOW_PIXMAP:
      g_error ("called _gdk_x11_window_destroy on a pixmap (use gdk_pixmap_unref)");
      break;
    }
}

/* Like internal_destroy, but also destroys the reference created by
   _gdk_x11_window_new. */

void
_gdk_x11_window_destroy (GdkWindow *window)
{
  gdk_window_internal_destroy (window, TRUE, TRUE);
  gdk_window_unref (window);
}

/* This function is called when the XWindow is really gone. */

void
gdk_window_destroy_notify (GdkWindow *window)
{
  GdkWindowPrivate *private;

  g_return_if_fail (window != NULL);

  private = (GdkWindowPrivate*) window;

  if (!private->destroyed)
    {
      if (private->window_type != GDK_WINDOW_FOREIGN)
        g_warning ("GdkWindow %#lx unexpectedly destroyed", private->impl->xwindow);

      gdk_window_internal_destroy (window, FALSE, FALSE);
    }

  gdk_xid_table_remove (private->impl->xwindow);
  gdk_window_unref (window);
}



static void gdk_window_restack (GdkWindowPrivate *private, gboolean raise);

/*
 * Where a window's parent is in the X window a native child of it
 * sits in: nothing for a toplevel, or under a parent that is native.
 */
static void
gdk_window_x11_parent_offset (GdkWindowPrivate *private, gint *x, gint *y)
{
  GdkWindowPrivate *parent = (GdkWindowPrivate *) private->parent;

  *x = *y = 0;
  if (private->window_type == GDK_WINDOW_CHILD && parent)
    _gdk_window_native (parent, x, y);
}

/*
 * Where a window is in the X window it sits in: for a native window,
 * its native ancestor's; for any other, the same, since it has no X
 * window of its own.
 */
static GdkWindowPrivate *
gdk_window_x11_parent_native (GdkWindowPrivate *private, gint *x, gint *y)
{
  GdkWindowPrivate *parent = (GdkWindowPrivate *) private->parent;
  gint ox, oy;
  GdkWindowPrivate *native;

  if (!parent)
    {
      *x = private->x;
      *y = private->y;
      return NULL;
    }
  native = _gdk_window_native (parent, &ox, &oy);
  *x = ox + private->x;
  *y = oy + private->y;
  return native;
}

/*
 * The native windows inside a window that is not native have X windows
 * placed and mapped by what the windows between them and their X parent
 * say: after one of those moves, shows or hides, they follow.
 */
static void
gdk_window_x11_sync_natives (GdkWindowPrivate *private)
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
          gint x, y;

          for (w = (GdkWindowPrivate *) child->parent; w && !w->native;
               w = (GdkWindowPrivate *) w->parent)
            if (!w->mapped)
              viewable = FALSE;
          gdk_window_x11_parent_native (child, &x, &y);
          XMoveWindow (gdk_display, child->impl->xwindow,
                       GDK_X11_TO_X (x), GDK_X11_TO_X (y));
          if (viewable)
            XMapWindow (gdk_display, child->impl->xwindow);
          else
            XUnmapWindow (gdk_display, child->impl->xwindow);
        }
      else
        gdk_window_x11_sync_natives (child);
    }
}

/*
 * The native windows inside a window that is not native sit in the X
 * window of its native ancestor. When that becomes another -- the
 * window was given another parent, or an X window of its own -- they
 * are put into it.
 */
static void
gdk_window_x11_rehome_natives (GdkWindowPrivate *private)
{
  GList *l;

  for (l = private->children; l; l = l->next)
    {
      GdkWindowPrivate *child = l->data;

      if (child->destroyed)
        continue;
      if (child->native)
        {
          GdkWindowPrivate *native;
          gint x, y;

          native = gdk_window_x11_parent_native (child, &x, &y);
          if (native && child->window_type == GDK_WINDOW_CHILD)
            XReparentWindow (gdk_display, child->impl->xwindow,
                             native->impl->xwindow,
                             GDK_X11_TO_X (x), GDK_X11_TO_X (y));
        }
      else
        gdk_window_x11_rehome_natives (child);
    }
}

/*
 * A child made a window of X's own after the fact, for something that
 * needs one: GL, or another program's window. What it showed goes on
 * showing; from now on it draws into a framebuffer of its own.
 */
gboolean
_gdk_x11_window_begin_move_drag (GdkWindow *window, gint button,
                                 gint x_root, gint y_root, guint32 time)
{
  return FALSE;
}

void
_gdk_x11_window_ensure_native (GdkWindow *window)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  GdkWindowPrivate *native;
  XSetWindowAttributes xattributes;
  gint x, y;

  g_return_if_fail (window != NULL);

  if (private->native || private->destroyed)
    return;

  native = gdk_window_x11_parent_native (private, &x, &y);
  if (!native)
    return;

  xattributes.event_mask = StructureNotifyMask | ExposureMask |
    ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
    EnterWindowMask | LeaveWindowMask | KeyPressMask | KeyReleaseMask |
    FocusChangeMask;
  xattributes.background_pixmap = None;
  xattributes.border_pixel = BlackPixel (gdk_display, gdk_screen);
  xattributes.colormap = _gdk_x11_colormap ();
  xattributes.cursor = private->impl->cursor ? private->impl->cursor->xcursor : None;
  private->impl->xwindow = XCreateWindow (gdk_display, native->impl->xwindow,
                                          GDK_X11_TO_X (x), GDK_X11_TO_X (y),
                                          GDK_X11_TO_X (private->width),
                                          GDK_X11_TO_X (private->height), 0,
                                          private->depth ? _gdk_x11_depth () : 0,
                                          private->depth ? InputOutput : InputOnly,
                                          _gdk_x11_visual (),
                                          CWEventMask | CWBackPixmap | CWBorderPixel |
                                          CWColormap | CWCursor, &xattributes);
  gdk_xid_table_insert (&private->impl->xwindow, window);
  /* The table's, as in _gdk_x11_window_new. */
  gdk_window_ref (window);
  private->native = TRUE;
  _gdk_x11_window_set_events (window, private->event_mask);

  /* Its own X children now sit under its X window. */
  gdk_window_x11_rehome_natives (private);
  gdk_window_x11_sync_natives (private);
  if (private->mapped)
    gdk_window_x11_sync_natives ((GdkWindowPrivate *) private->parent);
}

void
_gdk_x11_window_show (GdkWindow *window)
{
  GdkWindowPrivate *private;

  g_return_if_fail (window != NULL);

  private = (GdkWindowPrivate*) window;
  if (!private->destroyed)
    {
      private->mapped = TRUE;
      if (private->native)
        {
          XRaiseWindow (private->impl->xdisplay, private->impl->xwindow);
          XMapWindow (private->impl->xdisplay, private->impl->xwindow);
        }
      gdk_window_restack (private, TRUE);
      _gdk_window_mapped (window, TRUE);
      if (!private->native)
        gdk_window_x11_sync_natives (private);
    }
}

void
_gdk_x11_window_hide (GdkWindow *window)
{
  GdkWindowPrivate *private;

  g_return_if_fail (window != NULL);

  private = (GdkWindowPrivate*) window;
  if (!private->destroyed)
    {
      private->mapped = FALSE;
      if (private->native)
        XUnmapWindow (private->impl->xdisplay, private->impl->xwindow);
      _gdk_window_mapped (window, FALSE);
      if (!private->native)
        gdk_window_x11_sync_natives (private);
    }
}

void
_gdk_x11_window_withdraw (GdkWindow *window)
{
  GdkWindowPrivate *private;

  g_return_if_fail (window != NULL);

  private = (GdkWindowPrivate*) window;
  if (!private->destroyed && private->native)
    XWithdrawWindow (private->impl->xdisplay, private->impl->xwindow, 0);
}

void
_gdk_x11_window_move (GdkWindow *window,
                 gint       x,
                 gint       y)
{
  GdkWindowPrivate *private;

  g_return_if_fail (window != NULL);

  private = (GdkWindowPrivate*) window;
  if (!private->destroyed)
    {
      if (private->native)
        {
          gint ox, oy;

          gdk_window_x11_parent_offset (private, &ox, &oy);
          XMoveWindow (private->impl->xdisplay, private->impl->xwindow,
                       GDK_X11_TO_X (ox + x), GDK_X11_TO_X (oy + y));
        }

      if (private->window_type == GDK_WINDOW_CHILD)
        {
          gint old_x = private->x, old_y = private->y;

          private->x = x;
          private->y = y;
          _gdk_window_geometry_changed (window, old_x, old_y,
                                        private->width, private->height);
          if (!private->native)
            gdk_window_x11_sync_natives (private);
        }
    }
}

void
_gdk_x11_window_resize (GdkWindow *window,
                   gint       width,
                   gint       height)
{
  GdkWindowPrivate *private;

  g_return_if_fail (window != NULL);

  if (width < 1)
    width = 1;
  if (height < 1)
    height = 1;

  private = (GdkWindowPrivate*) window;

  if (!private->destroyed &&
      ((private->resize_count > 0) ||
       (private->width != (guint16) width) ||
       (private->height != (guint16) height)))
    {
      if (private->native)
        {
          XResizeWindow (private->impl->xdisplay, private->impl->xwindow,
                         GDK_X11_TO_X (width), GDK_X11_TO_X (height));
          private->resize_count += 1;
        }

      if (private->window_type == GDK_WINDOW_CHILD)
        {
          gint old_width = private->width, old_height = private->height;

          private->width = width;
          private->height = height;
          _gdk_window_geometry_changed (window, private->x, private->y,
                                        old_width, old_height);
          /* A framebuffer of its own is of the size it had. */
          if (private->native)
            _gdk_window_resized (window);
        }
    }
}

void
_gdk_x11_window_move_resize (GdkWindow *window,
                        gint       x,
                        gint       y,
                        gint       width,
                        gint       height)
{
  GdkWindowPrivate *private;

  g_return_if_fail (window != NULL);

  if (width < 1)
    width = 1;
  if (height < 1)
    height = 1;

  private = (GdkWindowPrivate*) window;
  if (!private->destroyed)
    {
      if (private->native)
        {
          gint ox, oy;

          gdk_window_x11_parent_offset (private, &ox, &oy);
          XMoveResizeWindow (private->impl->xdisplay, private->impl->xwindow,
                             GDK_X11_TO_X (ox + x), GDK_X11_TO_X (oy + y),
                             GDK_X11_TO_X (width), GDK_X11_TO_X (height));
          /* A toplevel learns its size from the server: until it does, a
           * resize back to the size it last knew must still be sent */
          private->resize_count += 1;
        }

      if (private->window_type == GDK_WINDOW_CHILD)
        {
          gint old_x = private->x, old_y = private->y;
          gint old_width = private->width, old_height = private->height;

          private->x = x;
          private->y = y;
          private->width = width;
          private->height = height;
          _gdk_window_geometry_changed (window, old_x, old_y,
                                        old_width, old_height);
          if (!private->native)
            gdk_window_x11_sync_natives (private);
          else if (width != old_width || height != old_height)
            _gdk_window_resized (window);
        }
    }
}

void
_gdk_x11_window_reparent (GdkWindow *window,
                     GdkWindow *new_parent,
                     gint       x,
                     gint       y)
{
  GdkWindowPrivate *window_private;
  GdkWindowPrivate *parent_private;
  GdkWindowPrivate *old_parent_private;
  gboolean was_mapped;

  g_return_if_fail (window != NULL);

  if (!new_parent)
    new_parent = (GdkWindow*) &gdk_root_parent;

  window_private = (GdkWindowPrivate*) window;
  old_parent_private = (GdkWindowPrivate*)window_private->parent;
  parent_private = (GdkWindowPrivate*) new_parent;

  if (!window_private->destroyed && !parent_private->destroyed &&
      window_private->native)
    {
      gint ox, oy;

      /* Where the new parent is in the X window it sits in. */
      _gdk_window_native (parent_private, &ox, &oy);
      XReparentWindow (window_private->impl->xdisplay,
                       window_private->impl->xwindow,
                       gdk_x11_window_xid (new_parent),
                       GDK_X11_TO_X (ox + x), GDK_X11_TO_X (oy + y));
    }

  /* To the old picture it is gone; to the new one it is arriving. */
  was_mapped = window_private->mapped;
  if (was_mapped)
    {
      window_private->mapped = FALSE;
      _gdk_window_mapped (window, FALSE);
    }

  window_private->parent = new_parent;
  window_private->x = x;
  window_private->y = y;

  if (old_parent_private)
    old_parent_private->children = g_list_remove (old_parent_private->children, window);

  parent_private->children = g_list_prepend (parent_private->children, window);

  if (was_mapped)
    {
      window_private->mapped = TRUE;
      _gdk_window_mapped (window, TRUE);
    }
  if (!window_private->native && !window_private->destroyed &&
      !parent_private->destroyed)
    {
      gdk_window_x11_rehome_natives (window_private);
      gdk_window_x11_sync_natives (window_private);
    }
}

/* A raised window goes to the head of its parent's list; a lowered one
 * to the tail: the list is the stacking order, top first. */
static void
gdk_window_restack (GdkWindowPrivate *private, gboolean raise)
{
  GdkWindowPrivate *parent = (GdkWindowPrivate *) private->parent;

  if (!parent || !g_list_find (parent->children, private))
    return;
  parent->children = g_list_remove (parent->children, private);
  if (raise)
    parent->children = g_list_prepend (parent->children, private);
  else
    parent->children = g_list_append (parent->children, private);
  _gdk_window_restacked ((GdkWindow *) private);
}

void
_gdk_x11_window_raise (GdkWindow *window)
{
  GdkWindowPrivate *private;

  g_return_if_fail (window != NULL);

  private = (GdkWindowPrivate*) window;

  if (!private->destroyed)
    {
      if (private->native)
        XRaiseWindow (private->impl->xdisplay, private->impl->xwindow);
      gdk_window_restack (private, TRUE);
    }
}

void
_gdk_x11_window_lower (GdkWindow *window)
{
  GdkWindowPrivate *private;

  g_return_if_fail (window != NULL);

  private = (GdkWindowPrivate*) window;

  if (!private->destroyed)
    {
      if (private->native)
        XLowerWindow (private->impl->xdisplay, private->impl->xwindow);
      gdk_window_restack (private, FALSE);
    }
}


void
_gdk_x11_window_set_hints (GdkWindow *window,
                      gint       x,
                      gint       y,
                      gint       min_width,
                      gint       min_height,
                      gint       max_width,
                      gint       max_height,
                      gint       flags)
{
  GdkWindowPrivate *private;
  XSizeHints size_hints;

  g_return_if_fail (window != NULL);

  private = (GdkWindowPrivate*) window;
  if (private->destroyed)
    return;

  size_hints.flags = 0;

  if (flags & GDK_HINT_POS)
    {
      size_hints.flags |= PPosition;
      size_hints.x = GDK_X11_TO_X (x);
      size_hints.y = GDK_X11_TO_X (y);
    }

  if (flags & GDK_HINT_MIN_SIZE)
    {
      size_hints.flags |= PMinSize;
      size_hints.min_width = GDK_X11_TO_X (min_width);
      size_hints.min_height = GDK_X11_TO_X (min_height);
    }

  if (flags & GDK_HINT_MAX_SIZE)
    {
      size_hints.flags |= PMaxSize;
      size_hints.max_width = GDK_X11_TO_X (max_width);
      size_hints.max_height = GDK_X11_TO_X (max_height);
    }

  /* FIXME: Would it be better to delete this property of
   *        flags == 0? It would save space on the server
   */
  XSetWMNormalHints (private->impl->xdisplay, GDK_WINDOW_XID (private), &size_hints);
}

void
_gdk_x11_window_set_geometry_hints (GdkWindow      *window,
                               GdkGeometry    *geometry,
                               GdkWindowHints  geom_mask)
{
  GdkWindowPrivate *private;
  XSizeHints size_hints;

  g_return_if_fail (window != NULL);

  private = (GdkWindowPrivate*) window;
  if (private->destroyed)
    return;

  size_hints.flags = 0;

  if (geom_mask & GDK_HINT_POS)
    {
      size_hints.flags |= PPosition;
      /* We need to initialize the following obsolete fields because KWM
       * apparently uses these fields if they are non-zero.
       * #@#!#!$!.
       */
      size_hints.x = 0;
      size_hints.y = 0;
    }

  if (geom_mask & GDK_HINT_MIN_SIZE)
    {
      size_hints.flags |= PMinSize;
      size_hints.min_width = GDK_X11_TO_X (geometry->min_width);
      size_hints.min_height = GDK_X11_TO_X (geometry->min_height);
    }

  if (geom_mask & GDK_HINT_MAX_SIZE)
    {
      size_hints.flags |= PMaxSize;
      size_hints.max_width = GDK_X11_TO_X (MAX (geometry->max_width, 1));
      size_hints.max_height = GDK_X11_TO_X (MAX (geometry->max_height, 1));
    }

  if (geom_mask & GDK_HINT_BASE_SIZE)
    {
      size_hints.flags |= PBaseSize;
      size_hints.base_width = GDK_X11_TO_X (geometry->base_width);
      size_hints.base_height = GDK_X11_TO_X (geometry->base_height);
    }

  if (geom_mask & GDK_HINT_RESIZE_INC)
    {
      size_hints.flags |= PResizeInc;
      size_hints.width_inc = GDK_X11_TO_X (geometry->width_inc);
      size_hints.height_inc = GDK_X11_TO_X (geometry->height_inc);
    }

  if (geom_mask & GDK_HINT_ASPECT)
    {
      size_hints.flags |= PAspect;
      if (geometry->min_aspect <= 1)
        {
          size_hints.min_aspect.x = 65536 * geometry->min_aspect;
          size_hints.min_aspect.y = 65536;
        }
      else
        {
          size_hints.min_aspect.x = 65536;
          size_hints.min_aspect.y = 65536 / geometry->min_aspect;;
        }
      if (geometry->max_aspect <= 1)
        {
          size_hints.max_aspect.x = 65536 * geometry->max_aspect;
          size_hints.max_aspect.y = 65536;
        }
      else
        {
          size_hints.max_aspect.x = 65536;
          size_hints.max_aspect.y = 65536 / geometry->max_aspect;;
        }
    }

  /* FIXME: Would it be better to delete this property of
   *        geom_mask == 0? It would save space on the server
   */
  XSetWMNormalHints (private->impl->xdisplay, GDK_WINDOW_XID (private), &size_hints);
}

void
_gdk_x11_window_set_title (GdkWindow   *window,
                      const gchar *title)
{
  GdkWindowPrivate *private;

  g_return_if_fail (window != NULL);

  private = (GdkWindowPrivate*) window;
  if (!private->destroyed)
    {
      XmbSetWMProperties (private->impl->xdisplay, GDK_WINDOW_XID (private),
                          title, title, NULL, 0, NULL, NULL, NULL);
      gdk_window_set_utf8_name (private, "_NET_WM_NAME", title);
      gdk_window_set_utf8_name (private, "_NET_WM_ICON_NAME", title);
    }
}

void
_gdk_x11_window_set_role (GdkWindow   *window,
                     const gchar *role)
{
  GdkWindowPrivate *private;

  g_return_if_fail (window != NULL);

  private = (GdkWindowPrivate*) window;

  if (role)
    XChangeProperty (private->impl->xdisplay, GDK_WINDOW_XID (private),
                     _gdk_x11_atom_intern ("WM_WINDOW_ROLE", FALSE), XA_STRING,
                     8, PropModeReplace, (unsigned char *) role, strlen (role));
  else
    XDeleteProperty (private->impl->xdisplay, GDK_WINDOW_XID (private),
                     _gdk_x11_atom_intern ("WM_WINDOW_ROLE", FALSE));
}

void
_gdk_x11_window_set_transient_for (GdkWindow *window,
                              GdkWindow *parent)
{
  GdkWindowPrivate *private;
  GdkWindowPrivate *parent_private;

  g_return_if_fail (window != NULL);

  private = (GdkWindowPrivate*) window;
  parent_private = (GdkWindowPrivate*) parent;

  if (!private->destroyed && !parent_private->destroyed)
    XSetTransientForHint (private->impl->xdisplay,
                          GDK_WINDOW_XID (private), GDK_WINDOW_XID (parent_private));
}



void
_gdk_x11_window_set_cursor (GdkWindow *window,
                       GdkCursor *cursor)
{
  GdkWindowPrivate *window_private;
  GdkCursorPrivate *cursor_private;
  Cursor xcursor;

  g_return_if_fail (window != NULL);

  window_private = (GdkWindowPrivate*) window;
  cursor_private = (GdkCursorPrivate*) cursor;

  if (!cursor)
    xcursor = None;
  else
    xcursor = gdk_x11_cursor_xcursor ((GdkCursor *) cursor_private);

  if (window_private->destroyed)
    return;

  /* Kept per window; the native window shows the pointer window's. */
  if (window_private->impl->cursor)
    _gdk_x11_cursor_impl_unref (window_private->impl->cursor);
  window_private->impl->cursor = cursor ? _gdk_x11_cursor_impl_ref (GDK_CURSOR_IMPL (cursor)) : NULL;
  if (window_private->native)
    XDefineCursor (window_private->impl->xdisplay, window_private->impl->xwindow, xcursor);
  else
    _gdk_x11_cursor_changed ((GdkWindow *) _gdk_window_native (window_private, NULL, NULL));
}

/*
 * The cursor a native window shows: the one set on the window the
 * pointer is in, or the nearest ancestor's that has one.
 */
void
_gdk_x11_cursor_changed (GdkWindow *window)
{
  GdkWindowPrivate *native = (GdkWindowPrivate *) window;
  GdkWindowPrivate *w = native->pointer_window ? native->pointer_window : native;
  Cursor xcursor = None;

  if (native->destroyed || !native->impl || native->impl->xwindow == None)
    return;
  for (; w; w = (GdkWindowPrivate *) w->parent)
    {
      if (w->impl && w->impl->cursor)
        {
          xcursor = w->impl->cursor->xcursor;
          break;
        }
      if (w == native)
        break;
    }
  if (native->impl->xcursor_shown != xcursor)
    {
      native->impl->xcursor_shown = xcursor;
      XDefineCursor (native->impl->xdisplay, native->impl->xwindow, xcursor);
    }
}

void
_gdk_x11_window_set_colormap (GdkWindow   *window,
                         GdkColormap *colormap)
{
  GdkWindowPrivate *window_private;

  g_return_if_fail (window != NULL);
  g_return_if_fail (colormap != NULL);

  window_private = (GdkWindowPrivate*) window;

  if (!window_private->destroyed)
    {
      /* There is one X colormap, which every window has already. */
      if (window_private->colormap)
        gdk_colormap_unref (window_private->colormap);
      window_private->colormap = colormap;
      gdk_colormap_ref (window_private->colormap);
    }
}


void
_gdk_x11_window_get_geometry (GdkWindow *window,
                         gint      *x,
                         gint      *y,
                         gint      *width,
                         gint      *height,
                         gint      *depth)
{
  GdkWindowPrivate *window_private;
  Window root;
  gint tx;
  gint ty;
  guint twidth;
  guint theight;
  guint tborder_width;
  guint tdepth;

  if (!window)
    window = (GdkWindow*) &gdk_root_parent;

  window_private = (GdkWindowPrivate*) window;

  if (!window_private->destroyed && !window_private->native)
    {
      if (x)
        *x = window_private->x;
      if (y)
        *y = window_private->y;
      if (width)
        *width = window_private->width;
      if (height)
        *height = window_private->height;
      if (depth)
        *depth = window_private->depth;
    }
  else if (!window_private->destroyed)
    {
      XGetGeometry (window_private->impl->xdisplay, window_private->impl->xwindow,
                    &root, &tx, &ty, &twidth, &theight, &tborder_width, &tdepth);

      if (x)
        *x = GDK_X11_FROM_X (tx);
      if (y)
        *y = GDK_X11_FROM_X (ty);
      if (width)
        *width = GDK_X11_FROM_X (twidth);
      if (height)
        *height = GDK_X11_FROM_X (theight);
      if (depth)
        *depth = tdepth;
    }
}






gint
_gdk_x11_window_get_origin (GdkWindow *window,
                       gint      *x,
                       gint      *y)
{
  GdkWindowPrivate *private;
  gint return_val;
  Window child;
  gint tx = 0;
  gint ty = 0;

  g_return_val_if_fail (window != NULL, 0);

  private = (GdkWindowPrivate*) window;

  if (!private->destroyed)
    {
      gint ox, oy;
      GdkWindowPrivate *native = _gdk_window_native (private, &ox, &oy);

      return_val = XTranslateCoordinates (native->impl->xdisplay,
                                          native->impl->xwindow,
                                          gdk_root_window,
                                          GDK_X11_TO_X (ox), GDK_X11_TO_X (oy),
                                          &tx, &ty, &child);
    }
  else
    return_val = 0;

  if (x)
    *x = GDK_X11_FROM_X (tx);
  if (y)
    *y = GDK_X11_FROM_X (ty);

  return return_val;
}

gboolean
_gdk_x11_window_get_deskrelative_origin (GdkWindow *window,
                                    gint      *x,
                                    gint      *y)
{
  GdkWindowPrivate *private;
  gboolean return_val = FALSE;
  gint num_children, format_return;
  Window win, *child, parent, root;
  gint tx = 0;
  gint ty = 0;
  Atom type_return;
  static Atom atom = 0;
  gulong number_return, bytes_after_return;
  guchar *data_return;

  g_return_val_if_fail (window != NULL, 0);

  private = (GdkWindowPrivate*) window;

  if (!private->destroyed)
    {
      gint ox, oy;
      GdkWindowPrivate *native = _gdk_window_native (private, &ox, &oy);

      if (!atom)
        atom = XInternAtom (private->impl->xdisplay, "ENLIGHTENMENT_DESKTOP", False);
      win = native->impl->xwindow;

      while (XQueryTree (private->impl->xdisplay, win, &root, &parent,
                         &child, (unsigned int *)&num_children))
        {
          if ((child) && (num_children > 0))
            XFree (child);

          if (!parent)
            break;
          else
            win = parent;

          if (win == root)
            break;

          data_return = NULL;
          XGetWindowProperty (private->impl->xdisplay, win, atom, 0, 0,
                              False, XA_CARDINAL, &type_return, &format_return,
                              &number_return, &bytes_after_return, &data_return);
          if (type_return == XA_CARDINAL)
            {
              XFree (data_return);
              break;
            }
        }

      return_val = XTranslateCoordinates (private->impl->xdisplay,
                                          native->impl->xwindow,
                                          win,
                                          GDK_X11_TO_X (ox), GDK_X11_TO_X (oy),
                                          &tx, &ty, &root);
      if (x)
        *x = GDK_X11_FROM_X (tx);
      if (y)
        *y = GDK_X11_FROM_X (ty);
    }


  return return_val;
}

void
_gdk_x11_window_get_root_origin (GdkWindow *window,
                            gint      *x,
                            gint      *y)
{
  GdkWindowPrivate *private;
  Window xwindow;
  Window xparent;
  Window root;
  Window *children;
  unsigned int nchildren;

  g_return_if_fail (window != NULL);

  private = (GdkWindowPrivate*) window;
  if (x)
    *x = 0;
  if (y)
    *y = 0;
  if (private->destroyed)
    return;

  while (private->parent && ((GdkWindowPrivate*) private->parent)->parent)
    private = (GdkWindowPrivate*) private->parent;
  if (private->destroyed)
    return;

  xparent = private->impl->xwindow;
  do
    {
      xwindow = xparent;
      if (!XQueryTree (private->impl->xdisplay, xwindow,
                       &root, &xparent,
                       &children, &nchildren))
        return;

      if (children)
        XFree (children);
    }
  while (xparent != root);

  if (xparent == root)
    {
      unsigned int ww, wh, wb, wd;
      int wx, wy;

      if (XGetGeometry (private->impl->xdisplay, xwindow, &root, &wx, &wy, &ww, &wh, &wb, &wd))
        {
          if (x)
            *x = GDK_X11_FROM_X (wx);
          if (y)
            *y = GDK_X11_FROM_X (wy);
        }
    }
}

GdkWindow*
_gdk_x11_window_get_pointer (GdkWindow       *window,
                        gint            *x,
                        gint            *y,
                        GdkModifierType *mask)
{
  GdkWindowPrivate *private;
  GdkWindow *return_val;
  Window root;
  Window child;
  int rootx, rooty;
  int winx = 0;
  int winy = 0;
  unsigned int xmask = 0;

  if (!window)
    window = (GdkWindow*) &gdk_root_parent;

  private = (GdkWindowPrivate*) window;

  return_val = NULL;
  if (!private->destroyed)
    {
      gint ox, oy;
      GdkWindowPrivate *native = _gdk_window_native (private, &ox, &oy);

      if (XQueryPointer (native->impl->xdisplay, native->impl->xwindow, &root, &child,
                         &rootx, &rooty, &winx, &winy, &xmask))
        {
          GdkWindowPrivate *below;

          winx = GDK_X11_FROM_X (winx) - ox;
          winy = GDK_X11_FROM_X (winy) - oy;
          /* The child of the window the pointer is in, if any. */
          below = _gdk_window_child_at (private, winx, winy);
          while (below && below != private && below->parent != window)
            below = (GdkWindowPrivate *) below->parent;
          if (below && below != private)
            return_val = (GdkWindow *) below;
          else if (child)
            return_val = _gdk_x11_window_lookup (child);
        }
    }

  if (x)
    *x = winx;
  if (y)
    *y = winy;
  if (mask)
    *mask = xmask;

  return return_val;
}

GdkWindow*
_gdk_x11_window_at_pointer (gint *win_x,
                       gint *win_y)
{
  GdkWindowPrivate *private;
  GdkWindow *window;
  Window root;
  Window xwindow;
  Window xwindow_last = 0;
  int rootx = -1, rooty = -1;
  int winx, winy;
  unsigned int xmask;

  private = &gdk_root_parent;

  xwindow = private->impl->xwindow;

  XGrabServer (private->impl->xdisplay);
  while (xwindow)
    {
      xwindow_last = xwindow;
      XQueryPointer (private->impl->xdisplay,
                     xwindow,
                     &root, &xwindow,
                     &rootx, &rooty,
                     &winx, &winy,
                     &xmask);
    }
  XUngrabServer (private->impl->xdisplay);

  window = _gdk_x11_window_lookup (xwindow_last);
  winx = GDK_X11_FROM_X (winx);
  winy = GDK_X11_FROM_X (winy);
  if (window)
    {
      GdkWindowPrivate *below = _gdk_window_child_at ((GdkWindowPrivate *) window, winx, winy);
      gint ox, oy;

      _gdk_window_native (below, &ox, &oy);
      winx -= ox;
      winy -= oy;
      window = (GdkWindow *) below;
    }

  if (win_x)
    *win_x = window ? winx : -1;
  if (win_y)
    *win_y = window ? winy : -1;

  return window;
}



void
_gdk_x11_window_set_events (GdkWindow       *window,
                       GdkEventMask     event_mask)
{
  GdkWindowPrivate *private;
  long xevent_mask;
  int i;

  g_return_if_fail (window != NULL);

  private = (GdkWindowPrivate*) window;
  if (private->destroyed)
    return;

  private->event_mask = event_mask;
  if (!private->native)
    {
      /*
       * Property changes can only be seen on the native window: a
       * child waiting on a large selection transfer needs them there.
       */
      GdkWindowPrivate *native = _gdk_window_native (private, NULL, NULL);

      if ((event_mask & GDK_PROPERTY_CHANGE_MASK) && native &&
          !(native->event_mask & GDK_PROPERTY_CHANGE_MASK))
        _gdk_x11_window_set_events ((GdkWindow *) native,
                                    native->event_mask | GDK_PROPERTY_CHANGE_MASK);
      return;
    }

  xevent_mask = StructureNotifyMask | ExposureMask |
    ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
    EnterWindowMask | LeaveWindowMask | KeyPressMask | KeyReleaseMask |
    FocusChangeMask;
  for (i = 0; i < gdk_nevent_masks; i++)
    {
      if (event_mask & (1 << (i + 1)))
        xevent_mask |= gdk_event_mask_table[i];
    }
  xevent_mask &= ~PointerMotionHintMask;

  XSelectInput (gdk_display, private->impl->xwindow,
                xevent_mask);
}

static gboolean
gdk_window_have_shape_ext (void)
{
  enum { UNKNOWN, NO, YES };
  static gint have_shape = UNKNOWN;

  if (have_shape == UNKNOWN)
    {
      int ignore;
      if (XQueryExtension (gdk_display, "SHAPE", &ignore, &ignore, &ignore))
        have_shape = YES;
      else
        have_shape = NO;
    }

  return (have_shape == YES);
}

/* ---------------------------------------------------------------------
 * Refresh rates
 *
 * RandR says which rectangles of the root window are screens, and the
 * timings of what each shows. They are read when first asked for and
 * again after RandR says they have changed.
 */

#ifdef HAVE_XRANDR

typedef struct
{
  gint x, y, width, height;     /* in X's pixels, of the root window */
  gint rate;                    /* thousandths of a hertz */
} GdkX11Screen;

static GdkX11Screen *randr_screens = NULL;
static gint randr_n_screens = -1;         /* not read yet */
static gint randr_event_base = -1;
static gint randr_have = -1;

static gboolean
randr_init (void)
{
  int error_base, major = 0, minor = 0;

  if (randr_have >= 0)
    return randr_have;
  randr_have = FALSE;
  if (XRRQueryExtension (gdk_display, &randr_event_base, &error_base) &&
      XRRQueryVersion (gdk_display, &major, &minor) &&
      (major > 1 || (major == 1 && minor >= 3)))
    {
      int mask = RRScreenChangeNotifyMask | RRCrtcChangeNotifyMask |
        RROutputChangeNotifyMask | RROutputPropertyNotifyMask;

      randr_have = TRUE;
      /*
       * A connection has one choice of RandR's events, and this
       * replaces whatever a program that watches RandR itself made
       * before; so it is everything the server has to say, and such a
       * program goes on getting what it asked for.
       */
#ifdef RRProviderChangeNotifyMask
      if (major > 1 || minor >= 4)
        mask |= RRProviderChangeNotifyMask | RRProviderPropertyNotifyMask |
          RRResourceChangeNotifyMask;
#endif
#ifdef RRLeaseNotifyMask
      if (major > 1 || minor >= 6)
        mask |= RRLeaseNotifyMask;
#endif
      XRRSelectInput (gdk_display, gdk_root_window, mask);
    }
  return randr_have;
}

static void
randr_read (void)
{
  XRRScreenResources *resources;
  gint i, j;

  g_free (randr_screens);
  randr_screens = NULL;
  randr_n_screens = 0;

  resources = XRRGetScreenResourcesCurrent (gdk_display, gdk_root_window);
  if (!resources)
    return;
  randr_screens = g_new0 (GdkX11Screen, MAX (resources->ncrtc, 1));
  for (i = 0; i < resources->ncrtc; i++)
    {
      XRRCrtcInfo *crtc = XRRGetCrtcInfo (gdk_display, resources, resources->crtcs[i]);
      GdkX11Screen *screen = &randr_screens[randr_n_screens];

      if (!crtc)
        continue;
      for (j = 0; crtc->mode != None && j < resources->nmode; j++)
        {
          const XRRModeInfo *mode = &resources->modes[j];
          gdouble lines;

          if (mode->id != crtc->mode)
            continue;
          /* A line is drawn twice in a double scan mode; an interlaced
           * one draws half of them on each pass down the screen. */
          lines = mode->vTotal;
          if (mode->modeFlags & RR_DoubleScan)
            lines *= 2;
          if (mode->modeFlags & RR_Interlace)
            lines /= 2;
          screen->x = crtc->x;
          screen->y = crtc->y;
          screen->width = crtc->width;
          screen->height = crtc->height;
          screen->rate = 0;
          if (mode->hTotal > 0 && lines > 0 && mode->dotClock > 0)
            screen->rate = (gint) (mode->dotClock * 1000.0 / (mode->hTotal * lines) + 0.5);
          randr_n_screens++;
          break;
        }
      XRRFreeCrtcInfo (crtc);
    }
  XRRFreeScreenResources (resources);
}

#endif /* HAVE_XRANDR */

/* An event that may be RandR's, saying the screens are not what they
 * were. It goes on to whoever else wants it. */
void
_gdk_x11_screen_event (XEvent *xevent)
{
#ifdef HAVE_XRANDR
  if (randr_have > 0 &&
      (xevent->type == randr_event_base + RRScreenChangeNotify ||
       xevent->type == randr_event_base + RRNotify))
    {
      XRRUpdateConfiguration (xevent);
      randr_n_screens = -1;
    }
#endif
}

/*
 * The rate of the screen the middle of the window is on; of the first
 * screen with one, if it is on none. Where its toplevel is on the
 * root window is asked of X once and kept until X says the toplevel
 * has moved: what GDK keeps as a toplevel's position is where the
 * window manager's frame has it, as often as not.
 */
gint
_gdk_x11_window_refresh_rate (GdkWindow *window)
{
#ifdef HAVE_XRANDR
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  GdkWindowPrivate *top = private, *parent;
  GdkWindowImpl *impl;
  gint x, y, i, rate = 0;

  if (!randr_init ())
    return 0;
  if (randr_n_screens < 0)
    randr_read ();

  x = private->width / 2;
  y = private->height / 2;
  while ((parent = (GdkWindowPrivate *) top->parent) != NULL &&
         parent->window_type != GDK_WINDOW_ROOT)
    {
      x += top->x;
      y += top->y;
      top = parent;
    }
  impl = top->impl;
  if (top->destroyed || !impl || impl->xwindow == None)
    return 0;
  /* Nothing says when a window of another program's has moved. */
  if (!impl->root_known || top->window_type == GDK_WINDOW_FOREIGN)
    {
      Window child;
      gint rx = 0, ry = 0;

      _gdk_x11_error_trap_push ();
      if (XTranslateCoordinates (gdk_display, impl->xwindow, gdk_root_window,
                                 0, 0, &rx, &ry, &child))
        {
          impl->root_x = rx;
          impl->root_y = ry;
          impl->root_known = TRUE;
        }
      if (_gdk_x11_error_trap_pop ())
        impl->root_known = FALSE;
    }
  x = GDK_X11_TO_X (x) + (impl->root_known ? impl->root_x : GDK_X11_TO_X (top->x));
  y = GDK_X11_TO_X (y) + (impl->root_known ? impl->root_y : GDK_X11_TO_X (top->y));

  for (i = 0; i < randr_n_screens; i++)
    {
      const GdkX11Screen *screen = &randr_screens[i];

      if (rate == 0)
        rate = screen->rate;
      if (x >= screen->x && x < screen->x + screen->width &&
          y >= screen->y && y < screen->y + screen->height && screen->rate > 0)
        return screen->rate;
    }
  return rate;
#else
  return 0;
#endif
}

/*
 * This needs the X11 shape extension.
 * If not available, shaped windows will look
 * ugly, but programs still work.    Stefan Wille
 */
void
_gdk_x11_window_shape_combine_mask (GdkWindow *window,
                               GdkBitmap *mask,
                               gint x, gint y)
{
  GdkWindowPrivate *window_private;
  GdkPixmap *temp = NULL;
  Pixmap pixmap;

  g_return_if_fail (window != NULL);

#ifdef HAVE_SHAPE_EXT
  window_private = (GdkWindowPrivate*) window;
  if (window_private->destroyed || !window_private->native)
    return;

  if (gdk_window_have_shape_ext ())
    {
      if (mask)
        {
          /* The mask is pixels of the window; the X window has more. */
          pixmap = _gdk_x11_bitmap_xid_at_scale (mask, &temp);
        }
      else
        {
          x = 0;
          y = 0;
          pixmap = None;
        }

      XShapeCombineMask (window_private->impl->xdisplay,
                         window_private->impl->xwindow,
                         ShapeBounding,
                         GDK_X11_TO_X (x), GDK_X11_TO_X (y),
                         pixmap,
                         ShapeSet);
      if (temp)
        gdk_pixmap_unref (temp);
    }
#endif /* HAVE_SHAPE_EXT */
}



void
_gdk_x11_window_set_override_redirect (GdkWindow *window,
                                  gboolean override_redirect)
{
  GdkWindowPrivate *private;
  XSetWindowAttributes attr;

  g_return_if_fail (window != NULL);
  private = (GdkWindowPrivate*) window;
  if (private->destroyed)
    return;

  attr.override_redirect = (override_redirect == FALSE)?False:True;
  XChangeWindowAttributes (gdk_display,
                           GDK_WINDOW_XID (private),
                           CWOverrideRedirect,
                           &attr);
}

void
_gdk_x11_window_set_icon (GdkWindow *window,
                     GdkWindow *icon_window,
                     GdkPixmap *pixmap,
                     GdkBitmap *mask)
{
  XWMHints *wm_hints;
  GdkWindowPrivate *window_private;
  GdkWindowPrivate *private;

  g_return_if_fail (window != NULL);
  window_private = (GdkWindowPrivate*) window;
  if (window_private->destroyed)
    return;

  wm_hints = XGetWMHints (window_private->impl->xdisplay, GDK_WINDOW_XID (window_private));
  if (!wm_hints)
    wm_hints = XAllocWMHints ();

  if (icon_window != NULL)
    {
      private = (GdkWindowPrivate *)icon_window;
      wm_hints->flags |= IconWindowHint;
      wm_hints->icon_window = GDK_WINDOW_XID (private);
    }

  if (pixmap != NULL)
    {
      wm_hints->flags |= IconPixmapHint;
      wm_hints->icon_pixmap = _gdk_x11_pixmap_xid (pixmap);
    }

  if (mask != NULL)
    {
      wm_hints->flags |= IconMaskHint;
      wm_hints->icon_mask = _gdk_x11_pixmap_xid (mask);
    }

  XSetWMHints (window_private->impl->xdisplay, GDK_WINDOW_XID (window_private), wm_hints);
  XFree (wm_hints);
}

/*
 * The window's icon as _NET_WM_ICON: for each picture its width, its
 * height and its pixels, a CARDINAL each, alpha at the top and blue
 * at the bottom, the colours straight, not multiplied by the alpha,
 * which is how window managers and panels take them. Pictures that
 * are premultiplied have the alpha divided out.
 *
 * A property goes to the server in one request, which has a most it
 * can be; pictures are left off the end until the rest fit.
 */
static void
gdk_x11_window_set_net_wm_icon (GdkWindow          *window,
                                const GdkIconImage *images,
                                gint                n_images,
                                gboolean            straight)
{
  GdkWindowPrivate *private = (GdkWindowPrivate*) window;
  Display *xdisplay;
  Atom atom;
  glong most, total = 0;
  gulong *data, *d;
  gint i, n = 0;

  if (private->destroyed)
    return;
  xdisplay = private->impl->xdisplay;
  atom = XInternAtom (xdisplay, "_NET_WM_ICON", False);

  /* in units of four bytes, less the request itself */
  most = XExtendedMaxRequestSize (xdisplay);
  if (most == 0)
    most = XMaxRequestSize (xdisplay);
  most -= 16;

  for (i = 0; i < n_images; i++)
    {
      glong size = 2 + (glong) images[i].width * images[i].height;

      if (images[i].width <= 0 || images[i].height <= 0 || !images[i].pixels)
        continue;
      if (total + size > most)
        break;
      total += size;
      n = i + 1;
    }
  if (total == 0)
    {
      XDeleteProperty (xdisplay, GDK_WINDOW_XID (private), atom);
      return;
    }

  /* Xlib takes a property of 32 bits as longs, whatever a long is */
  d = data = g_new (gulong, total);
  for (i = 0; i < n; i++)
    {
      const guchar *p = images[i].pixels;
      glong count = (glong) images[i].width * images[i].height;

      if (images[i].width <= 0 || images[i].height <= 0 || !p)
        continue;
      *d++ = images[i].width;
      *d++ = images[i].height;
      for (; count > 0; count--, p += 4)
        {
          guint a = p[3], r = p[0], g = p[1], b = p[2];

          if (!straight && a)
            {
              r = MIN ((r * 255 + a / 2) / a, 255);
              g = MIN ((g * 255 + a / 2) / a, 255);
              b = MIN ((b * 255 + a / 2) / a, 255);
            }
          *d++ = (gulong) a << 24 | r << 16 | g << 8 | b;
        }
    }
  XChangeProperty (xdisplay, GDK_WINDOW_XID (private), atom, XA_CARDINAL, 32,
                   PropModeReplace, (guchar *) data, total);
  g_free (data);
}

void
_gdk_x11_window_set_icon_images (GdkWindow          *window,
                                 const GdkIconImage *images,
                                 gint                n_images)
{
  gdk_x11_window_set_net_wm_icon (window, images, n_images, FALSE);
}

/*
 * The icon the program is installed with, as _NET_WM_ICON, its pixels
 * as its files have them; and for a window manager that knows only the
 * hints, the picture of it nearest 48 pixels as their pixmap and mask,
 * made once and kept with it.
 */
void
_gdk_x11_window_set_themed_icon (GdkWindow   *window,
                                 const gchar *name)
{
  GdkWindowPrivate *private = (GdkWindowPrivate*) window;
  GdkThemedIcon *icon;

  if (private->destroyed)
    return;
  icon = _gdk_themed_icon (name);
  if (icon->n_images == 0)
    return;

  gdk_x11_window_set_net_wm_icon (window, icon->images, icon->n_images, TRUE);

  if (!icon->pixmap)
    {
      const GdkIconImage *best = &icon->images[0];
      guchar *pixels, *p;
      gint i;

      for (i = 1; i < icon->n_images; i++)
        if (ABS (icon->images[i].width - 48) < ABS (best->width - 48))
          best = &icon->images[i];

      /* a pixmap is made of premultiplied pixels */
      pixels = g_memdup (best->pixels, best->width * best->height * 4);
      for (p = pixels, i = best->width * best->height; i > 0; i--, p += 4)
        {
          p[0] = (p[0] * p[3] + 127) / 255;
          p[1] = (p[1] * p[3] + 127) / 255;
          p[2] = (p[2] * p[3] + 127) / 255;
        }
      icon->pixmap = _gdk_pixmap_of_rgba (NULL, &icon->mask, NULL, pixels,
                                          best->width, best->height);
    }
  if (icon->pixmap)
    _gdk_x11_window_set_icon (window, NULL, icon->pixmap, icon->mask);
}

void
_gdk_x11_window_set_icon_name (GdkWindow   *window,
                          const gchar *name)
{
  GdkWindowPrivate *window_private;
  XTextProperty property;
  gint res;
  char *list;

  g_return_if_fail (window != NULL);
  window_private = (GdkWindowPrivate*) window;
  if (window_private->destroyed)
    return;
  /* Xlib's prototype predates const; it only reads the list. */
  list = (char *) name;
  res = XmbTextListToTextProperty (window_private->impl->xdisplay,
                                   &list, 1, XStdICCTextStyle,
                                   &property);
  if (res < 0)
    {
      g_warning ("Error converting icon name to text property: %d\n", res);
      return;
    }

  XSetWMIconName (window_private->impl->xdisplay, GDK_WINDOW_XID (window_private),
                  &property);

  if (property.value)
    XFree (property.value);

  gdk_window_set_utf8_name (window_private, "_NET_WM_ICON_NAME", name);
}

void
_gdk_x11_window_set_group (GdkWindow *window,
                      GdkWindow *leader)
{
  XWMHints *wm_hints;
  GdkWindowPrivate *window_private;
  GdkWindowPrivate *private;

  g_return_if_fail (window != NULL);
  window_private = (GdkWindowPrivate*) window;
  if (window_private->destroyed || !leader)
    return;   /* no leader: the group hint is left as it is */

  private = (GdkWindowPrivate *)leader;

  wm_hints = XGetWMHints (window_private->impl->xdisplay, GDK_WINDOW_XID (window_private));
  if (!wm_hints)
    wm_hints = XAllocWMHints ();

  wm_hints->flags |= WindowGroupHint;
  wm_hints->window_group = GDK_WINDOW_XID (private);

  XSetWMHints (window_private->impl->xdisplay, GDK_WINDOW_XID (window_private), wm_hints);
  XFree (wm_hints);
}

void
_gdk_x11_window_set_group_resize (GdkWindow *window, gboolean horizontally,
                                  gboolean vertically)
{
  /* Each toplevel of a group is a window of its own here. */
}

static void
gdk_window_set_mwm_hints (GdkWindow *window,
                          MotifWmHints *new_hints)
{
  static Atom hints_atom = None;
  MotifWmHints *hints;
  Atom type;
  gint format;
  gulong nitems;
  gulong bytes_after;

  GdkWindowPrivate *window_private;

  g_return_if_fail (window != NULL);
  window_private = (GdkWindowPrivate*) window;
  if (window_private->destroyed)
    return;

  if (!hints_atom)
    hints_atom = XInternAtom (window_private->impl->xdisplay,
                              _XA_MOTIF_WM_HINTS, FALSE);

  XGetWindowProperty (window_private->impl->xdisplay, GDK_WINDOW_XID (window_private),
                      hints_atom, 0, sizeof (MotifWmHints)/sizeof (long),
                      False, AnyPropertyType, &type, &format, &nitems,
                      &bytes_after, (guchar **)&hints);

  if (type == None)
    hints = new_hints;
  else
    {
      if (new_hints->flags & MWM_HINTS_FUNCTIONS)
        {
          hints->flags |= MWM_HINTS_FUNCTIONS;
          hints->functions = new_hints->functions;
        }
      if (new_hints->flags & MWM_HINTS_DECORATIONS)
        {
          hints->flags |= MWM_HINTS_DECORATIONS;
          hints->decorations = new_hints->decorations;
        }
    }

  XChangeProperty (window_private->impl->xdisplay, GDK_WINDOW_XID (window_private),
                   hints_atom, hints_atom, 32, PropModeReplace,
                   (guchar *)hints, sizeof (MotifWmHints)/sizeof (long));

  if (hints != new_hints)
    XFree (hints);
}

void
_gdk_x11_window_set_decorations (GdkWindow      *window,
                            GdkWMDecoration decorations)
{
  MotifWmHints hints;

  hints.flags = MWM_HINTS_DECORATIONS;
  hints.decorations = decorations;

  gdk_window_set_mwm_hints (window, &hints);
}

void
_gdk_x11_window_set_functions (GdkWindow    *window,
                          GdkWMFunction functions)
{
  MotifWmHints hints;

  hints.flags = MWM_HINTS_FUNCTIONS;
  hints.functions = functions;

  gdk_window_set_mwm_hints (window, &hints);
}

/* The window manager draws the title bar, in colours of its own. */
void
_gdk_x11_window_set_title_colors (GdkWindow      *window,
                                  const GdkColor *colors)
{
}


/*
 * The window manager's part, as the Extended Window Manager Hints have
 * it: a mapped window asks the window manager to change its state; an
 * unmapped one says in its _NET_WM_STATE what state it wants to be
 * mapped in, as the window manager reads it then.
 */
static void
x11_send_wm_message (GdkWindowPrivate *private,
                     const gchar      *message,
                     glong             l0,
                     glong             l1)
{
  Display *xdisplay = private->impl->xdisplay;
  XEvent xev;

  memset (&xev, 0, sizeof (xev));
  xev.xclient.type = ClientMessage;
  xev.xclient.serial = 0;
  xev.xclient.send_event = True;
  xev.xclient.window = GDK_WINDOW_XID (private);
  xev.xclient.message_type = XInternAtom (xdisplay, message, False);
  xev.xclient.format = 32;
  xev.xclient.data.l[0] = l0;
  xev.xclient.data.l[1] = l1;
  xev.xclient.data.l[2] = 0;
  xev.xclient.data.l[3] = 1;    /* from an application */

  XSendEvent (xdisplay, gdk_root_window, False,
              SubstructureRedirectMask | SubstructureNotifyMask, &xev);
}

static void
x11_set_wm_state (GdkWindow   *window,
                  const gchar *state,
                  gboolean     set)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  Display *xdisplay;
  Window xid;
  Atom wm_state, atom, type;
  gint format;
  gulong nitems, bytes_after, i, n;
  Atom *atoms = NULL, *new_atoms;

  if (private->destroyed)
    return;
  xdisplay = private->impl->xdisplay;
  atom = XInternAtom (xdisplay, state, False);

  if (private->mapped)
    {
      x11_send_wm_message (private, "_NET_WM_STATE", set ? 1 : 0, atom);
      return;
    }

  xid = GDK_WINDOW_XID (private);
  wm_state = XInternAtom (xdisplay, "_NET_WM_STATE", False);
  XGetWindowProperty (xdisplay, xid, wm_state, 0, G_MAXLONG, False, XA_ATOM,
                      &type, &format, &nitems, &bytes_after,
                      (guchar **) &atoms);
  if (type != XA_ATOM || format != 32)
    nitems = 0;

  new_atoms = g_new (Atom, nitems + 1);
  for (i = 0, n = 0; i < nitems; i++)
    if (atoms[i] != atom)
      new_atoms[n++] = atoms[i];
  if (set)
    new_atoms[n++] = atom;

  XChangeProperty (xdisplay, xid, wm_state, XA_ATOM, 32, PropModeReplace,
                   (guchar *) new_atoms, n);
  g_free (new_atoms);
  if (atoms)
    XFree (atoms);
}

void
_gdk_x11_window_iconify (GdkWindow *window)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  Display *xdisplay;
  XWMHints *wm_hints;

  if (private->destroyed)
    return;
  xdisplay = private->impl->xdisplay;

  if (private->mapped)
    {
      XIconifyWindow (xdisplay, GDK_WINDOW_XID (private),
                      DefaultScreen (xdisplay));
      return;
    }

  /* Not shown yet: shown iconified. */
  wm_hints = XGetWMHints (xdisplay, GDK_WINDOW_XID (private));
  if (!wm_hints)
    wm_hints = XAllocWMHints ();
  wm_hints->flags |= StateHint;
  wm_hints->initial_state = IconicState;
  XSetWMHints (xdisplay, GDK_WINDOW_XID (private), wm_hints);
  XFree (wm_hints);
}

/* The desktop the window manager shows now, or 0. */
static gulong
x11_current_desktop (Display *xdisplay)
{
  Atom type;
  gint format;
  gulong nitems, bytes_after, desktop = 0;
  guchar *data = NULL;

  XGetWindowProperty (xdisplay, gdk_root_window,
                      XInternAtom (xdisplay, "_NET_CURRENT_DESKTOP", False),
                      0, 1, False, XA_CARDINAL, &type, &format, &nitems,
                      &bytes_after, &data);
  if (type == XA_CARDINAL && format == 32 && nitems == 1)
    desktop = *(gulong *) data;
  if (data)
    XFree (data);

  return desktop;
}

/*
 * Sticky is on every desktop: the state says so, and the desktop is
 * all of them, 0xFFFFFFFF. Unstuck, the window stays on the desktop
 * shown now.
 */
void
_gdk_x11_window_set_sticky (GdkWindow *window, gboolean sticky)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  Display *xdisplay;
  gulong desktop;

  if (private->destroyed)
    return;
  xdisplay = private->impl->xdisplay;

  x11_set_wm_state (window, "_NET_WM_STATE_STICKY", sticky);

  desktop = sticky ? 0xFFFFFFFF : x11_current_desktop (xdisplay);
  if (private->mapped)
    x11_send_wm_message (private, "_NET_WM_DESKTOP", desktop, 0);
  else
    XChangeProperty (xdisplay, GDK_WINDOW_XID (private),
                     XInternAtom (xdisplay, "_NET_WM_DESKTOP", False),
                     XA_CARDINAL, 32, PropModeReplace,
                     (guchar *) &desktop, 1);
}

void
_gdk_x11_window_set_keep_above (GdkWindow *window, gboolean setting)
{
  x11_set_wm_state (window, "_NET_WM_STATE_ABOVE", setting);
}

void
_gdk_x11_window_set_skip_taskbar_hint (GdkWindow *window, gboolean setting)
{
  x11_set_wm_state (window, "_NET_WM_STATE_SKIP_TASKBAR", setting);
}
