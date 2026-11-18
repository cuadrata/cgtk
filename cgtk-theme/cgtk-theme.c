/* cgtk-theme - choose and edit CGTK themes
 * Copyright (C) 2026 The CGTK authors
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
 * cgtk-theme chooses the theme the user's rc file includes, and the font
 * it sets, and edits themes: their colours and their engine's options.
 * The rc file is $XDG_CONFIG_HOME/gtk-1.0/gtkrc (gtk_rc_get_user_file()).
 *
 * The window itself shows the theme being edited: it reads no rc file
 * but a preview of its own, rewritten at each change. Running programs
 * see a change when it is applied, which writes the theme and the rc file
 * and tells every CGTK program to read its rc files again; with "Apply
 * as you edit", that is a moment after each change. An installed theme
 * is never written: editing one makes a copy among the user's themes,
 * in $XDG_DATA_HOME/themes (gtk_rc_get_user_theme_dir()).
 *
 * Foo and Foo-Dark are one theme in the list, and the Dark check box
 * chooses between them.
 */

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "theme.h"

#define APPLY_DELAY 400         /* ms after the last change, as you edit */

/* ---------------------------------------------------------------------
 * State
 */

static GList *themes;           /* ThemeInfo */
static ThemeInfo *current;      /* the theme chosen */
static Theme *theme;            /* it, as edited */
static gboolean edited;         /* since it was read or applied */
static gchar *font;             /* the font the rc file sets, or NULL */

static gchar *preview_rc;       /* the window's own rc file */
static gchar *preview_theme_rc; /* the theme it includes */

static guint apply_timeout;
static gboolean updating;       /* setting widgets, not the user */

static gboolean want_dark;      /* the Dark check box as last set */

static GtkWidget *window, *theme_clist, *dark_check, *delete_button, *status_label;
static GtkWidget *preview_default;
static GtkWidget *live_check, *apply_button;
static GtkWidget *font_entry, *font_check, *options_box;

static void theme_changed (gboolean theme_edited);
static void apply (void);

/* ---------------------------------------------------------------------
 * Colours: the few that make a theme, each taking the others of its kind
 * along by the same amount, and every one of them
 */

typedef struct
{
  const gchar *label;
  gint set, state;
  gint related[3][2];           /* set, state; ends at a set of -1 */
  gboolean anywhere;            /* its colour goes wherever else it is */
} Role;

static const Role roles[] = {
  { "Window", SET_BG, GTK_STATE_NORMAL,
    { { SET_BG, GTK_STATE_ACTIVE }, { SET_BG, GTK_STATE_PRELIGHT }, { SET_BG, GTK_STATE_INSENSITIVE } } },
  { "Window text", SET_FG, GTK_STATE_NORMAL,
    { { SET_FG, GTK_STATE_ACTIVE }, { SET_FG, GTK_STATE_PRELIGHT }, { SET_FG, GTK_STATE_INSENSITIVE } } },
  { "Fields", SET_BASE, GTK_STATE_NORMAL,
    { { SET_BASE, GTK_STATE_ACTIVE }, { SET_BASE, GTK_STATE_PRELIGHT }, { SET_BASE, GTK_STATE_INSENSITIVE } } },
  { "Field text", SET_TEXT, GTK_STATE_NORMAL,
    { { SET_TEXT, GTK_STATE_ACTIVE }, { SET_TEXT, GTK_STATE_PRELIGHT }, { SET_TEXT, GTK_STATE_INSENSITIVE } } },
  /* An accent is used for more than selections: Fabrica fills its
   * progress bars with it as bg[PRELIGHT]. */
  { "Selection", SET_BG, GTK_STATE_SELECTED,
    { { SET_BASE, GTK_STATE_SELECTED }, { -1, 0 }, { -1, 0 } }, TRUE },
  { "Selected text", SET_FG, GTK_STATE_SELECTED,
    { { SET_TEXT, GTK_STATE_SELECTED }, { -1, 0 }, { -1, 0 } } },
};
#define N_ROLES (sizeof (roles) / sizeof (roles[0]))

static const gchar *set_labels[N_SETS] = { "Text", "Background", "Fields", "Field text" };
static const gchar *state_labels[N_STATES] =
  { "Normal", "Pressed", "Hover", "Selected", "Disabled" };

/* A button showing a colour, which opens a colour selector. */
typedef struct
{
  GtkWidget *button, *swatch;
  const Role *role;             /* or NULL: one colour, set and state */
  gint set, state;
} ColorButton;

static GList *color_buttons;

static void
color_button_get (ColorButton *cb, GdkColor *color)
{
  gint set = cb->role ? cb->role->set : cb->set;
  gint state = cb->role ? cb->role->state : cb->state;

  if (!theme || !theme_get_color (theme, set, state, color))
    {
      GtkStyle *style = gtk_widget_get_default_style ();
      GdkColor *colors[N_SETS] = { style->fg, style->bg, style->base, style->text };

      *color = colors[set][state];
    }
}

static gint
swatch_expose (GtkWidget *widget, GdkEventExpose *event, ColorButton *cb)
{
  GdkColor color;
  GdkGC *gc;

  color_button_get (cb, &color);
  if (!gdk_colormap_alloc_color (gtk_widget_get_colormap (widget), &color, FALSE, TRUE))
    return TRUE;
  gc = gdk_gc_new (widget->window);
  gdk_gc_set_foreground (gc, &color);
  gdk_draw_rectangle (widget->window, gc, TRUE, 0, 0,
                      widget->allocation.width, widget->allocation.height);
  gdk_draw_rectangle (widget->window, widget->style->black_gc, FALSE, 0, 0,
                      widget->allocation.width - 1, widget->allocation.height - 1);
  gdk_gc_unref (gc);
  return TRUE;
}

static void
color_buttons_redraw (void)
{
  GList *l;

  for (l = color_buttons; l; l = l->next)
    gtk_widget_queue_draw (((ColorButton *) l->data)->swatch);
}

/* What a colour selector started from: the colours it changes, and
 * the places in the theme that each was. */
typedef struct
{
  ColorButton *cb;
  GtkWidget *dialog;
  GdkColor start;
  GList *places;                /* NULL until set where the theme had none */
  GdkColor related[3];
  GList *related_places[3];
} ColorEdit;

static gushort
shifted (gushort value, gint by)
{
  return CLAMP ((gint) value + by, 0, 65535);
}

/* The colour, and with a role the others of its kind by the same amount
 * from where they started. */
static void
color_edit_set (ColorEdit *edit, const GdkColor *color)
{
  ColorButton *cb = edit->cb;
  gint set = cb->role ? cb->role->set : cb->set;
  gint state = cb->role ? cb->role->state : cb->state;
  gint i;

  if (cb->role)
    {
      gint dr = (gint) color->red - edit->start.red;
      gint dg = (gint) color->green - edit->start.green;
      gint db = (gint) color->blue - edit->start.blue;

      for (i = 0; i < 3; i++)
        if (edit->related_places[i])
          {
            GdkColor c = edit->related[i];

            c.red = shifted (c.red, dr);
            c.green = shifted (c.green, dg);
            c.blue = shifted (c.blue, db);
            theme_set_places (theme, edit->related_places[i], &c);
          }
    }

  if (edit->places)
    theme_set_places (theme, edit->places, color);
  else
    {
      theme_set_color (theme, set, state, color);
      edit->places = theme_find_color (theme, set, state, color);
    }

  theme_changed (TRUE);
}

static void
color_edit_changed (GtkWidget *colorsel, ColorEdit *edit)
{
  gdouble rgb[4];
  GdkColor color;

  gtk_color_selection_get_color (GTK_COLOR_SELECTION (colorsel), rgb);
  color.red = rgb[0] * 65535.0;
  color.green = rgb[1] * 65535.0;
  color.blue = rgb[2] * 65535.0;
  color_edit_set (edit, &color);
}

static void
color_edit_cancel (GtkWidget *button, ColorEdit *edit)
{
  color_edit_set (edit, &edit->start);
  gtk_widget_destroy (edit->dialog);
}

static void
color_edit_destroy (GtkWidget *dialog, ColorEdit *edit)
{
  gint i;

  g_list_free (edit->places);
  for (i = 0; i < 3; i++)
    g_list_free (edit->related_places[i]);
  g_free (edit);
}

