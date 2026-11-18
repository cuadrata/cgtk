/* GDK - The GIMP Drawing Kit
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
 * Glyphs, faces and sets. Nothing in here knows about X, about
 * drawables, or about what a font name means.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <langinfo.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "gdk.h"
#include "gdkftfont.h"


#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_SYNTHESIS_H

/* 26.6 fixed point to whole pixels. */
#define PIXELS_ROUND(x) ((gint) (((x) + 32) >> 6))
#define PIXELS_CEIL(x)  ((gint) (((x) + 63) >> 6))

typedef struct _GdkFTFontPrivate GdkFTFontPrivate;
typedef struct _GdkFTSetPrivate  GdkFTSetPrivate;

struct _GdkFTFontPrivate
{
  GdkFTFont font;

  guint ref_count;
  gchar *key;

  FT_Face face;
  FT_Int32 load_flags;
  FT_Render_Mode render_mode;
  FT_Int32 mono_load_flags;
  gboolean embolden;
  gboolean transform;
  FT_Matrix matrix;

  gboolean bitmap;              /* drawn from a fixed size strike */

  GHashTable *glyphs;           /* codepoint -> GdkFTGlyph, or &no_glyph */
  GdkFTGlyph *notdef;

  /* The same glyphs hinted and drawn for one bit a pixel. */
  GHashTable *mono_glyphs;
  GdkFTGlyph *mono_notdef;
};

struct _GdkFTSetPrivate
{
  GdkFTSet set;

  GdkFTFont *primary;

  GdkFTFallbackFunc fallback;
  gpointer fallback_data;
  GDestroyNotify fallback_destroy;

  GHashTable *fallbacks;        /* codepoint -> GdkFTFont, or &no_font */
  GSList *held;                 /* one reference to each fallback font */
};

static FT_Library ft_library;
static gboolean ft_library_failed;
static GHashTable *font_cache;  /* key -> GdkFTFontPrivate */

/* Markers for "looked for and not there", so it is only looked for once. */
static GdkFTGlyph no_glyph;
static GdkFTFontPrivate no_font;

/* What control characters and failed glyphs are drawn as. */
static const GdkFTGlyph empty_glyph = { 0, 0, 0, 0, 0, NULL };

void
_gdk_ft_options_init (GdkFTOptions *options)
{
  g_return_if_fail (options != NULL);

  options->antialias = TRUE;
  options->hinting = TRUE;
  options->hint_style = GDK_FT_HINT_SLIGHT;
  options->autohint = FALSE;
  options->embolden = FALSE;
  options->matrix[0] = 1.0;
  options->matrix[1] = 0.0;
  options->matrix[2] = 0.0;
  options->matrix[3] = 1.0;
}

static gboolean
gdk_ft_init (void)
{
  if (ft_library)
    return TRUE;
  if (ft_library_failed)
    return FALSE;

  if (FT_Init_FreeType (&ft_library) != 0)
    {
      g_warning ("Could not initialise FreeType; scalable fonts are off");
      ft_library_failed = TRUE;
      return FALSE;
    }

  font_cache = g_hash_table_new (g_str_hash, g_str_equal);

  return TRUE;
}

static gchar *
gdk_ft_font_key (const gchar        *path,
                 gint                index,
                 gdouble             pixel_size,
                 const GdkFTOptions *options)
{
  return g_strdup_printf ("%s\n%d\n%.3f\n%d%d%d%d%d\n%.4f %.4f %.4f %.4f",
                          path, index, pixel_size,
                          options->antialias ? 1 : 0,
                          options->hinting ? 1 : 0,
                          options->hint_style,
                          options->autohint ? 1 : 0,
                          options->embolden ? 1 : 0,
                          options->matrix[0], options->matrix[1],
                          options->matrix[2], options->matrix[3]);
}

/*
 * Load flags as other toolkits derive them from the same settings, so
 * that text here looks like text in everything else on the desktop.
 * Embedded bitmaps are never used: what is drawn is always the outline,
 * which is what keeps one face looking the same at every size.
 */
