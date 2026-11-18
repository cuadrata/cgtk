/* GTK - The GIMP Toolkit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 * Copyright (C) 2026 The CGTK authors
 *
 * GtkFontSelection widget for Gtk+, by Damon Chaplin, May 1998.
 * Based on the GnomeFontSelector widget, by Elliot Lee, but major changes.
 * The GnomeFontSelector was derived from app/text_tool.c in the GIMP.
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

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "gdk/gdkftfont.h"
#include "gtkbutton.h"
#include "gtkclist.h"
#include "gtkentry.h"
#include "gtkfontsel.h"
#include "gtkframe.h"
#include "gtkhbbox.h"
#include "gtklabel.h"
#include "gtkscrolledwindow.h"
#include "gtksignal.h"
#include "gtktable.h"
#include "gtkvbox.h"
#include "gtkintl.h"

static void    gtk_font_selection_class_init         (GtkFontSelectionClass *klass);
static void    gtk_font_selection_init               (GtkFontSelection *fontsel);
static void    gtk_font_selection_destroy            (GtkObject      *object);

static void    gtk_font_selection_dialog_class_init  (GtkFontSelectionDialogClass *klass);
static void    gtk_font_selection_dialog_init        (GtkFontSelectionDialog *fontseldiag);

static gint    gtk_font_selection_dialog_on_configure(GtkWidget      *widget,
                                                      GdkEventConfigure *event,
                                                      GtkFontSelectionDialog *fsd);

static GtkNotebookClass *font_selection_parent_class = NULL;
static GtkWindowClass *font_selection_dialog_parent_class = NULL;

/*****************************************************************************
 * Choosing among installed fonts
 *
 * What GtkFontSelection shows when GDK draws text with installed fonts:
 * a family, a style and a size, each an entry over a list, and a preview
 * under them, as later toolkits lay it out. The families are what is
 * installed, with Sans, Serif and Monospace first and the families that
 * are only bitmaps marked; the styles are what the chosen family has.
 *
 * Sizes are in the unit each kind of font comes in: fonts with outlines
 * in points, bitmap fonts in the pixel sizes they were drawn at. What
 * is kept is the size last chosen, in pixels, which either can be given
 * in, and each family shows it in its own unit -- to a tenth of a point,
 * or the nearest size a bitmap was drawn at -- so choosing another
 * family or style never moves it.
 *
 * The name handed back is a modern one -- "DejaVu Sans Bold 10",
 * "Terminus 16px" -- which gdk_font_load understands; names of either
 * kind are understood when one is set.
 *
 * Everything the user can click and everything a program can set goes
 * through the same few functions, with the callbacks off whenever code
 * rather than the user selects a row or fills a list.
 *****************************************************************************/

#define SCALABLE_KEY "gtk-font-selection-scalable"

#define DEFAULT_PREVIEW_TEXT "abcdefghijk ABCDEFGHIJK"
#define MIN_PREVIEW_HEIGHT 44
#define MAX_PREVIEW_HEIGHT 160

/* The sizes offered for a font with outlines, in points. */
static const gdouble point_sizes[] = {
  6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 18, 20, 22, 24, 26, 28, 32, 36, 40,
  48, 56, 64, 72
};

/* The generic families, offered first, and what their names parse as. */
static const struct
{
  const gchar *shown;
  const gchar *parsed;
  gboolean monospace;
}
generic_families[] =
{
  { "Sans",      "sans-serif", FALSE },
  { "Serif",     "serif",      FALSE },
  { "Monospace", "monospace",  TRUE }
};

typedef struct
{
  gchar **families;
  gboolean *bitmap;             /* installed only as bitmaps */
  gint n_families;
  gint family;                  /* row, or -1 */

  GdkFTName *styles;
  gint n_styles;
  gint style;                   /* row, or -1 */

  /* The style and the size, in pixels, last chosen. */
  GdkFTName want;

  gdouble *sizes;               /* the size list, in the family's unit */
  gint n_sizes;
  gdouble size;                 /* the size shown, in the family's unit */
  GtkWidget *size_label;

  gboolean monospace[GTK_NUM_FONT_FILTERS];
  GtkFontType font_type[GTK_NUM_FONT_FILTERS];

  gint updating;                /* above 0: code is choosing, callbacks off */
} Scalable;

static Scalable *
scalable_of (GtkFontSelection *fontsel)
{
  return gtk_object_get_data (GTK_OBJECT (fontsel), SCALABLE_KEY);
}

static void
scalable_free (gpointer data)
{
  Scalable *priv = data;

  g_strfreev (priv->families);
  g_free (priv->bitmap);
  g_free (priv->styles);
  g_free (priv->sizes);
  g_free (priv);
}

static gboolean
scalable_is_bitmap (Scalable *priv)
{
  return priv->family >= 0 && priv->bitmap[priv->family];
}

