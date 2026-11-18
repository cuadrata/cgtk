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

#include <gdk/gdkkeysyms.h>
#include "gtkbutton.h"
#include "gtkdialog.h"
#include "gtkhbox.h"
#include "gtkhseparator.h"
#include "gtkmain.h"
#include "gtksignal.h"
#include "gtkvbox.h"

enum
{
  RESPONSE,
  LAST_SIGNAL
};

static void gtk_dialog_class_init (GtkDialogClass *klass);
static void gtk_dialog_init       (GtkDialog      *dialog);
static gint gtk_dialog_key_press  (GtkWidget      *widget,
                                   GdkEventKey    *event);

static GtkWindowClass *parent_class = NULL;
static guint dialog_signals[LAST_SIGNAL] = { 0 };

static const gchar response_key[] = "gtk-dialog-response";


GtkType
gtk_dialog_get_type (void)
{
  static GtkType dialog_type = 0;

  if (!dialog_type)
    {
      static const GtkTypeInfo dialog_info =
      {
        "GtkDialog",
        sizeof (GtkDialog),
        sizeof (GtkDialogClass),
        (GtkClassInitFunc) gtk_dialog_class_init,
        (GtkObjectInitFunc) gtk_dialog_init,
        /* reserved_1 */ NULL,
        /* reserved_2 */ NULL,
        (GtkClassInitFunc) NULL,
      };

      dialog_type = gtk_type_unique (GTK_TYPE_WINDOW, &dialog_info);
    }

  return dialog_type;
}

static void
gtk_dialog_class_init (GtkDialogClass *class)
{
  GtkObjectClass *object_class = (GtkObjectClass *) class;
  GtkWidgetClass *widget_class = (GtkWidgetClass *) class;

  parent_class = gtk_type_class (GTK_TYPE_WINDOW);

  dialog_signals[RESPONSE] =
    gtk_signal_new ("response",
                    GTK_RUN_LAST,
                    object_class->type,
                    GTK_SIGNAL_OFFSET (GtkDialogClass, response),
                    gtk_marshal_NONE__INT,
                    GTK_TYPE_NONE, 1,
                    GTK_TYPE_INT);
  gtk_object_class_add_signals (object_class, dialog_signals, LAST_SIGNAL);

  widget_class->key_press_event = gtk_dialog_key_press;
  class->response = NULL;
}

static void
gtk_dialog_init (GtkDialog *dialog)
{
  GtkWidget *separator;

  dialog->vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (dialog), dialog->vbox);
  gtk_widget_show (dialog->vbox);

  dialog->action_area = gtk_hbox_new (TRUE, 5);
  gtk_container_set_border_width (GTK_CONTAINER (dialog->action_area), 10);
  gtk_box_pack_end (GTK_BOX (dialog->vbox), dialog->action_area, FALSE, TRUE, 0);
  gtk_widget_show (dialog->action_area);

  separator = gtk_hseparator_new ();
  gtk_box_pack_end (GTK_BOX (dialog->vbox), separator, FALSE, TRUE, 0);
  gtk_widget_show (separator);
}

GtkWidget*
gtk_dialog_new (void)
{
  return GTK_WIDGET (gtk_type_new (GTK_TYPE_DIALOG));
}

static void
gtk_dialog_button_clicked (GtkWidget *button,
                           GtkDialog *dialog)
{
  gtk_dialog_response (dialog, GPOINTER_TO_INT (gtk_object_get_data (GTK_OBJECT (button), response_key)));
}

GtkWidget*
gtk_dialog_add_button (GtkDialog   *dialog,
                       const gchar *label,
                       gint         response_id)
{
  GtkWidget *button;

  g_return_val_if_fail (dialog != NULL, NULL);
  g_return_val_if_fail (GTK_IS_DIALOG (dialog), NULL);
  g_return_val_if_fail (label != NULL, NULL);

  button = gtk_button_new_with_label (label);
  GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
  gtk_object_set_data (GTK_OBJECT (button), response_key, GINT_TO_POINTER (response_id));
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
                      GTK_SIGNAL_FUNC (gtk_dialog_button_clicked), dialog);
  gtk_box_pack_start (GTK_BOX (dialog->action_area), button, TRUE, TRUE, 0);
  gtk_widget_show (button);

  /* A dialog that has such buttons answers to Escape too. */
  gtk_object_set_data (GTK_OBJECT (dialog), response_key, dialog);

  return button;
}

