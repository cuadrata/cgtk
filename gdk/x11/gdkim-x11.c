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

#include <stdlib.h>
#include <X11/Xlocale.h>
#include "gdk.h"
#include "gdkprivate-x11.h"
#include "gdkbackend-x11.h"
#include "gdki18n.h"
#include "gdkx.h"
#include "gdkftfont.h"

#if HAVE_CONFIG_H
#  include <config.h>
#    include <string.h>
#endif


#ifdef USE_XIM

static void   x11_input_contexts_lost    (XIM im);
static void   x11_input_contexts_queue_refresh (void);

static XIM xim_im;              /* the input method server's, while one runs */

#endif /* USE_XIM */

/*
 *--------------------------------------------------------------
 * _gdk_x11_set_locale
 *
 * Arguments:
 *
 * Results:
 *
 * Side effects:
 *
 *--------------------------------------------------------------
 */

gchar*
_gdk_x11_set_locale (void)
{
  gchar *current_locale;

  if (!setlocale (LC_ALL,""))
    g_warning ("locale not supported by C library");

  if (!XSupportsLocale ())
    {
      g_warning ("locale not supported by Xlib, locale set to C");
      setlocale (LC_ALL, "C");
    }

  if (!XSetLocaleModifiers (""))
    g_warning ("can not set locale modifiers");

  current_locale = setlocale (LC_ALL, NULL);

  return current_locale;
}

#ifdef USE_XIM

/*
 * The input method server's input method, for the input contexts
 * (below). One that starts later is taken up when it comes, and one
 * that goes away takes its contexts with it.
 */
#ifdef USE_X11R6_XIM
static void gdk_im_instantiate_cb (Display *display,
                                   XPointer client_data, XPointer call_data);

static void
gdk_im_destroy_cb (XIM im, XPointer client_data, XPointer call_data)
{
  GDK_NOTE (XIM, g_message ("The input method went away."));

  x11_input_contexts_lost (xim_im);
  xim_im = NULL;

  XRegisterIMInstantiateCallback (gdk_display, NULL, NULL, NULL,
                                  gdk_im_instantiate_cb, NULL);
}
#endif

static gint
gdk_im_real_open (void)
{
#ifdef USE_X11R6_XIM
  XIMCallback destroy_cb;
#endif

  xim_im = XOpenIM (gdk_display, NULL, NULL, NULL);
  if (xim_im == NULL)
    {
      GDK_NOTE (XIM, g_warning ("Unable to open IM."));
      return FALSE;
    }

#ifdef USE_X11R6_XIM
  destroy_cb.callback = gdk_im_destroy_cb;
  destroy_cb.client_data = NULL;
  if (NULL != (void *) XSetIMValues (xim_im, XNDestroyCallback, &destroy_cb, NULL))
    GDK_NOTE (XIM, g_warning ("Could not set destroy callback to IM. Be careful to not destroy your input method."));
#endif

  x11_input_contexts_queue_refresh ();
  return TRUE;
}

#ifdef USE_X11R6_XIM
static void
gdk_im_instantiate_cb (Display *display,
                       XPointer client_data, XPointer call_data)
{
  GDK_NOTE (XIM, g_message ("New IM is instantiated."));
  if (display != gdk_display)
    return;

  if (gdk_im_real_open ())
    XUnregisterIMInstantiateCallback (gdk_display, NULL, NULL, NULL,
                                      gdk_im_instantiate_cb, NULL);
}
#endif

gint
gdk_im_open (void)
{
  gdk_xim_ic = NULL;
  gdk_xim_window = NULL;
  xim_im = NULL;

  if (gdk_im_real_open ())
    return TRUE;

#ifdef USE_X11R6_XIM
  XRegisterIMInstantiateCallback (gdk_display, NULL, NULL, NULL,
                                  gdk_im_instantiate_cb, NULL);
#endif

  return FALSE;
}

void
gdk_im_close (void)
{
  if (xim_im)
    {
      XCloseIM (xim_im);
      xim_im = NULL;
    }
}

/* ---------------------------------------------------------------------
 * The X11 input context, for GtkIMContext. XIM, with the text being
 * composed handed to the program to draw at its cursor (preedit
 * callbacks); an input method that can't do that draws it itself. With
 * no input method server, Xlib's own built-in one still does dead keys
 * and compose sequences; password fields only ever get that one.
 */