/* A size in the chosen family's unit, in pixels. */
static gdouble
scalable_pixels (Scalable *priv,
                 gdouble   size)
{
  return scalable_is_bitmap (priv) ? size : size * _gdk_ft_display_dpi () / 72.0;
}

/* Fill a list from code; a list in browse mode selects its first row as
   it is filled, and that is nobody's choice. */
static void
clist_set_rows (Scalable   *priv,
                GtkWidget  *clist,
                gchar     **rows,
                gint        n_rows)
{
  gint i;

  priv->updating++;
  gtk_clist_freeze (GTK_CLIST (clist));
  gtk_clist_clear (GTK_CLIST (clist));
  for (i = 0; i < n_rows; i++)
    gtk_clist_append (GTK_CLIST (clist), &rows[i]);
  gtk_clist_thaw (GTK_CLIST (clist));
  priv->updating--;
}

/* Select a row from code, and keep it in view. */
static void
clist_select (Scalable  *priv,
              GtkWidget *clist,
              gint       row)
{
  priv->updating++;
  if (row < 0)
    gtk_clist_unselect_all (GTK_CLIST (clist));
  else
    {
      gtk_clist_select_row (GTK_CLIST (clist), row, 0);
      if (GTK_WIDGET_MAPPED (clist) &&
          gtk_clist_row_is_visible (GTK_CLIST (clist), row) != GTK_VISIBILITY_FULL)
        gtk_clist_moveto (GTK_CLIST (clist), row, -1, 0.5, 0);
    }
  priv->updating--;
}

/* A list that is shown for the first time scrolls to what is selected. */
static void
scalable_clist_map (GtkWidget *clist,
                    gpointer   data)
{
  GList *selection = GTK_CLIST (clist)->selection;

  if (selection)
    gtk_clist_moveto (GTK_CLIST (clist), GPOINTER_TO_INT (selection->data),
                      -1, 0.5, 0);
}

static void
scalable_update_preview (GtkFontSelection *fontsel)
{
  gchar *name = gtk_font_selection_get_font_name (fontsel);
  GtkStyle *style;
  GdkFont *font;
  gint height;

  if (!name)
    return;

  font = gdk_font_load (name);
  g_free (name);
  if (!font)
    return;

  if (fontsel->font)
    gdk_font_unref (fontsel->font);
  fontsel->font = font;

  style = gtk_style_copy (fontsel->preview_entry->style);
  gdk_font_unref (style->font);
  style->font = gdk_font_ref (font);
  gtk_widget_set_style (fontsel->preview_entry, style);
  gtk_style_unref (style);

  /* Tall enough for the text, within reason, and never jumping smaller. */
  height = font->ascent + font->descent + 2 * 6;
  height = CLAMP (height, MIN_PREVIEW_HEIGHT, MAX_PREVIEW_HEIGHT);
  if (height > fontsel->preview_entry->requisition.height)
    gtk_widget_set_usize (fontsel->preview_entry, -1, height);

  if (!*gtk_entry_get_text (GTK_ENTRY (fontsel->preview_entry)))
    gtk_entry_set_text (GTK_ENTRY (fontsel->preview_entry),
                        _(DEFAULT_PREVIEW_TEXT));
  gtk_entry_set_position (GTK_ENTRY (fontsel->preview_entry), 0);
}

/*
 * Show the size last chosen in the family's unit: to a tenth of a
 * point, or the nearest size a bitmap was drawn at, the smaller of two
 * as near, as a name resolves.
 */
static void
scalable_show_size (GtkFontSelection *fontsel)
{
  Scalable *priv = scalable_of (fontsel);
  gchar buffer[32];
  gdouble size;
  gint i, row = -1;

  if (scalable_is_bitmap (priv))
    {
      size = priv->n_sizes > 0 ? priv->sizes[0] : priv->want.pixel_size;
      for (i = 1; i < priv->n_sizes; i++)
        if (ABS (priv->sizes[i] - priv->want.pixel_size) <
            ABS (size - priv->want.pixel_size))
          size = priv->sizes[i];
      fontsel->metric = GTK_FONT_METRIC_PIXELS;
      fontsel->size = (gint) size;
    }
  else
    {
      size = priv->want.pixel_size * 72.0 / _gdk_ft_display_dpi ();
      size = CLAMP (floor (size * 10 + 0.5) / 10, 1, 999);
      fontsel->metric = GTK_FONT_METRIC_POINTS;
      fontsel->size = (gint) (size * 10);
    }
  priv->size = size;
  fontsel->selected_size = fontsel->size;

  g_snprintf (buffer, sizeof buffer, "%g", size);
  gtk_entry_set_text (GTK_ENTRY (fontsel->size_entry), buffer);
  for (i = 0; i < priv->n_sizes; i++)
    if (priv->sizes[i] == size)
      row = i;
  clist_select (priv, fontsel->size_clist, row);

  scalable_update_preview (fontsel);
}

