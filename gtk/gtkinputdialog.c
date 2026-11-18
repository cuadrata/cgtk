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
 * gtkinputdialog.c
 *
 * Copyright 1997 Owen Taylor <owt1@cornell.edu>
 *
 */

/*
 * Modified by the GTK+ Team and others 1997-1999. See the AUTHORS
 * file for a list of people on the GTK+ Team. See the ChangeLog
 * files for a list of changes. These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/.
 */


#include "gtkbutton.h"
#include "gtkinputdialog.h"
#include "gtklabel.h"
#include "gtksignal.h"
#include "gtkvbox.h"

#include "gtkintl.h"

enum
{
  ENABLE_DEVICE,
  DISABLE_DEVICE,
  LAST_SIGNAL
};

/* Forward declarations */

static void gtk_input_dialog_class_init (GtkInputDialogClass *klass);
static void gtk_input_dialog_init (GtkInputDialog *inputd);
static void gtk_input_dialog_finalize (GtkObject *object);

static GtkObjectClass *parent_class = NULL;
static guint input_dialog_signals[LAST_SIGNAL] = { 0 };

GtkType
gtk_input_dialog_get_type (void)
{
  static GtkType input_dialog_type = 0;

  if (!input_dialog_type)
    {
      static const GtkTypeInfo input_dialog_info =
      {
        "GtkInputDialog",
        sizeof (GtkInputDialog),
        sizeof (GtkInputDialogClass),
        (GtkClassInitFunc) gtk_input_dialog_class_init,
        (GtkObjectInitFunc) gtk_input_dialog_init,
        /* reserved_1 */ NULL,
        /* reserved_2 */ NULL,
        (GtkClassInitFunc) NULL,
      };

      input_dialog_type = gtk_type_unique (GTK_TYPE_DIALOG,
                                           &input_dialog_info);
    }

  return input_dialog_type;
}

static void
gtk_input_dialog_class_init (GtkInputDialogClass *klass)
{
  GtkObjectClass *object_class;

  object_class = (GtkObjectClass*) klass;

  parent_class = gtk_type_class (GTK_TYPE_DIALOG);

  input_dialog_signals[ENABLE_DEVICE] =
    gtk_signal_new ("enable_device",
                    GTK_RUN_LAST,
                    object_class->type,
                    GTK_SIGNAL_OFFSET (GtkInputDialogClass, enable_device),
                    gtk_marshal_NONE__INT,
                    GTK_TYPE_NONE, 1, GTK_TYPE_INT);

  input_dialog_signals[DISABLE_DEVICE] =
    gtk_signal_new ("disable_device",
                    GTK_RUN_LAST,
                    object_class->type,
                    GTK_SIGNAL_OFFSET (GtkInputDialogClass, disable_device),
                    gtk_marshal_NONE__INT,
                    GTK_TYPE_NONE, 1, GTK_TYPE_INT);

  gtk_object_class_add_signals (object_class, input_dialog_signals,
                                LAST_SIGNAL);


  object_class->finalize = gtk_input_dialog_finalize;
  klass->enable_device = NULL;
  klass->disable_device = NULL;
}

static void
gtk_input_dialog_init (GtkInputDialog *inputd)
{
  GtkWidget *vbox;
  GtkWidget *label;

  /* shell and main vbox */

  gtk_window_set_title (GTK_WINDOW (inputd), _("Input"));

  vbox = gtk_vbox_new (FALSE, 4);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (inputd)->vbox), vbox, TRUE, TRUE, 0);

  /* The core pointer is the only device, and it has nothing to set. */

  label = gtk_label_new (_("No input devices"));
  gtk_container_add (GTK_CONTAINER (vbox), label);

  gtk_widget_show (label);

  /* We create the save button in any case, so that clients can
     connect to it, without paying attention to whether it exits */
  inputd->save_button = gtk_button_new_with_label (_("Save"));
  GTK_WIDGET_SET_FLAGS (inputd->save_button, GTK_CAN_DEFAULT);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG(inputd)->action_area),
                      inputd->save_button, TRUE, TRUE, 0);
  gtk_widget_show (inputd->save_button);

  gtk_widget_set_sensitive(inputd->save_button, FALSE);

  inputd->close_button = gtk_button_new_with_label (_("Close"));
  GTK_WIDGET_SET_FLAGS (inputd->close_button, GTK_CAN_DEFAULT);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG(inputd)->action_area),
                      inputd->close_button, TRUE, TRUE, 0);

  gtk_widget_show (inputd->close_button);
  gtk_widget_grab_default (inputd->close_button);

  gtk_widget_show (vbox);
}


GtkWidget*
gtk_input_dialog_new (void)
{
  GtkInputDialog *inputd;

  inputd = gtk_type_new (GTK_TYPE_INPUT_DIALOG);

  return GTK_WIDGET (inputd);
}

static void
gtk_input_dialog_finalize (GtkObject *object)
{
  /*  GtkInputDialog *inputd = GTK_INPUT_DIALOG (object); */

  /* Clean up ? */

  (* GTK_OBJECT_CLASS (parent_class)->finalize) (object);
}