typedef struct
{
  GdkInputContext base;
  GdkICPrivate icp;             /* what the event loop filters and looks up with */
  XIM im;                       /* the input method icp.xic belongs to */
  XIMCallback callbacks[4];
  GdkWindow *window;            /* the client window */
  GdkWindow *native;            /* the X window it is part of */
  gboolean focused, password;
  GdkWChar *preedit;            /* the text being composed */
  XIMFeedback *feedback;
  gint preedit_len, caret;
  XPoint spot;
} X11InputContext;

static GList *x11_input_contexts = NULL;
static XIM xim_local = NULL;

/* Xlib's built-in input method: compose sequences, and nothing else. */
static XIM
local_im (void)
{
  static gboolean tried = FALSE;

  if (!tried)
    {
      gchar *saved = g_strdup (XSetLocaleModifiers (NULL));

      tried = TRUE;
      if (XSetLocaleModifiers ("@im=none"))
        xim_local = XOpenIM (gdk_display, NULL, NULL, NULL);
      XSetLocaleModifiers (saved ? saved : "");
      g_free (saved);
    }
  return xim_local;
}

static void x11_ic_focus_in (GdkInputContext *context);
static void preedit_set (X11InputContext *ic, gint first, gint length,
                         const GdkWChar *text, const XIMFeedback *feedback, gint n);
static void preedit_clear (X11InputContext *ic);

/* Fields with the focus take the input method there is now: a server's
 * that came, or Xlib's own when it went. From an idle, not inside Xlib's
 * callbacks. */
static guint refresh_idle = 0;

static gint
x11_input_contexts_refresh (gpointer data)
{
  GList *l;

  refresh_idle = 0;
  for (l = x11_input_contexts; l; l = l->next)
    {
      X11InputContext *ic = l->data;

      if (ic->focused)
        x11_ic_focus_in (&ic->base);
    }
  return FALSE;
}

static void
x11_input_contexts_queue_refresh (void)
{
  if (!refresh_idle && x11_input_contexts)
    refresh_idle = g_idle_add (x11_input_contexts_refresh, NULL);
}

/* The server's input method went away, its contexts with it, and what
 * they were composing. */
static void
x11_input_contexts_lost (XIM im)
{
  GList *l;

  for (l = x11_input_contexts; l; l = l->next)
    {
      X11InputContext *ic = l->data;

      if (ic->im != im)
        continue;
      if (gdk_xim_ic == &ic->icp)
        {
          gdk_xim_ic = NULL;
          gdk_xim_window = NULL;
        }
      ic->icp.xic = NULL;
      ic->im = NULL;
      preedit_clear (ic);
    }
  x11_input_contexts_queue_refresh ();
}

/* Wide characters, which are Unicode here, as UTF-8. */
static gchar *
wide_to_utf8 (const GdkWChar *wide, gint n)
{
  gchar *out = g_new (gchar, n * 4 + 1), *p = out;
  gint i;

  for (i = 0; i < n; i++)
    p += _gdk_ft_encode_utf8 (wide[i], p);
  *p = '\0';
  return out;
}

static gchar *
locale_to_utf8 (const gchar *str, gint len)
{
  wchar_t *wide = g_new (wchar_t, len + 1);
  GdkWChar *gw;
  gchar *copy = g_strndup (str, len), *utf8;
  size_t n = mbstowcs (wide, copy, len + 1);
  size_t i;

  g_free (copy);
  if (n == (size_t) -1)
    {
      g_free (wide);
      return g_strndup (str, len);
    }
  gw = g_new (GdkWChar, n + 1);
  for (i = 0; i < n; i++)
    gw[i] = wide[i];
  utf8 = wide_to_utf8 (gw, n);
  g_free (gw);
  g_free (wide);
  return utf8;
}