/* The sizes on offer: points, or the pixel sizes a bitmap style has. */
static void
scalable_fill_sizes (GtkFontSelection *fontsel)
{
  Scalable *priv = scalable_of (fontsel);
  gchar **rows;
  gint i;

  g_free (priv->sizes);
  if (scalable_is_bitmap (priv) && priv->style >= 0)
    {
      GdkFTName *style = &priv->styles[priv->style];

      priv->sizes = _gdk_ft_list_sizes (priv->families[priv->family],
                                        style->weight, style->slant,
                                        style->width, &priv->n_sizes);
    }
  else
    {
      priv->n_sizes = sizeof point_sizes / sizeof point_sizes[0];
      priv->sizes = g_memdup (point_sizes, sizeof point_sizes);
    }

  rows = g_new0 (gchar *, priv->n_sizes + 1);
  for (i = 0; i < priv->n_sizes; i++)
    rows[i] = g_strdup_printf ("%g", priv->sizes[i]);
  clist_set_rows (priv, fontsel->size_clist, rows, priv->n_sizes);
  g_strfreev (rows);

  gtk_label_set_text (GTK_LABEL (priv->size_label),
                      scalable_is_bitmap (priv) ? _("Size (px):") : _("Size (pt):"));
}

static void
scalable_select_style (GtkFontSelection *fontsel,
                       gint              row)
{
  Scalable *priv = scalable_of (fontsel);
  gchar *label;

  if (row < 0 || row >= priv->n_styles)
    return;

  priv->style = row;
  label = _gdk_ft_style_name (priv->styles[row].weight,
                              priv->styles[row].slant,
                              priv->styles[row].width);
  gtk_entry_set_text (GTK_ENTRY (fontsel->font_style_entry), label);
  g_free (label);
  clist_select (priv, fontsel->font_style_clist, row);

  scalable_fill_sizes (fontsel);
  scalable_show_size (fontsel);
}

/* The family's style closest to the one last chosen. */
static gint
scalable_best_style (Scalable *priv)
{
  gint best = -1, best_distance = G_MAXINT, i;

  for (i = 0; i < priv->n_styles; i++)
    {
      GdkFTName *s = &priv->styles[i];
      gint distance = ABS (s->weight - priv->want.weight) +
                      10 * ABS (s->width - priv->want.width) +
                      (s->slant != priv->want.slant ? 10000 : 0);

      if (distance < best_distance)
        {
          best = i;
          best_distance = distance;
        }
    }

  return best;
}

static void
scalable_select_family (GtkFontSelection *fontsel,
                        gint              row)
{
  Scalable *priv = scalable_of (fontsel);
  gchar **rows;
  gint i;

  if (row < 0 || row >= priv->n_families)
    return;

  priv->family = row;
  gtk_entry_set_text (GTK_ENTRY (fontsel->font_entry), priv->families[row]);
  clist_select (priv, fontsel->font_clist, row);

  g_free (priv->styles);
  priv->styles = _gdk_ft_list_styles (priv->families[row], &priv->n_styles);
  priv->style = -1;

  rows = g_new0 (gchar *, priv->n_styles + 1);
  for (i = 0; i < priv->n_styles; i++)
    rows[i] = _gdk_ft_style_name (priv->styles[i].weight,
                                  priv->styles[i].slant,
                                  priv->styles[i].width);
  clist_set_rows (priv, fontsel->font_style_clist, rows, priv->n_styles);
  g_strfreev (rows);

  if (priv->n_styles > 0)
    scalable_select_style (fontsel, scalable_best_style (priv));
  else
    gtk_entry_set_text (GTK_ENTRY (fontsel->font_style_entry), "");
}

static void
scalable_family_clicked (GtkWidget      *clist,
                         gint            row,
                         gint            column,
                         GdkEventButton *event,
                         gpointer        data)
{
  GtkFontSelection *fontsel = data;
  Scalable *priv = scalable_of (fontsel);

  if (!priv->updating && row != priv->family)
    scalable_select_family (fontsel, row);
}

static void
scalable_style_clicked (GtkWidget      *clist,
                        gint            row,
                        gint            column,
                        GdkEventButton *event,
                        gpointer        data)
{
  GtkFontSelection *fontsel = data;
  Scalable *priv = scalable_of (fontsel);

  if (priv->updating || row == priv->style || row < 0 || row >= priv->n_styles)
    return;

  priv->want.weight = priv->styles[row].weight;
  priv->want.slant = priv->styles[row].slant;
  priv->want.width = priv->styles[row].width;
  scalable_select_style (fontsel, row);
}

static void
scalable_size_clicked (GtkWidget      *clist,
                       gint            row,
                       gint            column,
                       GdkEventButton *event,
                       gpointer        data)
{
  GtkFontSelection *fontsel = data;
  Scalable *priv = scalable_of (fontsel);

  if (priv->updating || row < 0 || row >= priv->n_sizes)
    return;

  priv->want.pixel_size = scalable_pixels (priv, priv->sizes[row]);
  scalable_show_size (fontsel);
}