static void
gdk_ft_font_set_flags (GdkFTFontPrivate   *private,
                       const GdkFTOptions *options)
{
  FT_Int32 flags = FT_LOAD_NO_BITMAP;

  if (!options->hinting || options->hint_style == GDK_FT_HINT_NONE)
    flags |= FT_LOAD_NO_HINTING;
  else if (!options->antialias)
    flags |= FT_LOAD_TARGET_MONO;
  else if (options->hint_style == GDK_FT_HINT_SLIGHT)
    flags |= FT_LOAD_TARGET_LIGHT;
  else
    flags |= FT_LOAD_TARGET_NORMAL;

  if (options->autohint)
    flags |= FT_LOAD_FORCE_AUTOHINT;

  private->load_flags = flags;

  /*
   * One bit a pixel is hinted for one bit a pixel: the stems land on
   * whole pixels, where cutting antialiased glyphs at half coverage
   * breaks thin strokes apart at small sizes.
   */
  flags = FT_LOAD_NO_BITMAP;
  if (!options->hinting || options->hint_style == GDK_FT_HINT_NONE)
    flags |= FT_LOAD_NO_HINTING;
  else
    flags |= FT_LOAD_TARGET_MONO;
  if (options->autohint)
    flags |= FT_LOAD_FORCE_AUTOHINT;
  private->mono_load_flags = flags;
  private->render_mode = options->antialias ? FT_RENDER_MODE_NORMAL
                                            : FT_RENDER_MODE_MONO;
  private->embolden = options->embolden;

  private->matrix.xx = (FT_Fixed) (options->matrix[0] * 65536.0);
  private->matrix.xy = (FT_Fixed) (options->matrix[1] * 65536.0);
  private->matrix.yx = (FT_Fixed) (options->matrix[2] * 65536.0);
  private->matrix.yy = (FT_Fixed) (options->matrix[3] * 65536.0);
  private->transform = (private->matrix.xx != 0x10000 ||
                        private->matrix.xy != 0 ||
                        private->matrix.yx != 0 ||
                        private->matrix.yy != 0x10000);
}

/* The fixed size nearest a pixel size, the smaller of two as near. */
static gint
gdk_ft_nearest_strike (FT_Face face,
                       gdouble pixel_size)
{
  gint best = -1, i;
  gdouble best_distance = 0;

  for (i = 0; i < face->num_fixed_sizes; i++)
    {
      gdouble size = face->available_sizes[i].y_ppem / 64.0;
      gdouble distance = size > pixel_size ? size - pixel_size
                                           : pixel_size - size;

      if (best < 0 || distance < best_distance ||
          (distance == best_distance &&
           size < face->available_sizes[best].y_ppem / 64.0))
        {
          best = i;
          best_distance = distance;
        }
    }

  return best;
}

GdkFTFont *
_gdk_ft_font_open (const gchar        *path,
                   gint                index,
                   gdouble             pixel_size,
                   const GdkFTOptions *options)
{
  GdkFTFontPrivate *private;
  GdkFTOptions defaults;
  FT_Face face;
  gchar *key;

  g_return_val_if_fail (path != NULL, NULL);
  g_return_val_if_fail (pixel_size > 0, NULL);

  if (!gdk_ft_init ())
    return NULL;

  if (!options)
    {
      _gdk_ft_options_init (&defaults);
      options = &defaults;
    }

  key = gdk_ft_font_key (path, index, pixel_size, options);
  private = g_hash_table_lookup (font_cache, key);
  if (private)
    {
      g_free (key);
      private->ref_count++;
      return (GdkFTFont *) private;
    }

  if (FT_New_Face (ft_library, path, index, &face) != 0)
    {
      g_free (key);
      return NULL;
    }

  if (FT_IS_SCALABLE (face))
    {
      if (FT_Set_Char_Size (face, 0, (FT_F26Dot6) (pixel_size * 64.0 + 0.5),
                            72, 72) != 0)
        {
          FT_Done_Face (face);
          g_free (key);
          return NULL;
        }
    }
  else
    {
      /*
       * A bitmap font is drawn at the size it was drawn at, never scaled:
       * the strike nearest the size asked for, the smaller of two equally
       * near, as the X server chose. Characters are looked up in
       * Unicode, so a face in some other encoding cannot be used.
       */
      gint strike = gdk_ft_nearest_strike (face, pixel_size);

      if (strike < 0 ||
          FT_Select_Charmap (face, FT_ENCODING_UNICODE) != 0 ||
          FT_Select_Size (face, strike) != 0)
        {
          FT_Done_Face (face);
          g_free (key);
          return NULL;
        }
      pixel_size = PIXELS_ROUND (face->available_sizes[strike].y_ppem);
    }

  private = g_new0 (GdkFTFontPrivate, 1);
  private->ref_count = 1;
  private->key = key;
  private->face = face;
  private->bitmap = !FT_IS_SCALABLE (face);
  private->glyphs = g_hash_table_new (g_direct_hash, g_direct_equal);
  private->mono_glyphs = g_hash_table_new (g_direct_hash, g_direct_equal);
  gdk_ft_font_set_flags (private, options);

  /*
   * Round outwards: a line has to hold every glyph the face draws, and
   * GdkFont reports its metrics in whole pixels.
   */
  private->font.pixel_size = pixel_size;
  private->font.ascent = PIXELS_CEIL (face->size->metrics.ascender);
  private->font.descent = PIXELS_CEIL (-face->size->metrics.descender);
  if (private->font.ascent < 1)
    private->font.ascent = 1;
  if (private->font.descent < 0)
    private->font.descent = 0;
  private->font.family = g_strdup (face->family_name ? face->family_name : "");
  private->font.style = g_strdup (face->style_name ? face->style_name : "");
  private->font.bitmap = private->bitmap;

  g_hash_table_insert (font_cache, private->key, private);

  return (GdkFTFont *) private;
}