static void
preedit_set (X11InputContext *ic, gint first, gint length, const GdkWChar *text,
             const XIMFeedback *feedback, gint n)
{
  gint len = ic->preedit_len - length + n;
  gint rest = ic->preedit_len - first - length;
  GdkWChar *p = g_new (GdkWChar, len + 1);
  XIMFeedback *f = g_new0 (XIMFeedback, len + 1);

  /* The old text may be none at all, and no buffer. */
  if (first)
    {
      memcpy (p, ic->preedit, first * sizeof *p);
      memcpy (f, ic->feedback, first * sizeof *f);
    }
  if (n)
    {
      memcpy (p + first, text, n * sizeof *p);
      if (feedback)
        memcpy (f + first, feedback, n * sizeof *f);
    }
  if (rest)
    {
      memcpy (p + first + n, ic->preedit + first + length, rest * sizeof *p);
      memcpy (f + first + n, ic->feedback + first + length, rest * sizeof *f);
    }
  g_free (ic->preedit);
  g_free (ic->feedback);
  ic->preedit = p;
  ic->feedback = f;
  ic->preedit_len = len;
}

static int
preedit_start_cb (XIC xic, XPointer client_data, XPointer call_data)
{
  return -1;                    /* no limit on its length */
}

static void
preedit_done_cb (XIC xic, XPointer client_data, XPointer call_data)
{
  X11InputContext *ic = (X11InputContext *) client_data;

  preedit_set (ic, 0, ic->preedit_len, NULL, NULL, 0);
  ic->caret = 0;
  _gdk_input_context_preedit_changed (&ic->base);
}

static void
preedit_draw_cb (XIC xic, XPointer client_data, XIMPreeditDrawCallbackStruct *draw)
{
  X11InputContext *ic = (X11InputContext *) client_data;
  gint first = CLAMP (draw->chg_first, 0, ic->preedit_len);
  gint length = CLAMP (draw->chg_length, 0, ic->preedit_len - first);
  GdkWChar *text = NULL;
  gint n = 0;

  if (draw->text && draw->text->length > 0)
    {
      gint i;

      n = draw->text->length;
      text = g_new (GdkWChar, n + 1);
      if (draw->text->encoding_is_wchar)
        for (i = 0; i < n; i++)
          text[i] = draw->text->string.wide_char[i];
      else
        {
          wchar_t *wide = g_new (wchar_t, n + 1);
          size_t got = mbstowcs (wide, draw->text->string.multi_byte, n + 1);

          if (got == (size_t) -1)
            got = 0;
          n = MIN ((gint) got, n);
          for (i = 0; i < n; i++)
            text[i] = wide[i];
          g_free (wide);
        }
    }
  preedit_set (ic, first, length, text, draw->text ? draw->text->feedback : NULL, n);
  g_free (text);
  ic->caret = CLAMP (draw->caret, 0, ic->preedit_len);
  _gdk_input_context_preedit_changed (&ic->base);
}

static void
preedit_caret_cb (XIC xic, XPointer client_data, XIMPreeditCaretCallbackStruct *caret)
{
  X11InputContext *ic = (X11InputContext *) client_data;

  switch (caret->direction)
    {
    case XIMAbsolutePosition:
      ic->caret = caret->position;
      break;
    case XIMForwardChar:
      ic->caret++;
      break;
    case XIMBackwardChar:
      ic->caret--;
      break;
    case XIMLineStart:
      ic->caret = 0;
      break;
    case XIMLineEnd:
      ic->caret = ic->preedit_len;
      break;
    default:
      return;
    }
  ic->caret = CLAMP (ic->caret, 0, ic->preedit_len);
  caret->position = ic->caret;
  _gdk_input_context_preedit_changed (&ic->base);
}

/* The best style the input method has: the text being composed drawn by
 * the program, else by the input method, else not shown. */
static XIMStyle
choose_style (XIM im)
{
  static const XIMStyle wanted[] = {
    XIMPreeditCallbacks | XIMStatusNothing,
    XIMPreeditCallbacks | XIMStatusNone,
    XIMPreeditNothing | XIMStatusNothing,
    XIMPreeditNothing | XIMStatusNone,
    XIMPreeditNone | XIMStatusNothing,
    XIMPreeditNone | XIMStatusNone,
  };
  XIMStyles *styles = NULL;
  XIMStyle style = 0;
  guint i, j;

  if (XGetIMValues (im, XNQueryInputStyle, &styles, NULL) != NULL || !styles)
    return 0;
  for (i = 0; i < sizeof wanted / sizeof wanted[0] && !style; i++)
    for (j = 0; j < styles->count_styles; j++)
      if (styles->supported_styles[j] == wanted[i])
        {
          style = wanted[i];
          break;
        }
  XFree (styles);
  return style;
}

