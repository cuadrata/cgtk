/* GTK - The GIMP Toolkit
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

#ifndef __GTK_DIALOG_H__
#define __GTK_DIALOG_H__


#include <gdk/gdk.h>
#include <gtk/gtkwindow.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GTK_TYPE_DIALOG                  (gtk_dialog_get_type ())
#define GTK_DIALOG(obj)                  (GTK_CHECK_CAST ((obj), GTK_TYPE_DIALOG, GtkDialog))
#define GTK_DIALOG_CLASS(klass)          (GTK_CHECK_CLASS_CAST ((klass), GTK_TYPE_DIALOG, GtkDialogClass))
#define GTK_IS_DIALOG(obj)               (GTK_CHECK_TYPE ((obj), GTK_TYPE_DIALOG))
#define GTK_IS_DIALOG_CLASS(klass)       (GTK_CHECK_CLASS_TYPE ((klass), GTK_TYPE_DIALOG))


typedef struct _GtkDialog        GtkDialog;
typedef struct _GtkDialogClass   GtkDialogClass;
typedef struct _GtkDialogButton  GtkDialogButton;

/* How a dialog is to stand to the window it is for. */
typedef enum
{
  GTK_DIALOG_MODAL               = 1 << 0,
  GTK_DIALOG_DESTROY_WITH_PARENT = 1 << 1
} GtkDialogFlags;

/* What a dialog answers with: one of these, which are all below
 * zero, or a number of the program's own, which is not.
 */
typedef enum
{
  GTK_RESPONSE_NONE         = -1,
  GTK_RESPONSE_REJECT       = -2,
  GTK_RESPONSE_ACCEPT       = -3,
  GTK_RESPONSE_DELETE_EVENT = -4,
  GTK_RESPONSE_OK           = -5,
  GTK_RESPONSE_CANCEL       = -6,
  GTK_RESPONSE_CLOSE        = -7,
  GTK_RESPONSE_YES          = -8,
  GTK_RESPONSE_NO           = -9,
  GTK_RESPONSE_APPLY        = -10,
  GTK_RESPONSE_HELP         = -11
} GtkResponseType;


struct _GtkDialog
{
  GtkWindow window;

  GtkWidget *vbox;
  GtkWidget *action_area;
};

struct _GtkDialogClass
{
  GtkWindowClass parent_class;

  void (* response) (GtkDialog *dialog,
                     gint       response_id);
};


GtkType    gtk_dialog_get_type (void);
GtkWidget* gtk_dialog_new      (void);

/* Buttons that answer for the dialog, as GTK+ 2 has them. A button
 * added so emits "response" with its number when it is clicked, and
 * Escape answers GTK_RESPONSE_DELETE_EVENT. gtk_dialog_run() shows
 * the dialog, keeps the rest of the program from being used, and
 * returns the answer, which is GTK_RESPONSE_DELETE_EVENT too when
 * the window was closed. The dialog is still there afterwards, to be
 * asked things and then destroyed; unless it was destroyed meanwhile,
 * which is GTK_RESPONSE_NONE.
 */
GtkWidget* gtk_dialog_add_button           (GtkDialog   *dialog,
                                            const gchar *label,
                                            gint         response_id);
void       gtk_dialog_set_default_response (GtkDialog   *dialog,
                                            gint         response_id);
void       gtk_dialog_response             (GtkDialog   *dialog,
                                            gint         response_id);
gint       gtk_dialog_run                  (GtkDialog   *dialog);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GTK_DIALOG_H__ */