GdkFTFont *
_gdk_ft_font_ref (GdkFTFont *font)
{
  GdkFTFontPrivate *private = (GdkFTFontPrivate *) font;

  g_return_val_if_fail (font != NULL, NULL);

  private->ref_count++;

  return font;
}

static void
gdk_ft_glyph_free (gpointer key,
                   gpointer value,
                   gpointer data)
{
  GdkFTGlyph *glyph = value;

  if (glyph == &no_glyph)
    return;

  g_free (glyph->coverage);
  g_free (glyph);
}

void
_gdk_ft_font_unref (GdkFTFont *font)
{
  GdkFTFontPrivate *private = (GdkFTFontPrivate *) font;

  g_return_if_fail (font != NULL);
  g_return_if_fail (private->ref_count > 0);

  if (--private->ref_count > 0)
    return;

  g_hash_table_remove (font_cache, private->key);

  g_hash_table_foreach (private->glyphs, gdk_ft_glyph_free, NULL);
  g_hash_table_destroy (private->glyphs);
  if (private->notdef)
    gdk_ft_glyph_free (NULL, private->notdef, NULL);
  g_hash_table_foreach (private->mono_glyphs, gdk_ft_glyph_free, NULL);
  g_hash_table_destroy (private->mono_glyphs);
  if (private->mono_notdef)
    gdk_ft_glyph_free (NULL, private->mono_notdef, NULL);

  FT_Done_Face (private->face);
  g_free (private->font.family);
  g_free (private->font.style);
  g_free (private->key);
  g_free (private);
}

/*
 * Copy what FreeType drew into coverage, one byte a pixel. The bitmap
 * is read the way its pitch says, which is negative for rows that run
 * upwards, and only as far as its pitch says a row goes.
 */
static gboolean
gdk_ft_glyph_copy_bitmap (GdkFTGlyph      *glyph,
                          const FT_Bitmap *bitmap)
{
  gint width = bitmap->width;
  gint height = bitmap->rows;
  gint pitch = bitmap->pitch < 0 ? -bitmap->pitch : bitmap->pitch;
  gint row, col;

  if (width <= 0 || height <= 0)
    return TRUE;

  switch (bitmap->pixel_mode)
    {
    case FT_PIXEL_MODE_GRAY:
      if (width > pitch)
        return FALSE;
      break;
    case FT_PIXEL_MODE_MONO:
      if ((width + 7) / 8 > pitch)
        return FALSE;
      break;
    default:
      return FALSE;
    }

  glyph->width = width;
  glyph->height = height;
  glyph->coverage = g_malloc ((gsize) width * height);

  for (row = 0; row < height; row++)
    {
      const guchar *src;
      guchar *dst = glyph->coverage + (gsize) row * width;

      if (bitmap->pitch >= 0)
        src = bitmap->buffer + (gsize) row * pitch;
      else
        src = bitmap->buffer + (gsize) (height - 1 - row) * pitch;

      if (bitmap->pixel_mode == FT_PIXEL_MODE_GRAY)
        {
          if (bitmap->num_grays == 256)
            memcpy (dst, src, width);
          else
            for (col = 0; col < width; col++)
              dst[col] = src[col] * 255 / (bitmap->num_grays - 1);
        }
      else
        for (col = 0; col < width; col++)
          dst[col] = (src[col >> 3] & (0x80 >> (col & 7))) ? 255 : 0;
    }

  return TRUE;
}

