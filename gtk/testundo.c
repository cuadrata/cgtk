/* GTK - The GIMP Toolkit
 * Copyright (C) 2026 Rupert Carmichael
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <https://www.gnu.org/licenses/>.
 */

/*
 * Undo and redo in GtkEntry and GtkText, typed with key events as the
 * user would. Needs a display: without one it is skipped.
 */

#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

static gint failures = 0;

static void
check (gboolean ok, const gchar *what)
{
  if (!ok)
    {
      printf ("FAIL: %s\n", what);
      failures++;
    }
}

static void
check_text (GtkEditable *editable, const gchar *want, const gchar *what)
{
  gchar *text = gtk_editable_get_chars (editable, 0, -1);

  if (strcmp (text, want) != 0)
    {
      printf ("FAIL: %s: \"%s\", not \"%s\"\n", what, text, want);
      failures++;
    }
  g_free (text);
}

static void
key (GtkWidget *widget, guint keyval, guint state)
{
  GdkEventKey event;
  gchar string[2] = { 0, 0 };

  memset (&event, 0, sizeof event);
  event.type = GDK_KEY_PRESS;
  event.window = widget->window;
  event.send_event = TRUE;
  event.time = GDK_CURRENT_TIME;
  event.state = state;
  event.keyval = keyval;
  if (keyval >= 0x20 && keyval < 0x7f && !(state & GDK_CONTROL_MASK))
    string[0] = keyval;
  event.string = string;
  event.length = strlen (string);
  gtk_widget_event (widget, (GdkEvent *) &event);
}

static void
type (GtkWidget *widget, const gchar *text)
{
  for (; *text; text++)
    key (widget, *text == '\n' ? GDK_Return : (guchar) *text, 0);
}

/* The classic filter of GTK+ 1.2's documentation: what is typed goes in
 * in capitals instead. */
static void
upper_insert (GtkEditable *editable, const gchar *text, gint length,
              gint *position, gpointer data)
{
  gchar *upper = g_strndup (text, length);

  g_strup (upper);
  gtk_signal_handler_block_by_func (GTK_OBJECT (editable),
                                    GTK_SIGNAL_FUNC (upper_insert), data);
  gtk_editable_insert_text (editable, upper, length, position);
  gtk_signal_handler_unblock_by_func (GTK_OBJECT (editable),
                                      GTK_SIGNAL_FUNC (upper_insert), data);
  gtk_signal_emit_stop_by_name (GTK_OBJECT (editable), "insert_text");
  g_free (upper);
}

