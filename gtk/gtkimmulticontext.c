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
#include "gtkimmulticontext.h"
#include "gtkimcontextsimple.h"
#include "gtksignal.h"

static GtkObjectClass *parent_class = NULL;

/* ---- what the platform's context says ---- */

static void
native_commit (const gchar *utf8, gpointer data)
{
  gtk_signal_emit_by_name (GTK_OBJECT (data), "commit", utf8);
}

static void
native_preedit_changed (gpointer data)
{
  GtkIMMulticontext *multi = GTK_IM_MULTICONTEXT (data);
  gchar *str;
  gboolean composing;

  gdk_input_context_get_preedit (multi->native, &str, NULL, NULL, NULL);
  composing = *str != '\0';
  g_free (str);

  if (composing && !multi->native_preedit)
    {
      multi->native_preedit = TRUE;
      gtk_signal_emit_by_name (GTK_OBJECT (multi), "preedit_start");
    }
  gtk_signal_emit_by_name (GTK_OBJECT (multi), "preedit_changed");
  if (!composing && multi->native_preedit)
    {
      multi->native_preedit = FALSE;
      gtk_signal_emit_by_name (GTK_OBJECT (multi), "preedit_end");
    }
}

static gboolean
native_retrieve_surrounding (gpointer data)
{
  gboolean handled = FALSE;

  gtk_signal_emit_by_name (GTK_OBJECT (data), "retrieve_surrounding", &handled);
  return handled;
}

static gboolean
native_delete_surrounding (gint offset, gint n_chars, gpointer data)
{
  gboolean handled = FALSE;

  gtk_signal_emit_by_name (GTK_OBJECT (data), "delete_surrounding", offset, n_chars, &handled);
  return handled;
}

static const GdkInputContextFuncs native_funcs = {
  native_commit,
  native_preedit_changed,
  native_retrieve_surrounding,
  native_delete_surrounding,
};

/* ---- the simple context's signals, passed on ---- */

static void
simple_commit (GtkIMContext *simple, const gchar *str, GtkIMMulticontext *multi)
{
  gtk_signal_emit_by_name (GTK_OBJECT (multi), "commit", str);
}

static void
simple_preedit_start (GtkIMContext *simple, GtkIMMulticontext *multi)
{
  gtk_signal_emit_by_name (GTK_OBJECT (multi), "preedit_start");
}

static void
simple_preedit_changed (GtkIMContext *simple, GtkIMMulticontext *multi)
{
  gtk_signal_emit_by_name (GTK_OBJECT (multi), "preedit_changed");
}

static void
simple_preedit_end (GtkIMContext *simple, GtkIMMulticontext *multi)
{
  gtk_signal_emit_by_name (GTK_OBJECT (multi), "preedit_end");
}

/* ---- GtkIMContext ---- */

static void
multi_set_client_window (GtkIMContext *context, GdkWindow *window)
{
  GtkIMMulticontext *multi = GTK_IM_MULTICONTEXT (context);

  if (multi->native)
    gdk_input_context_set_window (multi->native, window);
  gtk_im_context_set_client_window (multi->simple, window);
}

static void
multi_get_preedit_string (GtkIMContext *context, gchar **str, gint *cursor_pos,
                          gint *mark_start, gint *mark_end)
{
  GtkIMMulticontext *multi = GTK_IM_MULTICONTEXT (context);

  if (multi->native && multi->native_preedit)
    gdk_input_context_get_preedit (multi->native, str, cursor_pos, mark_start, mark_end);
  else
    gtk_im_context_get_preedit_string (multi->simple, str, cursor_pos, mark_start, mark_end);
}

static gboolean
multi_filter_keypress (GtkIMContext *context, GdkEventKey *event)
{
  GtkIMMulticontext *multi = GTK_IM_MULTICONTEXT (context);

  /* A code being typed after Ctrl+Shift+U has every key. */
  if (GTK_IM_CONTEXT_SIMPLE (multi->simple)->in_hex)
    return gtk_im_context_filter_keypress (multi->simple, event);
  if (multi->native && gdk_input_context_filter_key (multi->native, event))
    return TRUE;
  return gtk_im_context_filter_keypress (multi->simple, event);
}

static void
multi_focus_in (GtkIMContext *context)
{
  GtkIMMulticontext *multi = GTK_IM_MULTICONTEXT (context);

  if (multi->native)
    gdk_input_context_focus_in (multi->native);
  gtk_im_context_focus_in (multi->simple);
}

static void
multi_focus_out (GtkIMContext *context)
{
  GtkIMMulticontext *multi = GTK_IM_MULTICONTEXT (context);

  if (multi->native)
    gdk_input_context_focus_out (multi->native);
  gtk_im_context_focus_out (multi->simple);
}