static void
color_button_clicked (GtkWidget *button, ColorButton *cb)
{
  ColorEdit *edit = g_new0 (ColorEdit, 1);
  GtkColorSelectionDialog *dialog;
  gdouble rgb[4];
  gint i;

  if (!theme)
    {
      g_free (edit);
      return;
    }
  edit->cb = cb;
  color_button_get (cb, &edit->start);
  if (cb->role)
    {
      if (theme_get_color (theme, cb->role->set, cb->role->state, &edit->start))
        edit->places = theme_find_color (theme, cb->role->anywhere ? -1 : cb->role->set,
                                         cb->role->state, &edit->start);
      for (i = 0; i < 3 && cb->role->related[i][0] >= 0; i++)
        if (theme_get_color (theme, cb->role->related[i][0], cb->role->related[i][1],
                             &edit->related[i]))
          edit->related_places[i] = theme_find_color (theme, cb->role->related[i][0],
                                                      cb->role->related[i][1],
                                                      &edit->related[i]);
    }
  else if (theme_get_color (theme, cb->set, cb->state, &edit->start))
    edit->places = theme_find_color (theme, cb->set, cb->state, &edit->start);

  edit->dialog = gtk_color_selection_dialog_new (cb->role ? cb->role->label : "Colour");
  dialog = GTK_COLOR_SELECTION_DIALOG (edit->dialog);
  gtk_window_set_transient_for (GTK_WINDOW (edit->dialog), GTK_WINDOW (window));
  gtk_widget_hide (dialog->help_button);
  rgb[0] = edit->start.red / 65535.0;
  rgb[1] = edit->start.green / 65535.0;
  rgb[2] = edit->start.blue / 65535.0;
  rgb[3] = 1.0;
  gtk_color_selection_set_color (GTK_COLOR_SELECTION (dialog->colorsel), rgb);
  gtk_signal_connect (GTK_OBJECT (dialog->colorsel), "color_changed",
                      GTK_SIGNAL_FUNC (color_edit_changed), edit);
  gtk_signal_connect_object (GTK_OBJECT (dialog->ok_button), "clicked",
                             GTK_SIGNAL_FUNC (gtk_widget_destroy), GTK_OBJECT (edit->dialog));
  gtk_signal_connect (GTK_OBJECT (dialog->cancel_button), "clicked",
                      GTK_SIGNAL_FUNC (color_edit_cancel), edit);
  gtk_signal_connect (GTK_OBJECT (edit->dialog), "destroy",
                      GTK_SIGNAL_FUNC (color_edit_destroy), edit);
  gtk_widget_show (edit->dialog);
}

static GtkWidget *
color_button_new (const Role *role, gint set, gint state)
{
  ColorButton *cb = g_new0 (ColorButton, 1);

  cb->role = role;
  cb->set = set;
  cb->state = state;
  cb->button = gtk_button_new ();
  cb->swatch = gtk_drawing_area_new ();
  gtk_drawing_area_size (GTK_DRAWING_AREA (cb->swatch), 36, 14);
  gtk_container_add (GTK_CONTAINER (cb->button), cb->swatch);
  gtk_signal_connect (GTK_OBJECT (cb->swatch), "expose_event",
                      GTK_SIGNAL_FUNC (swatch_expose), cb);
  gtk_signal_connect (GTK_OBJECT (cb->button), "clicked",
                      GTK_SIGNAL_FUNC (color_button_clicked), cb);
  color_buttons = g_list_append (color_buttons, cb);
  return cb->button;
}

/* The six that make a theme, then every colour */
static GtkWidget *
colors_page (void)
{
  GtkWidget *vbox = gtk_vbox_new (FALSE, 8);
  GtkWidget *table = gtk_table_new (N_ROLES, 2, FALSE);
  GtkWidget *all_colors_table;
  guint i;
  gint set, state;

  gtk_container_set_border_width (GTK_CONTAINER (vbox), 8);
  gtk_table_set_row_spacings (GTK_TABLE (table), 4);
  gtk_table_set_col_spacings (GTK_TABLE (table), 8);
  for (i = 0; i < N_ROLES; i++)
    {
      GtkWidget *label = gtk_label_new (roles[i].label);

      gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
      gtk_table_attach (GTK_TABLE (table), label, 0, 1, i, i + 1, GTK_FILL, 0, 0, 0);
      gtk_table_attach (GTK_TABLE (table), color_button_new (&roles[i], 0, 0),
                        1, 2, i, i + 1, 0, 0, 0, 0);
    }
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (vbox), gtk_hseparator_new (), FALSE, FALSE, 0);

  all_colors_table = gtk_table_new (N_SETS + 1, N_STATES + 1, FALSE);
  gtk_table_set_row_spacings (GTK_TABLE (all_colors_table), 4);
  gtk_table_set_col_spacings (GTK_TABLE (all_colors_table), 4);
  for (state = 0; state < N_STATES; state++)
    gtk_table_attach (GTK_TABLE (all_colors_table), gtk_label_new (state_labels[state]),
                      state + 1, state + 2, 0, 1, GTK_FILL, 0, 0, 0);
  for (set = 0; set < N_SETS; set++)
    {
      GtkWidget *label = gtk_label_new (set_labels[set]);

      gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
      gtk_table_attach (GTK_TABLE (all_colors_table), label, 0, 1, set + 1, set + 2,
                        GTK_FILL, 0, 0, 0);
      for (state = 0; state < N_STATES; state++)
        gtk_table_attach (GTK_TABLE (all_colors_table), color_button_new (NULL, set, state),
                          state + 1, state + 2, set + 1, set + 2, 0, 0, 0, 0);
    }
  gtk_box_pack_start (GTK_BOX (vbox), all_colors_table, FALSE, FALSE, 0);

  return vbox;
}

/* ---------------------------------------------------------------------
 * The font
 */

static void
font_changed (void)
{
  const gchar *name = gtk_entry_get_text (GTK_ENTRY (font_entry));

  if (updating)
    return;
  g_free (font);
  font = GTK_TOGGLE_BUTTON (font_check)->active && *name ? g_strdup (name) : NULL;
  gtk_widget_set_sensitive (font_entry, GTK_TOGGLE_BUTTON (font_check)->active);
  theme_changed (FALSE);
}

static void
font_dialog_ok (GtkWidget *button, GtkWidget *dialog)
{
  gchar *name = gtk_font_selection_dialog_get_font_name (GTK_FONT_SELECTION_DIALOG (dialog));

  if (name)
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (font_check), TRUE);
      gtk_entry_set_text (GTK_ENTRY (font_entry), name);
      g_free (name);
    }
  gtk_widget_destroy (dialog);
}

static void
font_choose (void)
{
  GtkWidget *dialog = gtk_font_selection_dialog_new ("Font");
  const gchar *name = gtk_entry_get_text (GTK_ENTRY (font_entry));

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (window));
  if (*name)
    gtk_font_selection_dialog_set_font_name (GTK_FONT_SELECTION_DIALOG (dialog), name);
  gtk_signal_connect (GTK_OBJECT (GTK_FONT_SELECTION_DIALOG (dialog)->ok_button), "clicked",
                      GTK_SIGNAL_FUNC (font_dialog_ok), dialog);
  gtk_signal_connect_object (GTK_OBJECT (GTK_FONT_SELECTION_DIALOG (dialog)->cancel_button),
                             "clicked", GTK_SIGNAL_FUNC (gtk_widget_destroy), GTK_OBJECT (dialog));
  gtk_widget_show (dialog);
}

static GtkWidget *
font_page (void)
{
  GtkWidget *vbox = gtk_vbox_new (FALSE, 8);
  GtkWidget *hbox = gtk_hbox_new (FALSE, 8);
  GtkWidget *button, *label;

  gtk_container_set_border_width (GTK_CONTAINER (vbox), 8);
  font_check = gtk_check_button_new_with_label ("Use this font in every theme:");
  gtk_signal_connect (GTK_OBJECT (font_check), "toggled", GTK_SIGNAL_FUNC (font_changed), NULL);
  gtk_box_pack_start (GTK_BOX (vbox), font_check, FALSE, FALSE, 0);

  font_entry = gtk_entry_new ();
  gtk_signal_connect (GTK_OBJECT (font_entry), "changed", GTK_SIGNAL_FUNC (font_changed), NULL);
  gtk_box_pack_start (GTK_BOX (hbox), font_entry, TRUE, TRUE, 0);
  button = gtk_button_new_with_label ("Choose...");
  gtk_signal_connect (GTK_OBJECT (button), "clicked", GTK_SIGNAL_FUNC (font_choose), NULL);
  gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  label = gtk_label_new ("Otherwise each theme's own font is used. "
                         "Fonts are named as \"Sans 10\" or \"DejaVu Serif Bold 12\".");
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);
  return vbox;
}

/* ---------------------------------------------------------------------
 * The engine's options
 */

typedef enum { OPT_BOOL, OPT_INT, OPT_FLOAT, OPT_CHOICE } OptionKind;