static GdkFTGlyph *
gdk_ft_glyph_render (GdkFTFontPrivate *private,
                     FT_UInt           index,
                     gboolean          mono)
{
  FT_Face face = private->face;
  FT_GlyphSlot slot;
  GdkFTGlyph *glyph;

  glyph = g_new0 (GdkFTGlyph, 1);

  if (FT_Load_Glyph (face, index,
                     private->bitmap ? FT_LOAD_DEFAULT :
                     mono ? private->mono_load_flags : private->load_flags) != 0)
    return glyph;

  slot = face->glyph;

  /*
   * A face made up for one the family does not have: sheared for
   * italic, thickened for bold, after hinting so the stems it thickens
   * are already on the pixel grid. The advance takes the emboldening
   * and not the shear, so a made up italic sets the same width as the
   * roman it came from.
   */
  if (slot->format == FT_GLYPH_FORMAT_OUTLINE)
    {
      if (private->transform)
        FT_Outline_Transform (&slot->outline, &private->matrix);
      if (private->embolden)
        FT_GlyphSlot_Embolden (slot);
    }

  /*
   * Whole pixel advances, so that the width of a string is the sum of
   * the widths of its characters.
   */
  glyph->advance = PIXELS_ROUND (slot->advance.x);
  if (glyph->advance < 0)
    glyph->advance = 0;

  if (slot->format != FT_GLYPH_FORMAT_BITMAP &&
      FT_Render_Glyph (slot, mono ? FT_RENDER_MODE_MONO
                                  : private->render_mode) != 0)
    return glyph;

  if (!gdk_ft_glyph_copy_bitmap (glyph, &slot->bitmap))
    {
      g_free (glyph->coverage);
      glyph->coverage = NULL;
      glyph->width = glyph->height = 0;
      return glyph;
    }

  glyph->left = slot->bitmap_left;
  glyph->top = slot->bitmap_top;

  return glyph;
}

static const GdkFTGlyph *
gdk_ft_font_glyph_for (GdkFTFont *font,
                       guint32    codepoint,
                       gboolean   mono)
{
  GdkFTFontPrivate *private = (GdkFTFontPrivate *) font;
  GHashTable *glyphs = mono ? private->mono_glyphs : private->glyphs;
  GdkFTGlyph *glyph;
  FT_UInt index;

  glyph = g_hash_table_lookup (glyphs, GUINT_TO_POINTER (codepoint));
  if (glyph)
    return glyph == &no_glyph ? NULL : glyph;

  index = FT_Get_Char_Index (private->face, codepoint);
  glyph = index ? gdk_ft_glyph_render (private, index, mono) : &no_glyph;

  g_hash_table_insert (glyphs, GUINT_TO_POINTER (codepoint), glyph);

  return glyph == &no_glyph ? NULL : glyph;
}

const GdkFTGlyph *
_gdk_ft_font_glyph (GdkFTFont *font,
                    guint32    codepoint)
{
  g_return_val_if_fail (font != NULL, NULL);

  return gdk_ft_font_glyph_for (font, codepoint, FALSE);
}

static const GdkFTGlyph *
gdk_ft_font_notdef (GdkFTFont *font,
                    gboolean   mono)
{
  GdkFTFontPrivate *private = (GdkFTFontPrivate *) font;
  GdkFTGlyph **notdef = mono ? &private->mono_notdef : &private->notdef;

  if (!*notdef)
    *notdef = gdk_ft_glyph_render (private, 0, mono);

  return *notdef;
}


/*
 * Sets
 */

