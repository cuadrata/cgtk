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

#ifndef CGTK_THEME_THEME_H
#define CGTK_THEME_THEME_H

#include <gtk/gtk.h>

/* The four colour sets of a style, as gtkrc names them. */
enum { SET_FG, SET_BG, SET_BASE, SET_TEXT, N_SETS };
#define N_STATES 5                      /* GtkStateType */

/* A theme that can be chosen: its name and its rc file. A theme Foo
 * and a theme Foo-Dark are the light and dark versions of one. */
typedef struct _ThemeInfo ThemeInfo;
struct _ThemeInfo
{
  gchar *name;
  gchar *dir;                   /* the theme's directory */
  gchar *rc_path;               /* its gtk-1.4/gtkrc, or a 1.2 theme's gtk/gtkrc */
  gboolean user;                /* the user's own (theme_user_dir), so it can be changed */
  ThemeInfo *light;             /* Foo, for Foo-Dark; else NULL */
  ThemeInfo *dark;              /* Foo-Dark, for Foo; else NULL */
};

#define THEME_DARK_SUFFIX "-Dark"

GList     *theme_list           (void);
gboolean   theme_name_is_dark   (const gchar *name);
void       theme_list_free      (GList *list);
ThemeInfo *theme_list_find      (GList *list, const gchar *name);
ThemeInfo *theme_list_find_rc   (GList *list, const gchar *rc_path);
gchar     *theme_user_dir       (void);

/* A theme's rc file, as text, with what the editor changes in it found:
 * the colours of every style, and the engine and its options in the
 * main style, the one "class GtkWidget" names. The text is changed in
 * place, so its comments and everything else in it stay. */
typedef struct _Theme Theme;

Theme       *theme_load          (const gchar *rc_path);
Theme       *theme_new           (const gchar *text);
void         theme_free          (Theme *theme);
const gchar *theme_text          (Theme *theme);
const gchar *theme_engine        (Theme *theme);

gboolean     theme_get_color     (Theme *theme, gint set, gint state, GdkColor *color);
void         theme_set_color     (Theme *theme, gint set, gint state, const GdkColor *color);

/* The places in any style where a colour is: at set and state, or with
 * a set of -1 anywhere. They stay the same places while colours change,
 * so a colour being dragged takes the same ones with it all the way. */
GList       *theme_find_color    (Theme *theme, gint set, gint state, const GdkColor *color);
void         theme_set_places    (Theme *theme, GList *places, const GdkColor *color);

gchar       *theme_get_option    (Theme *theme, const gchar *key);
void         theme_set_option    (Theme *theme, const gchar *key, const gchar *value);

void         theme_prepend       (Theme *theme, const gchar *text);

gboolean     theme_write_file    (const gchar *path, const gchar *text);

/* The user's rc file (gtk_rc_get_user_file()): which theme it includes,
 * and the font it sets if any. */
gchar       *gtkrc_path          (void);
void         gtkrc_read          (gchar **theme_rc, gchar **font);
gchar       *gtkrc_render        (const gchar *theme_rc, const gchar *font);

#endif