typedef struct
{
  const gchar *engine, *key, *label;
  OptionKind kind;
  const gchar *fallback;        /* the engine's own default */
  gdouble min, max, step;
  const gchar *choices[7];      /* for OPT_CHOICE, ending in NULL */
  const gchar *choice_labels[7];
} OptionInfo;

static const OptionInfo option_infos[] = {
  { "fabrica", "contrast", "Contrast", OPT_FLOAT, "1.0", 0.0, 2.0, 0.1 },
  { "fabrica", "rounded_buttons", "Rounded buttons", OPT_BOOL, "TRUE" },
  { "gelidus", "rect_scrollbar", "Square scrollbar sliders", OPT_BOOL, "FALSE" },
  { "gelidus", "mark_type1", "Marks on sliders and handles", OPT_CHOICE, "SLASH", 0, 0, 0,
    { "NOTHING", "SLASH", "INVSLASH", "DOT", "INVDOT", NULL },
    { "None", "Slashes", "Back slashes", "Dots", "Sunken dots", NULL } },
  { "gelidus", "mark_type2", "Marks on scrollbar arrows", OPT_CHOICE, "INVSLASH", 0, 0, 0,
    { "NOTHING", "SLASH", "INVSLASH", "DOT", "INVDOT", "ARROW", NULL },
    { "None", "Slashes", "Back slashes", "Dots", "Sunken dots", "Arrows", NULL } },
  { "gelidus", "paned_dots", "Dots on paned handles", OPT_CHOICE, "FULL", 0, 0, 0,
    { "NONE", "SOME", "FULL", NULL },
    { "None", "A few", "All along", NULL } },
  { "unicus", "slider_width", "Scrollbar width", OPT_INT, "12", 4, 64, 1 },
};
#define N_OPTION_INFOS (sizeof (option_infos) / sizeof (option_infos[0]))

static gchar *
option_value (const OptionInfo *info)
{
  gchar *value = theme ? theme_get_option (theme, info->key) : NULL;

  return value ? value : g_strdup (info->fallback);
}

static void
option_set (const OptionInfo *info, const gchar *value)
{
  if (updating || !theme)
    return;
  theme_set_option (theme, info->key, value);
  theme_changed (TRUE);
}

static void
option_toggled (GtkWidget *check, const OptionInfo *info)
{
  option_set (info, GTK_TOGGLE_BUTTON (check)->active ? "TRUE" : "FALSE");
}

static void
option_spun (GtkAdjustment *adj, const OptionInfo *info)
{
  gchar *value = info->kind == OPT_INT ? g_strdup_printf ("%d", (gint) (adj->value + 0.5))
                                       : g_strdup_printf ("%.1f", adj->value);

  option_set (info, value);
  g_free (value);
}

static void
option_chosen (GtkWidget *item, const OptionInfo *info)
{
  option_set (info, gtk_object_get_data (GTK_OBJECT (item), "value"));
}

static void
options_rebuild (void)
{
  GtkWidget *table;
  const gchar *engine = theme ? theme_engine (theme) : NULL;
  guint i, row = 0;

  gtk_container_foreach (GTK_CONTAINER (options_box), (GtkCallback) gtk_widget_destroy, NULL);
  table = gtk_table_new (N_OPTION_INFOS, 2, FALSE);
  gtk_table_set_row_spacings (GTK_TABLE (table), 4);
  gtk_table_set_col_spacings (GTK_TABLE (table), 8);

  updating = TRUE;
  for (i = 0; i < N_OPTION_INFOS; i++)
    {
      const OptionInfo *info = &option_infos[i];
      gchar *value;
      GtkWidget *widget;

      if (!engine || strcmp (engine, info->engine) != 0)
        continue;
      value = option_value (info);
      if (info->kind == OPT_BOOL)
        {
          widget = gtk_check_button_new_with_label (info->label);
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget),
                                        g_strcasecmp (value, "TRUE") == 0);
          gtk_signal_connect (GTK_OBJECT (widget), "toggled",
                              GTK_SIGNAL_FUNC (option_toggled), (gpointer) info);
          gtk_table_attach (GTK_TABLE (table), widget, 0, 2, row, row + 1, GTK_FILL, 0, 0, 0);
        }
      else
        {
          GtkWidget *label = gtk_label_new (info->label);

          gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
          gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1, GTK_FILL, 0, 0, 0);
          if (info->kind == OPT_CHOICE)
            {
              GtkWidget *menu = gtk_menu_new ();
              gint j, active = 0;

              widget = gtk_option_menu_new ();
              for (j = 0; info->choices[j]; j++)
                {
                  GtkWidget *item = gtk_menu_item_new_with_label (info->choice_labels[j]);

                  gtk_object_set_data (GTK_OBJECT (item), "value", (gpointer) info->choices[j]);
                  gtk_signal_connect (GTK_OBJECT (item), "activate",
                                      GTK_SIGNAL_FUNC (option_chosen), (gpointer) info);
                  gtk_menu_append (GTK_MENU (menu), item);
                  gtk_widget_show (item);
                  if (g_strcasecmp (value, info->choices[j]) == 0)
                    active = j;
                }
              gtk_option_menu_set_menu (GTK_OPTION_MENU (widget), menu);
              gtk_option_menu_set_history (GTK_OPTION_MENU (widget), active);
            }
          else
            {
              GtkObject *adj = gtk_adjustment_new (strtod (value, NULL), info->min, info->max,
                                                   info->step, info->step * 10, 0);

              widget = gtk_spin_button_new (GTK_ADJUSTMENT (adj), info->step,
                                            info->kind == OPT_INT ? 0 : 1);
              gtk_signal_connect (adj, "value_changed",
                                  GTK_SIGNAL_FUNC (option_spun), (gpointer) info);
            }
          gtk_table_attach (GTK_TABLE (table), widget, 1, 2, row, row + 1, GTK_FILL, 0, 0, 0);
        }
      g_free (value);
      row++;
    }
  updating = FALSE;

  if (row == 0)
    {
      GtkWidget *label = gtk_label_new (engine ? "This theme's engine has no options to set."
                                               : "This theme has no engine.");

      gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.0);
      gtk_box_pack_start (GTK_BOX (options_box), label, FALSE, FALSE, 0);
      gtk_widget_show (label);
      /* never added, it is still floating: sinking it frees it */
      gtk_object_sink (GTK_OBJECT (table));
      return;
    }
  gtk_box_pack_start (GTK_BOX (options_box), table, FALSE, FALSE, 0);
  gtk_widget_show_all (table);
}

static GtkWidget *
options_page (void)
{
  options_box = gtk_vbox_new (FALSE, 8);
  gtk_container_set_border_width (GTK_CONTAINER (options_box), 8);
  return options_box;
}

/* ---------------------------------------------------------------------
 * Widgets to see the theme on: a little of everything a theme draws
 */

static GtkWidget *
preview_menu_item (GtkWidget *menu, GtkWidget *item)
{
  gtk_menu_append (GTK_MENU (menu), item);
  return item;
}

static GtkWidget *
preview_menu_bar (void)
{
  GtkWidget *menubar = gtk_menu_bar_new ();
  GtkWidget *menu, *submenu, *item;
  GSList *group;

  menu = gtk_menu_new ();
  preview_menu_item (menu, gtk_menu_item_new_with_label ("Open"));
  item = preview_menu_item (menu, gtk_menu_item_new_with_label ("Save"));
  gtk_widget_set_sensitive (item, FALSE);
  preview_menu_item (menu, gtk_menu_item_new ());
  submenu = gtk_menu_new ();
  preview_menu_item (submenu, gtk_menu_item_new_with_label ("letter.txt"));
  preview_menu_item (submenu, gtk_menu_item_new_with_label ("notes.txt"));
  item = preview_menu_item (menu, gtk_menu_item_new_with_label ("Recent"));
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
  preview_menu_item (menu, gtk_menu_item_new ());
  preview_menu_item (menu, gtk_menu_item_new_with_label ("Close"));
  item = gtk_menu_item_new_with_label ("File");
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), menu);
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), item);

  menu = gtk_menu_new ();
  item = preview_menu_item (menu, gtk_check_menu_item_new_with_label ("Toolbar"));
  gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), TRUE);
  preview_menu_item (menu, gtk_check_menu_item_new_with_label ("Status Bar"));
  preview_menu_item (menu, gtk_menu_item_new ());
  item = preview_menu_item (menu, gtk_radio_menu_item_new_with_label (NULL, "Small"));
  group = gtk_radio_menu_item_group (GTK_RADIO_MENU_ITEM (item));
  item = preview_menu_item (menu, gtk_radio_menu_item_new_with_label (group, "Large"));
  gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), TRUE);
  item = gtk_menu_item_new_with_label ("View");
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), menu);
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), item);

  menu = gtk_menu_new ();
  preview_menu_item (menu, gtk_menu_item_new_with_label ("About"));
  item = gtk_menu_item_new_with_label ("Help");
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), menu);
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), item);

  return menubar;
}

