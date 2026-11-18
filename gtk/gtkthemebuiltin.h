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

/* Private: the theme engines built into the library. */

#ifndef __GTK_THEME_BUILTIN_H__
#define __GTK_THEME_BUILTIN_H__

#include <gtk/gtkthemes.h>

void gtk_theme_altus_init (GtkThemeEngine *engine);
void gtk_theme_clarus_init (GtkThemeEngine *engine);
void gtk_theme_fabrica_init (GtkThemeEngine *engine);
void gtk_theme_fenestramm_init (GtkThemeEngine *engine);
void gtk_theme_gelidus_init (GtkThemeEngine *engine);
void gtk_theme_metallum_init (GtkThemeEngine *engine);
void gtk_theme_unicus_init (GtkThemeEngine *engine);
void gtk_theme_priscus_init (GtkThemeEngine *engine);

/* gtkstyle.c: the built-in look, which the Priscus engine is. */
const GtkStyleClass *_gtk_style_default_class (void);
void _gtk_style_default_colors (GdkColor fg[5], GdkColor bg[5],
                                GdkColor text[5], GdkColor base[5]);

#endif /* __GTK_THEME_BUILTIN_H__ */