GdkFTSet *
_gdk_ft_set_new (GdkFTFont         *font,
                 GdkFTFallbackFunc  fallback,
                 gpointer           data,
                 GDestroyNotify     destroy)
{
  GdkFTSetPrivate *private;

  g_return_val_if_fail (font != NULL, NULL);

  private = g_new0 (GdkFTSetPrivate, 1);
  private->primary = _gdk_ft_font_ref (font);
  private->fallback = fallback;
  private->fallback_data = data;
  private->fallback_destroy = destroy;
  private->fallbacks = g_hash_table_new (g_direct_hash, g_direct_equal);

  private->set.ascent = font->ascent;
  private->set.descent = font->descent;

  return (GdkFTSet *) private;
}

void
_gdk_ft_set_free (GdkFTSet *set)
{
  GdkFTSetPrivate *private = (GdkFTSetPrivate *) set;
  GSList *l;

  g_return_if_fail (set != NULL);

  for (l = private->held; l; l = l->next)
    _gdk_ft_font_unref (l->data);
  g_slist_free (private->held);
  g_hash_table_destroy (private->fallbacks);

  if (private->fallback_destroy)
    private->fallback_destroy (private->fallback_data);

  _gdk_ft_font_unref (private->primary);
  g_free (private);
}

GdkFTFont *
_gdk_ft_set_primary (GdkFTSet *set)
{
  g_return_val_if_fail (set != NULL, NULL);

  return ((GdkFTSetPrivate *) set)->primary;
}

/* The face in the set with this character, asking the fallback once. */
static GdkFTFont *
gdk_ft_set_fallback_for (GdkFTSetPrivate *private,
                         guint32          codepoint)
{
  GdkFTFont *font;

  font = g_hash_table_lookup (private->fallbacks, GUINT_TO_POINTER (codepoint));
  if (font)
    return font == (GdkFTFont *) &no_font ? NULL : font;

  font = private->fallback ?
    private->fallback (private->fallback_data, codepoint) : NULL;

  if (font && !_gdk_ft_font_glyph (font, codepoint))
    {
      _gdk_ft_font_unref (font);
      font = NULL;
    }

  if (font)
    {
      /* The fallback hands out a reference each time; keep one. */
      if (g_slist_find (private->held, font))
        _gdk_ft_font_unref (font);
      else
        private->held = g_slist_prepend (private->held, font);
    }

  g_hash_table_insert (private->fallbacks, GUINT_TO_POINTER (codepoint),
                       font ? font : (GdkFTFont *) &no_font);

  return font;
}

/*
 * A byte that went through a signed char on its way to being a wide
 * character arrives as 0xffffff80 to 0xffffffff. Old programs do this
 * when a conversion fails -- XMMS copies the bytes of a title it cannot
 * convert one by one -- and no such value is a character, so it is read
 * as the Latin-1 byte it was, as the decoder reads a byte that is not
 * valid text. A byte the decoder escaped is drawn the same way.
 */
static guint32
gdk_ft_codepoint (guint32 codepoint)
{
  if (codepoint >= 0xffffff80 ||
      (codepoint >= GDK_FT_ESCAPE_BASE + 0x80 &&
       codepoint <= GDK_FT_ESCAPE_BASE + 0xff))
    return codepoint & 0xff;

  return codepoint;
}

/*
 * The glyph a character is drawn with: from the face that has it, or
 * the first face's missing glyph box. Control characters are drawn as
 * nothing, as they were by the X fonts. Never NULL.
 */
static const GdkFTGlyph *
gdk_ft_set_glyph_for (GdkFTSetPrivate *private,
                      guint32          codepoint,
                      gboolean         mono)
{
  const GdkFTGlyph *glyph;
  GdkFTFont *font;

  codepoint = gdk_ft_codepoint (codepoint);

  /* C0 and C1 controls: the X fonts drew nothing for them either. */
  if (codepoint < 0x20 || (codepoint >= 0x7f && codepoint < 0xa0))
    return &empty_glyph;

  glyph = gdk_ft_font_glyph_for (private->primary, codepoint, mono);
  if (glyph)
    return glyph;

  font = gdk_ft_set_fallback_for (private, codepoint);
  if (font)
    glyph = gdk_ft_font_glyph_for (font, codepoint, mono);
  if (glyph)
    return glyph;

  glyph = gdk_ft_font_notdef (private->primary, mono);

  return glyph ? glyph : &empty_glyph;
}