/* A toolbar of words, in a handle box, as many programs had them, with
 * buttons that show only under the pointer or when down, as GNOME 1's
 * and most others' did */
static GtkWidget *
preview_toolbar (void)
{
  GtkWidget *handle_box = gtk_handle_box_new ();
  GtkWidget *toolbar = gtk_toolbar_new (GTK_ORIENTATION_HORIZONTAL,
                                        GTK_TOOLBAR_TEXT);
  GtkWidget *widget;

  gtk_toolbar_set_button_relief (GTK_TOOLBAR (toolbar), GTK_RELIEF_NONE);
  gtk_toolbar_append_item (GTK_TOOLBAR (toolbar), "New", "A new file", NULL,
                           NULL, NULL, NULL);
  gtk_toolbar_append_item (GTK_TOOLBAR (toolbar), "Open", "Open a file", NULL,
                           NULL, NULL, NULL);
  gtk_toolbar_append_space (GTK_TOOLBAR (toolbar));
  widget = gtk_toolbar_append_element (GTK_TOOLBAR (toolbar),
                                       GTK_TOOLBAR_CHILD_TOGGLEBUTTON, NULL,
                                       "Bold", "Bold text", NULL, NULL,
                                       NULL, NULL);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
  gtk_toolbar_append_element (GTK_TOOLBAR (toolbar),
                              GTK_TOOLBAR_CHILD_TOGGLEBUTTON, NULL,
                              "Italic", "Italic text", NULL, NULL,
                              NULL, NULL);
  gtk_container_add (GTK_CONTAINER (handle_box), toolbar);

  return handle_box;
}

/* Buttons, check and radio buttons, and fields, each also as disabled */
static GtkWidget *
preview_controls (void)
{
  GtkWidget *vbox = gtk_vbox_new (FALSE, 6);
  GtkWidget *hbox, *widget, *menu;
  GList *strings = NULL;
  GSList *group;

  hbox = gtk_hbox_new (FALSE, 6);
  gtk_box_pack_start (GTK_BOX (hbox), gtk_button_new_with_label ("Button"),
                      FALSE, FALSE, 0);
  widget = gtk_toggle_button_new_with_label ("Toggle");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
  gtk_box_pack_start (GTK_BOX (hbox), widget, FALSE, FALSE, 0);
  widget = gtk_button_new_with_label ("Disabled");
  gtk_widget_set_sensitive (widget, FALSE);
  gtk_box_pack_start (GTK_BOX (hbox), widget, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  hbox = gtk_hbox_new (FALSE, 6);
  widget = gtk_check_button_new_with_label ("Check");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
  gtk_box_pack_start (GTK_BOX (hbox), widget, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), gtk_check_button_new_with_label ("Clear"),
                      FALSE, FALSE, 0);
  widget = gtk_check_button_new_with_label ("Disabled");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
  gtk_widget_set_sensitive (widget, FALSE);
  gtk_box_pack_start (GTK_BOX (hbox), widget, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  hbox = gtk_hbox_new (FALSE, 6);
  widget = gtk_radio_button_new_with_label (NULL, "One");
  group = gtk_radio_button_group (GTK_RADIO_BUTTON (widget));
  gtk_box_pack_start (GTK_BOX (hbox), widget, FALSE, FALSE, 0);
  widget = gtk_radio_button_new_with_label (group, "Two");
  group = gtk_radio_button_group (GTK_RADIO_BUTTON (widget));
  gtk_box_pack_start (GTK_BOX (hbox), widget, FALSE, FALSE, 0);
  widget = gtk_radio_button_new_with_label (group, "Disabled");
  gtk_widget_set_sensitive (widget, FALSE);
  gtk_box_pack_start (GTK_BOX (hbox), widget, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  widget = gtk_entry_new ();
  gtk_entry_set_text (GTK_ENTRY (widget), "Some text to select");
  gtk_editable_select_region (GTK_EDITABLE (widget), 5, 9);
  gtk_box_pack_start (GTK_BOX (vbox), widget, FALSE, FALSE, 0);
  widget = gtk_entry_new ();
  gtk_entry_set_text (GTK_ENTRY (widget), "Disabled");
  gtk_widget_set_sensitive (widget, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), widget, FALSE, FALSE, 0);

  hbox = gtk_hbox_new (FALSE, 6);
  widget = gtk_spin_button_new (GTK_ADJUSTMENT (gtk_adjustment_new (5, 0, 10, 1, 2, 0)),
                                1, 0);
  gtk_widget_set_usize (widget, 50, -1);
  gtk_box_pack_start (GTK_BOX (hbox), widget, FALSE, FALSE, 0);
  menu = gtk_menu_new ();
  gtk_menu_append (GTK_MENU (menu), gtk_menu_item_new_with_label ("First"));
  gtk_menu_append (GTK_MENU (menu), gtk_menu_item_new_with_label ("Second"));
  gtk_menu_append (GTK_MENU (menu), gtk_menu_item_new_with_label ("Third"));
  widget = gtk_option_menu_new ();
  gtk_option_menu_set_menu (GTK_OPTION_MENU (widget), menu);
  gtk_box_pack_start (GTK_BOX (hbox), widget, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  strings = g_list_append (strings, "Apples");
  strings = g_list_append (strings, "Oranges");
  strings = g_list_append (strings, "Pears");
  widget = gtk_combo_new ();
  gtk_combo_set_popdown_strings (GTK_COMBO (widget), strings);
  g_list_free (strings);
  gtk_box_pack_start (GTK_BOX (vbox), widget, FALSE, FALSE, 0);

  hbox = gtk_hbox_new (FALSE, 6);
  preview_default = gtk_button_new_with_label ("Default");
  GTK_WIDGET_SET_FLAGS (preview_default, GTK_CAN_DEFAULT);
  gtk_box_pack_start (GTK_BOX (hbox), preview_default, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  return vbox;
}

/* Two panes with the handle between: a sunken frame holding a tree, and
 * a raised one holding arrows each way, in a cross */
static GtkWidget *
preview_panes (void)
{
  static const struct
  {
    GtkArrowType type;
    gint x, y;
  } arrows[] = {
    { GTK_ARROW_UP, 1, 0 }, { GTK_ARROW_LEFT, 0, 1 },
    { GTK_ARROW_RIGHT, 2, 1 }, { GTK_ARROW_DOWN, 1, 2 }
  };
  GtkWidget *paned = gtk_hpaned_new ();
  GtkWidget *frame, *ctree, *table, *align;
  GtkCTreeNode *node;
  gchar *text[1];
  guint i;

  frame = gtk_frame_new ("Sunken");
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
  ctree = gtk_ctree_new (1, 0);
  gtk_ctree_set_line_style (GTK_CTREE (ctree), GTK_CTREE_LINES_DOTTED);
  gtk_container_set_border_width (GTK_CONTAINER (ctree), 4);
  text[0] = "Music";
  node = gtk_ctree_insert_node (GTK_CTREE (ctree), NULL, NULL, text, 4,
                                NULL, NULL, NULL, NULL, FALSE, TRUE);
  text[0] = "Jazz";
  gtk_ctree_insert_node (GTK_CTREE (ctree), node, NULL, text, 4,
                         NULL, NULL, NULL, NULL, TRUE, FALSE);
  text[0] = "Rock";
  gtk_ctree_insert_node (GTK_CTREE (ctree), node, NULL, text, 4,
                         NULL, NULL, NULL, NULL, TRUE, FALSE);
  text[0] = "Notes";
  node = gtk_ctree_insert_node (GTK_CTREE (ctree), NULL, NULL, text, 4,
                                NULL, NULL, NULL, NULL, FALSE, FALSE);
  text[0] = "todo.txt";
  gtk_ctree_insert_node (GTK_CTREE (ctree), node, NULL, text, 4,
                         NULL, NULL, NULL, NULL, TRUE, FALSE);
  gtk_container_add (GTK_CONTAINER (frame), ctree);
  gtk_paned_pack1 (GTK_PANED (paned), frame, TRUE, TRUE);

  frame = gtk_frame_new ("Raised");
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_OUT);
  align = gtk_alignment_new (0.5, 0.5, 0.0, 0.0);
  table = gtk_table_new (3, 3, TRUE);
  for (i = 0; i < sizeof (arrows) / sizeof (arrows[0]); i++)
    {
      GtkWidget *arrow = gtk_arrow_new (arrows[i].type, GTK_SHADOW_OUT);

      gtk_widget_set_usize (arrow, 22, 22);
      gtk_table_attach (GTK_TABLE (table), arrow, arrows[i].x, arrows[i].x + 1,
                        arrows[i].y, arrows[i].y + 1, 0, 0, 0, 0);
    }
  gtk_container_add (GTK_CONTAINER (align), table);
  gtk_container_add (GTK_CONTAINER (frame), align);
  gtk_paned_pack2 (GTK_PANED (paned), frame, TRUE, TRUE);

  return paned;
}

/* A notebook of a list, some text and scales, and a progress bar */
static GtkWidget *
preview_pages (void)
{
  GtkWidget *vbox = gtk_vbox_new (FALSE, 6);
  GtkWidget *notebook = gtk_notebook_new ();
  GtkWidget *hbox, *box, *widget, *clist, *text, *scrolled;
  gchar *rows[][2] = { { "Alpha", "First" }, { "Beta", "Second" }, { "Gamma", "Third" },
                       { "Delta", "Fourth" }, { "Epsilon", "Fifth" } };
  gchar *titles[] = { "Name", "Value" };
  guint i;

  clist = gtk_clist_new_with_titles (2, titles);
  for (i = 0; i < sizeof (rows) / sizeof (rows[0]); i++)
    gtk_clist_append (GTK_CLIST (clist), rows[i]);
  gtk_clist_select_row (GTK_CLIST (clist), 1, 0);
  gtk_clist_set_column_width (GTK_CLIST (clist), 0, 80);
  scrolled = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
  gtk_container_set_border_width (GTK_CONTAINER (scrolled), 4);
  gtk_container_add (GTK_CONTAINER (scrolled), clist);
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), scrolled,
                            gtk_label_new ("List"));

  text = gtk_text_new (NULL, NULL);
  gtk_text_set_editable (GTK_TEXT (text), TRUE);
  gtk_text_set_word_wrap (GTK_TEXT (text), TRUE);
  gtk_text_insert (GTK_TEXT (text), NULL, NULL, NULL,
                   "Text over a few\n"
                   "lines, in a field\n"
                   "that scrolls, as an\n"
                   "editor shows it.\n\n"
                   "The quick brown fox\n"
                   "jumps over the\n"
                   "lazy dog.\n", -1);
  scrolled = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
  gtk_container_set_border_width (GTK_CONTAINER (scrolled), 4);
  gtk_container_add (GTK_CONTAINER (scrolled), text);
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), scrolled,
                            gtk_label_new ("Text"));

  hbox = gtk_hbox_new (FALSE, 8);
  gtk_container_set_border_width (GTK_CONTAINER (hbox), 6);
  box = gtk_vbox_new (FALSE, 6);
  widget = gtk_hscale_new (GTK_ADJUSTMENT (gtk_adjustment_new (40, 0, 100, 1, 10, 0)));
  gtk_scale_set_digits (GTK_SCALE (widget), 0);
  gtk_box_pack_start (GTK_BOX (box), widget, FALSE, FALSE, 0);
  widget = gtk_hscale_new (GTK_ADJUSTMENT (gtk_adjustment_new (70, 0, 100, 1, 10, 0)));
  gtk_scale_set_draw_value (GTK_SCALE (widget), FALSE);
  gtk_widget_set_sensitive (widget, FALSE);
  gtk_box_pack_start (GTK_BOX (box), widget, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), box, TRUE, TRUE, 0);
  widget = gtk_vscale_new (GTK_ADJUSTMENT (gtk_adjustment_new (30, 0, 100, 1, 10, 0)));
  gtk_scale_set_draw_value (GTK_SCALE (widget), FALSE);
  gtk_box_pack_start (GTK_BOX (hbox), widget, FALSE, FALSE, 0);
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), hbox,
                            gtk_label_new ("Scales"));

  gtk_widget_set_usize (notebook, -1, 150);
  gtk_box_pack_start (GTK_BOX (vbox), notebook, TRUE, TRUE, 0);

  widget = gtk_progress_bar_new ();
  gtk_progress_bar_update (GTK_PROGRESS_BAR (widget), 0.6);
  gtk_box_pack_start (GTK_BOX (vbox), widget, FALSE, FALSE, 0);

  widget = gtk_hscale_new (GTK_ADJUSTMENT (gtk_adjustment_new (40, 0, 100, 1, 10, 0)));
  gtk_scale_set_draw_value (GTK_SCALE (widget), FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), widget, FALSE, FALSE, 0);

  widget = gtk_hscrollbar_new (GTK_ADJUSTMENT (gtk_adjustment_new (30, 0, 100, 1, 10, 25)));
  gtk_box_pack_start (GTK_BOX (vbox), widget, FALSE, FALSE, 0);

  return vbox;
}