/* Its input method is going: so is what it was composing. */
static void
preedit_clear (X11InputContext *ic)
{
  if (!ic->preedit_len)
    return;
  preedit_set (ic, 0, ic->preedit_len, NULL, NULL, 0);
  ic->caret = 0;
  _gdk_input_context_preedit_changed (&ic->base);
}

static void
xic_destroy (X11InputContext *ic)
{
  if (gdk_xim_ic == &ic->icp)
    {
      gdk_xim_ic = NULL;
      gdk_xim_window = NULL;
    }
  if (ic->icp.xic)
    XDestroyIC (ic->icp.xic);
  ic->icp.xic = NULL;
  ic->im = NULL;
}

/* An XIC in the right input method, made if need be. */
static gboolean
xic_ensure (X11InputContext *ic)
{
  XIM im = ic->password || !xim_im ? local_im () : xim_im;
  XVaNestedList preedit = NULL;
  XIMStyle style;
  XWindowAttributes attrs;
  unsigned long filter = 0;
  Window xid;

  if (ic->icp.xic && ic->im == im)
    return TRUE;
  xic_destroy (ic);
  if (!im || !ic->native || ((GdkWindowPrivate *) ic->native)->destroyed ||
      !(style = choose_style (im)))
    return FALSE;

  if (style & XIMPreeditCallbacks)
    {
      ic->callbacks[0].callback = (XIMProc) preedit_start_cb;
      ic->callbacks[1].callback = (XIMProc) preedit_done_cb;
      ic->callbacks[2].callback = (XIMProc) preedit_draw_cb;
      ic->callbacks[3].callback = (XIMProc) preedit_caret_cb;
      ic->callbacks[0].client_data = ic->callbacks[1].client_data =
        ic->callbacks[2].client_data = ic->callbacks[3].client_data = (XPointer) ic;
      preedit = XVaCreateNestedList (0,
                                     XNPreeditStartCallback, &ic->callbacks[0],
                                     XNPreeditDoneCallback, &ic->callbacks[1],
                                     XNPreeditDrawCallback, &ic->callbacks[2],
                                     XNPreeditCaretCallback, &ic->callbacks[3],
                                     XNSpotLocation, &ic->spot,
                                     NULL);
    }
  xid = GDK_WINDOW_XID (ic->native);
  ic->icp.xic = XCreateIC (im, XNInputStyle, style, XNClientWindow, xid, XNFocusWindow, xid,
                           preedit ? XNPreeditAttributes : NULL, preedit, NULL);
  if (preedit)
    XFree (preedit);
  if (!ic->icp.xic)
    return FALSE;
  ic->im = im;

  /* The input method may want events the window doesn't take yet. */
  if (XGetICValues (ic->icp.xic, XNFilterEvents, &filter, NULL) == NULL && filter &&
      XGetWindowAttributes (gdk_display, xid, &attrs))
    XSelectInput (gdk_display, xid, attrs.your_event_mask | filter);
  return TRUE;
}

static GdkInputContext *
x11_ic_create (void)
{
  X11InputContext *ic = g_new0 (X11InputContext, 1);

  x11_input_contexts = g_list_prepend (x11_input_contexts, ic);
  return &ic->base;
}

static void
x11_ic_destroy (GdkInputContext *context)
{
  X11InputContext *ic = (X11InputContext *) context;

  x11_input_contexts = g_list_remove (x11_input_contexts, ic);
  xic_destroy (ic);
  if (ic->window)
    gdk_window_unref (ic->window);
  g_free (ic->preedit);
  g_free (ic->feedback);
  g_free (ic);
}

static void
x11_ic_set_window (GdkInputContext *context, GdkWindow *window)
{
  X11InputContext *ic = (X11InputContext *) context;

  preedit_clear (ic);
  xic_destroy (ic);
  if (window)
    gdk_window_ref (window);
  if (ic->window)
    gdk_window_unref (ic->window);
  ic->window = window;
  ic->native = window ? (GdkWindow *) _gdk_window_native ((GdkWindowPrivate *) window,
                                                           NULL, NULL) : NULL;
}

static void
x11_ic_focus_in (GdkInputContext *context)
{
  X11InputContext *ic = (X11InputContext *) context;

  ic->focused = TRUE;
  if (!xic_ensure (ic))
    return;
  XSetICFocus (ic->icp.xic);
  gdk_xim_ic = &ic->icp;
  gdk_xim_window = ic->native;
}

