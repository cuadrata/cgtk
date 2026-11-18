/* GTK - The GIMP Toolkit
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

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <langinfo.h>
#include "gdk/gdkftfont.h"
#include "gtkimcontext.h"
#include "gtksignal.h"

enum {
  PREEDIT_START,
  PREEDIT_END,
  PREEDIT_CHANGED,
  COMMIT,
  RETRIEVE_SURROUNDING,
  DELETE_SURROUNDING,
  LAST_SIGNAL
};

static guint im_context_signals[LAST_SIGNAL] = { 0 };

static void
default_get_preedit_string (GtkIMContext *context, gchar **str, gint *cursor_pos,
                            gint *mark_start, gint *mark_end)
{
  *str = g_strdup ("");
  *cursor_pos = *mark_start = *mark_end = 0;
}

static gboolean
default_filter_keypress (GtkIMContext *context, GdkEventKey *event)
{
  return FALSE;
}

static void
gtk_im_context_class_init (GtkIMContextClass *class)
{
  GtkObjectClass *object_class = (GtkObjectClass *) class;

  im_context_signals[PREEDIT_START] =
    gtk_signal_new ("preedit_start", GTK_RUN_LAST, object_class->type,
                    GTK_SIGNAL_OFFSET (GtkIMContextClass, preedit_start),
                    gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);
  im_context_signals[PREEDIT_END] =
    gtk_signal_new ("preedit_end", GTK_RUN_LAST, object_class->type,
                    GTK_SIGNAL_OFFSET (GtkIMContextClass, preedit_end),
                    gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);
  im_context_signals[PREEDIT_CHANGED] =
    gtk_signal_new ("preedit_changed", GTK_RUN_LAST, object_class->type,
                    GTK_SIGNAL_OFFSET (GtkIMContextClass, preedit_changed),
                    gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);
  im_context_signals[COMMIT] =
    gtk_signal_new ("commit", GTK_RUN_LAST, object_class->type,
                    GTK_SIGNAL_OFFSET (GtkIMContextClass, commit),
                    gtk_marshal_NONE__STRING, GTK_TYPE_NONE, 1, GTK_TYPE_STRING);
  im_context_signals[RETRIEVE_SURROUNDING] =
    gtk_signal_new ("retrieve_surrounding", GTK_RUN_LAST, object_class->type,
                    GTK_SIGNAL_OFFSET (GtkIMContextClass, retrieve_surrounding),
                    gtk_marshal_BOOL__NONE, GTK_TYPE_BOOL, 0);
  im_context_signals[DELETE_SURROUNDING] =
    gtk_signal_new ("delete_surrounding", GTK_RUN_LAST, object_class->type,
                    GTK_SIGNAL_OFFSET (GtkIMContextClass, delete_surrounding),
                    gtk_marshal_BOOL__INT_INT, GTK_TYPE_BOOL, 2, GTK_TYPE_INT, GTK_TYPE_INT);
  gtk_object_class_add_signals (object_class, im_context_signals, LAST_SIGNAL);

  class->get_preedit_string = default_get_preedit_string;
  class->filter_keypress = default_filter_keypress;
}

GtkType
gtk_im_context_get_type (void)
{
  static GtkType type = 0;

  if (!type)
    {
      static const GtkTypeInfo info =
      {
        "GtkIMContext",
        sizeof (GtkIMContext),
        sizeof (GtkIMContextClass),
        (GtkClassInitFunc) gtk_im_context_class_init,
        (GtkObjectInitFunc) NULL,
        /* reserved_1 */ NULL,
        /* reserved_2 */ NULL,
        (GtkClassInitFunc) NULL,
      };

      type = gtk_type_unique (GTK_TYPE_OBJECT, &info);
    }
  return type;
}

#define CLASS(context) GTK_IM_CONTEXT_CLASS (GTK_OBJECT (context)->klass)

void
gtk_im_context_set_client_window (GtkIMContext *context, GdkWindow *window)
{
  g_return_if_fail (GTK_IS_IM_CONTEXT (context));
  if (CLASS (context)->set_client_window)
    CLASS (context)->set_client_window (context, window);
}

void
gtk_im_context_get_preedit_string (GtkIMContext *context, gchar **str, gint *cursor_pos,
                                   gint *mark_start, gint *mark_end)
{
  gchar *s = NULL;
  gint c = 0, ms = 0, me = 0;

  g_return_if_fail (GTK_IS_IM_CONTEXT (context));
  CLASS (context)->get_preedit_string (context, &s, &c, &ms, &me);
  if (str)
    *str = s ? s : g_strdup ("");
  else
    g_free (s);
  if (cursor_pos)
    *cursor_pos = c;
  if (mark_start)
    *mark_start = ms;
  if (mark_end)
    *mark_end = me;
}

gboolean
gtk_im_context_filter_keypress (GtkIMContext *context, GdkEventKey *event)
{
  g_return_val_if_fail (GTK_IS_IM_CONTEXT (context), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);
  return CLASS (context)->filter_keypress (context, event);
}