static GtkWidget *
preview_area (void)
{
  GtkWidget *frame = gtk_frame_new ("Preview");
  GtkWidget *vbox = gtk_vbox_new (FALSE, 6);
  GtkWidget *hbox, *column, *statusbar;

  gtk_container_set_border_width (GTK_CONTAINER (vbox), 8);
  gtk_container_add (GTK_CONTAINER (frame), vbox);

  gtk_box_pack_start (GTK_BOX (vbox), preview_menu_bar (), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), preview_toolbar (), FALSE, FALSE, 0);

  hbox = gtk_hbox_new (FALSE, 10);
  column = preview_controls ();
  gtk_box_pack_start (GTK_BOX (column), preview_panes (), TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), column, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), preview_pages (), TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);

  statusbar = gtk_statusbar_new ();
  gtk_statusbar_push (GTK_STATUSBAR (statusbar),
                      gtk_statusbar_get_context_id (GTK_STATUSBAR (statusbar), "preview"),
                      "Ready");
  gtk_box_pack_start (GTK_BOX (vbox), statusbar, FALSE, FALSE, 0);

  return frame;
}

/* ---------------------------------------------------------------------
 * Showing a change, and applying it
 */

static gchar *
preview_text (void)
{
  return gtkrc_render (preview_theme_rc, font);
}

/* The theme as edited, for this window. It is read from elsewhere than
 * the theme's own directory, so pictures and the like it names are
 * looked for there. */
static void
preview_write_theme (void)
{
  gchar *text = g_strdup_printf ("pixmap_path \"%s/gtk-1.4:%s/gtk:%s\"\n%s",
                                 current->dir, current->dir, current->dir, theme_text (theme));

  theme_write_file (preview_theme_rc, text);
  g_free (text);
}

/* This window: its preview files, read again. */
static void
preview_update (void)
{
  gchar *text = preview_text ();
  GList *l;

  if (theme)
    preview_write_theme ();
  theme_write_file (preview_rc, text);
  g_free (text);

  if (gtk_rc_reparse_all ())
    for (l = gtk_container_get_toplevels (); l; l = l->next)
      gtk_widget_reset_rc_styles (l->data);
  color_buttons_redraw ();
}

/* A path for the user to read, with their home directory as ~ */
static gchar *
path_for_reading (const gchar *path)
{
  const gchar *home = g_get_home_dir ();
  gsize len = home ? strlen (home) : 0;

  if (len > 1 && strncmp (path, home, len) == 0 && path[len] == '/')
    return g_strconcat ("~", path + len, NULL);
  return g_strdup (path);
}

static void
status_update (void)
{
  gchar *text;

  if (!current)
    text = g_strdup ("No theme.");
  else if (current->user)
    {
      gchar *shown = path_for_reading (current->dir);

      text = g_strdup_printf ("%s, in %s.", current->name, shown);
      g_free (shown);
    }
  else if (edited)
    {
      gchar *user_dir = theme_user_dir ();
      gchar *shown = path_for_reading (user_dir);

      text = g_strdup_printf ("%s is installed: applying makes a copy of it in %s.",
                              current->name, shown);
      g_free (shown);
      g_free (user_dir);
    }
  else
    text = g_strdup_printf ("%s, installed in %s.", current->name, current->dir);
  gtk_label_set_text (GTK_LABEL (status_label), text);
  g_free (text);

  gtk_widget_set_sensitive (delete_button, current && current->user);

  /* the version in use, which can be changed if it has another */
  updating = TRUE;
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dark_check),
                                current && theme_name_is_dark (current->name));
  gtk_widget_set_sensitive (dark_check, current && (current->light || current->dark));
  updating = FALSE;
}

static gint
apply_later (gpointer data)
{
  apply_timeout = 0;
  apply ();
  return FALSE;
}

/* After any change, to the theme or the font: the preview now, the
 * rest when it is applied. */
