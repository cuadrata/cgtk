/* GDK - The GIMP Drawing Kit
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

/*
 * Modified by the GTK+ Team and others 1997-1999. See the AUTHORS
 * file for a list of people on the GTK+ Team. See the ChangeLog
 * files for a list of changes. These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/.
 */

/*
 * Fonts. A GdkFont is a set of FreeType faces (gdkftfont.c) found
 * from its name (gdkftname.c): a modern name like "Sans 10", or an X
 * name, which is translated -- the families an X name asks for are
 * installed as bitmap or outline faces, and fontconfig finds them.
 * There is no server to load a font from, so a name that resolves to
 * nothing installed loads nothing. Text is UTF-8, or the locale's own
 * encoding if it set one; bytes that are neither are read as Latin-1.
 */

#include "config.h"
#include <string.h>
#include "gdk.h"
#include "gdkprivate.h"
#include "gdkftfont.h"

static GHashTable *font_name_hash = NULL;
static GHashTable *fontset_name_hash = NULL;

#define FONT_FT(font) ((GdkFTSet *) ((GdkFontPrivate *) (font))->ft)

gint
_gdk_font_ft_decode (GdkFont     *font,
                     const gchar *text,
                     gint         text_length,
                     guint32     *result)
{
  return _gdk_ft_decode_text (text, text_length, result, FALSE);
}

static void
gdk_font_extents (GdkFont     *font,
                  const gchar *text,
                  gint         text_length,
                  gint        *lbearing,
                  gint        *rbearing,
                  gint        *width,
                  gint        *ascent,
                  gint        *descent)
{
  guint32 *chars = g_new (guint32, MAX (text_length, 1));
  gint n = _gdk_font_ft_decode (font, text, text_length, chars);

  _gdk_ft_set_text_extents (FONT_FT (font), chars, n,
                            lbearing, rbearing, width, ascent, descent);
  g_free (chars);
}

/* Wide characters are Unicode already. */
static void
gdk_font_extents_wc (GdkFont        *font,
                     const GdkWChar *text,
                     gint            text_length,
                     gint           *lbearing,
                     gint           *rbearing,
                     gint           *width,
                     gint           *ascent,
                     gint           *descent)
{
  _gdk_ft_set_text_extents (FONT_FT (font), text, text_length,
                            lbearing, rbearing, width, ascent, descent);
}

static void
gdk_font_hash_insert (GdkFontType type, GdkFont *font, const gchar *font_name)
{
  GdkFontPrivate *private = (GdkFontPrivate *)font;
  GHashTable **hashp = (type == GDK_FONT_FONT) ?
    &font_name_hash : &fontset_name_hash;

  if (!*hashp)
    *hashp = g_hash_table_new (g_str_hash, g_str_equal);

  private->names = g_slist_prepend (private->names, g_strdup (font_name));
  g_hash_table_insert (*hashp, private->names->data, font);
}

static void
gdk_font_hash_remove (GdkFontType type, GdkFont *font)
{
  GdkFontPrivate *private = (GdkFontPrivate *)font;
  GSList *tmp_list;
  GHashTable *hash = (type == GDK_FONT_FONT) ?
    font_name_hash : fontset_name_hash;

  tmp_list = private->names;
  while (tmp_list)
    {
      g_hash_table_remove (hash, tmp_list->data);
      g_free (tmp_list->data);

      tmp_list = tmp_list->next;
    }

  g_slist_free (private->names);
  private->names = NULL;
}

static GdkFont *
gdk_font_hash_lookup (GdkFontType type, const gchar *font_name)
{
  GdkFont *result;
  GHashTable *hash = (type == GDK_FONT_FONT) ?
    font_name_hash : fontset_name_hash;

  if (!hash)
    return NULL;
  else
    {
      result = g_hash_table_lookup (hash, font_name);
      if (result)
        gdk_font_ref (result);

      return result;
    }
}

static GdkFont *
gdk_font_load_any (const gchar *name,
                   GdkFontType  type)
{
  static gint next_id = 1;
  GdkFontPrivate *private;
  GdkFont *font;
  GdkFTSet *set;

  g_return_val_if_fail (name != NULL, NULL);

  font = gdk_font_hash_lookup (type, name);
  if (font)
    return font;

  set = _gdk_ft_set_open_name (name, _gdk_ft_display_dpi (), 1);
  if (!set)
    return NULL;

  private = g_new0 (GdkFontPrivate, 1);
  private->ref_count = 1;
  private->ft = set;
  private->ft_id = next_id++;

  font = (GdkFont *) private;
  font->type = type;
  font->ascent = set->ascent;
  font->descent = set->descent;

  gdk_font_hash_insert (type, font, name);

  return font;
}

GdkFont*
gdk_font_load (const gchar *font_name)
{
  return gdk_font_load_any (font_name, GDK_FONT_FONT);
}

GdkFont*
gdk_fontset_load (const gchar *fontset_name)
{
  return gdk_font_load_any (fontset_name, GDK_FONT_FONTSET);
}

GdkFont*
gdk_font_ref (GdkFont *font)
{
  GdkFontPrivate *private;

  g_return_val_if_fail (font != NULL, NULL);

  private = (GdkFontPrivate*) font;
  private->ref_count += 1;
  return font;
}

void
gdk_font_unref (GdkFont *font)
{
  GdkFontPrivate *private;
  private = (GdkFontPrivate*) font;

  g_return_if_fail (font != NULL);
  g_return_if_fail (private->ref_count > 0);

  private->ref_count -= 1;
  if (private->ref_count == 0)
    {
      gdk_font_hash_remove (font->type, font);
      _gdk_ft_set_free (FONT_FT (font));
      if (private->ft_scaled)
        _gdk_ft_set_free (private->ft_scaled);
      g_free (font);
    }
}