GdkFTFont *
_gdk_ft_set_font_for (GdkFTSet *set,
                      guint32   codepoint)
{
  GdkFTSetPrivate *private = (GdkFTSetPrivate *) set;

  g_return_val_if_fail (set != NULL, NULL);

  codepoint = gdk_ft_codepoint (codepoint);
  if (codepoint < 0x20 || (codepoint >= 0x7f && codepoint < 0xa0))
    return NULL;
  if (_gdk_ft_font_glyph (private->primary, codepoint))
    return private->primary;

  return gdk_ft_set_fallback_for (private, codepoint);
}

void
_gdk_ft_set_text_extents (GdkFTSet      *set,
                          const guint32 *text,
                          gint           text_length,
                          gint          *lbearing,
                          gint          *rbearing,
                          gint          *width,
                          gint          *ascent,
                          gint          *descent)
{
  gint pen = 0;
  gint left = 0, right = 0, top = 0, bottom = 0;
  gboolean ink = FALSE;
  gint i;

  g_return_if_fail (set != NULL);
  g_return_if_fail (text != NULL || text_length == 0);

  for (i = 0; i < text_length; i++)
    {
      const GdkFTGlyph *glyph =
        gdk_ft_set_glyph_for ((GdkFTSetPrivate *) set, text[i], FALSE);

      if (glyph->width > 0 && glyph->height > 0)
        {
          gint l = pen + glyph->left;
          gint r = l + glyph->width;
          gint t = glyph->top;
          gint b = glyph->top - glyph->height;

          if (!ink)
            {
              left = l;
              right = r;
              top = t;
              bottom = b;
              ink = TRUE;
            }
          else
            {
              left = MIN (left, l);
              right = MAX (right, r);
              top = MAX (top, t);
              bottom = MIN (bottom, b);
            }
        }

      pen += glyph->advance;
    }

  if (lbearing)
    *lbearing = left;
  if (rbearing)
    *rbearing = right;
  if (width)
    *width = pen;
  if (ascent)
    *ascent = top;
  if (descent)
    *descent = -bottom;
}

gboolean
_gdk_ft_set_render (GdkFTSet      *set,
                    GdkFTSet      *metrics,
                    gint           scale,
                    const guint32 *text,
                    gint           text_length,
                    gboolean       mono,
                    GdkFTImage    *image)
{
  GdkFTSetPrivate *private = (GdkFTSetPrivate *) set;
  GdkFTSetPrivate *mprivate = (GdkFTSetPrivate *) metrics;
  const GdkFTGlyph **glyphs;
  gint *pens, *mags;
  gint left = 0, right = 0, top = 0, bottom = 0;
  gboolean ink = FALSE;
  gint pen = 0;
  gint i;

  g_return_val_if_fail (set != NULL, FALSE);
  g_return_val_if_fail (image != NULL, FALSE);

  memset (image, 0, sizeof *image);

  if (text_length <= 0)
    return FALSE;
  if (!metrics || scale < 1)
    {
      mprivate = private;
      scale = 1;
    }

  glyphs = g_new (const GdkFTGlyph *, text_length);
  pens = g_new (gint, text_length);
  mags = g_new (gint, text_length);

  /*
   * The pen always moves by the antialiased glyphs' advances of the
   * set the text was measured with, times the scale, so one bit text,
   * and text at any scale, lines up with the widths the widgets worked
   * out. Only what is drawn at each position differs.
   */
  for (i = 0; i < text_length; i++)
    {
      const GdkFTGlyph *mglyph = gdk_ft_set_glyph_for (mprivate, text[i], FALSE);
      const GdkFTGlyph *glyph;
      gint mag = 1;

      if (mprivate == private)
        glyph = mono ? gdk_ft_set_glyph_for (private, text[i], TRUE) : mglyph;
      else
        {
          GdkFTFont *mfont = _gdk_ft_set_font_for (metrics, text[i]);

          if (mfont && mfont->bitmap)
            {
              glyph = gdk_ft_set_glyph_for (mprivate, text[i], mono);
              mag = scale;
            }
          else
            glyph = gdk_ft_set_glyph_for (private, text[i], mono);
        }

      glyphs[i] = glyph;
      pens[i] = pen;
      mags[i] = mag;

      if (glyph->width > 0 && glyph->height > 0)
        {
          gint l = pen + glyph->left * mag;
          gint r = l + glyph->width * mag;
          gint t = glyph->top * mag;
          gint b = t - glyph->height * mag;

          if (!ink)
            {
              left = l;
              right = r;
              top = t;
              bottom = b;
              ink = TRUE;
            }
          else
            {
              left = MIN (left, l);
              right = MAX (right, r);
              top = MAX (top, t);
              bottom = MIN (bottom, b);
            }
        }

      pen += mglyph->advance * scale;
    }

  if (!ink)
    {
      g_free (glyphs);
      g_free (pens);
      g_free (mags);
      return FALSE;
    }

  image->x = left;
  image->y = -top;
  image->width = right - left;
  image->height = top - bottom;
  /* Rows padded to 32 bits, as X wants them sent: they go as they are. */
  image->stride = (image->width + 3) & ~3;
  image->pixels = g_malloc0 ((gsize) image->stride * image->height);

  for (i = 0; i < text_length; i++)
    {
      const GdkFTGlyph *glyph = glyphs[i];
      gint mag = mags[i];
      gint x0 = pens[i] + glyph->left * mag - image->x;
      gint y0 = -glyph->top * mag - image->y;
      gint row, col;

      for (row = 0; row < glyph->height * mag; row++)
        {
          const guchar *src = glyph->coverage + (gsize) (row / mag) * glyph->width;
          guchar *dst = image->pixels + (gsize) (y0 + row) * image->stride + x0;

          /*
           * Where glyphs overlap, cover what either covers, as drawing
           * one over the other in the same colour would.
           */
          for (col = 0; col < glyph->width * mag; col++)
            {
              guint a = dst[col], b = src[col / mag];

              dst[col] = a + b - (a * b + 127) / 255;
            }
        }
    }

  g_free (glyphs);
  g_free (pens);
  g_free (mags);

  return TRUE;
}