static void
theme_changed (gboolean theme_edited)
{
  if (updating)
    return;
  if (theme_edited && theme)
    edited = TRUE;
  preview_update ();
  status_update ();
  if (GTK_TOGGLE_BUTTON (live_check)->active)
    {
      if (apply_timeout)
        gtk_timeout_remove (apply_timeout);
      apply_timeout = gtk_timeout_add (APPLY_DELAY, apply_later, NULL);
    }
}

static void
broadcast_reload (void)
{
  GdkEventClient event;

  memset (&event, 0, sizeof (event));
  event.type = GDK_CLIENT_EVENT;
  event.send_event = TRUE;
  event.data_format = 32;
  event.message_type = gdk_atom_intern ("_GTK_READ_RCFILES", FALSE);
  gdk_event_send_clientmessage_toall ((GdkEvent *) &event);
  gdk_flush ();
}

static void list_fill (void);

/* A name for a copy of an installed theme: "Altus Custom", or with a
 * number if there is one already. A dark version's copy is "Altus
 * Custom-Dark", so that it pairs with a copy of the light one. */
static gchar *
copy_name (const gchar *base)
{
  gboolean dark = theme_name_is_dark (base);
  const gchar *suffix = dark ? THEME_DARK_SUFFIX : "";
  gchar *stem = dark ? g_strndup (base, strlen (base) - strlen (THEME_DARK_SUFFIX))
                     : g_strdup (base);
  gchar *name = g_strdup_printf ("%s Custom%s", stem, suffix);
  gint n = 2;

  while (theme_list_find (themes, name))
    {
      g_free (name);
      name = g_strdup_printf ("%s Custom %d%s", stem, n++, suffix);
    }
  g_free (stem);
  return name;
}

/* The theme written to a theme of the user's called name. */
static gboolean
save_as (const gchar *name)
{
  gchar *user_dir = theme_user_dir ();
  gchar *rc = g_strconcat (user_dir, "/", name, "/gtk-1.4/gtkrc", NULL);
  gboolean ok;

  if (current && (!current->user || strcmp (current->name, name) != 0))
    {
      /* Pictures and the like a theme names are looked for where it was. */
      gchar *header = g_strdup_printf ("# %s: made with cgtk-theme from %s.\n"
                                       "pixmap_path \"%s/gtk-1.4:%s/gtk:%s\"\n\n",
                                       name, current->name, current->dir, current->dir,
                                       current->dir);

      theme_prepend (theme, header);
      g_free (header);
    }
  ok = theme_write_file (rc, theme_text (theme));
  if (ok)
    {
      theme_list_free (themes);
      themes = theme_list ();
      current = theme_list_find_rc (themes, rc);
    }
  g_free (rc);
  g_free (user_dir);
  return ok;
}

/* A row of buttons at their usual size, at the end of the row, the
 * first one the default: Return presses it. */
static GtkWidget *
button_box (GtkWidget *first, ...)
{
  GtkWidget *box = gtk_hbutton_box_new ();
  GtkWidget *button;
  va_list args;

  gtk_button_box_set_layout (GTK_BUTTON_BOX (box), GTK_BUTTONBOX_END);
  gtk_button_box_set_spacing (GTK_BUTTON_BOX (box), 6);
  va_start (args, first);
  for (button = first; button; button = va_arg (args, GtkWidget *))
    {
      GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
      gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);
    }
  va_end (args);
  return box;
}

static void
show_error (const gchar *message)
{
  GtkWidget *dialog = gtk_dialog_new ();
  GtkWidget *label = gtk_label_new (message);
  GtkWidget *button = gtk_button_new_with_label ("OK");

  gtk_window_set_title (GTK_WINDOW (dialog), "cgtk-theme");
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (window));
  gtk_misc_set_padding (GTK_MISC (label), 12, 12);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), label, TRUE, TRUE, 0);
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
                             GTK_SIGNAL_FUNC (gtk_widget_destroy), GTK_OBJECT (dialog));
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->action_area),
                      button_box (button, NULL), TRUE, TRUE, 0);
  gtk_widget_show_all (dialog);
  gtk_widget_grab_default (button);
}

/* The theme, if changed, saved; the rc file written; every program told. */
static void
apply (void)
{
  gchar *path, *text;

  if (apply_timeout)
    {
      gtk_timeout_remove (apply_timeout);
      apply_timeout = 0;
    }
  if (!current)
    return;

  if (edited)
    {
      gchar *name = current->user ? g_strdup (current->name) : copy_name (current->name);

      if (!save_as (name))
        {
          gchar *message = g_strdup_printf ("The theme could not be saved as \"%s\".", name);

          show_error (message);
          g_free (message);
          g_free (name);
          return;
        }
      g_free (name);
      list_fill ();
    }

  path = gtkrc_path ();
  text = gtkrc_render (current->rc_path, font);
  if (!theme_write_file (path, text))
    {
      gchar *message = g_strdup_printf ("%s could not be written.", path);

      show_error (message);
      g_free (message);
    }
  g_free (text);
  g_free (path);

  broadcast_reload ();
  edited = FALSE;
  status_update ();
}

/* ---------------------------------------------------------------------
 * Choosing a theme
 */

static void
theme_select (ThemeInfo *info)
{
  if (apply_timeout)
    {
      gtk_timeout_remove (apply_timeout);
      apply_timeout = 0;
    }
  theme_free (theme);
  theme = info ? theme_load (info->rc_path) : NULL;
  current = theme ? info : NULL;
  edited = FALSE;
  options_rebuild ();
  preview_update ();
  status_update ();
}

/* Keep the current theme selected in the list: a row is only ever
 * unselected by choosing another, or by a Ctrl-click, which is undone. */
static void
list_show_current (void)
{
  GtkCList *clist = GTK_CLIST (theme_clist);
  ThemeInfo *listed = current && current->light ? current->light : current;
  gint row;

  for (row = clist->rows - 1; row >= 0; row--)
    if (listed && !strcmp (gtk_clist_get_row_data (clist, row), listed->name))
      break;

  updating = TRUE;
  if (row >= 0)
    {
      gtk_clist_select_row (clist, row, 0);
      if (gtk_clist_row_is_visible (clist, row) != GTK_VISIBILITY_FULL)
        gtk_clist_moveto (clist, row, 0, 0.5, 0.0);
    }
  else
    gtk_clist_unselect_all (clist);
  updating = FALSE;
}

static gint
list_show_current_idle (gpointer data)
{
  list_show_current ();
  return FALSE;
}

static void
list_selected (GtkCList *clist, gint row, gint column, GdkEvent *event)
{
  ThemeInfo *info;

  if (updating)
    return;
  info = theme_list_find (themes, gtk_clist_get_row_data (clist, row));
  if (info && want_dark && info->dark)
    info = info->dark;
  if (!info || info == current)
    return;
  theme_select (info);
  if (GTK_TOGGLE_BUTTON (live_check)->active)
    apply ();
}

static void
list_unselected (GtkCList *clist, gint row, gint column, GdkEvent *event)
{
  if (!updating && !clist->selection)
    gtk_idle_add (list_show_current_idle, NULL);
}

/* The theme list: one row for each theme, a light one standing for its
 * dark version too, the current one selected. An rc file that names no
 * theme found has none selected. */
static void
list_fill (void)
{
  GtkCList *clist = GTK_CLIST (theme_clist);
  GList *l;

  updating = TRUE;
  gtk_clist_freeze (clist);
  gtk_clist_clear (clist);
  for (l = themes; l; l = l->next)
    {
      ThemeInfo *info = l->data;
      gchar *text[1];
      gint row;

      if (info->light)
        continue;
      text[0] = info->name;
      row = gtk_clist_append (clist, text);
      gtk_clist_set_row_data_full (clist, row, g_strdup (info->name), g_free);
    }
  gtk_clist_thaw (clist);
  updating = FALSE;
  list_show_current ();
}

/* A name asked for, then the theme saved and chosen under it. */
static void
save_as_ok (GtkWidget *button, GtkWidget *dialog)
{
  GtkWidget *entry = gtk_object_get_data (GTK_OBJECT (dialog), "entry");
  gchar *name = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));

  g_strstrip (name);
  if (!*name || strchr (name, '/') || name[0] == '.')
    {
      show_error ("A theme's name cannot be empty, start with a dot, or have a / in it.");
      g_free (name);
      return;
    }
  gtk_widget_destroy (dialog);
  if (!save_as (name))
    {
      show_error ("The theme could not be saved.");
      g_free (name);
      return;
    }
  edited = FALSE;
  list_fill ();
  apply ();
  g_free (name);
}

