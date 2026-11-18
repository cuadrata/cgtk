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

#ifndef __GTK_EDITABLE_H__
#define __GTK_EDITABLE_H__


#include <gdk/gdk.h>
#include <gtk/gtkwidget.h>
#include <gtk/gtkimcontext.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GTK_TYPE_EDITABLE            (gtk_editable_get_type ())
#define GTK_EDITABLE(obj)            (GTK_CHECK_CAST ((obj), GTK_TYPE_EDITABLE, GtkEditable))
#define GTK_EDITABLE_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), GTK_TYPE_EDITABLE, GtkEditableClass))
#define GTK_IS_EDITABLE(obj)         (GTK_CHECK_TYPE ((obj), GTK_TYPE_EDITABLE))
#define GTK_IS_EDITABLE_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), GTK_TYPE_EDITABLE))


typedef struct _GtkEditable       GtkEditable;
typedef struct _GtkEditableClass  GtkEditableClass;

typedef void (*GtkTextFunction) (GtkEditable  *editable, guint32 time);

struct _GtkEditable
{
  GtkWidget widget;

  /*< public >*/
  guint      current_pos;

  guint      selection_start_pos;
  guint      selection_end_pos;
  guint      has_selection : 1;

  /*< private >*/
  guint      editable : 1;
  guint      visible : 1;
  GdkIC     *ic;          /* unused: the input context is im_context */
  GdkICAttr *ic_attr;

  gchar *clipboard_text;

  /* The input method, and the text it is composing, shown at the cursor
   * but not yet in the text: preedit_cursor is where its own cursor is,
   * and the marked part is what the input method is working on. */
  GtkIMContext *im_context;
  GdkWChar *preedit;
  gint preedit_length;
  gint preedit_cursor;
  gint preedit_mark_start, preedit_mark_end;
};

struct _GtkEditableClass
{
  GtkWidgetClass parent_class;

  /* Signals for notification/filtering of changes */
  void (* changed)      (GtkEditable    *editable);
  void (* insert_text)  (GtkEditable    *editable,
                         const gchar    *text,
                         gint            length,
                         gint           *position);
  void (* delete_text)  (GtkEditable    *editable,
                         gint            start_pos,
                         gint            end_pos);

  /* Bindings actions */
  void (* activate)        (GtkEditable *editable);
  void (* set_editable)    (GtkEditable *editable,
                            gboolean     is_editable);
  void (* move_cursor)     (GtkEditable *editable,
                            gint         x,
                            gint         y);
  void (* move_word)       (GtkEditable *editable,
                            gint         n);
  void (* move_page)       (GtkEditable *editable,
                            gint         x,
                            gint         y);
  void (* move_to_row)     (GtkEditable *editable,
                            gint         row);
  void (* move_to_column)  (GtkEditable *editable,
                            gint         row);
  void (* kill_char)       (GtkEditable *editable,
                            gint         direction);
  void (* kill_word)       (GtkEditable *editable,
                            gint         direction);
  void (* kill_line)       (GtkEditable *editable,
                            gint         direction);
  void (* cut_clipboard)   (GtkEditable *editable);
  void (* copy_clipboard)  (GtkEditable *editable);
  void (* paste_clipboard) (GtkEditable *editable);

  /* Virtual functions. get_chars is in paricular not a signal because
   * it returns malloced memory. The others are not signals because
   * they would not be particularly useful as such. (All changes to
   * selection and position do not go through these functions)
   */
  void (* update_text)  (GtkEditable    *editable,
                         gint            start_pos,
                         gint            end_pos);
  gchar* (* get_chars)  (GtkEditable    *editable,
                         gint            start_pos,
                         gint            end_pos);
  void (* set_selection)(GtkEditable    *editable,
                         gint            start_pos,
                         gint            end_pos);
  void (* set_position) (GtkEditable    *editable,
                         gint            position);
};

GtkType    gtk_editable_get_type       (void);
void       gtk_editable_select_region  (GtkEditable      *editable,
                                        gint              start,
                                        gint              end);
void       gtk_editable_insert_text   (GtkEditable       *editable,
                                        const gchar      *new_text,
                                        gint              new_text_length,
                                        gint             *position);
void       gtk_editable_delete_text    (GtkEditable      *editable,
                                        gint              start_pos,
                                        gint              end_pos);
gchar*     gtk_editable_get_chars      (GtkEditable      *editable,
                                        gint              start_pos,
                                        gint              end_pos);
void       gtk_editable_cut_clipboard  (GtkEditable      *editable);
void       gtk_editable_copy_clipboard (GtkEditable      *editable);
void       gtk_editable_paste_clipboard (GtkEditable     *editable);
void       gtk_editable_claim_selection (GtkEditable     *editable,
                                         gboolean         claim,
                                         guint32          time);
void       gtk_editable_delete_selection (GtkEditable    *editable);

/*
 * Undo and redo of what the user did to the text: typing, the input
 * method's text, cut, paste and deleting, each step at a time, typing
 * a word at a time. A program's own changes (gtk_entry_set_text,
 * gtk_text_insert, gtk_editable_insert_text and the like) cannot be
 * undone and forget the steps before them, unless the program makes
 * them between gtk_editable_begin_user_action and
 * gtk_editable_end_user_action, as for a menu item of its own; all the
 * changes in one such pair are one step. Nothing is kept for an entry
 * whose text is hidden, and nothing is undone while the editable
 * cannot be edited. Ctrl+Z undoes, Ctrl+Shift+Z and Ctrl+Y redo, and
 * the right-click menu has both.
 */
void       gtk_editable_undo              (GtkEditable  *editable);
void       gtk_editable_redo              (GtkEditable  *editable);
gboolean   gtk_editable_can_undo          (GtkEditable  *editable);
gboolean   gtk_editable_can_redo          (GtkEditable  *editable);
void       gtk_editable_begin_user_action (GtkEditable  *editable);
void       gtk_editable_end_user_action   (GtkEditable  *editable);

/* For GtkEntry and GtkText: the text from start to end has just been
 * put in, or is about to be taken out. */
void       _gtk_editable_undo_inserted  (GtkEditable    *editable,
                                         gint            start,
                                         gint            end);
void       _gtk_editable_undo_deleting  (GtkEditable    *editable,
                                         gint            start,
                                         gint            end);

/* For GtkEntry and GtkText: their right-click menu, and whether a key
 * asks for it from the keyboard. */
void       _gtk_editable_popup_menu     (GtkEditable    *editable,
                                         GdkEventButton *event);
/* Whether the input method has text being composed to show. */
#define    _gtk_editable_preedit_shown(editable) \
  ((editable)->preedit_length > 0 && (editable)->visible)
gboolean   _gtk_editable_popup_key      (GdkEventKey    *event);
/* For their unrealize: the input context leaves their windows. */
void       _gtk_editable_im_unrealize   (GtkEditable    *editable);

void       gtk_editable_changed         (GtkEditable    *editable);
void       gtk_editable_set_position    (GtkEditable    *editable,
                                         gint            position);
gint       gtk_editable_get_position    (GtkEditable    *editable);
void       gtk_editable_set_editable    (GtkEditable    *editable,
                                         gboolean        is_editable);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GTK_EDITABLE_H__ */
