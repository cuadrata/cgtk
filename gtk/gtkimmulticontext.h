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
 * The input context a text widget makes: the platform's own, from the
 * GDK backend, with the simple one for what that leaves. GTK_IM_MODULE
 * set to "simple" keeps input methods out, as in GTK 3: dead keys and
 * compose sequences still work.
 */

#ifndef __GTK_IM_MULTICONTEXT_H__
#define __GTK_IM_MULTICONTEXT_H__

#include <gtk/gtkimcontext.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GTK_TYPE_IM_MULTICONTEXT     (gtk_im_multicontext_get_type ())
#define GTK_IM_MULTICONTEXT(obj)     (GTK_CHECK_CAST ((obj), GTK_TYPE_IM_MULTICONTEXT, GtkIMMulticontext))
#define GTK_IS_IM_MULTICONTEXT(obj)  (GTK_CHECK_TYPE ((obj), GTK_TYPE_IM_MULTICONTEXT))

typedef struct _GtkIMMulticontext       GtkIMMulticontext;
typedef struct _GtkIMMulticontextClass  GtkIMMulticontextClass;

struct _GtkIMMulticontext
{
  GtkIMContext object;
  GdkInputContext *native;      /* NULL if the platform has none */
  GtkIMContext *simple;
  gboolean native_preedit;      /* the native one is composing */
  gboolean no_input_method;     /* GTK_IM_MODULE=simple */
};

struct _GtkIMMulticontextClass
{
  GtkIMContextClass parent_class;
};

GtkType       gtk_im_multicontext_get_type (void);
GtkIMContext *gtk_im_multicontext_new      (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GTK_IM_MULTICONTEXT_H__ */
