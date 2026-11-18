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
#include "gdk/gdkftfont.h"
#include "gdk/gdkkeysyms.h"
#include "gtkimcontextsimple.h"
#include "gtksignal.h"

static GtkIMContextClass *parent_class = NULL;

static void
hex_end (GtkIMContextSimple *simple)
{
  if (!simple->in_hex)
    return;
  simple->in_hex = FALSE;
  simple->n_hex = 0;
  gtk_signal_emit_by_name (GTK_OBJECT (simple), "preedit_changed");
  gtk_signal_emit_by_name (GTK_OBJECT (simple), "preedit_end");
}

static gint
hex_digit (guint keyval)
{
  if (keyval >= GDK_0 && keyval <= GDK_9)
    return keyval - GDK_0;
  if (keyval >= GDK_KP_0 && keyval <= GDK_KP_9)
    return keyval - GDK_KP_0;
  if (keyval >= GDK_a && keyval <= GDK_f)
    return keyval - GDK_a + 10;
  if (keyval >= GDK_A && keyval <= GDK_F)
    return keyval - GDK_A + 10;
  return -1;
}

static gboolean
is_modifier (guint keyval)
{
  return keyval >= GDK_Shift_L && keyval <= GDK_Hyper_R;
}

/* The code typed so far, as a character to commit. */
static void
hex_commit (GtkIMContextSimple *simple)
{
  gulong code;
  gchar utf8[8];

  simple->hex[simple->n_hex] = '\0';
  code = strtoul (simple->hex, NULL, 16);
  hex_end (simple);
  if (code > 0 && code <= 0x10ffff && (code < 0xd800 || code > 0xdfff))
    {
      utf8[_gdk_ft_encode_utf8 (code, utf8)] = '\0';
      gtk_signal_emit_by_name (GTK_OBJECT (simple), "commit", utf8);
    }
}

static gboolean
simple_filter_keypress (GtkIMContext *context, GdkEventKey *event)
{
  GtkIMContextSimple *simple = GTK_IM_CONTEXT_SIMPLE (context);
  gint digit;

  if (event->type != GDK_KEY_PRESS)
    return simple->in_hex && !is_modifier (event->keyval);

  if (simple->in_hex)
    {
      if (is_modifier (event->keyval))
        return FALSE;
      if ((digit = hex_digit (event->keyval)) >= 0)
        {
          if (simple->n_hex < 6)
            {
              simple->hex[simple->n_hex++] = "0123456789abcdef"[digit];
              gtk_signal_emit_by_name (GTK_OBJECT (simple), "preedit_changed");
            }
        }
      else if (event->keyval == GDK_BackSpace)
        {
          if (--simple->n_hex < 0)
            hex_end (simple);
          else
            gtk_signal_emit_by_name (GTK_OBJECT (simple), "preedit_changed");
        }
      else if (event->keyval == GDK_Return || event->keyval == GDK_KP_Enter ||
               event->keyval == GDK_space || event->keyval == GDK_KP_Space)
        hex_commit (simple);
      else if (event->keyval == GDK_Escape)
        hex_end (simple);
      return TRUE;
    }

  if ((event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) == (GDK_CONTROL_MASK | GDK_SHIFT_MASK) &&
      (event->keyval == GDK_u || event->keyval == GDK_U))
    {
      simple->in_hex = TRUE;
      simple->n_hex = 0;
      gtk_signal_emit_by_name (GTK_OBJECT (simple), "preedit_start");
      gtk_signal_emit_by_name (GTK_OBJECT (simple), "preedit_changed");
      return TRUE;
    }

  /* Typed text: not a control character, nor typed with Control or Alt. */
  if (event->length > 0 && (guchar) event->string[0] >= 0x20 && event->string[0] != 0x7f &&
      !(event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)))
    {
      gchar *utf8 = gtk_im_locale_to_utf8 (event->string, event->length);

      gtk_signal_emit_by_name (GTK_OBJECT (simple), "commit", utf8);
      g_free (utf8);
      return TRUE;
    }
  return FALSE;
}

static void
simple_get_preedit_string (GtkIMContext *context, gchar **str, gint *cursor_pos,
                           gint *mark_start, gint *mark_end)
{
  GtkIMContextSimple *simple = GTK_IM_CONTEXT_SIMPLE (context);

  if (!simple->in_hex)
    {
      *str = g_strdup ("");
      *cursor_pos = *mark_start = *mark_end = 0;
      return;
    }
  simple->hex[simple->n_hex] = '\0';
  *str = g_strconcat ("u", simple->hex, NULL);
  *cursor_pos = 1 + simple->n_hex;
  *mark_start = *mark_end = 0;          /* underlined, not marked */
}

static void
simple_reset (GtkIMContext *context)
{
  hex_end (GTK_IM_CONTEXT_SIMPLE (context));
}

static void
gtk_im_context_simple_class_init (GtkIMContextSimpleClass *class)
{
  GtkIMContextClass *im_class = (GtkIMContextClass *) class;

  parent_class = gtk_type_class (GTK_TYPE_IM_CONTEXT);
  im_class->filter_keypress = simple_filter_keypress;
  im_class->get_preedit_string = simple_get_preedit_string;
  im_class->reset = simple_reset;
  im_class->focus_out = simple_reset;
}

GtkType
gtk_im_context_simple_get_type (void)
{
  static GtkType type = 0;

  if (!type)
    {
      static const GtkTypeInfo info =
      {
        "GtkIMContextSimple",
        sizeof (GtkIMContextSimple),
        sizeof (GtkIMContextSimpleClass),
        (GtkClassInitFunc) gtk_im_context_simple_class_init,
        (GtkObjectInitFunc) NULL,
        /* reserved_1 */ NULL,
        /* reserved_2 */ NULL,
        (GtkClassInitFunc) NULL,
      };

      type = gtk_type_unique (GTK_TYPE_IM_CONTEXT, &info);
    }
  return type;
}

GtkIMContext *
gtk_im_context_simple_new (void)
{
  return GTK_IM_CONTEXT (gtk_type_new (GTK_TYPE_IM_CONTEXT_SIMPLE));
}