void
_gdk_ft_image_free (GdkFTImage *image)
{
  g_return_if_fail (image != NULL);

  g_free (image->pixels);
  memset (image, 0, sizeof *image);
}


/*
 * Decoding. This part needs nothing from FreeType.
 */

gint
_gdk_ft_decode_latin1 (const gchar *text,
                       gint         text_length,
                       guint32     *result)
{
  gint i;

  g_return_val_if_fail (text != NULL || text_length == 0, 0);
  g_return_val_if_fail (result != NULL || text_length == 0, 0);

  for (i = 0; i < text_length; i++)
    result[i] = (guchar) text[i];

  return text_length;
}

/* One UTF-8 sequence, or -1 if the bytes do not make one. */
static gint
decode_utf8 (const guchar *p,
             gint          length,
             guint32      *result)
{
  guint32 cp;
  gint extra, i;

  if (p[0] < 0x80)
    {
      *result = p[0];
      return 1;
    }
  else if ((p[0] & 0xe0) == 0xc0)
    {
      cp = p[0] & 0x1f;
      extra = 1;
    }
  else if ((p[0] & 0xf0) == 0xe0)
    {
      cp = p[0] & 0x0f;
      extra = 2;
    }
  else if ((p[0] & 0xf8) == 0xf0)
    {
      cp = p[0] & 0x07;
      extra = 3;
    }
  else
    return -1;

  if (extra >= length)
    return -1;

  for (i = 1; i <= extra; i++)
    {
      if ((p[i] & 0xc0) != 0x80)
        return -1;
      cp = (cp << 6) | (p[i] & 0x3f);
    }

  /* Overlong forms, surrogates and values past Unicode are not UTF-8. */
  if ((extra == 1 && cp < 0x80) ||
      (extra == 2 && cp < 0x800) ||
      (extra == 3 && cp < 0x10000) ||
      (cp >= 0xd800 && cp <= 0xdfff) ||
      cp > 0x10ffff)
    return -1;

  *result = cp;

  return extra + 1;
}

/*
 * Whether the locale names an encoding of its own. UTF-8 does not
 * count, and neither does the C locale's ASCII: both are read as UTF-8.
 * What is left -- ISO-8859-15, KOI8-R, EUC-JP and the like -- is read
 * as the locale says, because programs running in one hand GDK text
 * in it.
 */
static gboolean
locale_is_legacy (void)
{
  const gchar *codeset = nl_langinfo (CODESET);

  if (!codeset || !*codeset)
    return FALSE;

  return !(g_strcasecmp (codeset, "UTF-8") == 0 ||
           g_strcasecmp (codeset, "utf8") == 0 ||
           g_strcasecmp (codeset, "ANSI_X3.4-1968") == 0 ||
           g_strcasecmp (codeset, "ASCII") == 0 ||
           g_strcasecmp (codeset, "US-ASCII") == 0 ||
           g_strcasecmp (codeset, "646") == 0);
}

