/* GTK - The GIMP Toolkit
 * Drawing helpers the built-in theme engines share
 *
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

#ifndef __GTK_THEME_COMMON_H__
#define __GTK_THEME_COMMON_H__

#include "gtkstyle.h"
#include "gtkrc.h"

/* GLib 2's, which GLib 1.2 has not */
#ifndef G_N_ELEMENTS
#define G_N_ELEMENTS(arr) (sizeof (arr) / sizeof ((arr)[0]))
#endif

/* Drawing clipped to area, when there is one. */
void _gtk_theme_clip (GdkGC *gc, GdkRectangle *area);
void _gtk_theme_unclip (GdkGC *gc, GdkRectangle *area);
void _gtk_theme_line (GdkWindow *window, GdkGC *gc, GdkRectangle *area,
                      gint x1, gint y1, gint x2, gint y2);
void _gtk_theme_dot (GdkWindow *window, GdkRectangle *area, GdkGC *gc1,
                     GdkGC *gc2, gint x, gint y);
void _gtk_theme_rectangle (GdkWindow *window, GdkGC *gc, GdkRectangle *area,
                           gboolean filled, gint x, gint y, gint width,
                           gint height);
void _gtk_theme_simple_border (GdkWindow *window, GdkRectangle *area,
                               GdkGC *tl, GdkGC *br, gint x, gint y,
                               gint width, gint height);
void _gtk_theme_sanitize_size (GdkWindow *window, gint *width, gint *height);
void _gtk_theme_button_rect (GtkWidget *widget, GdkWindow *window, gint *x,
                             gint *y, gint *width, gint *height);
GtkStateType _gtk_theme_toggle_state (const gchar *detail,
                                      GtkStateType state_type,
                                      GtkShadowType shadow_type);
void _gtk_theme_entry_frame (GtkWidget *widget, const gchar *detail,
                             gint *x, gint *y, gint *width, gint *height);
GdkGC * _gtk_theme_aa_gc (GtkStyle *style, GdkWindow *window,
                          GdkRectangle *area, const GdkColor *color);
GdkGC * _gtk_theme_shade_gc (GtkStyle *style, const GdkColor *color,
                             gdouble r, gdouble g, gdouble b);
gboolean _gtk_theme_is_dark (GtkStyle *style);
gboolean _gtk_theme_parent_is (GtkWidget *widget, GtkType type);

/* An engine's style class, made once for each thickness */
GtkStyleClass *_gtk_theme_class (GSList **classes, const GtkStyleClass *klass,
                                 gint xthickness, gint ythickness);

/* An engine's options in a gtkrc: its words, and "= value" after one */
typedef struct
{
  const gchar *name;
  guint token;
} GtkThemeSymbol;

guint _gtk_theme_scope_enter (GScanner *scanner, const gchar *scope,
                              const GtkThemeSymbol *symbols, guint n_symbols);
guint _gtk_theme_parse_equals (GScanner *scanner);
guint _gtk_theme_parse_int (GScanner *scanner, gint *value);
guint _gtk_theme_parse_number (GScanner *scanner, gdouble *value);
guint _gtk_theme_parse_boolean (GScanner *scanner, guint token_true,
                                guint token_false, gboolean *value);
guint _gtk_theme_parse_color (GScanner *scanner, GdkColor *color);

void _gtk_theme_destroy_rc_style (GtkRcStyle *rc_style);
void _gtk_theme_destroy_style (GtkStyle *style);
void _gtk_theme_draw_string (GtkStyle *style, GdkWindow *window,
                             GtkStateType state_type, GdkRectangle *area,
                             GtkWidget *widget, gchar *detail, gint x, gint y,
                             const gchar *string);
void _gtk_theme_draw_polygon (GtkStyle *style, GdkWindow *window,
                              GtkStateType state_type,
                              GtkShadowType shadow_type, GdkRectangle *area,
                              GtkWidget *widget, gchar *detail,
                              GdkPoint *points, gint npoints, gint fill);
void _gtk_theme_draw_diamond (GtkStyle *style, GdkWindow *window,
                              GtkStateType state_type,
                              GtkShadowType shadow_type, GdkRectangle *area,
                              GtkWidget *widget, gchar *detail, gint x,
                              gint y, gint width, gint height);
void _gtk_theme_draw_extension (GtkStyle *style, GdkWindow *window,
                                GtkStateType state_type,
                                GtkShadowType shadow_type, GdkRectangle *area,
                                GtkWidget *widget, gchar *detail, gint x,
                                gint y, gint width, gint height,
                                GtkPositionType gap_side);

#endif /* __GTK_THEME_COMMON_H__ */
