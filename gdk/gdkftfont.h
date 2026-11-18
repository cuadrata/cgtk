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
 * Scalable fonts. Internal to GDK; not installed.
 *
 * Three layers, each usable without the ones above it:
 *
 *   gdkftfont.c  A face at a size, rasterised by FreeType into 8 bit
 *                coverage, and sets of faces used as one font. Knows
 *                nothing about X or about font names.
 *   gdkftname.c  Font names, and what they resolve to. Fontconfig
 *                answers the second question, so a name means here
 *                what it means to every other program on the system.
 *   gdkftdraw.c  Putting a run of coverage into a framebuffer.
 */

#ifndef __GDK_FT_FONT_H__
#define __GDK_FT_FONT_H__

#include <gdk/gdktypes.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct _GdkFTOptions GdkFTOptions;
typedef struct _GdkFTGlyph   GdkFTGlyph;
typedef struct _GdkFTFont    GdkFTFont;
typedef struct _GdkFTSet     GdkFTSet;
typedef struct _GdkFTImage   GdkFTImage;

/*
 * How a face is rasterised. These come from the font configuration,
 * so the user's choices about hinting and antialiasing apply here as
 * they do everywhere else; _gdk_ft_options_init gives the usual ones.
 */
#define GDK_FT_HINT_NONE   0
#define GDK_FT_HINT_SLIGHT 1
#define GDK_FT_HINT_MEDIUM 2
#define GDK_FT_HINT_FULL   3

struct _GdkFTOptions
{
  gboolean antialias;
  gboolean hinting;
  gint     hint_style;
  gboolean autohint;
  gboolean embolden;      /* no bold face: thicken the regular one */
  gdouble  matrix[4];     /* xx, xy, yx, yy; a shear for a made up italic */
};

void _gdk_ft_options_init (GdkFTOptions *options);

/*
 * A glyph as coverage, one byte a pixel, 0 to 255, and where it sits
 * relative to the pen on the baseline: left is positive to the right of
 * the pen, top is positive above the baseline. A glyph with nothing to
 * draw has width and height 0 and no coverage.
 */
struct _GdkFTGlyph
{
  gint    advance;        /* whole pixels, never negative */
  gint    left;
  gint    top;
  gint    width;
  gint    height;
  guchar *coverage;
};

/*
 * A face at a size. Metrics are whole pixels, rounded outwards. A
 * bitmap face is at the size of the strike it was opened at, which is
 * the pixel size here, whatever was asked for.
 */
struct _GdkFTFont
{
  gint     ascent;
  gint     descent;
  gdouble  pixel_size;
  gchar   *family;        /* as the face names itself */
  gchar   *style;
  gboolean bitmap;
};

/*
 * Opening the same file at the same size with the same options twice
 * gives the same font, with its glyphs already rasterised. An outline
 * face is scaled to the size; a bitmap face is opened at its strike
 * nearest the size, the smaller of two as near, and drawn exactly as it
 * was drawn, never scaled or smoothed. Returns NULL for a file that
 * cannot be read, or a bitmap face with no Unicode character map.
 */
GdkFTFont        *_gdk_ft_font_open   (const gchar        *path,
                                       gint                index,
                                       gdouble             pixel_size,
                                       const GdkFTOptions *options);
GdkFTFont        *_gdk_ft_font_ref    (GdkFTFont          *font);
void              _gdk_ft_font_unref  (GdkFTFont          *font);

/* NULL when the face has no glyph for the character. */
const GdkFTGlyph *_gdk_ft_font_glyph  (GdkFTFont          *font,
                                       guint32             codepoint);

/*
 * A set: faces used as one font. The first face answers every
 * character it has. A character it does not have is passed to the
 * fallback function, which returns a face that has it (a new reference)
 * or NULL; the answer is remembered. What the fallback finds never
 * changes the set's ascent and descent, so a line does not grow the
 * first time a character from another script appears in it.
 */
typedef GdkFTFont *(*GdkFTFallbackFunc) (gpointer data,
                                         guint32  codepoint);

struct _GdkFTSet
{
  gint ascent;
  gint descent;
};

GdkFTSet         *_gdk_ft_set_new     (GdkFTFont         *font,
                                       GdkFTFallbackFunc  fallback,
                                       gpointer           data,
                                       GDestroyNotify     destroy);
void              _gdk_ft_set_free    (GdkFTSet          *set);
GdkFTFont        *_gdk_ft_set_primary (GdkFTSet          *set);