/* A size typed in, in the family's unit. */
static void
scalable_size_entered (GtkWidget *entry,
                       gpointer   data)
{
  GtkFontSelection *fontsel = data;
  Scalable *priv = scalable_of (fontsel);
  gdouble size;

  if (priv->updating)
    return;

  size = strtod (gtk_entry_get_text (GTK_ENTRY (entry)), NULL);
  if (size > 0)
    priv->want.pixel_size = scalable_pixels (priv, size);
  scalable_show_size (fontsel); /* or put back what it was */
}

static gint
scalable_size_focus_out (GtkWidget     *entry,
                         GdkEventFocus *event,
                         gpointer       data)
{
  scalable_size_entered (entry, data);
  return FALSE;
}

/*
 * The families on offer: the generic ones first, then everything
 * installed, the families that are only bitmaps marked as such. A
 * program's filters can leave out either kind, or all but the fixed
 * width families.
 */
static void
scalable_load_families (GtkFontSelection *fontsel)
{
  Scalable *priv = scalable_of (fontsel);
  gboolean monospace = priv->monospace[GTK_FONT_FILTER_BASE] ||
                       priv->monospace[GTK_FONT_FILTER_USER];
  guint types = priv->font_type[GTK_FONT_FILTER_BASE] &
                priv->font_type[GTK_FONT_FILTER_USER];
  gboolean scalable = (types & GTK_FONT_SCALABLE) != 0;
  gboolean bitmaps = (types & GTK_FONT_BITMAP) != 0;
  gboolean *installed_bitmap;
  gchar **installed = _gdk_ft_list_families (monospace, &installed_bitmap);
  gchar **rows;
  gint n_installed, i, n = 0;

  for (n_installed = 0; installed[n_installed]; n_installed++)
    ;

  g_strfreev (priv->families);
  g_free (priv->bitmap);
  priv->families = g_new0 (gchar *, n_installed + 4);
  priv->bitmap = g_new0 (gboolean, n_installed + 4);

  for (i = 0; i < (gint) (sizeof generic_families / sizeof generic_families[0]); i++)
    if (scalable && (!monospace || generic_families[i].monospace))
      priv->families[n++] = g_strdup (generic_families[i].shown);
  for (i = 0; i < n_installed; i++)
    if (installed_bitmap[i] ? bitmaps : scalable)
      {
        priv->bitmap[n] = installed_bitmap[i];
        priv->families[n++] = installed[i];
      }
    else
      g_free (installed[i]);
  g_free (installed);
  g_free (installed_bitmap);

  priv->n_families = n;
  priv->family = -1;

  /* Marked on the name itself, where no amount of scrolling hides it. */
  rows = g_new0 (gchar *, n + 1);
  for (i = 0; i < n; i++)
    rows[i] = priv->bitmap[i] ? g_strdup_printf (_("%s (bitmap)"), priv->families[i])
                              : g_strdup (priv->families[i]);
  clist_set_rows (priv, fontsel->font_clist, rows, n);
  g_strfreev (rows);
}

/* The row showing a family, as it was named or as it resolves. */
static gint
scalable_find_family (Scalable    *priv,
                      const gchar *family)
{
  gchar *resolved;
  gint i, row = -1;

  for (i = 0; i < (gint) (sizeof generic_families / sizeof generic_families[0]); i++)
    if (g_strcasecmp (family, generic_families[i].parsed) == 0)
      family = generic_families[i].shown;

  for (i = 0; i < priv->n_families && row < 0; i++)
    if (g_strcasecmp (priv->families[i], family) == 0)
      row = i;
  if (row >= 0)
    return row;

  /* Not installed, or under another name: what it resolves to stands
     in for it, as it does wherever the name is used. */
  resolved = _gdk_ft_resolve_family (family);
  for (i = 0; i < priv->n_families && row < 0; i++)
    if (g_strcasecmp (priv->families[i], resolved) == 0)
      row = i;
  g_free (resolved);

  return row;
}

static void
scalable_list (GtkWidget  *table,
               gint        column,
               gint        width,
               gboolean    scroll_across,
               GtkWidget **clist_out)
{
  GtkWidget *scrolled, *clist;

  clist = gtk_clist_new (1);
  gtk_clist_column_titles_hide (GTK_CLIST (clist));
  gtk_clist_set_column_auto_resize (GTK_CLIST (clist), 0, TRUE);
  gtk_clist_set_selection_mode (GTK_CLIST (clist), GTK_SELECTION_BROWSE);
  gtk_signal_connect (GTK_OBJECT (clist), "map",
                      GTK_SIGNAL_FUNC (scalable_clist_map), NULL);

  scrolled = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                  scroll_across ? GTK_POLICY_AUTOMATIC
                                                : GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);
  gtk_widget_set_usize (scrolled, width, 170);
  gtk_container_add (GTK_CONTAINER (scrolled), clist);
  gtk_table_attach (GTK_TABLE (table), scrolled, column, column + 1, 2, 3,
                    GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);

  *clist_out = clist;
}