/* The button for an answer is the one Return presses. */
void
gtk_dialog_set_default_response (GtkDialog *dialog,
                                 gint       response_id)
{
  GList *children, *list;

  g_return_if_fail (dialog != NULL);
  g_return_if_fail (GTK_IS_DIALOG (dialog));

  children = gtk_container_children (GTK_CONTAINER (dialog->action_area));
  for (list = children; list; list = list->next)
    if (gtk_object_get_data (GTK_OBJECT (list->data), response_key) == GINT_TO_POINTER (response_id) &&
        GTK_WIDGET_CAN_DEFAULT (list->data))
      {
        gtk_widget_grab_default (GTK_WIDGET (list->data));
        gtk_widget_grab_focus (GTK_WIDGET (list->data));
        break;
      }
  g_list_free (children);
}

void
gtk_dialog_response (GtkDialog *dialog,
                     gint       response_id)
{
  g_return_if_fail (dialog != NULL);
  g_return_if_fail (GTK_IS_DIALOG (dialog));

  gtk_signal_emit (GTK_OBJECT (dialog), dialog_signals[RESPONSE], response_id);
}

static gint
gtk_dialog_key_press (GtkWidget   *widget,
                      GdkEventKey *event)
{
  if (GTK_WIDGET_CLASS (parent_class)->key_press_event &&
      GTK_WIDGET_CLASS (parent_class)->key_press_event (widget, event))
    return TRUE;

  if (event->keyval == GDK_Escape && gtk_object_get_data (GTK_OBJECT (widget), response_key))
    {
      gtk_dialog_response (GTK_DIALOG (widget), GTK_RESPONSE_DELETE_EVENT);
      return TRUE;
    }

  return FALSE;
}

typedef struct
{
  gint response;
  gboolean running;
} GtkDialogRun;

static void
gtk_dialog_run_over (GtkDialogRun *run)
{
  if (run->running)
    {
      run->running = FALSE;
      gtk_main_quit ();
    }
}

static void
gtk_dialog_run_response (GtkDialog    *dialog,
                         gint          response_id,
                         GtkDialogRun *run)
{
  run->response = response_id;
  gtk_dialog_run_over (run);
}

static gint
gtk_dialog_run_delete (GtkDialog    *dialog,
                       GdkEventAny  *event,
                       GtkDialogRun *run)
{
  run->response = GTK_RESPONSE_DELETE_EVENT;
  gtk_dialog_run_over (run);

  return TRUE;  /* it stays, for the caller to do away with */
}

static void
gtk_dialog_run_destroy (GtkDialog    *dialog,
                        GtkDialogRun *run)
{
  gtk_dialog_run_over (run);
}

gint
gtk_dialog_run (GtkDialog *dialog)
{
  GtkDialogRun run = { GTK_RESPONSE_NONE, TRUE };
  gboolean was_modal;
  guint handlers[3];

  g_return_val_if_fail (dialog != NULL, GTK_RESPONSE_NONE);
  g_return_val_if_fail (GTK_IS_DIALOG (dialog), GTK_RESPONSE_NONE);

  gtk_object_ref (GTK_OBJECT (dialog));

  was_modal = GTK_WINDOW (dialog)->modal;
  gtk_widget_show (GTK_WIDGET (dialog));
  if (!was_modal)
    gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

  handlers[0] = gtk_signal_connect (GTK_OBJECT (dialog), "response",
                                    GTK_SIGNAL_FUNC (gtk_dialog_run_response), &run);
  handlers[1] = gtk_signal_connect (GTK_OBJECT (dialog), "delete_event",
                                    GTK_SIGNAL_FUNC (gtk_dialog_run_delete), &run);
  handlers[2] = gtk_signal_connect (GTK_OBJECT (dialog), "destroy",
                                    GTK_SIGNAL_FUNC (gtk_dialog_run_destroy), &run);

  gtk_main ();

  if (!GTK_OBJECT_DESTROYED (dialog))
    {
      gtk_signal_disconnect (GTK_OBJECT (dialog), handlers[0]);
      gtk_signal_disconnect (GTK_OBJECT (dialog), handlers[1]);
      gtk_signal_disconnect (GTK_OBJECT (dialog), handlers[2]);
      if (!was_modal)
        gtk_window_set_modal (GTK_WINDOW (dialog), FALSE);
    }
  gtk_object_unref (GTK_OBJECT (dialog));

  return run.response;
}