/* The face a character is drawn from; NULL when none has it. */
GdkFTFont        *_gdk_ft_set_font_for (GdkFTSet         *set,
                                        guint32           codepoint);

/*
 * Extents of a run of characters. Advances are whole pixels and there
 * is no kerning, so the width of a string is the sum of the widths of
 * its characters: GtkEntry and GtkText place the cursor by adding them
 * up. Any of the results may be NULL; a run with no ink has zero ink
 * extents.
 */
void _gdk_ft_set_text_extents (GdkFTSet      *set,
                               const guint32 *text,
                               gint           text_length,
                               gint          *lbearing,
                               gint          *rbearing,
                               gint          *width,
                               gint          *ascent,
                               gint          *descent);

/*
 * A run of characters rasterised into one coverage image, laid out the
 * way _gdk_ft_set_text_extents measures it. x and y are where the top
 * left pixel is relative to the pen origin on the baseline, y growing
 * downwards. Returns FALSE, with nothing to free, for a run with no ink.
 *
 * mono draws each glyph hinted and rasterised for one bit a pixel, for
 * drawables that can only take one: coverage is then 0 or 255, and the
 * glyphs sit where the antialiased ones would.
 *
 * metrics is the set the text was measured with, and scale how many
 * device pixels each of its pixels is: the image is in device pixels,
 * the glyphs drawn from set, which for a scale above one is the same
 * font opened that many times bigger, but the pen moves by the metrics
 * set's advances times the scale, so text drawn at any scale takes up
 * what the widgets measured. A bitmap face has no bigger size to be
 * opened at; its glyphs are enlarged instead. For scale 1, metrics is
 * set.
 */
struct _GdkFTImage
{
  gint    x;
  gint    y;
  gint    width;
  gint    height;
  gint    stride;
  guchar *pixels;
};

gboolean _gdk_ft_set_render  (GdkFTSet      *set,
                              GdkFTSet      *metrics,
                              gint           scale,
                              const guint32 *text,
                              gint           text_length,
                              gboolean       mono,
                              GdkFTImage    *image);
void     _gdk_ft_image_free  (GdkFTImage    *image);

/*
 * Text as GDK is handed it, turned into characters, and back.
 *
 * Text is UTF-8, unless the program has set a locale with an encoding
 * of its own -- ISO-8859-15, EUC-JP -- in which case it is in that. The
 * C locale has none, so a program that never sets one gets UTF-8.
 *
 * Bytes that are not valid text are read as Latin-1, which is what old
 * text that is not UTF-8 nearly always is. With escape, such a byte
 * becomes GDK_FT_ESCAPE_BASE plus the byte instead, which draws as the
 * same Latin-1 letter but encodes back to the byte it was, so text goes
 * in and out of wide characters unchanged whatever it holds. These are
 * lone low surrogates, which no real text contains.
 *
 * The decoders return the number of characters written to result,
 * which has room for text_length of them. _gdk_ft_encode_text returns
 * a newly allocated, nul terminated string.
 */
#define GDK_FT_ESCAPE_BASE 0xdc00

gint   _gdk_ft_decode_latin1 (const gchar   *text,
                              gint           text_length,
                              guint32       *result);
gint   _gdk_ft_decode_text   (const gchar   *text,
                              gint           text_length,
                              guint32       *result,
                              gboolean       escape);
gchar *_gdk_ft_encode_text   (const guint32 *text,
                              gint           text_length);

/* A character as UTF-8 at out, which has room for four bytes; returns
 * how many it took. */
gint   _gdk_ft_encode_utf8   (guint32        c,
                              gchar         *out);

/*
 * Font names (gdkftname.c).
 *
 * A name is a family, the style words that pick a face within it, and
 * a size: "Sans 10", "DejaVu Sans Bold 11", "Monospace Italic 9px". A
 * bare size is in points at the display's resolution; px makes it
 * pixels. Several families may be given, separated by commas.
 *
 * The X names keep working, because gtkrc files, saved settings and
 * programs are full of them, but they are translated rather than looked
 * up: -adobe-helvetica-bold-r-normal--*-120-*-*-p-*-iso8859-1 means Sans
 * Bold at 12 points. An X name that gives no resolution is read at 75
 * dpi, which is what it was written for, and the size is truncated to
 * whole pixels as the X server did, so a layout built around one keeps
 * its size.
 */

