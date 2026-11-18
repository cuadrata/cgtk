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
 * The input context that needs nothing of the platform: typed text, and
 * Ctrl+Shift+U then a character's code in hex.
 */

#ifndef __GTK_IM_CONTEXT_SIMPLE_H__
#define __GTK_IM_CONTEXT_SIMPLE_H__

#include <gtk/gtkimcontext.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GTK_TYPE_IM_CONTEXT_SIMPLE     (gtk_im_context_simple_get_type ())
#define GTK_IM_CONTEXT_SIMPLE(obj)     (GTK_CHECK_CAST ((obj), GTK_TYPE_IM_CONTEXT_SIMPLE, GtkIMContextSimple))
#define GTK_IS_IM_CONTEXT_SIMPLE(obj)  (GTK_CHECK_TYPE ((obj), GTK_TYPE_IM_CONTEXT_SIMPLE))

typedef struct _GtkIMContextSimple       GtkIMContextSimple;
typedef struct _GtkIMContextSimpleClass  GtkIMContextSimpleClass;

struct _GtkIMContextSimple
{
  GtkIMContext object;
  gboolean in_hex;      /* after Ctrl+Shift+U */
  gchar hex[8];
  gint n_hex;
};

struct _GtkIMContextSimpleClass
{
  GtkIMContextClass parent_class;
};

GtkType       gtk_im_context_simple_get_type (void);
GtkIMContext *gtk_im_context_simple_new      (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GTK_IM_CONTEXT_SIMPLE_H__ */
