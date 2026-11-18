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

/* testftfont - the scalable font layer, without a display.
 *
 *   testftfont [-s PIXELS] [-m] [-b] [-l] [-o OUT.pgm] FONT[,FALLBACK...] TEXT...
 *   testftfont [-d DPI] [-b] [-l] [-o OUT.pgm] -n NAME TEXT...
 *
 * Opens FONT at PIXELS (13 by default), with the files after the commas
 * as the faces it falls back on -- or, with -n, whatever the font name
 * NAME resolves to at DPI (96 by default), falling back as the font
 * configuration says. For each TEXT it prints the metrics and the faces
 * the characters came from, and checks the things the widgets depend on:
 *
 *   - the width of a string is the sum of the widths of its characters;
 *   - the rendered image is exactly as big as the ink extents say.
 *
 * TEXT is read as GDK reads text -- UTF-8, or the locale's encoding if
 * it has one of its own -- or as Latin-1 with -l. -m
 * rasterises without antialiasing; -b renders as the one bit path
 * draws, glyphs hinted for one bit. -o writes what was rendered, one
 * line per TEXT, as a PGM. It also checks the decoders against a fixed
 * list first. Exits non-zero if any check fails.
 */

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gdk.h"
#include "gdkftfont.h"

static gint failures;

#define CHECK(cond, ...) \
  G_STMT_START { \
    if (!(cond)) { \
      failures++; \
      printf ("FAIL: "); \
      printf (__VA_ARGS__); \
      printf ("\n"); \
    } \
  } G_STMT_END

static void
check_decode (void)
{
  static const struct {
    const gchar *bytes;
    guint32 expect[8];
    gint n;
  } utf8[] = {
    { "abc",                 { 'a', 'b', 'c' }, 3 },
    { "Bj\xc3\xb6rk",        { 'B', 'j', 0xf6, 'r', 'k' }, 5 },
    { "R\xf3s",              { 'R', 0xf3, 's' }, 3 },          /* Latin-1 */
    { "\xe5\x9d\x82",        { 0x5742 }, 1 },
    { "\xf0\x9f\x8e\xb5",    { 0x1f3b5 }, 1 },
    { "\xc0\xaf",            { 0xc0, 0xaf }, 2 },              /* overlong */
    { "\xed\xa0\x80",        { 0xed, 0xa0, 0x80 }, 3 },        /* surrogate */
    { "\xe5\x9d",            { 0xe5, 0x9d }, 2 },              /* cut short */
  };
  guint32 out[16];
  gsize i;
  gint n, j, pass;

  n = _gdk_ft_decode_latin1 ("\xe9t\xe9", 3, out);
  CHECK (n == 3 && out[0] == 0xe9 && out[1] == 't' && out[2] == 0xe9,
         "latin-1 decoding");

  /*
   * UTF-8 in the C locale, which names no encoding of its own, and in a
   * UTF-8 one; and back again, escaped, to exactly the same bytes.
   */
  for (pass = 0; pass < 2; pass++)
    {
      if (pass == 0)
        setlocale (LC_CTYPE, "C");
      else if (!setlocale (LC_CTYPE, "C.UTF-8") &&
               !setlocale (LC_CTYPE, "en_US.UTF-8"))
        {
          printf ("no UTF-8 locale: only the C locale checked\n");
          break;
        }

      for (i = 0; i < sizeof utf8 / sizeof utf8[0]; i++)
        {
          const gchar *bytes = utf8[i].bytes;
          gchar *back;

          n = _gdk_ft_decode_text (bytes, strlen (bytes), out, FALSE);
          CHECK (n == utf8[i].n, "utf-8 case %d: %d characters, wanted %d",
                 (int) i, n, utf8[i].n);
          for (j = 0; j < n && j < utf8[i].n; j++)
            CHECK (out[j] == utf8[i].expect[j], "utf-8 case %d, character %d: "
                   "U+%04X, wanted U+%04X", (int) i, j, out[j],
                   utf8[i].expect[j]);

          n = _gdk_ft_decode_text (bytes, strlen (bytes), out, TRUE);
          back = _gdk_ft_encode_text (out, n);
          CHECK (strcmp (back, bytes) == 0, "utf-8 case %d does not come back "
                 "the same through escaping", (int) i);
          g_free (back);
        }
    }

  n = _gdk_ft_decode_text ("R\xf3s", 3, out, TRUE);
  CHECK (n == 3 && out[1] == GDK_FT_ESCAPE_BASE + 0xf3,
         "an invalid byte is not escaped");
}