#define GDK_FT_SLANT_ROMAN   0
#define GDK_FT_SLANT_ITALIC  100
#define GDK_FT_SLANT_OBLIQUE 110

#define GDK_FT_XLFD_DPI 75.0

typedef struct _GdkFTName GdkFTName;

struct _GdkFTName
{
  gchar  **families;    /* NULL terminated; the generic ones are spelt
                         * sans-serif, serif and monospace */
  gint     weight;      /* 100 to 900: 400 is regular, 700 bold */
  gint     slant;
  gint     width;       /* per cent of normal */
  gboolean monospace;   /* asked for fixed width spacing */
  gdouble  pixel_size;
  gdouble  points;      /* the size as written, when it was in points;
                         * 0 when it was pixels, as X names' sizes are */
};

gboolean  _gdk_ft_name_parse     (const gchar     *name,
                                  gdouble          dpi,
                                  GdkFTName       *result);
void      _gdk_ft_name_clear     (GdkFTName       *name);
gchar    *_gdk_ft_name_to_string (const GdkFTName *name);

/*
 * The set a name resolves to, with the fonts that cover what its first
 * face does not behind it, in the order the font configuration prefers
 * them. NULL if nothing installed can be drawn. scale opens the
 * faces that many times bigger than the name says, for drawing on a
 * scaled framebuffer; the name means the same font.
 */
GdkFTSet *_gdk_ft_set_open_name  (const gchar     *name,
                                  gdouble          dpi,
                                  gint             scale);

/*
 * What is installed, for choosing among (gdkftname.c).
 *
 * _gdk_ft_list_families gives every family with outlines, sorted, or
 * only the fixed width ones. Given bitmap, it gives the families
 * installed only as bitmaps too, and a parallel array saying which they
 * are: their names are the only way to reach them.
 * _gdk_ft_list_styles gives the styles a family has, as weight, slant
 * and width with no families or size, rounded to the steps names can
 * say and sorted with the normal width first; a family that is not
 * installed, or a generic one, has the styles of what it resolves to,
 * which _gdk_ft_resolve_family names. _gdk_ft_list_sizes gives the
 * pixel sizes a bitmap family has in a style, sorted, and nothing for a
 * family with outlines, which has every size.
 *
 * _gdk_ft_style_name says a style the way a name says it -- "Bold
 * Italic", "Condensed Light", "Regular" -- and _gdk_ft_name_compose
 * writes a whole name that parses back to the same family, style and
 * size, in points or with px.
 */
gchar   **_gdk_ft_list_families  (gboolean         monospace,
                                  gboolean       **bitmap);
GdkFTName *_gdk_ft_list_styles   (const gchar     *family,
                                  gint            *n_styles);
gdouble  *_gdk_ft_list_sizes     (const gchar     *family,
                                  gint             weight,
                                  gint             slant,
                                  gint             width,
                                  gint            *n_sizes);
gchar    *_gdk_ft_resolve_family (const gchar     *family);
gchar    *_gdk_ft_style_name     (gint             weight,
                                  gint             slant,
                                  gint             width);
gchar    *_gdk_ft_name_compose   (const gchar     *family,
                                  gint             weight,
                                  gint             slant,
                                  gint             width,
                                  gdouble          size,
                                  gboolean         pixels);

/*
 * Drawing (gdkftdraw.c): a run of characters through a paint, with the
 * pen at x, y on the baseline, the way XDrawString would have drawn it
 * through the paint's GC -- its foreground, clip and function. The
 * font supplies the set for the paint's scale.
 */
struct _GdkPaint;
void    _gdk_ft_draw_text   (struct _GdkPaint *paint,
                             GdkFont       *font,
                             gint           x,
                             gint           y,
                             const guint32 *text,
                             gint           text_length);

/* What a size in points is read at: Xft.dpi if the desktop sets it, or 96. */
gdouble _gdk_ft_display_dpi (void);

/* gdkfont.c: the characters in some bytes, as a GdkFont reads them. */
gint     _gdk_font_ft_decode        (GdkFont       *font,
                                     const gchar   *text,
                                     gint           text_length,
                                     guint32       *result);
/* The font's set for drawing at a scale: its own for 1, else one opened
 * that much bigger the first time the scale is drawn at. */
GdkFTSet *_gdk_font_ft_set_at_scale (GdkFont       *font,
                                     gint           scale);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GDK_FT_FONT_H__ */