static void
save_as_clicked (void)
{
  GtkWidget *dialog = gtk_dialog_new ();
  GtkWidget *label = gtk_label_new ("Save the theme as:");
  GtkWidget *entry = gtk_entry_new ();
  GtkWidget *ok = gtk_button_new_with_label ("Save");
  GtkWidget *cancel = gtk_button_new_with_label ("Cancel");
  gchar *name;

  if (!current)
    return;
  name = current->user ? g_strdup (current->name) : copy_name (current->name);
  gtk_window_set_title (GTK_WINDOW (dialog), "Save As");
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (window));
  gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), 8);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), label, FALSE, FALSE, 4);
  gtk_entry_set_text (GTK_ENTRY (entry), name);
  g_free (name);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), entry, FALSE, FALSE, 4);
  gtk_object_set_data (GTK_OBJECT (dialog), "entry", entry);
  gtk_signal_connect (GTK_OBJECT (ok), "clicked", GTK_SIGNAL_FUNC (save_as_ok), dialog);
  gtk_signal_connect (GTK_OBJECT (entry), "activate", GTK_SIGNAL_FUNC (save_as_ok), dialog);
  gtk_signal_connect_object (GTK_OBJECT (cancel), "clicked",
                             GTK_SIGNAL_FUNC (gtk_widget_destroy), GTK_OBJECT (dialog));
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->action_area),
                      button_box (ok, cancel, NULL), TRUE, TRUE, 0);
  gtk_widget_show_all (dialog);
  gtk_widget_grab_default (ok);
  gtk_widget_grab_focus (entry);
}

static gboolean
remove_tree (const gchar *path)
{
  gchar *argv[] = { "rm", "-rf", "--", (gchar *) path, NULL };
  pid_t pid = fork ();
  int status;

  if (pid < 0)
    return FALSE;
  if (pid == 0)
    {
      execvp (argv[0], argv);
      _exit (127);
    }
  return waitpid (pid, &status, 0) == pid && WIFEXITED (status) && WEXITSTATUS (status) == 0;
}

static void
delete_ok (GtkWidget *button, GtkWidget *dialog)
{
  gchar *gtkrc_theme, *gtkrc_font;
  gboolean in_use;

  gtk_widget_destroy (dialog);
  if (!current || !current->user)
    return;

  gtkrc_read (&gtkrc_theme, &gtkrc_font);
  in_use = gtkrc_theme && strcmp (gtkrc_theme, current->rc_path) == 0;
  g_free (gtkrc_theme);
  g_free (gtkrc_font);

  if (!remove_tree (current->dir))
    {
      show_error ("The theme could not be deleted.");
      return;
    }
  theme_list_free (themes);
  themes = theme_list ();
  {
    ThemeInfo *info = theme_list_find (themes, "Altus");

    if (!info)
      info = themes ? themes->data : NULL;
    if (info && want_dark && info->dark)
      info = info->dark;
    theme_select (info);
  }
  list_fill ();
  if (in_use)
    apply ();
}

static void
delete_clicked (void)
{
  GtkWidget *dialog = gtk_dialog_new ();
  GtkWidget *label, *ok, *cancel;
  gchar *text;

  if (!current || !current->user)
    return;
  text = g_strdup_printf ("Delete the theme \"%s\" and everything in\n%s?",
                          current->name, current->dir);
  label = gtk_label_new (text);
  g_free (text);
  gtk_window_set_title (GTK_WINDOW (dialog), "Delete Theme");
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (window));
  gtk_misc_set_padding (GTK_MISC (label), 12, 12);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), label, TRUE, TRUE, 0);
  ok = gtk_button_new_with_label ("Delete");
  cancel = gtk_button_new_with_label ("Cancel");
  gtk_signal_connect (GTK_OBJECT (ok), "clicked", GTK_SIGNAL_FUNC (delete_ok), dialog);
  gtk_signal_connect_object (GTK_OBJECT (cancel), "clicked",
                             GTK_SIGNAL_FUNC (gtk_widget_destroy), GTK_OBJECT (dialog));
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->action_area),
                      button_box (ok, cancel, NULL), TRUE, TRUE, 0);
  gtk_widget_show_all (dialog);
  /* Return cancels: deleting should take a click */
  gtk_widget_grab_default (cancel);
}

/* ---------------------------------------------------------------------
 * The window
 */

static void
quit (void)
{
  if (apply_timeout)
    apply ();
  unlink (preview_rc);
  unlink (preview_theme_rc);
  gtk_main_quit ();
}

/* Killed: the preview files go all the same. */
static void
quit_signal (int sig)
{
  unlink (preview_rc);
  unlink (preview_theme_rc);
  signal (sig, SIG_DFL);
  raise (sig);
}

/* The other version of the theme, light or dark */
static void
dark_toggled (GtkToggleButton *button)
{
  ThemeInfo *other;

  if (updating || !current)
    return;
  want_dark = button->active;
  other = want_dark ? current->dark : current->light;
  if (!other)
    return;
  theme_select (other);
  list_show_current ();
  if (GTK_TOGGLE_BUTTON (live_check)->active)
    apply ();
}

/* The list is tall enough for this many themes, the toolkit's own, at
 * whatever size the theme makes its rows */
#define THEME_LIST_ROWS 8

static void
list_style_set (GtkWidget *clist, GtkStyle *previous)
{
  GtkRequisition title;
  gint height;

  gtk_widget_size_request (GTK_CLIST (clist)->column[0].button, &title);
  height = title.height + THEME_LIST_ROWS * (GTK_CLIST (clist)->row_height + 1) + 1 +
           2 * clist->style->klass->ythickness;
  gtk_widget_set_usize (clist, -1, height);
}

/* The list of themes, whether to use their dark versions, and the button
 * to delete one of your own */
static GtkWidget *
themes_pane (void)
{
  GtkWidget *vbox, *scrolled;

  vbox = gtk_vbox_new (FALSE, 4);
  scrolled = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  theme_clist = gtk_clist_new_with_titles (1, (gchar *[]) { "Theme" });
  gtk_clist_column_titles_passive (GTK_CLIST (theme_clist));
  gtk_clist_set_selection_mode (GTK_CLIST (theme_clist), GTK_SELECTION_SINGLE);
  gtk_clist_set_column_auto_resize (GTK_CLIST (theme_clist), 0, TRUE);
  gtk_signal_connect (GTK_OBJECT (theme_clist), "select_row",
                      GTK_SIGNAL_FUNC (list_selected), NULL);
  gtk_signal_connect (GTK_OBJECT (theme_clist), "unselect_row",
                      GTK_SIGNAL_FUNC (list_unselected), NULL);
  /* as its style is now, and as it changes */
  gtk_signal_connect_after (GTK_OBJECT (theme_clist), "style_set",
                            GTK_SIGNAL_FUNC (list_style_set), NULL);
  list_style_set (theme_clist, NULL);
  gtk_container_add (GTK_CONTAINER (scrolled), theme_clist);
  gtk_box_pack_start (GTK_BOX (vbox), scrolled, TRUE, TRUE, 0);

  dark_check = gtk_check_button_new_with_label ("Dark");
  gtk_signal_connect (GTK_OBJECT (dark_check), "toggled",
                      GTK_SIGNAL_FUNC (dark_toggled), NULL);
  gtk_box_pack_start (GTK_BOX (vbox), dark_check, FALSE, FALSE, 0);

  delete_button = gtk_button_new_with_label ("Delete");
  gtk_signal_connect (GTK_OBJECT (delete_button), "clicked",
                      GTK_SIGNAL_FUNC (delete_clicked), NULL);
  gtk_box_pack_start (GTK_BOX (vbox), delete_button, FALSE, FALSE, 0);
  return vbox;
}

