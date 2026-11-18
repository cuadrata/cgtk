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

/*
 * An input method as a text widget sees it: keys go in, and text comes
 * out, finished (commit) or still being composed (preedit), which the
 * widget shows at its cursor. Text is UTF-8; positions in it count
 * characters.
 */

#ifndef __GTK_IM_CONTEXT_H__
#define __GTK_IM_CONTEXT_H__

#include <gdk/gdk.h>
#include <gtk/gtkobject.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GTK_TYPE_IM_CONTEXT            (gtk_im_context_get_type ())
#define GTK_IM_CONTEXT(obj)            (GTK_CHECK_CAST ((obj), GTK_TYPE_IM_CONTEXT, GtkIMContext))
#define GTK_IM_CONTEXT_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), GTK_TYPE_IM_CONTEXT, GtkIMContextClass))
#define GTK_IS_IM_CONTEXT(obj)         (GTK_CHECK_TYPE ((obj), GTK_TYPE_IM_CONTEXT))
#define GTK_IS_IM_CONTEXT_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), GTK_TYPE_IM_CONTEXT))

typedef struct _GtkIMContext       GtkIMContext;
typedef struct _GtkIMContextClass  GtkIMContextClass;

struct _GtkIMContext
{
  GtkObject object;
};

struct _GtkIMContextClass
{
  GtkObjectClass parent_class;

  /* Signals */
  void     (*preedit_start)        (GtkIMContext *context);
  void     (*preedit_end)          (GtkIMContext *context);
  void     (*preedit_changed)      (GtkIMContext *context);
  void     (*commit)               (GtkIMContext *context, const gchar *str);
  gboolean (*retrieve_surrounding) (GtkIMContext *context);
  gboolean (*delete_surrounding)   (GtkIMContext *context, gint offset, gint n_chars);

  /* Virtual functions */
  void     (*set_client_window)    (GtkIMContext *context, GdkWindow *window);
  void     (*get_preedit_string)   (GtkIMContext *context, gchar **str, gint *cursor_pos,
                                    gint *mark_start, gint *mark_end);
  gboolean (*filter_keypress)      (GtkIMContext *context, GdkEventKey *event);
  void     (*focus_in)             (GtkIMContext *context);
  void     (*focus_out)            (GtkIMContext *context);
  void     (*reset)                (GtkIMContext *context);
  void     (*set_cursor_location)  (GtkIMContext *context, GdkRectangle *area);
  void     (*set_surrounding)      (GtkIMContext *context, const gchar *text, gint len,
                                    gint cursor_index);
  void     (*set_purpose)          (GtkIMContext *context, GdkInputPurpose purpose);
};

GtkType  gtk_im_context_get_type            (void);

void     gtk_im_context_set_client_window   (GtkIMContext *context, GdkWindow *window);
/* The text being composed (to g_free), its cursor, and the part the
 * input method marks (mark_start == mark_end if none). */
void     gtk_im_context_get_preedit_string  (GtkIMContext *context, gchar **str,
                                             gint *cursor_pos, gint *mark_start,
                                             gint *mark_end);
gboolean gtk_im_context_filter_keypress     (GtkIMContext *context, GdkEventKey *event);
void     gtk_im_context_focus_in            (GtkIMContext *context);
void     gtk_im_context_focus_out           (GtkIMContext *context);
void     gtk_im_context_reset               (GtkIMContext *context);
/* In the client window's coordinates. */
void     gtk_im_context_set_cursor_location (GtkIMContext *context, GdkRectangle *area);
void     gtk_im_context_set_surrounding     (GtkIMContext *context, const gchar *text,
                                             gint len, gint cursor_index);
void     gtk_im_context_set_purpose         (GtkIMContext *context, GdkInputPurpose purpose);

/* UTF-8 and the locale's encoding, which GTK widgets keep text in. */
gchar   *gtk_im_utf8_to_locale              (const gchar *utf8);
gchar   *gtk_im_locale_to_utf8              (const gchar *str, gint len);
/* Characters in UTF-8 text, and where character n starts. */
gint     gtk_im_utf8_length                 (const gchar *utf8);
const gchar *gtk_im_utf8_offset             (const gchar *utf8, gint n);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GTK_IM_CONTEXT_H__ */
