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
#include <X11/Xatom.h>
#include <string.h>
#include "gdk.h"
#include "gdkprivate-x11.h"
#include "gdkbackend-x11.h"
#include "gdkx.h"

/*
 * Only native windows are X windows: a child's selection traffic goes
 * through its native window. So remember which GdkWindow owns each
 * selection, and which last asked for one through each native window,
 * to give the events to that window rather than the native one.
 */
typedef struct
{
  Window     xwindow;
  GdkAtom    selection;
  GdkWindow *window;
} SelectionUser;

static GSList *owners;
static GSList *requestors;

/* The newest entry for xwindow and selection; None or GDK_NONE match any. */
static SelectionUser *
user_find (GSList  **list,
           Window    xwindow,
           GdkAtom   selection)
{
  GSList *l, *next;
  SelectionUser *found = NULL;

  for (l = *list; l; l = next)
    {
      SelectionUser *u = l->data;

      next = l->next;
      if (((GdkWindowPrivate *) u->window)->destroyed)
        {
          *list = g_slist_remove (*list, u);
          gdk_window_unref (u->window);
          g_free (u);
          continue;
        }
      if (!found && (xwindow == None || u->xwindow == xwindow) &&
          (selection == GDK_NONE || u->selection == selection))
        found = u;
    }
  return found;
}

static void
user_set (GSList    **list,
          Window      xwindow,
          GdkAtom     selection,
          GdkWindow  *window)
{
  SelectionUser *u = user_find (list, xwindow, selection);

  if (u)
    {
      *list = g_slist_remove (*list, u);
      gdk_window_unref (u->window);
    }
  else
    u = g_new (SelectionUser, 1);
  u->xwindow = xwindow;
  u->selection = selection;
  u->window = gdk_window_ref (window);
  *list = g_slist_prepend (*list, u);
}

/* The GdkWindow a selection event is for: window, unless a child's. */
GdkWindow *
_gdk_x11_selection_event_window (GdkWindow *window,
                                 XEvent    *xevent)
{
  SelectionUser *u = NULL;

  switch (xevent->type)
    {
    case SelectionNotify:
      u = user_find (&requestors, xevent->xselection.requestor,
                     xevent->xselection.selection);
      break;
    case SelectionRequest:
      u = user_find (&owners, xevent->xselectionrequest.owner,
                     xevent->xselectionrequest.selection);
      break;
    case SelectionClear:
      u = user_find (&owners, xevent->xselectionclear.window,
                     xevent->xselectionclear.selection);
      break;
    case PropertyNotify:
      /* The rest of a large (INCR) transfer. */
      if (xevent->xproperty.atom == gdk_selection_property)
        u = user_find (&requestors, xevent->xproperty.window, GDK_NONE);
      break;
    }
  return u ? u->window : window;
}

gboolean
_gdk_x11_selection_owner_set (GdkWindow *owner,
                         GdkAtom    selection,
                         guint32    time,
                         gint       send_event)
{
  Display *xdisplay;
  Window xwindow;

  if (owner)
    {
      GdkWindowPrivate *private;

      private = (GdkWindowPrivate*) owner;
      if (private->destroyed)
        return FALSE;

      xdisplay = private->impl->xdisplay;
      xwindow = GDK_WINDOW_XID (private);
    }
  else
    {
      xdisplay = gdk_display;
      xwindow = None;
    }

  XSetSelectionOwner (xdisplay, selection, xwindow, time);
  if (XGetSelectionOwner (xdisplay, selection) != xwindow)
    return FALSE;
  if (owner)
    user_set (&owners, xwindow, selection, owner);
  return TRUE;
}

GdkWindow*
_gdk_x11_selection_owner_get (GdkAtom selection)
{
  Window xwindow;

  SelectionUser *u;

  xwindow = XGetSelectionOwner (gdk_display, selection);
  if (xwindow == None)
    return NULL;

  if ((u = user_find (&owners, xwindow, selection)) != NULL)
    return u->window;
  return _gdk_x11_window_lookup (xwindow);
}