void
gtk_im_context_focus_in (GtkIMContext *context)
{
  g_return_if_fail (GTK_IS_IM_CONTEXT (context));
  if (CLASS (context)->focus_in)
    CLASS (context)->focus_in (context);
}

void
gtk_im_context_focus_out (GtkIMContext *context)
{
  g_return_if_fail (GTK_IS_IM_CONTEXT (context));
  if (CLASS (context)->focus_out)
    CLASS (context)->focus_out (context);
}

void
gtk_im_context_reset (GtkIMContext *context)
{
  g_return_if_fail (GTK_IS_IM_CONTEXT (context));
  if (CLASS (context)->reset)
    CLASS (context)->reset (context);
}

void
gtk_im_context_set_cursor_location (GtkIMContext *context, GdkRectangle *area)
{
  g_return_if_fail (GTK_IS_IM_CONTEXT (context));
  g_return_if_fail (area != NULL);
  if (CLASS (context)->set_cursor_location)
    CLASS (context)->set_cursor_location (context, area);
}

void
gtk_im_context_set_surrounding (GtkIMContext *context, const gchar *text, gint len,
                                gint cursor_index)
{
  g_return_if_fail (GTK_IS_IM_CONTEXT (context));
  if (CLASS (context)->set_surrounding)
    CLASS (context)->set_surrounding (context, text, len, cursor_index);
}

void
gtk_im_context_set_purpose (GtkIMContext *context, GdkInputPurpose purpose)
{
  g_return_if_fail (GTK_IS_IM_CONTEXT (context));
  if (CLASS (context)->set_purpose)
    CLASS (context)->set_purpose (context, purpose);
}

/* ---------------------------------------------------------------------
 * UTF-8 and the locale's encoding. wchar_t is Unicode where this runs
 * (glibc, musl, the BSDs), which takes text between them.
 */

/* The next character and its length; a bad byte stands for U+FFFD. */
static guint32
utf8_get (const guchar *s, gint *len)
{
  guint32 c = s[0];
  gint n, i;

  if (c < 0x80)
    n = 1;
  else if ((c & 0xe0) == 0xc0)
    n = 2, c &= 0x1f;
  else if ((c & 0xf0) == 0xe0)
    n = 3, c &= 0x0f;
  else if ((c & 0xf8) == 0xf0)
    n = 4, c &= 0x07;
  else
    {
      *len = 1;
      return 0xfffd;
    }
  for (i = 1; i < n; i++)
    {
      if ((s[i] & 0xc0) != 0x80)
        {
          *len = i;
          return 0xfffd;
        }
      c = (c << 6) | (s[i] & 0x3f);
    }
  *len = n;
  return c;
}

static gboolean
locale_is_utf8 (void)
{
  const char *codeset = nl_langinfo (CODESET);

  return codeset && (!strcmp (codeset, "UTF-8") || !strcmp (codeset, "utf8"));
}

gint
gtk_im_utf8_length (const gchar *utf8)
{
  gint n = 0, len;

  while (*utf8)
    {
      utf8_get ((const guchar *) utf8, &len);
      utf8 += len;
      n++;
    }
  return n;
}

const gchar *
gtk_im_utf8_offset (const gchar *utf8, gint n)
{
  gint len;

  while (n-- > 0 && *utf8)
    {
      utf8_get ((const guchar *) utf8, &len);
      utf8 += len;
    }
  return utf8;
}

gchar *
gtk_im_utf8_to_locale (const gchar *utf8)
{
  GString *out;
  gchar *result;

  g_return_val_if_fail (utf8 != NULL, NULL);
  if (locale_is_utf8 ())
    return g_strdup (utf8);

  out = g_string_new ("");
  while (*utf8)
    {
      gchar mb[MB_LEN_MAX + 1];
      gint len;
      wchar_t wc = utf8_get ((const guchar *) utf8, &len);
      mbstate_t state;
      size_t n;

      memset (&state, 0, sizeof state);
      n = wcrtomb (mb, wc, &state);
      if (n == (size_t) -1)
        g_string_append_c (out, '?');
      else
        {
          mb[n] = '\0';
          g_string_append (out, mb);
        }
      utf8 += len;
    }
  result = out->str;
  g_string_free (out, FALSE);
  return result;
}

gchar *
gtk_im_locale_to_utf8 (const gchar *str, gint len)
{
  GString *out;
  gchar *result;
  mbstate_t state;

  g_return_val_if_fail (str != NULL, NULL);
  if (len < 0)
    len = strlen (str);
  if (locale_is_utf8 ())
    return g_strndup (str, len);

  out = g_string_new ("");
  memset (&state, 0, sizeof state);
  while (len > 0)
    {
      wchar_t wc;
      gchar u[5];
      gint m;
      size_t n = mbrtowc (&wc, str, len, &state);

      if (n == (size_t) -1 || n == (size_t) -2)
        {
          wc = 0xfffd;
          n = 1;
          memset (&state, 0, sizeof state);
        }
      else if (n == 0)
        break;
      m = _gdk_ft_encode_utf8 ((guint32) wc, u);
      u[m] = '\0';
      g_string_append (out, u);
      str += n;
      len -= n;
    }
  result = out->str;
  g_string_free (out, FALSE);
  return result;
}