/* Fall back on the files after the first, in order. */
typedef struct
{
  gchar **paths;
  gdouble pixel_size;
  GdkFTOptions options;
} Fallbacks;

static GdkFTFont *
fallback (gpointer data,
          guint32  codepoint)
{
  Fallbacks *fb = data;
  gint i;

  for (i = 1; fb->paths[i]; i++)
    {
      GdkFTFont *font = _gdk_ft_font_open (fb->paths[i], 0, fb->pixel_size,
                                           &fb->options);

      if (font && _gdk_ft_font_glyph (font, codepoint))
        return font;
      if (font)
        _gdk_ft_font_unref (font);
    }

  return NULL;
}

static void
fallbacks_free (gpointer data)
{
  Fallbacks *fb = data;

  g_strfreev (fb->paths);
  g_free (fb);
}

static void
write_pgm (const gchar *path,
           GdkFTImage  *images,
           gint        *pens,
           gint         n,
           gint         line_height,
           gint         ascent)
{
  gint width = 1, height = line_height * n;
  guchar *canvas;
  FILE *f;
  gint i, row, col;

  for (i = 0; i < n; i++)
    if (images[i].pixels)
      width = MAX (width, 4 + images[i].x + images[i].width);
  width += 4;

  canvas = g_malloc0 ((gsize) width * height);

  for (i = 0; i < n; i++)
    {
      GdkFTImage *im = &images[i];
      gint ox = 4 + im->x, oy = i * line_height + ascent + im->y;

      for (row = 0; row < im->height; row++)
        for (col = 0; col < im->width; col++)
          if (oy + row >= 0 && oy + row < height && ox + col >= 0)
            canvas[(oy + row) * width + ox + col] =
              im->pixels[row * im->stride + col];

      /* the advance, as a tick under the line */
      if (pens[i] + 4 < width)
        canvas[(i * line_height + line_height - 1) * width + pens[i] + 4] = 128;
    }

  /* dark text on white, as it would be read */
  for (i = 0; i < width * height; i++)
    canvas[i] = 255 - canvas[i];

  f = fopen (path, "wb");
  if (!f)
    {
      perror (path);
      failures++;
      g_free (canvas);
      return;
    }
  fprintf (f, "P5\n%d %d\n255\n", width, height);
  fwrite (canvas, 1, (gsize) width * height, f);
  fclose (f);
  g_free (canvas);
}