void
_gdk_x11_selection_convert (GdkWindow *requestor,
                       GdkAtom    selection,
                       GdkAtom    target,
                       guint32    time)
{
  GdkWindowPrivate *private;

  g_return_if_fail (requestor != NULL);

  private = (GdkWindowPrivate*) requestor;
  if (private->destroyed)
    return;

  user_set (&requestors, GDK_WINDOW_XID (private), selection, requestor);
  XConvertSelection (private->impl->xdisplay, selection, target,
                     gdk_selection_property, GDK_WINDOW_XID (private), time);
}

gint
_gdk_x11_selection_property_get (GdkWindow  *requestor,
                            guchar    **data,
                            GdkAtom    *ret_type,
                            gint       *ret_format)
{
  GdkWindowPrivate *private;
  gulong nitems;
  gulong nbytes;
  gulong length;
  GdkAtom prop_type;
  gint prop_format;
  guchar *t = NULL;

  g_return_val_if_fail (requestor != NULL, 0);

  /* If retrieved chunks are typically small, (and the ICCCM says the
     should be) it would be a win to try first with a buffer of
     moderate length, to avoid two round trips to the server */

  private = (GdkWindowPrivate*) requestor;
  if (private->destroyed)
    return 0;

  t = NULL;
  XGetWindowProperty (private->impl->xdisplay, GDK_WINDOW_XID (private),
                      gdk_selection_property, 0, 0, False,
                      AnyPropertyType, &prop_type, &prop_format,
                      &nitems, &nbytes, &t);

  if (ret_type)
    *ret_type = prop_type;
  if (ret_format)
    *ret_format = prop_format;

  if (prop_type == None)
    {
      *data = NULL;
      return 0;
    }

  if (t)
    {
      XFree (t);
      t = NULL;
    }

  /* Add on an extra byte to handle null termination. X guarantees
     that t will be 1 longer than nbytes and null terminated */
  length = nbytes + 1;

  /* We can't delete the selection here, because it might be the INCR
     protocol, in which case the client has to make sure they'll be
     notified of PropertyChange events _before_ the property is deleted.
     Otherwise there's no guarantee we'll win the race ... */
  XGetWindowProperty (private->impl->xdisplay, GDK_WINDOW_XID (private),
                      gdk_selection_property, 0, (nbytes + 3) / 4, False,
                      AnyPropertyType, &prop_type, &prop_format,
                      &nitems, &nbytes, &t);

  if (prop_type != None)
    {
      *data = g_new (guchar, length);
      memcpy (*data, t, length);
      if (t)
        XFree (t);
      return length-1;
    }
  else
    {
      *data = NULL;
      return 0;
    }
}


void
_gdk_x11_selection_send_notify (guint32  requestor,
                           GdkAtom  selection,
                           GdkAtom  target,
                           GdkAtom  property,
                           guint32  time)
{
  XSelectionEvent xevent;

  xevent.type = SelectionNotify;
  xevent.serial = 0;
  xevent.send_event = True;
  xevent.display = gdk_display;
  xevent.requestor = requestor;
  xevent.selection = selection;
  xevent.target = target;
  xevent.property = property;
  xevent.time = time;

  gdk_send_xevent (requestor, False, NoEventMask, (XEvent*) &xevent);
}


/* The output of XmbTextPropertyToTextList may include stuff not valid
 * for COMPOUND_TEXT. This routine tries to correct this by:
 *
 * a) Canonicalizing CR LF and CR to LF
 * b) Stripping out all other non-allowed control characters
 *
 * See the COMPOUND_TEXT spec distributed with X for explanations
 * what is allowed.
 */