static void
window_build (void)
{
  GtkWidget *vbox, *hbox, *left, *notebook, *button, *close_button;

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (window), "Themes");
  gtk_window_set_wmclass (GTK_WINDOW (window), "cgtk-theme", "Cgtk-theme");
  gtk_signal_connect (GTK_OBJECT (window), "delete_event", GTK_SIGNAL_FUNC (quit), NULL);

  vbox = gtk_vbox_new (FALSE, 8);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 8);
  gtk_container_add (GTK_CONTAINER (window), vbox);

  /* the themes above what is edited in them, the preview beside both */
  hbox = gtk_hbox_new (FALSE, 8);
  left = gtk_vbox_new (FALSE, 8);
  gtk_box_pack_start (GTK_BOX (left), themes_pane (), TRUE, TRUE, 0);
  notebook = gtk_notebook_new ();
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), colors_page (), gtk_label_new ("Colours"));
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), font_page (), gtk_label_new ("Font"));
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), options_page (), gtk_label_new ("Options"));
  gtk_box_pack_start (GTK_BOX (left), notebook, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), left, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), preview_area (), TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);

  status_label = gtk_label_new ("");
  gtk_misc_set_alignment (GTK_MISC (status_label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (vbox), status_label, FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (vbox), gtk_hseparator_new (), FALSE, FALSE, 0);

  hbox = gtk_hbox_new (FALSE, 8);
  live_check = gtk_check_button_new_with_label ("Apply as you edit");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (live_check), TRUE);
  gtk_box_pack_start (GTK_BOX (hbox), live_check, FALSE, FALSE, 0);
  button = gtk_button_new_with_label ("Save As...");
  gtk_signal_connect (GTK_OBJECT (button), "clicked", GTK_SIGNAL_FUNC (save_as_clicked), NULL);
  apply_button = gtk_button_new_with_label ("Apply");
  gtk_signal_connect (GTK_OBJECT (apply_button), "clicked", GTK_SIGNAL_FUNC (apply), NULL);
  close_button = gtk_button_new_with_label ("Close");
  gtk_signal_connect (GTK_OBJECT (close_button), "clicked", GTK_SIGNAL_FUNC (quit), NULL);
  gtk_box_pack_end (GTK_BOX (hbox), button_box (button, apply_button, close_button, NULL),
                    FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  gtk_widget_show_all (window);
  /* the preview's default button: the window has no other */
  gtk_widget_grab_default (preview_default);
}

/* ---------------------------------------------------------------------
 * The command line
 */

static void
usage (FILE *out)
{
  fprintf (out,
           "Usage: cgtk-theme [OPTION]... [THEME]\n"
           "Choose the theme and font CGTK programs use, and edit themes.\n"
           "\n"
           "With no THEME and no option, opens a window to do so.\n"
           "\n"
           "  THEME            use THEME: a theme's name, or its directory\n"
           "      --dark       use the dark version of THEME, or of the theme\n"
           "                   in use\n"
           "      --light      use the light version of THEME, or of the theme\n"
           "                   in use\n"
           "  -f, --font FONT  use FONT in every theme, as \"Sans 10\"; \"\" for\n"
           "                   each theme's own\n"
           "  -l, --list       list the themes there are\n"
           "  -h, --help       show this and stop\n"
           "\n"
           "Themes are looked for in $XDG_DATA_HOME/themes (~/.local/share/themes)\n"
           "and in the toolkit's theme directory.\n");
}

/* A theme named on the command line: a directory, or a theme's name. */
static ThemeInfo *
theme_named (const gchar *arg)
{
  ThemeInfo *info = theme_list_find (themes, arg);
  gchar *rc;

  if (info)
    return info;
  rc = g_strconcat (arg, "/gtk-1.4/gtkrc", NULL);
  if (access (rc, R_OK) != 0)
    {
      g_free (rc);
      rc = g_strconcat (arg, "/gtk/gtkrc", NULL);
    }
  if (access (rc, R_OK) == 0)
    {
      info = g_new0 (ThemeInfo, 1);
      info->name = g_strdup (arg);
      info->dir = g_strdup (arg);
      info->rc_path = rc;
      themes = g_list_append (themes, info);
      return info;
    }
  g_free (rc);
  return NULL;
}

/* cgtk-theme THEME, --dark, --light and -f: the rc file written, and
 * programs told if there is a display to tell them through. */
static gint
command_line (gint argc, gchar **argv)
{
  gchar *gtkrc_theme = NULL, *gtkrc_font = NULL, *path, *text;
  const gchar *theme_arg = NULL;
  gboolean set_font = FALSE;
  gint variant = 0;             /* 1 for --dark, -1 for --light */
  gint i;

  gtkrc_read (&gtkrc_theme, &gtkrc_font);
  for (i = 1; i < argc; i++)
    {
      if (strcmp (argv[i], "-h") == 0 || strcmp (argv[i], "--help") == 0)
        {
          usage (stdout);
          return 0;
        }
      else if (strcmp (argv[i], "-l") == 0 || strcmp (argv[i], "--list") == 0)
        {
          GList *l;

          for (l = themes; l; l = l->next)
            printf ("%s%s\n", ((ThemeInfo *) l->data)->name,
                    gtkrc_theme && strcmp (gtkrc_theme, ((ThemeInfo *) l->data)->rc_path) == 0
                    ? " (in use)" : "");
          return 0;
        }
      else if (strcmp (argv[i], "--dark") == 0)
        variant = 1;
      else if (strcmp (argv[i], "--light") == 0)
        variant = -1;
      else if ((strcmp (argv[i], "-f") == 0 || strcmp (argv[i], "--font") == 0) && i + 1 < argc)
        {
          g_free (gtkrc_font);
          gtkrc_font = *argv[++i] ? g_strdup (argv[i]) : NULL;
          set_font = TRUE;
        }
      else if (argv[i][0] == '-' || theme_arg)
        {
          usage (stderr);
          return 1;
        }
      else
        theme_arg = argv[i];
    }

  if (theme_arg)
    {
      ThemeInfo *info = theme_named (theme_arg);

      if (!info)
        {
          fprintf (stderr, "cgtk-theme: there is no theme \"%s\"; cgtk-theme -l lists them.\n",
                   theme_arg);
          return 1;
        }
      g_free (gtkrc_theme);
      gtkrc_theme = g_strdup (info->rc_path);
    }

  if (variant)
    {
      /* the theme named, or in use, or else the default */
      ThemeInfo *info = gtkrc_theme ? theme_list_find_rc (themes, gtkrc_theme) : NULL;
      ThemeInfo *other;

      if (!info && !gtkrc_theme)
        info = theme_list_find (themes, "Altus");
      if (!info)
        {
          fprintf (stderr, "cgtk-theme: the theme in use is not one cgtk-theme -l lists.\n");
          return 1;
        }
      if (variant > 0)
        other = theme_name_is_dark (info->name) ? info : info->dark;
      else
        other = theme_name_is_dark (info->name) ? info->light : info;
      if (!other)
        {
          fprintf (stderr, "cgtk-theme: %s has no %s version.\n", info->name,
                   variant > 0 ? "dark" : "light");
          return 1;
        }
      g_free (gtkrc_theme);
      gtkrc_theme = g_strdup (other->rc_path);
    }

  path = gtkrc_path ();
  text = gtkrc_render (gtkrc_theme, gtkrc_font);
  if (!theme_write_file (path, text))
    {
      fprintf (stderr, "cgtk-theme: %s could not be written.\n", path);
      return 1;
    }
  g_free (text);
  g_free (path);

  if ((theme_arg || variant || set_font) && (getenv ("WAYLAND_DISPLAY") || getenv ("DISPLAY")) &&
      gdk_init_check (&argc, &argv))
    broadcast_reload ();
  return 0;
}

int
main (int argc, char **argv)
{
  gchar *gtkrc_theme, *runtime_dir, *default_files[2];

  gtk_set_locale ();
  themes = theme_list ();
  if (argc > 1 && strcmp (argv[1], "-d") != 0 && strcmp (argv[1], "--dock") != 0)
    return command_line (argc, argv);

  /* The window reads only its preview, and the default theme beneath. */
  runtime_dir = getenv ("XDG_RUNTIME_DIR");
  if (!runtime_dir || !*runtime_dir)
    runtime_dir = "/tmp";
  preview_rc = g_strdup_printf ("%s/cgtk-theme-%lu.rc", runtime_dir, (gulong) getpid ());
  preview_theme_rc = g_strdup_printf ("%s/cgtk-theme-%lu-theme.rc", runtime_dir,
                                      (gulong) getpid ());
  gtkrc_read (&gtkrc_theme, &font);
  current = gtkrc_theme ? theme_list_find_rc (themes, gtkrc_theme) : NULL;
  if (!current)
    current = theme_list_find (themes, "Altus");
  if (!current && themes)
    current = themes->data;
  g_free (gtkrc_theme);
  want_dark = current && theme_name_is_dark (current->name);
  theme = current ? theme_load (current->rc_path) : NULL;
  if (!theme)
    current = NULL;
  else
    preview_write_theme ();
  {
    gchar *text = preview_text ();

    theme_write_file (preview_rc, text);
    g_free (text);
  }
  default_files[0] = preview_rc;
  default_files[1] = NULL;
  gtk_rc_set_default_files (default_files);
  signal (SIGTERM, quit_signal);
  signal (SIGINT, quit_signal);
  signal (SIGHUP, quit_signal);

  gtk_init (&argc, &argv);
  window_build ();

  updating = TRUE;
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (font_check), font != NULL);
  gtk_entry_set_text (GTK_ENTRY (font_entry), font ? font : "");
  gtk_widget_set_sensitive (font_entry, font != NULL);
  updating = FALSE;
  list_fill ();
  options_rebuild ();
  status_update ();

  gtk_main ();
  return 0;
}
