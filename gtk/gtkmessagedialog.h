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

#ifndef __GTK_MESSAGE_DIALOG_H__
#define __GTK_MESSAGE_DIALOG_H__


#include <gdk/gdk.h>
#include <gtk/gtkdialog.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GTK_TYPE_MESSAGE_DIALOG            (gtk_message_dialog_get_type ())
#define GTK_MESSAGE_DIALOG(obj)            (GTK_CHECK_CAST ((obj), GTK_TYPE_MESSAGE_DIALOG, GtkMessageDialog))
#define GTK_MESSAGE_DIALOG_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), GTK_TYPE_MESSAGE_DIALOG, GtkMessageDialogClass))
#define GTK_IS_MESSAGE_DIALOG(obj)         (GTK_CHECK_TYPE ((obj), GTK_TYPE_MESSAGE_DIALOG))
#define GTK_IS_MESSAGE_DIALOG_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), GTK_TYPE_MESSAGE_DIALOG))


typedef struct _GtkMessageDialog       GtkMessageDialog;
typedef struct _GtkMessageDialogClass  GtkMessageDialogClass;

/* What kind of thing is said. Nothing is drawn for it: it is there
 * for the program to say, and for what may one day be drawn.
 */
typedef enum
{
  GTK_MESSAGE_INFO,
  GTK_MESSAGE_WARNING,
  GTK_MESSAGE_QUESTION,
  GTK_MESSAGE_ERROR
} GtkMessageType;

/* The buttons a message has, each answering with the GtkResponseType
 * of its name.
 */
typedef enum
{
  GTK_BUTTONS_NONE,
  GTK_BUTTONS_OK,
  GTK_BUTTONS_CLOSE,
  GTK_BUTTONS_CANCEL,
  GTK_BUTTONS_YES_NO,
  GTK_BUTTONS_OK_CANCEL
} GtkButtonsType;

struct _GtkMessageDialog
{
  GtkDialog dialog;

  GtkWidget *label;
  GtkWidget *secondary_label;
  GtkMessageType message_type;
};

struct _GtkMessageDialogClass
{
  GtkDialogClass parent_class;
};


GtkType    gtk_message_dialog_get_type (void);

/* A dialog that says one thing and has the buttons to answer it with,
 * for the window parent, which may be NULL. The message is made as
 * printf() makes it. It is not shown: gtk_dialog_run() shows it and
 * returns the answer, or the program shows it and hears "response".
 */
GtkWidget* gtk_message_dialog_new      (GtkWindow      *parent,
                                        GtkDialogFlags  flags,
                                        GtkMessageType  type,
                                        GtkButtonsType  buttons,
                                        const gchar    *message_format,
                                        ...) G_GNUC_PRINTF (5, 6);

/* More, under the message: what it means, or what each answer does. */
void       gtk_message_dialog_format_secondary_text (GtkMessageDialog *message_dialog,
                                                     const gchar      *message_format,
                                                     ...) G_GNUC_PRINTF (2, 3);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GTK_MESSAGE_DIALOG_H__ */