static gchar *
sanitize_ctext (const char *str,
                gint       *length)
{
  gchar *result = g_malloc (*length + 1);
  gint out_length = 0;
  gint i;
  const guchar *ustr = (const guchar *)str;

  for (i=0; i < *length; i++)
    {
      guchar c = ustr[i];

      if (c == '\r')
        {
          result[out_length++] = '\n';
          if (i + 1 < *length && ustr[i + 1] == '\n')
            i++;
        }
      else if (c == 27 /* ESC */)
        {
          /* Check for "extended segments, which can contain arbitrary
           * octets. See CTEXT spec, section 6.
           */

          if (i + 5 < *length &&
              ustr[i + 1] == '%' &&
              ustr[i + 2] == '/' &&
              (ustr[i + 3] >= 48 && ustr[i + 3] <= 52) &&
              ustr[i + 4] >= 128 &&
              ustr[i + 5] >= 128)
            {
              int extra_len = 6 + (ustr[i + 4] - 128) * 128 + ustr[i + 5] - 128;
              extra_len = MAX (extra_len, *length - i);

              memcpy (result + out_length, ustr + i, extra_len);
              out_length += extra_len;
              i += extra_len - 1;
            }
          else
            result[out_length++] = c;
        }
      else if (c == '\n' || c == '\t' || c == 27 /* ESC */ ||
               (c >= 32 && c <= 127) || /* GL */
               c == 155 /* CONTROL SEQUENCE INTRODUCER */ ||
               (c >= 160 && c <= 255)) /* GR */
        {
          result[out_length++] = c;
        }
    }

  result[out_length] = '\0';
  *length = out_length;

  return result;
}

gint
_gdk_x11_text_property_to_text_list (GdkAtom encoding, gint format,
                                guchar *text, gint length,
                                gchar ***list)
{
  XTextProperty property;
  gint count = 0;
  gint res;
  gchar *sanitized_text = NULL;

  if (!list)
    return 0;

  property.encoding = encoding;
  property.format = format;

  if (encoding == _gdk_x11_atom_intern ("COMPOUND_TEXT", FALSE) && format == 8)
    {
      gint sanitized_text_length = length;

      sanitized_text = sanitize_ctext ((const char *) text, &sanitized_text_length);
      property.value = (guchar *) sanitized_text;
      property.nitems = sanitized_text_length;
    }
  else
    {
      property.value = text;
      property.nitems = length;
    }

  res = XmbTextPropertyToTextList (GDK_DISPLAY(), &property, list, &count);

  if (sanitized_text)
    g_free (sanitized_text);

  if (res == XNoMemory || res == XLocaleNotSupported ||
      res == XConverterNotFound)
    return 0;
  else
    return count;
}

void
_gdk_x11_free_text_list (gchar **list)
{
  g_return_if_fail (list != NULL);

  XFreeStringList (list);
}

gint
_gdk_x11_string_to_compound_text (const gchar *str,
                             GdkAtom *encoding, gint *format,
                             guchar **ctext, gint *length)
{
  gint res;
  XTextProperty property;
  gint sanitized_text_length;
  gchar *sanitized_text;

  res = XmbTextListToTextProperty (GDK_DISPLAY(),
                                   (char **)&str, 1, XCompoundTextStyle,
                                   &property);
  /* A positive result means some characters were replaced but the
   * property is valid; only a negative one is a failure. Fail cleanly
   * instead of reaching the assertion below with no property. */
  if (res < 0)
    {
      if (encoding)
        *encoding = None;
      if (format)
        *format = None;
      if (ctext)
        *ctext = NULL;
      if (length)
        *length = 0;
      return res;
    }

  g_assert (property.encoding == _gdk_x11_atom_intern ("COMPOUND_TEXT", FALSE) && property.format == 8);

  if (encoding)
    *encoding = property.encoding;
  if (format)
    *format = property.format;

  sanitized_text_length = property.nitems;
  sanitized_text = sanitize_ctext ((const char *) property.value, &sanitized_text_length);

  if (ctext)
    *ctext = (guchar *) sanitized_text;
  else
    g_free (sanitized_text);

  if (length)
    *length = sanitized_text_length;

  if (property.value)
    XFree (property.value);

  return res;
}

void _gdk_x11_free_compound_text (guchar *ctext)
{
  if (ctext)
    g_free (ctext);
}

/* What GTK has always sent in one piece; more goes by INCR. */
gint
_gdk_x11_selection_max_size (void)
{
  return 4000;
}