static GtkWidget *
scalable_label (GtkWidget   *table,
                gint         column,
                const gchar *text)
{
  GtkWidget *label = gtk_label_new (text);

  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, column, column + 1, 0, 1,
                    GTK_FILL, 0, 0, 0);

  return label;
}

static GtkWidget *
scalable_entry (GtkWidget *table,
                gint       column,
                gboolean   editable)
{
  GtkWidget *entry = gtk_entry_new ();

  gtk_entry_set_editable (GTK_ENTRY (entry), editable);
  gtk_table_attach (GTK_TABLE (table), entry, column, column + 1, 1, 2,
                    GTK_EXPAND | GTK_FILL, 0, 0, 0);

  return entry;
}

static void
gtk_font_selection_scalable_init (GtkFontSelection *fontsel)
{
  GtkWidget *table, *frame, *vbox;
  Scalable *priv;

  priv = g_new0 (Scalable, 1);
  priv->family = priv->style = -1;
  priv->font_type[GTK_FONT_FILTER_BASE] = GTK_FONT_ALL;
  priv->font_type[GTK_FONT_FILTER_USER] = GTK_FONT_ALL;
  gtk_object_set_data_full (GTK_OBJECT (fontsel), SCALABLE_KEY, priv,
                            scalable_free);

  /* Still a notebook, as it always was, but with the one page. */
  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (fontsel), FALSE);
  gtk_notebook_set_show_border (GTK_NOTEBOOK (fontsel), FALSE);

  fontsel->main_vbox = gtk_vbox_new (FALSE, 8);
  gtk_notebook_append_page (GTK_NOTEBOOK (fontsel), fontsel->main_vbox,
                            gtk_label_new (_("Font")));

  table = gtk_table_new (3, 3, FALSE);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_table_set_col_spacings (GTK_TABLE (table), 8);
  gtk_box_pack_start (GTK_BOX (fontsel->main_vbox), table, TRUE, TRUE, 0);

  fontsel->font_label = scalable_label (table, 0, _("Family:"));
  scalable_label (table, 1, _("Style:"));
  priv->size_label = scalable_label (table, 2, _("Size (pt):"));

  fontsel->font_entry = scalable_entry (table, 0, FALSE);
  fontsel->font_style_entry = scalable_entry (table, 1, FALSE);
  fontsel->size_entry = scalable_entry (table, 2, TRUE);
  gtk_widget_set_usize (fontsel->size_entry, 64, -1);

  /* Family names can be long, and scroll; styles and sizes never do. */
  scalable_list (table, 0, 240, TRUE, &fontsel->font_clist);
  scalable_list (table, 1, -1, FALSE, &fontsel->font_style_clist);
  scalable_list (table, 2, 64, FALSE, &fontsel->size_clist);

  frame = gtk_frame_new (_("Preview:"));
  gtk_box_pack_start (GTK_BOX (fontsel->main_vbox), frame, FALSE, TRUE, 0);
  fontsel->preview_entry = gtk_entry_new ();
  gtk_widget_set_usize (fontsel->preview_entry, -1, MIN_PREVIEW_HEIGHT);
  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 4);
  gtk_container_add (GTK_CONTAINER (frame), vbox);
  gtk_box_pack_start (GTK_BOX (vbox), fontsel->preview_entry, FALSE, FALSE, 0);

  fontsel->message_label = gtk_label_new ("");
  gtk_box_pack_start (GTK_BOX (fontsel->main_vbox), fontsel->message_label,
                      FALSE, FALSE, 0);

  gtk_widget_show_all (fontsel->main_vbox);

  gtk_signal_connect (GTK_OBJECT (fontsel->font_clist), "select_row",
                      GTK_SIGNAL_FUNC (scalable_family_clicked), fontsel);
  gtk_signal_connect (GTK_OBJECT (fontsel->font_style_clist), "select_row",
                      GTK_SIGNAL_FUNC (scalable_style_clicked), fontsel);
  gtk_signal_connect (GTK_OBJECT (fontsel->size_clist), "select_row",
                      GTK_SIGNAL_FUNC (scalable_size_clicked), fontsel);
  gtk_signal_connect (GTK_OBJECT (fontsel->size_entry), "activate",
                      GTK_SIGNAL_FUNC (scalable_size_entered), fontsel);
  gtk_signal_connect (GTK_OBJECT (fontsel->size_entry), "focus_out_event",
                      GTK_SIGNAL_FUNC (scalable_size_focus_out), fontsel);

  /* Sans, regular, 10 points, until something is set. */
  scalable_load_families (fontsel);
  gtk_font_selection_set_font_name (fontsel, "Sans 10");
}