/* A byte that is not valid text, as a character. */
static guint32
invalid_byte (guchar   byte,
              gboolean escape)
{
  return escape && byte >= 0x80 ? GDK_FT_ESCAPE_BASE + byte : byte;
}

gint
_gdk_ft_decode_text (const gchar *text,
                     gint         text_length,
                     guint32     *result,
                     gboolean     escape)
{
  const guchar *p = (const guchar *) text;
  gint left = text_length;
  gint n = 0;

  g_return_val_if_fail (text != NULL || text_length == 0, 0);
  g_return_val_if_fail (result != NULL || text_length == 0, 0);

  if (!locale_is_legacy ())
    {
      while (left > 0)
        {
          gint used = decode_utf8 (p, left, &result[n]);

          if (used < 0)
            {
              result[n] = invalid_byte (*p, escape);
              used = 1;
            }

          n++;
          p += used;
          left -= used;
        }

      return n;
    }

#ifdef __STDC_ISO_10646__
  {
    mbstate_t state;

    memset (&state, 0, sizeof state);

    while (left > 0)
      {
        wchar_t wc;
        size_t used = mbrtowc (&wc, (const char *) p, left, &state);

        if (used == (size_t) -1 || used == (size_t) -2)
          {
            result[n++] = invalid_byte (*p++, escape);
            left--;
            memset (&state, 0, sizeof state);
            continue;
          }

        if (used == 0)
          {
            /* An embedded nul is a character like any other. */
            result[n++] = 0;
            used = 1;
          }
        else
          result[n++] = wc;

        p += used;
        left -= used;
      }

    return n;
  }
#else
  return _gdk_ft_decode_latin1 (text, text_length, result);
#endif
}

gint
_gdk_ft_encode_utf8 (guint32  c,
                     gchar   *out)
{
  if (c < 0x80)
    {
      out[0] = c;
      return 1;
    }
  if (c < 0x800)
    {
      out[0] = 0xc0 | (c >> 6);
      out[1] = 0x80 | (c & 0x3f);
      return 2;
    }
  if (c < 0x10000)
    {
      out[0] = 0xe0 | (c >> 12);
      out[1] = 0x80 | ((c >> 6) & 0x3f);
      out[2] = 0x80 | (c & 0x3f);
      return 3;
    }
  out[0] = 0xf0 | (c >> 18);
  out[1] = 0x80 | ((c >> 12) & 0x3f);
  out[2] = 0x80 | ((c >> 6) & 0x3f);
  out[3] = 0x80 | (c & 0x3f);
  return 4;
}

gchar *
_gdk_ft_encode_text (const guint32 *text,
                     gint           text_length)
{
  gboolean legacy = locale_is_legacy ();
  gsize room = (gsize) text_length * MAX (4, MB_CUR_MAX) + 1;
  gchar *result = g_malloc (room);
  gchar *q = result;
  mbstate_t state;
  gint i;

  g_return_val_if_fail (text != NULL || text_length == 0, NULL);

  memset (&state, 0, sizeof state);

  for (i = 0; i < text_length; i++)
    {
      guint32 c = text[i];

      /*
       * An escaped byte, or one that came in through a signed char, goes
       * back out as the byte it was, so text that was never valid comes
       * back exactly as it went in.
       */
      if ((c >= GDK_FT_ESCAPE_BASE + 0x80 && c <= GDK_FT_ESCAPE_BASE + 0xff) ||
          c >= 0xffffff80)
        *q++ = (gchar) (c & 0xff);
      else if (!legacy)
        {
          if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
            *q++ = '?';
          else
            q += _gdk_ft_encode_utf8 (c, q);
        }
      else
        {
#ifdef __STDC_ISO_10646__
          size_t used = wcrtomb (q, (wchar_t) c, &state);

          if (used == (size_t) -1)
            {
              *q++ = '?';
              memset (&state, 0, sizeof state);
            }
          else
            q += used;
#else
          *q++ = c < 0x100 ? (gchar) c : '?';
#endif
        }
    }

  *q = '\0';

  return result;
}
