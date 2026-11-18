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
 * Input contexts, whatever the backend: what the program asks goes to the
 * backend's class, and what the backend says goes back through the funcs
 * the program gave.
 */

#include "config.h"
#include "gdk.h"
#include "gdkbackend.h"

GdkInputContext *
gdk_input_context_new (const GdkInputContextFuncs *funcs, gpointer data)
{
  const GdkInputContextClass *klass = _gdk_backend ? _gdk_backend->input_context_class : NULL;
  GdkInputContext *context;

  g_return_val_if_fail (funcs != NULL, NULL);

  if (!klass || (context = klass->create ()) == NULL)
    return NULL;
  context->klass = klass;
  context->funcs = funcs;
  context->data = data;
  return context;
}

void
gdk_input_context_destroy (GdkInputContext *context)
{
  if (context)
    context->klass->destroy (context);
}

void
gdk_input_context_set_window (GdkInputContext *context, GdkWindow *window)
{
  g_return_if_fail (context != NULL);
  context->klass->set_window (context, window);
}

void
gdk_input_context_focus_in (GdkInputContext *context)
{
  g_return_if_fail (context != NULL);
  context->klass->focus_in (context);
}

void
gdk_input_context_focus_out (GdkInputContext *context)
{
  g_return_if_fail (context != NULL);
  context->klass->focus_out (context);
}

void
gdk_input_context_reset (GdkInputContext *context)
{
  g_return_if_fail (context != NULL);
  context->klass->reset (context);
}

gboolean
gdk_input_context_filter_key (GdkInputContext *context, GdkEventKey *event)
{
  g_return_val_if_fail (context != NULL, FALSE);
  g_return_val_if_fail (event != NULL, FALSE);
  return context->klass->filter_key (context, event);
}

void
gdk_input_context_get_preedit (GdkInputContext *context, gchar **utf8,
                               gint *cursor, gint *mark_start, gint *mark_end)
{
  gchar *text = NULL;
  gint c = 0, ms = 0, me = 0;

  g_return_if_fail (context != NULL);
  context->klass->get_preedit (context, &text, &c, &ms, &me);
  if (utf8)
    *utf8 = text ? text : g_strdup ("");
  else
    g_free (text);
  if (cursor)
    *cursor = c;
  if (mark_start)
    *mark_start = ms;
  if (mark_end)
    *mark_end = me;
}

void
gdk_input_context_set_cursor_location (GdkInputContext *context, GdkRectangle *area)
{
  g_return_if_fail (context != NULL);
  g_return_if_fail (area != NULL);
  context->klass->set_cursor_location (context, area);
}

void
gdk_input_context_set_surrounding (GdkInputContext *context, const gchar *utf8,
                                   gint length, gint cursor_index)
{
  g_return_if_fail (context != NULL);
  context->klass->set_surrounding (context, utf8, length, cursor_index);
}

void
gdk_input_context_set_purpose (GdkInputContext *context, GdkInputPurpose purpose)
{
  g_return_if_fail (context != NULL);
  context->klass->set_purpose (context, purpose);
}

/* For backends: what the input method said, to the program. */

void
_gdk_input_context_commit (GdkInputContext *context, const gchar *utf8)
{
  if (context->funcs->commit && utf8 && *utf8)
    context->funcs->commit (utf8, context->data);
}

void
_gdk_input_context_preedit_changed (GdkInputContext *context)
{
  if (context->funcs->preedit_changed)
    context->funcs->preedit_changed (context->data);
}

gboolean
_gdk_input_context_retrieve_surrounding (GdkInputContext *context)
{
  return context->funcs->retrieve_surrounding &&
    context->funcs->retrieve_surrounding (context->data);
}

gboolean
_gdk_input_context_delete_surrounding (GdkInputContext *context, gint offset, gint n_chars)
{
  return context->funcs->delete_surrounding &&
    context->funcs->delete_surrounding (offset, n_chars, context->data);
}