static gchar *
scalable_get_font_name (GtkFontSelection *fontsel)
{
  Scalable *priv = scalable_of (fontsel);
  GdkFTName *style = NULL;

  if (priv->family < 0)
    return NULL;
  if (priv->style >= 0)
    style = &priv->styles[priv->style];

  return _gdk_ft_name_compose (priv->families[priv->family],
                               style ? style->weight : 400,
                               style ? style->slant : GDK_FT_SLANT_ROMAN,
                               style ? style->width : 100,
                               priv->size, scalable_is_bitmap (priv));
}

/* A name of either kind: its family, the style closest to its own, and
   its size, shown in the family's unit. */
static gboolean
scalable_set_font_name (GtkFontSelection *fontsel,
                        const gchar      *fontname)
{
  Scalable *priv = scalable_of (fontsel);
  GdkFTName name;
  gint row;

  if (!_gdk_ft_name_parse (fontname, _gdk_ft_display_dpi (), &name))
    return FALSE;

  row = scalable_find_family (priv, name.families[0]);
  if (row >= 0)
    {
      priv->want.weight = name.weight;
      priv->want.slant = name.slant;
      priv->want.width = name.width;
      priv->want.pixel_size = name.points > 0 ?
        name.points * _gdk_ft_display_dpi () / 72.0 : name.pixel_size;

      scalable_select_family (fontsel, row);
    }

  _gdk_ft_name_clear (&name);

  return row >= 0;
}

/*
 * Of the X filters, two mean something among installed fonts: the font
 * type, which can leave out bitmap or scalable families, and fixed
 * width spacing -- a program that asks for "m" or "c" is offered fixed
 * width families only. Foundries, charsets and the rest are X's.
 */
static void
scalable_set_filter (GtkFontSelection  *fontsel,
                     GtkFontFilterType  filter_type,
                     GtkFontType        font_type,
                     gchar            **spacings)
{
  Scalable *priv = scalable_of (fontsel);
  gboolean monospace = spacings && spacings[0];
  gchar *family;
  gint i;

  for (i = 0; spacings && spacings[i]; i++)
    if (g_strcasecmp (spacings[i], "m") != 0 &&
        g_strcasecmp (spacings[i], "c") != 0)
      monospace = FALSE;

  if (priv->monospace[filter_type] == monospace &&
      priv->font_type[filter_type] == font_type)
    return;
  priv->monospace[filter_type] = monospace;
  priv->font_type[filter_type] = font_type;

  family = priv->family >= 0 ? g_strdup (priv->families[priv->family]) : NULL;
  scalable_load_families (fontsel);
  i = family ? scalable_find_family (priv, family) : -1;
  scalable_select_family (fontsel, i >= 0 ? i : 0);
  g_free (family);
}


/*****************************************************************************
 * GtkFontSelection
 *
 * The widget, and its public functions.
 *****************************************************************************/

GtkType
gtk_font_selection_get_type (void)
{
  static GtkType font_selection_type = 0;

  if(!font_selection_type)
    {
      static const GtkTypeInfo fontsel_type_info =
      {
        "GtkFontSelection",
        sizeof (GtkFontSelection),
        sizeof (GtkFontSelectionClass),
        (GtkClassInitFunc) gtk_font_selection_class_init,
        (GtkObjectInitFunc) gtk_font_selection_init,
        /* reserved_1 */ NULL,
        /* reserved_2 */ NULL,
        (GtkClassInitFunc) NULL,
      };

      font_selection_type = gtk_type_unique (GTK_TYPE_NOTEBOOK,
                                             &fontsel_type_info);
    }

  return font_selection_type;
}

static void
gtk_font_selection_class_init (GtkFontSelectionClass *klass)
{
  GtkObjectClass *object_class;

  object_class = (GtkObjectClass *) klass;

  font_selection_parent_class = gtk_type_class (GTK_TYPE_NOTEBOOK);

  object_class->destroy = gtk_font_selection_destroy;
}

static void
gtk_font_selection_init (GtkFontSelection *fontsel)
{
  gtk_font_selection_scalable_init (fontsel);
}

GtkWidget *
gtk_font_selection_new (void)
{
  GtkFontSelection *fontsel;

  fontsel = gtk_type_new (GTK_TYPE_FONT_SELECTION);

  return GTK_WIDGET (fontsel);
}

static void
gtk_font_selection_destroy (GtkObject *object)
{
  GtkFontSelection *fontsel;

  g_return_if_fail (object != NULL);
  g_return_if_fail (GTK_IS_FONT_SELECTION (object));

  fontsel = GTK_FONT_SELECTION (object);

  if (fontsel->font)
    gdk_font_unref (fontsel->font);
  fontsel->font = NULL;

  if (GTK_OBJECT_CLASS (font_selection_parent_class)->destroy)
    (* GTK_OBJECT_CLASS (font_selection_parent_class)->destroy) (object);
}

