/* GTK - The GIMP Toolkit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
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

/* The Priscus theme: the toolkit's built-in look, GTK+ 1.2's, asked for
 * by name like any engine (engine "priscus") and found by
 * gtk_theme_engine_get() without loading a module. The drawing and the
 * colours are gtkstyle.c's, which stay as GTK+ 1.2 had them, but for
 * insensitive text on a dark background (Priscus-Dark).
 */

#include "gtkrc.h"
#include "gtkstyle.h"
#include "gtkthemes.h"
#include "gtkthemebuiltin.h"
#include "gtkthemecommon.h"

/* The engine takes no options: engine "priscus" { } */
static guint
priscus_parse_rc_style (GScanner   *scanner,
                        GtkRcStyle *rc_style)
{
  guint token;

  token = g_scanner_get_next_token (scanner);
  if (token != G_TOKEN_RIGHT_CURLY)
    return G_TOKEN_RIGHT_CURLY;

  rc_style->engine_data = NULL;

  return G_TOKEN_NONE;
}

static void
priscus_merge_rc_style (GtkRcStyle *dest,
                        GtkRcStyle *src)
{
}

/*
 * GTK+ 1.2 engraves insensitive text with a white copy below and to the
 * right, which on a dark background smears it instead. A dark style
 * engraves it in its light colour, as the other engines do; a light one
 * is drawn exactly as GTK+ 1.2 drew it.
 */
static GtkStyleClass *
priscus_dark_class (void)
{
  static GtkStyleClass klass;

  if (!klass.draw_hline)
    {
      klass = *_gtk_style_default_class ();
      klass.draw_string = _gtk_theme_draw_string;
    }

  return &klass;
}

/* The colours are part of the look, so any the rc file leaves alone
 * are the built-in ones. */
static void
priscus_rc_style_to_style (GtkStyle   *style,
                           GtkRcStyle *rc_style)
{
  GdkColor fg[5], bg[5], text[5], base[5];
  gint i;

  _gtk_style_default_colors (fg, bg, text, base);
  for (i = 0; i < 5; i++)
    {
      if (!(rc_style->color_flags[i] & GTK_RC_FG))
        style->fg[i] = fg[i];
      if (!(rc_style->color_flags[i] & GTK_RC_BG))
        style->bg[i] = bg[i];
      if (!(rc_style->color_flags[i] & GTK_RC_TEXT))
        style->text[i] = text[i];
      if (!(rc_style->color_flags[i] & GTK_RC_BASE))
        style->base[i] = base[i];
    }

  style->klass = _gtk_theme_is_dark (style) ? priscus_dark_class () :
    (GtkStyleClass *) _gtk_style_default_class ();
}

static void
priscus_duplicate_style (GtkStyle *dest,
                         GtkStyle *src)
{
  dest->klass = src->klass;
}

void
gtk_theme_priscus_init (GtkThemeEngine *engine)
{
  engine->parse_rc_style = priscus_parse_rc_style;
  engine->merge_rc_style = priscus_merge_rc_style;
  engine->rc_style_to_style = priscus_rc_style_to_style;
  engine->duplicate_style = priscus_duplicate_style;
}