int
main (int argc, char **argv)
{
  gdouble pixel_size = 13;
  gdouble dpi = 96;
  const gchar *name = NULL;
  gboolean latin1 = FALSE;
  gboolean bits = FALSE;
  const gchar *out = NULL;
  GdkFTOptions options;
  Fallbacks *fb;
  GdkFTFont *font;
  GdkFTSet *set;
  GdkFTImage *images;
  gint *pens;
  gint i, first, n_lines;

  _gdk_ft_options_init (&options);

  for (i = 1; i < argc && argv[i][0] == '-'; i++)
    {
      if (strcmp (argv[i], "-s") == 0 && i + 1 < argc)
        pixel_size = atof (argv[++i]);
      else if (strcmp (argv[i], "-m") == 0)
        options.antialias = FALSE;
      else if (strcmp (argv[i], "-l") == 0)
        latin1 = TRUE;
      else if (strcmp (argv[i], "-b") == 0)
        bits = TRUE;
      else if (strcmp (argv[i], "-o") == 0 && i + 1 < argc)
        out = argv[++i];
      else if (strcmp (argv[i], "-d") == 0 && i + 1 < argc)
        dpi = atof (argv[++i]);
      else if (strcmp (argv[i], "-n") == 0 && i + 1 < argc)
        name = argv[++i];
      else
        break;
    }

  check_decode ();

  setlocale (LC_ALL, "");

  if (name)
    {
      set = _gdk_ft_set_open_name (name, dpi, 1);
      if (!set)
        {
          printf ("\"%s\": nothing to draw it with\n", name);
          return 1;
        }
      font = _gdk_ft_set_primary (set);
      printf ("\"%s\" is %s %s at %gpx: ascent %d, descent %d\n", name,
              font->family, font->style, font->pixel_size,
              set->ascent, set->descent);
      first = i;
      goto measure;
    }

  if (i >= argc)
    {
      printf ("%s\n", failures ? "decoding FAILED" : "decoding ok");
      return failures ? 1 : 0;
    }

  fb = g_new0 (Fallbacks, 1);
  fb->paths = g_strsplit (argv[i], ",", 0);
  fb->pixel_size = pixel_size;
  fb->options = options;

  font = _gdk_ft_font_open (fb->paths[0], 0, pixel_size, &options);
  if (!font)
    {
      printf ("%s: cannot open, or has no outlines\n", fb->paths[0]);
      fallbacks_free (fb);
      return 1;
    }

  printf ("%s %s at %.1fpx: ascent %d, descent %d\n",
          font->family, font->style, font->pixel_size,
          font->ascent, font->descent);

  set = _gdk_ft_set_new (font, fallback, fb, fallbacks_free);
  _gdk_ft_font_unref (font);

  first = i + 1;

 measure:
  n_lines = argc - first;
  images = g_new0 (GdkFTImage, MAX (n_lines, 1));
  pens = g_new0 (gint, MAX (n_lines, 1));

  for (i = first; i < argc; i++)
    {
      gint len = strlen (argv[i]);
      guint32 *text = g_new (guint32, len + 1);
      gint n, j, sum = 0;
      gint lb, rb, width, asc, desc;
      GdkFTImage *im = &images[i - first];

      n = latin1 ? _gdk_ft_decode_latin1 (argv[i], len, text)
                 : _gdk_ft_decode_text (argv[i], len, text, FALSE);

      _gdk_ft_set_text_extents (set, text, n, &lb, &rb, &width, &asc, &desc);
      for (j = 0; j < n; j++)
        {
          gint w;

          _gdk_ft_set_text_extents (set, &text[j], 1, NULL, NULL, &w,
                                    NULL, NULL);
          sum += w;
        }

      printf ("\"%s\": %d characters, width %d, ink %d..%d, "
              "up %d, down %d\n", argv[i], n, width, lb, rb, asc, desc);

      /* the faces the characters came from, in order of first use */
      {
        GSList *used = NULL, *l;

        for (j = 0; j < n; j++)
          {
            GdkFTFont *f = _gdk_ft_set_font_for (set, text[j]);

            if (f && !g_slist_find (used, f))
              used = g_slist_append (used, f);
          }
        printf ("    from");
        for (l = used; l; l = l->next)
          printf (" [%s %s]", ((GdkFTFont *) l->data)->family,
                  ((GdkFTFont *) l->data)->style);
        printf ("\n");
        g_slist_free (used);
      }
      CHECK (width == sum, "width %d is not the sum of the characters' %d",
             width, sum);

      if (_gdk_ft_set_render (set, set, 1, text, n, bits, im))
        {
          /* one bit glyphs are hinted differently, so their ink differs */
          CHECK (bits || (im->x == lb && im->width == rb - lb &&
                          im->y == -asc && im->height == asc + desc),
                 "image %d,%d %dx%d does not match the extents",
                 im->x, im->y, im->width, im->height);
        }
      else
        CHECK (rb == lb, "nothing rendered for a run with ink");

      pens[i - first] = width;
      g_free (text);
    }

  if (out && n_lines > 0)
    write_pgm (out, images, pens, n_lines, set->ascent + set->descent + 2,
               set->ascent + 1);

  for (i = 0; i < n_lines; i++)
    _gdk_ft_image_free (&images[i]);
  g_free (images);
  g_free (pens);
  _gdk_ft_set_free (set);

  printf ("%s\n", failures ? "FAILED" : "ok");

  return failures ? 1 : 0;
}