GdkFont*
gtk_font_selection_get_font (GtkFontSelection *fontsel)
{
  g_return_val_if_fail (fontsel != NULL, NULL);
  g_return_val_if_fail (GTK_IS_FONT_SELECTION (fontsel), NULL);

  if (!fontsel->font)
        scalable_update_preview (fontsel);
  return fontsel->font;
}

gchar *
gtk_font_selection_get_font_name (GtkFontSelection *fontsel)
{
  g_return_val_if_fail (fontsel != NULL, NULL);
  g_return_val_if_fail (GTK_IS_FONT_SELECTION (fontsel), NULL);

  return scalable_get_font_name (fontsel);
}

gboolean
gtk_font_selection_set_font_name (GtkFontSelection *fontsel,
                                  const gchar      *fontname)
{
  g_return_val_if_fail (fontsel != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_FONT_SELECTION (fontsel), FALSE);
  g_return_val_if_fail (fontname != NULL, FALSE);

  return scalable_set_font_name (fontsel, fontname);
}

void
gtk_font_selection_set_filter   (GtkFontSelection *fontsel,
                                 GtkFontFilterType filter_type,
                                 GtkFontType       font_type,
                                 gchar           **foundries,
                                 gchar           **weights,
                                 gchar           **slants,
                                 gchar           **setwidths,
                                 gchar           **spacings,
                                 gchar           **charsets)
{
  g_return_if_fail (fontsel != NULL);
  g_return_if_fail (GTK_IS_FONT_SELECTION (fontsel));

  scalable_set_filter (fontsel, filter_type, font_type, spacings);
}

gchar *
gtk_font_selection_get_preview_text (GtkFontSelection *fontsel)
{
  g_return_val_if_fail (fontsel != NULL, NULL);
  g_return_val_if_fail (GTK_IS_FONT_SELECTION (fontsel), NULL);

  return gtk_entry_get_text (GTK_ENTRY (fontsel->preview_entry));
}

void
gtk_font_selection_set_preview_text (GtkFontSelection *fontsel,
                                     const gchar      *text)
{
  g_return_if_fail (fontsel != NULL);
  g_return_if_fail (GTK_IS_FONT_SELECTION (fontsel));
  g_return_if_fail (text != NULL);

  gtk_entry_set_text (GTK_ENTRY (fontsel->preview_entry), text);
}


/*****************************************************************************
 * GtkFontSelectionDialog
 *****************************************************************************/

guint
gtk_font_selection_dialog_get_type (void)
{
  static guint font_selection_dialog_type = 0;

  if (!font_selection_dialog_type)
    {
      GtkTypeInfo fontsel_diag_info =
      {
        "GtkFontSelectionDialog",
        sizeof (GtkFontSelectionDialog),
        sizeof (GtkFontSelectionDialogClass),
        (GtkClassInitFunc) gtk_font_selection_dialog_class_init,
        (GtkObjectInitFunc) gtk_font_selection_dialog_init,
        /* reserved_1 */ NULL,
        /* reserved_2 */ NULL,
        (GtkClassInitFunc) NULL,
      };

      font_selection_dialog_type = gtk_type_unique (GTK_TYPE_WINDOW, &fontsel_diag_info);
    }

  return font_selection_dialog_type;
}

static void
gtk_font_selection_dialog_class_init (GtkFontSelectionDialogClass *klass)
{


  font_selection_dialog_parent_class = gtk_type_class (GTK_TYPE_WINDOW);
}

