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

#include <stdarg.h>
#include "gtkbox.h"
#include "gtkintl.h"
#include "gtklabel.h"
#include "gtkmessagedialog.h"
#include "gtksignal.h"
#include "gtkvbox.h"


static void gtk_message_dialog_init (GtkMessageDialog *dialog);


GtkType
gtk_message_dialog_get_type (void)
{
  static GtkType message_dialog_type = 0;

  if (!message_dialog_type)
    {
      static const GtkTypeInfo message_dialog_info =
      {
        "GtkMessageDialog",
        sizeof (GtkMessageDialog),
        sizeof (GtkMessageDialogClass),
        (GtkClassInitFunc) NULL,
        (GtkObjectInitFunc) gtk_message_dialog_init,
        /* reserved_1 */ NULL,
        /* reserved_2 */ NULL,
        (GtkClassInitFunc) NULL,
      };

      message_dialog_type = gtk_type_unique (GTK_TYPE_DIALOG, &message_dialog_info);
    }

  return message_dialog_type;
}

static GtkWidget*
gtk_message_dialog_label (GtkWidget *box)
{
  GtkWidget *label = gtk_label_new (NULL);

  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

  return label;
}

static void
gtk_message_dialog_init (GtkMessageDialog *dialog)
{
  GtkWidget *vbox = gtk_vbox_new (FALSE, 10);

  gtk_container_set_border_width (GTK_CONTAINER (vbox), 14);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), vbox, TRUE, TRUE, 0);
  gtk_widget_show (vbox);

  dialog->label = gtk_message_dialog_label (vbox);
  gtk_widget_show (dialog->label);
  dialog->secondary_label = gtk_message_dialog_label (vbox);
  dialog->message_type = GTK_MESSAGE_INFO;

  gtk_window_set_policy (GTK_WINDOW (dialog), FALSE, FALSE, TRUE);
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_MOUSE);
}

GtkWidget*
gtk_message_dialog_new (GtkWindow      *parent,
                        GtkDialogFlags  flags,
                        GtkMessageType  type,
                        GtkButtonsType  buttons,
                        const gchar    *message_format,
                        ...)
{
  GtkMessageDialog *message_dialog;
  GtkDialog *dialog;
  gint answer = GTK_RESPONSE_NONE;

  g_return_val_if_fail (parent == NULL || GTK_IS_WINDOW (parent), NULL);

  message_dialog = gtk_type_new (GTK_TYPE_MESSAGE_DIALOG);
  dialog = GTK_DIALOG (message_dialog);
  message_dialog->message_type = type;

  if (message_format)
    {
      va_list args;
      gchar *message;

      va_start (args, message_format);
      message = g_strdup_vprintf (message_format, args);
      va_end (args);
      gtk_label_set_text (GTK_LABEL (message_dialog->label), message);
      g_free (message);
    }

  switch (buttons)
    {
    case GTK_BUTTONS_OK:
      gtk_dialog_add_button (dialog, _("OK"), answer = GTK_RESPONSE_OK);
      break;
    case GTK_BUTTONS_CLOSE:
      gtk_dialog_add_button (dialog, _("Close"), answer = GTK_RESPONSE_CLOSE);
      break;
    case GTK_BUTTONS_CANCEL:
      gtk_dialog_add_button (dialog, _("Cancel"), answer = GTK_RESPONSE_CANCEL);
      break;
    case GTK_BUTTONS_YES_NO:
      gtk_dialog_add_button (dialog, _("Yes"), answer = GTK_RESPONSE_YES);
      gtk_dialog_add_button (dialog, _("No"), GTK_RESPONSE_NO);
      break;
    case GTK_BUTTONS_OK_CANCEL:
      gtk_dialog_add_button (dialog, _("OK"), answer = GTK_RESPONSE_OK);
      gtk_dialog_add_button (dialog, _("Cancel"), GTK_RESPONSE_CANCEL);
      break;
    default:
      break;
    }
  gtk_dialog_set_default_response (dialog, answer);

  /* It is called what its window is called, or what the program is. */
  if (parent && parent->title)
    gtk_window_set_title (GTK_WINDOW (dialog), parent->title);
  else if (g_get_prgname ())
    gtk_window_set_title (GTK_WINDOW (dialog), g_get_prgname ());
  if (parent)
    {
      gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);
      if (flags & GTK_DIALOG_DESTROY_WITH_PARENT)
        gtk_signal_connect_object_while_alive (GTK_OBJECT (parent), "destroy",
                                               GTK_SIGNAL_FUNC (gtk_widget_destroy), GTK_OBJECT (dialog));
    }
  if (flags & GTK_DIALOG_MODAL)
    gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

  return GTK_WIDGET (dialog);
}

void
gtk_message_dialog_format_secondary_text (GtkMessageDialog *message_dialog,
                                          const gchar      *message_format,
                                          ...)
{
  g_return_if_fail (message_dialog != NULL);
  g_return_if_fail (GTK_IS_MESSAGE_DIALOG (message_dialog));

  if (message_format)
    {
      va_list args;
      gchar *message;

      va_start (args, message_format);
      message = g_strdup_vprintf (message_format, args);
      va_end (args);
      gtk_label_set_text (GTK_LABEL (message_dialog->secondary_label), message);
      g_free (message);
      gtk_widget_show (message_dialog->secondary_label);
    }
  else
    gtk_widget_hide (message_dialog->secondary_label);
}