static void
multi_reset (GtkIMContext *context)
{
  GtkIMMulticontext *multi = GTK_IM_MULTICONTEXT (context);

  if (multi->native)
    gdk_input_context_reset (multi->native);
  gtk_im_context_reset (multi->simple);
}

static void
multi_set_cursor_location (GtkIMContext *context, GdkRectangle *area)
{
  GtkIMMulticontext *multi = GTK_IM_MULTICONTEXT (context);

  if (multi->native)
    gdk_input_context_set_cursor_location (multi->native, area);
}

static void
multi_set_surrounding (GtkIMContext *context, const gchar *text, gint len, gint cursor_index)
{
  GtkIMMulticontext *multi = GTK_IM_MULTICONTEXT (context);

  if (multi->native)
    gdk_input_context_set_surrounding (multi->native, text, len, cursor_index);
}

static void
multi_set_purpose (GtkIMContext *context, GdkInputPurpose purpose)
{
  GtkIMMulticontext *multi = GTK_IM_MULTICONTEXT (context);

  if (multi->native)
    gdk_input_context_set_purpose (multi->native, multi->no_input_method ?
                                   GDK_INPUT_PURPOSE_PASSWORD : purpose);
}

static void
multi_finalize (GtkObject *object)
{
  GtkIMMulticontext *multi = GTK_IM_MULTICONTEXT (object);

  if (multi->native)
    gdk_input_context_destroy (multi->native);
  gtk_signal_disconnect_by_data (GTK_OBJECT (multi->simple), multi);
  gtk_object_unref (GTK_OBJECT (multi->simple));
  parent_class->finalize (object);
}

static void
gtk_im_multicontext_init (GtkIMMulticontext *multi)
{
  const gchar *module = getenv ("GTK_IM_MODULE");

  multi->simple = gtk_im_context_simple_new ();
  gtk_object_ref (GTK_OBJECT (multi->simple));
  gtk_object_sink (GTK_OBJECT (multi->simple));
  gtk_signal_connect (GTK_OBJECT (multi->simple), "commit",
                      GTK_SIGNAL_FUNC (simple_commit), multi);
  gtk_signal_connect (GTK_OBJECT (multi->simple), "preedit_start",
                      GTK_SIGNAL_FUNC (simple_preedit_start), multi);
  gtk_signal_connect (GTK_OBJECT (multi->simple), "preedit_changed",
                      GTK_SIGNAL_FUNC (simple_preedit_changed), multi);
  gtk_signal_connect (GTK_OBJECT (multi->simple), "preedit_end",
                      GTK_SIGNAL_FUNC (simple_preedit_end), multi);

  /* With no input method, every field is as a password field is to the
   * platform's context: only dead keys and compose sequences. */
  multi->no_input_method = module && strcmp (module, "simple") == 0;
  multi->native = gdk_input_context_new (&native_funcs, multi);
  if (multi->native && multi->no_input_method)
    gdk_input_context_set_purpose (multi->native, GDK_INPUT_PURPOSE_PASSWORD);
}

static void
gtk_im_multicontext_class_init (GtkIMMulticontextClass *class)
{
  GtkObjectClass *object_class = (GtkObjectClass *) class;
  GtkIMContextClass *im_class = (GtkIMContextClass *) class;

  parent_class = gtk_type_class (GTK_TYPE_IM_CONTEXT);
  object_class->finalize = multi_finalize;
  im_class->set_client_window = multi_set_client_window;
  im_class->get_preedit_string = multi_get_preedit_string;
  im_class->filter_keypress = multi_filter_keypress;
  im_class->focus_in = multi_focus_in;
  im_class->focus_out = multi_focus_out;
  im_class->reset = multi_reset;
  im_class->set_cursor_location = multi_set_cursor_location;
  im_class->set_surrounding = multi_set_surrounding;
  im_class->set_purpose = multi_set_purpose;
}

GtkType
gtk_im_multicontext_get_type (void)
{
  static GtkType type = 0;

  if (!type)
    {
      static const GtkTypeInfo info =
      {
        "GtkIMMulticontext",
        sizeof (GtkIMMulticontext),
        sizeof (GtkIMMulticontextClass),
        (GtkClassInitFunc) gtk_im_multicontext_class_init,
        (GtkObjectInitFunc) gtk_im_multicontext_init,
        /* reserved_1 */ NULL,
        /* reserved_2 */ NULL,
        (GtkClassInitFunc) NULL,
      };

      type = gtk_type_unique (GTK_TYPE_IM_CONTEXT, &info);
    }
  return type;
}

GtkIMContext *
gtk_im_multicontext_new (void)
{
  return GTK_IM_CONTEXT (gtk_type_new (GTK_TYPE_IM_MULTICONTEXT));
}