static void
test_entry (GtkWidget *box)
{
  GtkWidget *widget = gtk_entry_new ();
  GtkEditable *editable = GTK_EDITABLE (widget);
  gint i, n, pos;

  gtk_box_pack_start (GTK_BOX (box), widget, FALSE, FALSE, 0);
  gtk_widget_show (widget);
  gtk_widget_grab_focus (widget);

  check (!gtk_editable_can_undo (editable), "entry: nothing to undo at first");

  /* Typing goes a word at a time. */
  type (widget, "hello world");
  check_text (editable, "hello world", "entry: typed");
  check (gtk_editable_can_undo (editable), "entry: can undo typing");
  gtk_editable_undo (editable);
  check_text (editable, "hello", "entry: undo the second word");
  check (gtk_editable_get_position (editable) == 5, "entry: cursor after undo");
  gtk_editable_undo (editable);
  check_text (editable, "", "entry: undo the first word");
  check (!gtk_editable_can_undo (editable), "entry: nothing more to undo");
  gtk_editable_redo (editable);
  check_text (editable, "hello", "entry: redo the first word");
  gtk_editable_redo (editable);
  check_text (editable, "hello world", "entry: redo the second word");
  check (!gtk_editable_can_redo (editable), "entry: nothing more to redo");

  /* Backspaces in a row are one step. */
  for (i = 0; i < 3; i++)
    key (widget, GDK_BackSpace, 0);
  check_text (editable, "hello wo", "entry: three backspaces");
  gtk_editable_undo (editable);
  check_text (editable, "hello world", "entry: undo the backspaces");

  /* So are deletes forward. */
  gtk_editable_set_position (editable, 0);
  for (i = 0; i < 2; i++)
    key (widget, GDK_Delete, 0);
  check_text (editable, "llo world", "entry: two deletes");
  gtk_editable_undo (editable);
  check_text (editable, "hello world", "entry: undo the deletes");

  /* The keys. */
  gtk_editable_set_position (editable, -1);
  type (widget, "!");
  key (widget, GDK_z, GDK_CONTROL_MASK);
  check_text (editable, "hello world", "entry: Ctrl+Z undoes");
  key (widget, GDK_Z, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
  check_text (editable, "hello world!", "entry: Ctrl+Shift+Z redoes");
  key (widget, GDK_z, GDK_CONTROL_MASK);
  key (widget, GDK_y, GDK_CONTROL_MASK);
  check_text (editable, "hello world!", "entry: Ctrl+Y redoes");

  /* Typing over a selection is one step with what it replaced. */
  gtk_editable_select_region (editable, 0, 5);
  type (widget, "J");
  check_text (editable, "J world!", "entry: typed over a selection");
  type (widget, "ane");
  check_text (editable, "Jane world!", "entry: typed on");
  gtk_editable_undo (editable);
  check_text (editable, "hello world!", "entry: undo the replacement whole");

  /* Something new after an undo forgets what could be redone. */
  gtk_editable_set_position (editable, -1);
  type (widget, "?");
  check (!gtk_editable_can_redo (editable), "entry: new typing ends redo");

  /* Cut and delete through the editable are the user's. */
  gtk_editable_select_region (editable, 0, 6);
  gtk_editable_cut_clipboard (editable);
  check_text (editable, "world!?", "entry: cut");
  gtk_editable_undo (editable);
  check_text (editable, "hello world!?", "entry: undo the cut");
  gtk_editable_select_region (editable, 5, 11);
  gtk_editable_delete_selection (editable);
  check_text (editable, "hello!?", "entry: delete the selection");
  gtk_editable_undo (editable);
  check_text (editable, "hello world!?", "entry: undo the delete");

  /* A program's own change is not undone, and ends the history. */
  gtk_entry_set_text (GTK_ENTRY (widget), "set");
  check (!gtk_editable_can_undo (editable), "entry: set_text ends the history");
  pos = 3;
  gtk_editable_insert_text (editable, "!", 1, &pos);
  check (!gtk_editable_can_undo (editable), "entry: insert_text is not undoable");

  /* Unless the program says it is the user's doing: one step. */
  gtk_editable_begin_user_action (editable);
  pos = 0;
  gtk_editable_insert_text (editable, "<", 1, &pos);
  pos = 5;
  gtk_editable_insert_text (editable, ">", 1, &pos);
  gtk_editable_end_user_action (editable);
  check_text (editable, "<set!>", "entry: a user action");
  gtk_editable_undo (editable);
  check_text (editable, "set!", "entry: the user action undone at once");

  /* A filter that puts in something else: what went in is undone. */
  gtk_entry_set_text (GTK_ENTRY (widget), "");
  gtk_signal_connect (GTK_OBJECT (widget), "insert_text",
                      GTK_SIGNAL_FUNC (upper_insert), NULL);
  type (widget, "abc");
  check_text (editable, "ABC", "entry: filtered typing");
  gtk_editable_undo (editable);
  check_text (editable, "", "entry: filtered typing undone");
  gtk_editable_redo (editable);
  check_text (editable, "ABC", "entry: filtered typing redone");
  gtk_signal_disconnect_by_func (GTK_OBJECT (widget),
                                 GTK_SIGNAL_FUNC (upper_insert), NULL);

  /* No more than a hundred steps are kept. */
  gtk_entry_set_text (GTK_ENTRY (widget), "");
  for (i = 0; i < 150; i++)
    type (widget, " w");
  for (n = 0; gtk_editable_can_undo (editable); n++)
    gtk_editable_undo (editable);
  check (n == 100, "entry: a hundred steps kept");

  /* Nothing can be undone while the entry cannot be edited. */
  type (widget, "x");
  gtk_editable_set_editable (editable, FALSE);
  check (!gtk_editable_can_undo (editable), "entry: not while not editable");
  gtk_editable_undo (editable);
  gtk_editable_set_editable (editable, TRUE);
  check (gtk_editable_can_undo (editable), "entry: again when editable");

  /* Hidden text is kept nowhere. */
  gtk_entry_set_visibility (GTK_ENTRY (widget), FALSE);
  gtk_entry_set_text (GTK_ENTRY (widget), "");
  type (widget, "secret");
  check (!gtk_editable_can_undo (editable), "entry: nothing kept of hidden text");
}

static void
test_text (GtkWidget *box)
{
  GtkWidget *widget = gtk_text_new (NULL, NULL);
  GtkEditable *editable = GTK_EDITABLE (widget);

  gtk_text_set_editable (GTK_TEXT (widget), TRUE);
  gtk_widget_set_usize (widget, 200, 100);
  gtk_box_pack_start (GTK_BOX (box), widget, TRUE, TRUE, 0);
  gtk_widget_show (widget);
  gtk_widget_grab_focus (widget);

  gtk_text_insert (GTK_TEXT (widget), NULL, NULL, NULL, "start\n", -1);
  check (!gtk_editable_can_undo (editable), "text: inserted by the program");

  gtk_editable_set_position (editable, -1);
  type (widget, "one two\nthree");
  check_text (editable, "start\none two\nthree", "text: typed");
  gtk_editable_undo (editable);
  check_text (editable, "start\none two", "text: undo the new line and its word");
  gtk_editable_undo (editable);
  check_text (editable, "start\none", "text: undo a word");
  gtk_editable_undo (editable);
  check_text (editable, "start\n", "text: undo all the typing");
  check (!gtk_editable_can_undo (editable), "text: nothing more");
  gtk_editable_redo (editable);
  gtk_editable_redo (editable);
  check_text (editable, "start\none two", "text: redo");

  key (widget, GDK_BackSpace, 0);
  key (widget, GDK_BackSpace, 0);
  key (widget, GDK_BackSpace, 0);
  check_text (editable, "start\none ", "text: backspaces");
  key (widget, GDK_z, GDK_CONTROL_MASK);
  check_text (editable, "start\none two", "text: Ctrl+Z");
  key (widget, GDK_Z, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
  check_text (editable, "start\none ", "text: Ctrl+Shift+Z");

  gtk_text_set_point (GTK_TEXT (widget), 0);
  gtk_text_forward_delete (GTK_TEXT (widget), 1);
  check (!gtk_editable_can_undo (editable), "text: deleted by the program");
}

int
main (int argc, char **argv)
{
  GtkWidget *window, *box;

  if (!gtk_init_check (&argc, &argv))
    {
      printf ("SKIP: no display\n");
      return 77;
    }

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  box = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (window), box);
  gtk_widget_show (box);
  gtk_widget_show (window);

  test_entry (box);
  test_text (box);

  gtk_widget_destroy (window);
  if (failures)
    printf ("%d failed\n", failures);
  return failures ? 1 : 0;
}