static void
gtk_font_selection_dialog_init (GtkFontSelectionDialog *fontseldiag)
{
  fontseldiag->dialog_width = -1;
  fontseldiag->auto_resize = TRUE;

  gtk_widget_set_events(GTK_WIDGET(fontseldiag), GDK_STRUCTURE_MASK);
  gtk_signal_connect (GTK_OBJECT (fontseldiag), "configure_event",
                      (GtkSignalFunc) gtk_font_selection_dialog_on_configure,
                      fontseldiag);

  gtk_container_set_border_width (GTK_CONTAINER (fontseldiag), 4);
  gtk_window_set_policy(GTK_WINDOW(fontseldiag), FALSE, TRUE, TRUE);

  fontseldiag->main_vbox = gtk_vbox_new (FALSE, 4);
  gtk_widget_show (fontseldiag->main_vbox);
  gtk_container_add (GTK_CONTAINER (fontseldiag), fontseldiag->main_vbox);

  fontseldiag->fontsel = gtk_font_selection_new();
  gtk_widget_show (fontseldiag->fontsel);
  gtk_box_pack_start (GTK_BOX (fontseldiag->main_vbox),
                      fontseldiag->fontsel, TRUE, TRUE, 0);

  /* Create the action area */
  fontseldiag->action_area = gtk_hbutton_box_new ();
  gtk_button_box_set_layout(GTK_BUTTON_BOX(fontseldiag->action_area),
                            GTK_BUTTONBOX_END);
  gtk_button_box_set_spacing(GTK_BUTTON_BOX(fontseldiag->action_area), 5);
  gtk_box_pack_start (GTK_BOX (fontseldiag->main_vbox),
                      fontseldiag->action_area, FALSE, FALSE, 0);
  gtk_widget_show (fontseldiag->action_area);

  fontseldiag->ok_button = gtk_button_new_with_label(_("OK"));
  GTK_WIDGET_SET_FLAGS (fontseldiag->ok_button, GTK_CAN_DEFAULT);
  gtk_widget_show(fontseldiag->ok_button);
  gtk_box_pack_start (GTK_BOX (fontseldiag->action_area),
                      fontseldiag->ok_button, TRUE, TRUE, 0);
  gtk_widget_grab_default (fontseldiag->ok_button);

  fontseldiag->apply_button = gtk_button_new_with_label(_("Apply"));
  GTK_WIDGET_SET_FLAGS (fontseldiag->apply_button, GTK_CAN_DEFAULT);
  /*gtk_widget_show(fontseldiag->apply_button);*/
  gtk_box_pack_start (GTK_BOX(fontseldiag->action_area),
                      fontseldiag->apply_button, TRUE, TRUE, 0);

  fontseldiag->cancel_button = gtk_button_new_with_label(_("Cancel"));
  GTK_WIDGET_SET_FLAGS (fontseldiag->cancel_button, GTK_CAN_DEFAULT);
  gtk_widget_show(fontseldiag->cancel_button);
  gtk_box_pack_start (GTK_BOX(fontseldiag->action_area),
                      fontseldiag->cancel_button, TRUE, TRUE, 0);


}

GtkWidget*
gtk_font_selection_dialog_new   (const gchar      *title)
{
  GtkFontSelectionDialog *fontseldiag;

  fontseldiag = gtk_type_new (GTK_TYPE_FONT_SELECTION_DIALOG);
  gtk_window_set_title (GTK_WINDOW (fontseldiag),
                        title ? title : _("Font Selection"));

  return GTK_WIDGET (fontseldiag);
}

gchar*
gtk_font_selection_dialog_get_font_name (GtkFontSelectionDialog *fsd)
{
  return gtk_font_selection_get_font_name(GTK_FONT_SELECTION(fsd->fontsel));
}

GdkFont*
gtk_font_selection_dialog_get_font      (GtkFontSelectionDialog *fsd)
{
  return gtk_font_selection_get_font(GTK_FONT_SELECTION(fsd->fontsel));
}

gboolean
gtk_font_selection_dialog_set_font_name (GtkFontSelectionDialog *fsd,
                                         const gchar      *fontname)
{
  return gtk_font_selection_set_font_name(GTK_FONT_SELECTION(fsd->fontsel),
                                          fontname);
}

void
gtk_font_selection_dialog_set_filter    (GtkFontSelectionDialog *fsd,
                                         GtkFontFilterType filter_type,
                                         GtkFontType       font_type,
                                         gchar           **foundries,
                                         gchar           **weights,
                                         gchar           **slants,
                                         gchar           **setwidths,
                                         gchar           **spacings,
                                         gchar           **charsets)
{
  gtk_font_selection_set_filter (GTK_FONT_SELECTION (fsd->fontsel),
                                 filter_type, font_type,
                                 foundries, weights, slants, setwidths,
                                 spacings, charsets);
}

gchar*
gtk_font_selection_dialog_get_preview_text (GtkFontSelectionDialog *fsd)
{
  return gtk_font_selection_get_preview_text(GTK_FONT_SELECTION(fsd->fontsel));
}

void
gtk_font_selection_dialog_set_preview_text (GtkFontSelectionDialog *fsd,
                                            const gchar   *text)
{
  gtk_font_selection_set_preview_text(GTK_FONT_SELECTION(fsd->fontsel), text);
}


/* This turns auto-shrink off if the user resizes the width of the dialog.
   It also turns it back on again if the user resizes it back to its normal
   width. */
static gint
gtk_font_selection_dialog_on_configure (GtkWidget         *widget,
                                        GdkEventConfigure *event,
                                        GtkFontSelectionDialog *fsd)
{
  /* This sets the initial width. */
  if (fsd->dialog_width == -1)
    fsd->dialog_width = event->width;
  else if (fsd->auto_resize && fsd->dialog_width != event->width)
    {
      fsd->auto_resize = FALSE;
      gtk_window_set_policy(GTK_WINDOW(fsd), FALSE, TRUE, FALSE);
    }
  else if (!fsd->auto_resize && fsd->dialog_width == event->width)
    {
      fsd->auto_resize = TRUE;
      gtk_window_set_policy(GTK_WINDOW(fsd), FALSE, TRUE, TRUE);
    }

  return FALSE;
}