GdkFTSet *
_gdk_font_ft_set_at_scale (GdkFont *font,
                           gint     scale)
{
  GdkFontPrivate *private = (GdkFontPrivate *) font;

  g_return_val_if_fail (font != NULL, NULL);

  if (scale <= 1 || !private->ft)
    return private->ft;

  /*
   * One scale at a time: a window that moves between displays of
   * different scales has its text reopened, which is rare, and a
   * program on one display never opens more than the two.
   */
  if (private->ft_scaled && private->ft_scale != scale)
    {
      _gdk_ft_set_free (private->ft_scaled);
      private->ft_scaled = NULL;
    }
  if (!private->ft_scaled)
    {
      const gchar *name = private->names ? private->names->data : NULL;

      private->ft_scale = scale;
      if (name)
        private->ft_scaled = _gdk_ft_set_open_name (name, _gdk_ft_display_dpi (),
                                                    scale);
    }

  return private->ft_scaled ? private->ft_scaled : private->ft;
}

gint
gdk_font_id (const GdkFont *font)
{
  g_return_val_if_fail (font != NULL, 0);

  return ((const GdkFontPrivate*) font)->ft_id;
}

gboolean
gdk_font_equal (const GdkFont *fonta,
                const GdkFont *fontb)
{
  g_return_val_if_fail (fonta != NULL, FALSE);
  g_return_val_if_fail (fontb != NULL, FALSE);

  return fonta == fontb;
}

gint
gdk_string_width (GdkFont     *font,
                  const gchar *string)
{
  g_return_val_if_fail (font != NULL, -1);
  g_return_val_if_fail (string != NULL, -1);

  return gdk_text_width (font, string, strlen (string));
}

gint
gdk_text_width (GdkFont      *font,
                const gchar  *text,
                gint          text_length)
{
  gint width;

  g_return_val_if_fail (font != NULL, -1);
  g_return_val_if_fail (text != NULL, -1);

  gdk_font_extents (font, text, text_length, NULL, NULL, &width, NULL, NULL);
  return width;
}

gint
gdk_text_width_wc (GdkFont        *font,
                   const GdkWChar *text,
                   gint            text_length)
{
  gint width;

  g_return_val_if_fail (font != NULL, -1);
  g_return_val_if_fail (text != NULL, -1);

  gdk_font_extents_wc (font, text, text_length, NULL, NULL, &width, NULL, NULL);
  return width;
}

gint
gdk_char_width (GdkFont *font,
                gchar    character)
{
  g_return_val_if_fail (font != NULL, -1);

  return gdk_text_width (font, &character, 1);
}

gint
gdk_char_width_wc (GdkFont *font,
                   GdkWChar character)
{
  g_return_val_if_fail (font != NULL, -1);

  return gdk_text_width_wc (font, &character, 1);
}

gint
gdk_string_measure (GdkFont     *font,
                    const gchar *string)
{
  g_return_val_if_fail (font != NULL, -1);
  g_return_val_if_fail (string != NULL, -1);

  return gdk_text_measure (font, string, strlen (string));
}

void
gdk_text_extents (GdkFont     *font,
                  const gchar *text,
                  gint         text_length,
                  gint        *lbearing,
                  gint        *rbearing,
                  gint        *width,
                  gint        *ascent,
                  gint        *descent)
{
  g_return_if_fail (font != NULL);
  g_return_if_fail (text != NULL);

  gdk_font_extents (font, text, text_length,
                    lbearing, rbearing, width, ascent, descent);
}

void
gdk_text_extents_wc (GdkFont        *font,
                     const GdkWChar *text,
                     gint            text_length,
                     gint           *lbearing,
                     gint           *rbearing,
                     gint           *width,
                     gint           *ascent,
                     gint           *descent)
{
  g_return_if_fail (font != NULL);
  g_return_if_fail (text != NULL);

  gdk_font_extents_wc (font, text, text_length,
                       lbearing, rbearing, width, ascent, descent);
}

void
gdk_string_extents (GdkFont     *font,
                    const gchar *string,
                    gint        *lbearing,
                    gint        *rbearing,
                    gint        *width,
                    gint        *ascent,
                    gint        *descent)
{
  g_return_if_fail (font != NULL);
  g_return_if_fail (string != NULL);

  gdk_text_extents (font, string, strlen (string),
                    lbearing, rbearing, width, ascent, descent);
}

gint
gdk_text_measure (GdkFont     *font,
                  const gchar *text,
                  gint         text_length)
{
  gint rbearing;

  g_return_val_if_fail (font != NULL, -1);
  g_return_val_if_fail (text != NULL, -1);

  gdk_font_extents (font, text, text_length, NULL, &rbearing, NULL, NULL, NULL);
  return rbearing;
}

gint
gdk_char_measure (GdkFont *font,
                  gchar    character)
{
  g_return_val_if_fail (font != NULL, -1);

  return gdk_text_measure (font, &character, 1);
}

gint
gdk_string_height (GdkFont     *font,
                   const gchar *string)
{
  g_return_val_if_fail (font != NULL, -1);
  g_return_val_if_fail (string != NULL, -1);

  return gdk_text_height (font, string, strlen (string));
}

gint
gdk_text_height (GdkFont     *font,
                 const gchar *text,
                 gint         text_length)
{
  gint ascent, descent;

  g_return_val_if_fail (font != NULL, -1);
  g_return_val_if_fail (text != NULL, -1);

  gdk_font_extents (font, text, text_length, NULL, NULL, NULL, &ascent, &descent);
  return ascent + descent;
}

gint
gdk_char_height (GdkFont *font,
                 gchar    character)
{
  g_return_val_if_fail (font != NULL, -1);

  return gdk_text_height (font, &character, 1);
}