static void
x11_ic_focus_out (GdkInputContext *context)
{
  X11InputContext *ic = (X11InputContext *) context;

  ic->focused = FALSE;
  if (ic->icp.xic)
    XUnsetICFocus (ic->icp.xic);
  if (gdk_xim_ic == &ic->icp)
    {
      gdk_xim_ic = NULL;
      gdk_xim_window = NULL;
    }
}

/* Whatever was being composed is committed, not lost. */
static void
x11_ic_reset (GdkInputContext *context)
{
  X11InputContext *ic = (X11InputContext *) context;
  gchar *left;

  if (!ic->icp.xic)
    return;
  left = XmbResetIC (ic->icp.xic);
  if (ic->preedit_len)
    {
      preedit_set (ic, 0, ic->preedit_len, NULL, NULL, 0);
      ic->caret = 0;
      _gdk_input_context_preedit_changed (context);
    }
  if (left && *left)
    {
      gchar *utf8 = locale_to_utf8 (left, strlen (left));

      _gdk_input_context_commit (context, utf8);
      g_free (utf8);
    }
  if (left)
    XFree (left);
}

/* What reaches here has been through XFilterEvent and XmbLookupString:
 * what the input method finished is the event's text. */
static gboolean
x11_ic_filter_key (GdkInputContext *context, GdkEventKey *event)
{
  gchar *utf8;

  if (event->type != GDK_KEY_PRESS || event->length <= 0 ||
      (guchar) event->string[0] < 0x20 || event->string[0] == 0x7f ||
      (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)))
    return FALSE;
  utf8 = locale_to_utf8 (event->string, event->length);
  _gdk_input_context_commit (context, utf8);
  g_free (utf8);
  return TRUE;
}

static void
x11_ic_get_preedit (GdkInputContext *context, gchar **utf8, gint *cursor,
                    gint *mark_start, gint *mark_end)
{
  X11InputContext *ic = (X11InputContext *) context;
  gint i, start = -1, end = -1;

  *utf8 = wide_to_utf8 (ic->preedit, ic->preedit_len);
  *cursor = ic->caret;
  for (i = 0; i < ic->preedit_len; i++)
    if (ic->feedback[i] & (XIMReverse | XIMHighlight))
      {
        if (start < 0)
          start = i;
        end = i + 1;
      }
  *mark_start = start < 0 ? 0 : start;
  *mark_end = end < 0 ? 0 : end;
}

static void
x11_ic_set_cursor_location (GdkInputContext *context, GdkRectangle *area)
{
  X11InputContext *ic = (X11InputContext *) context;
  XVaNestedList attr;
  gint x = 0, y = 0;

  if (ic->window)
    _gdk_window_native ((GdkWindowPrivate *) ic->window, &x, &y);
  ic->spot.x = x + area->x;
  ic->spot.y = y + area->y + area->height;
  if (!ic->icp.xic)
    return;
  attr = XVaCreateNestedList (0, XNSpotLocation, &ic->spot, NULL);
  XSetICValues (ic->icp.xic, XNPreeditAttributes, attr, NULL);
  XFree (attr);
}

static void
x11_ic_set_surrounding (GdkInputContext *context, const gchar *utf8, gint length,
                        gint cursor_index)
{
  /* XIM has no use for it. */
}

static void
x11_ic_set_purpose (GdkInputContext *context, GdkInputPurpose purpose)
{
  X11InputContext *ic = (X11InputContext *) context;
  gboolean password = purpose == GDK_INPUT_PURPOSE_PASSWORD;

  if (password == ic->password)
    return;
  ic->password = password;
  preedit_clear (ic);
  xic_destroy (ic);
  if (ic->focused)
    x11_ic_focus_in (context);
}

const GdkInputContextClass _gdk_x11_input_context_class = {
  x11_ic_create,
  x11_ic_destroy,
  x11_ic_set_window,
  x11_ic_focus_in,
  x11_ic_focus_out,
  x11_ic_reset,
  x11_ic_filter_key,
  x11_ic_get_preedit,
  x11_ic_set_cursor_location,
  x11_ic_set_surrounding,
  x11_ic_set_purpose,
};

#endif /* USE_XIM */
