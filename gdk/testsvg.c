/* GDK - The GIMP Drawing Kit
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

/* SVG pictures, checked by "make check": small ones are read and
 * drawn, whole, larger and in part, and their pixels are what they
 * should be, premultiplied; what is not a picture is refused with the
 * right error; more pixels than are allowed are not drawn; and
 * pictures that are damaged, cut short or far out of all bounds are
 * read or refused and drawn or not, and never anything else. It needs
 * no display: the pictures are in it, and one is written out to be
 * read back.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "gdk.h"

/* eight by four: the left half red, the right half blue and half
 * clear */
static const gchar halves[] =
  "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"4\">"
  "<rect width=\"4\" height=\"4\" fill=\"#ff0000\"/>"
  "<rect x=\"4\" width=\"4\" height=\"4\" fill=\"#0000ff\" fill-opacity=\"0.5\"/>"
  "</svg>";

/* forty across and twenty down in its own units, shown twenty by ten:
 * a disc of radius eight in the middle, green by a class of a style
 * sheet, with a yellow stroke two wide round a square beside it */
static const gchar shapes[] =
  "<?xml version=\"1.0\"?>\n"
  "<!-- a comment -->\n"
  "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"10\" viewBox=\"0 0 40 20\">\n"
  "  <style>.leaf { fill: #008000; }</style>\n"
  "  <g transform=\"translate(10, 0)\">\n"
  "    <circle class=\"leaf\" cx=\"10\" cy=\"10\" r=\"8\"/>\n"
  "  </g>\n"
  "  <path d=\"M 31 5 h 6 v 10 h -6 z\" fill=\"none\" stroke=\"#ffff00\" stroke-width=\"2\"/>\n"
  "</svg>\n";

/* ten by two, black at the left to white at the right */
static const gchar shaded[] =
  "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"2\">"
  "<defs><linearGradient id=\"g\" gradientUnits=\"userSpaceOnUse\" x1=\"0\" y1=\"0\" x2=\"10\" y2=\"0\">"
  "<stop offset=\"0\" stop-color=\"#000000\"/><stop offset=\"1\" stop-color=\"#ffffff\"/>"
  "</linearGradient></defs>"
  "<rect width=\"10\" height=\"2\" fill=\"url(#g)\"/>"
  "</svg>";

/* twelve by four, of three squares written oddly: blue, with
 * transforms that are given too few numbers or none, which do nothing;
 * red, by a percentage past what an int holds; and green, by a style
 * with declarations of nothing in it */
static const gchar odd[] =
  "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"12\" height=\"4\">"
  "<rect width=\"4\" height=\"4\" fill=\"#0000ff\" transform=\"translate() scale() matrix(1 2 3) skewX() skewY() rotate()\"/>"
  "<rect x=\"4\" width=\"4\" height=\"4\" fill=\"rgb(99999999999999999999999%, 0%, 0%)\"/>"
  "<rect x=\"8\" width=\"4\" height=\"4\" style=\"; fill: #00ff00; ; stroke: none; \"/>"
  "</svg>";

/* far out of all bounds, every way a number can be */
static const gchar wild[] =
  "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\">"
  "<path d=\"M -1e30 -1e30 L 1e30 8 L 8 1e30 z\" fill=\"#102030\"/>"
  "<path d=\"M 1e38 1e38 C 1e38 -1e38 -1e38 1e38 nan inf z\" stroke=\"#405060\" stroke-width=\"1e20\"/>"
  "<circle cx=\"8\" cy=\"8\" r=\"1e12\" fill=\"#708090\" stroke=\"#000\" stroke-width=\"3e9\" stroke-dasharray=\"1e-30 1e-30\"/>"
  "<rect x=\"-3e6\" y=\"-3e6\" width=\"6e6\" height=\"6e6\" rx=\"1e6\" transform=\"matrix(1e9 1e9 -1e9 1e9 0 0)\"/>"
  "<ellipse cx=\"2147483648\" cy=\"-2147483649\" rx=\"99999999999999999999\" ry=\"1e-40\" stroke=\"red\" stroke-width=\"1e400\"/>"
  "<line x1=\"0\" y1=\"0\" x2=\"16\" y2=\"1e-7\" stroke=\"#fff\" stroke-width=\"1e7\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>"
  "<path d=\"M 0 0 A 1e30 1e30 0 0 1 1e30 1e-30 A 1e38 1e-38 1e38 1 1 -1e38 1e38 z\" stroke=\"#000\"/>"
  "<path d=\"M 0 0 L 16 16\" stroke=\"#000\" stroke-width=\"1e7\" stroke-dasharray=\"0.05 0.05\" stroke-linecap=\"round\"/>"
  "<defs><radialGradient id=\"g\" r=\"1e30\" gradientTransform=\"scale(0)\"><stop offset=\"1e30\" stop-opacity=\"1e30\"/>"
  "<stop offset=\"-1e30\" stop-color=\"rgb(300, 300, 300)\"/></radialGradient></defs>"
  "<rect width=\"8\" height=\"8\" fill=\"url(#g)\" stroke=\"url(#g)\" transform=\"scale(0) matrix(0 0 0 0 0 0)\"/>"
  "<polygon points=\"0,0 1e9,0 1e9,1e9\" fill=\"url(#g)\" fill-opacity=\"1e30\" stroke-dashoffset=\"1e30\" stroke-dasharray=\"1 1e30 0\" stroke=\"#123\"/>"
  "</svg>";

static gint failed;

#define CHECK(what) \
  do { if (!(what)) { printf ("line %d: %s\n", __LINE__, #what); failed++; } } while (0)

/* The pixel of a picture width across is of that colour and alpha. */
static gboolean
is (const guchar *pixels, gint width, gint x, gint y,
    gint r, gint g, gint b, gint a)
{
  const guchar *p = pixels + (y * width + x) * 4;

  return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
}

/* No colour of any pixel is more than its alpha. */
static gboolean
premultiplied (const guchar *pixels, gint width, gint height)
{
  gint i;

  for (i = 0; i < width * height * 4; i += 4)
    if (pixels[i] > pixels[i + 3] || pixels[i + 1] > pixels[i + 3] || pixels[i + 2] > pixels[i + 3])
      return FALSE;

  return TRUE;
}

/* The text is not a picture. */
static void
refused (const gchar *name, const gchar *text, gssize length)
{
  GError *error = NULL;
  GdkSvg *svg = gdk_svg_load_from_data (text, length, &error);

  if (svg || !error || error->domain != GDK_SVG_ERROR || error->code != GDK_SVG_ERROR_FAILED)
    {
      printf ("%s: %s\n", name, svg ? "was read" : error ? error->message : "no error was set");
      failed++;
    }
  g_clear_error (&error);
  gdk_svg_free (svg);
}

/* Whatever the text is, it is read or refused, and drawn if read. */
static void
survives (const gchar *text, gssize length)
{
  GError *error = NULL;
  GdkSvg *svg = gdk_svg_load_from_data (text, length, &error);
  guchar *pixels;

  CHECK ((svg != NULL) != (error != NULL));
  g_clear_error (&error);
  if (!svg)
    return;
  pixels = gdk_svg_render (svg, 24, 16);
  CHECK (pixels && premultiplied (pixels, 24, 16));
  g_free (pixels);
  pixels = gdk_svg_render_area (svg, 1.5, -2.5, 3.25, 9, 7);
  CHECK (pixels && premultiplied (pixels, 9, 7));
  g_free (pixels);
  gdk_svg_free (svg);
}

int
main (void)
{
  const gchar *file = "testsvg.tmp";
  GError *error = NULL;
  gchar damaged[sizeof shapes], *unended;
  gdouble width = 0, height = 0;
  guchar *pixels;
  GdkSvg *svg;
  gint x, y, sum;
  gsize i;

  /* its size, and its pixels as they are */
  svg = gdk_svg_load_from_data (halves, -1, &error);
  CHECK (svg && !error);
  if (!svg)
    return 1;
  gdk_svg_get_size (svg, &width, &height);
  CHECK (width == 8 && height == 4);
  gdk_svg_get_size (svg, NULL, NULL);
  pixels = gdk_svg_render (svg, 8, 4);
  CHECK (pixels != NULL);
  for (y = 0; y < 4; y++)
    for (x = 0; x < 8; x++)
      CHECK (x < 4 ? is (pixels, 8, x, y, 255, 0, 0, 255) : is (pixels, 8, x, y, 0, 0, 127, 127));
  g_free (pixels);

  /* with straight alpha: the blue is whole, at half */
  pixels = gdk_svg_render_straight (svg, 8, 4);
  CHECK (pixels != NULL);
  for (y = 0; y < 4; y++)
    for (x = 0; x < 8; x++)
      CHECK (x < 4 ? is (pixels, 8, x, y, 255, 0, 0, 255) : is (pixels, 8, x, y, 0, 0, 255, 127));
  g_free (pixels);
  pixels = gdk_svg_render_area_straight (svg, 3, 1, 3, 6, 5);
  CHECK (pixels && is (pixels, 6, 2, 4, 255, 0, 0, 255) && is (pixels, 6, 3, 0, 0, 0, 255, 127));
  g_free (pixels);
  CHECK (gdk_svg_render_straight (svg, 32768, 4) == NULL);

  /* twice the size */
  pixels = gdk_svg_render (svg, 16, 8);
  CHECK (pixels && is (pixels, 16, 0, 0, 255, 0, 0, 255) && is (pixels, 16, 7, 7, 255, 0, 0, 255) &&
         is (pixels, 16, 8, 0, 0, 0, 127, 127) && is (pixels, 16, 15, 7, 0, 0, 127, 127));
  g_free (pixels);

  /* into a square: scaled alike both ways, at the top, clear below */
  pixels = gdk_svg_render (svg, 16, 16);
  CHECK (pixels && is (pixels, 16, 3, 7, 255, 0, 0, 255) && is (pixels, 16, 12, 7, 0, 0, 127, 127) &&
         is (pixels, 16, 3, 8, 0, 0, 0, 0) && is (pixels, 16, 15, 15, 0, 0, 0, 0));
  g_free (pixels);

  /* a part of it: across the join, three times the size, from one
   * unit down */
  pixels = gdk_svg_render_area (svg, 3, 1, 3, 6, 5);
  CHECK (pixels != NULL);
  for (y = 0; y < 5; y++)
    for (x = 0; x < 6; x++)
      CHECK (x < 3 ? is (pixels, 6, x, y, 255, 0, 0, 255) : is (pixels, 6, x, y, 0, 0, 127, 127));
  g_free (pixels);

  /* and beyond it, where there is nothing */
  pixels = gdk_svg_render_area (svg, 6, 2, 1, 4, 4);
  CHECK (pixels && is (pixels, 4, 1, 1, 0, 0, 127, 127) && is (pixels, 4, 2, 1, 0, 0, 0, 0) &&
         is (pixels, 4, 1, 2, 0, 0, 0, 0));
  g_free (pixels);

  /* no more pixels than are allowed, either way or in all */
  CHECK (gdk_svg_render (svg, 32768, 4) == NULL);
  CHECK (gdk_svg_render (svg, 4, 32768) == NULL);
  CHECK (gdk_svg_render (svg, 32767, 2100) == NULL);
  CHECK (gdk_svg_render_area (svg, 0, 0, 1, 32768, 4) == NULL);
  gdk_svg_free (svg);
  gdk_svg_free (NULL);

  /* straight alpha again, where there are edges: the green of the
   * disc is the same green at its edge, where it is part clear, and
   * a clear pixel beside it has its colour too */
  svg = gdk_svg_load_from_data (shapes, -1, &error);
  pixels = gdk_svg_render_straight (svg, 20, 10);
  CHECK (pixels && is (pixels, 20, 10, 5, 0, 128, 0, 255));
  CHECK (pixels[(1 * 20 + 8) * 4 + 3] > 0 && pixels[(1 * 20 + 8) * 4 + 3] < 255 &&
         pixels[(1 * 20 + 8) * 4] == 0 && pixels[(1 * 20 + 8) * 4 + 2] == 0 &&
         pixels[(1 * 20 + 8) * 4 + 1] >= 125 && pixels[(1 * 20 + 8) * 4 + 1] <= 131);
  CHECK (pixels[(1 * 20 + 6) * 4 + 3] == 0 && pixels[(1 * 20 + 6) * 4] == 0 &&
         pixels[(1 * 20 + 6) * 4 + 1] >= 118 && pixels[(1 * 20 + 6) * 4 + 1] <= 136);
  g_free (pixels);
  gdk_svg_free (svg);

  /* a view box, a group moved, a style sheet's class, a curve and a
   * stroke: the disc is green in the middle and clear at the corners,
   * and covers what a disc of radius four does, 50.3 pixels, less
   * the little that drawing its curve as straight lines leaves out */
  svg = gdk_svg_load_from_data (shapes, -1, &error);
  CHECK (svg && !error);
  gdk_svg_get_size (svg, &width, &height);
  CHECK (width == 20 && height == 10);
  pixels = gdk_svg_render (svg, 20, 10);
  CHECK (pixels && premultiplied (pixels, 20, 10));
  CHECK (is (pixels, 20, 10, 5, 0, 128, 0, 255) && is (pixels, 20, 9, 4, 0, 128, 0, 255));
  CHECK (is (pixels, 20, 0, 0, 0, 0, 0, 0) && is (pixels, 20, 5, 0, 0, 0, 0, 0) && is (pixels, 20, 14, 9, 0, 0, 0, 0));
  for (sum = 0, y = 0; y < 10; y++)
    for (x = 0; x < 15; x++)
      sum += pixels[(y * 20 + x) * 4 + 3];
  CHECK (sum > 48 * 255 && sum < 51 * 255);
  /* the stroke: a pixel wide in the picture, yellow, the square
   * within it empty */
  CHECK (is (pixels, 20, 15, 2, 255, 255, 0, 255) && is (pixels, 20, 18, 7, 255, 255, 0, 255) &&
         is (pixels, 20, 15, 7, 255, 255, 0, 255) && is (pixels, 20, 18, 2, 255, 255, 0, 255));
  CHECK (is (pixels, 20, 16, 4, 0, 0, 0, 0) && is (pixels, 20, 17, 5, 0, 0, 0, 0) && is (pixels, 20, 19, 5, 0, 0, 0, 0));
  g_free (pixels);
  gdk_svg_free (svg);

  /* a gradient: opaque, grey, and lighter all the way across */
  svg = gdk_svg_load_from_data (shaded, -1, &error);
  CHECK (svg && !error);
  pixels = gdk_svg_render (svg, 10, 2);
  CHECK (pixels != NULL);
  for (x = 0; x < 10; x++)
    {
      const guchar *p = pixels + x * 4;

      CHECK (p[3] == 255 && p[0] == p[1] && p[1] == p[2]);
      CHECK (x == 0 || p[0] > p[-4]);
    }
  CHECK (pixels[0] < 32 && pixels[9 * 4] > 223);
  g_free (pixels);
  gdk_svg_free (svg);

  /* written oddly, and drawn as what was meant */
  svg = gdk_svg_load_from_data (odd, -1, &error);
  CHECK (svg && !error);
  pixels = gdk_svg_render (svg, 12, 4);
  CHECK (pixels != NULL);
  for (y = 0; y < 4; y++)
    for (x = 0; x < 12; x++)
      CHECK (x < 4 ? is (pixels, 12, x, y, 0, 0, 255, 255)
             : x < 8 ? is (pixels, 12, x, y, 255, 0, 0, 255) : is (pixels, 12, x, y, 0, 255, 0, 255));
  g_free (pixels);
  gdk_svg_free (svg);

  /* one that says no size is as large as what is in it */
  svg = gdk_svg_load_from_data ("<svg xmlns=\"http://www.w3.org/2000/svg\"><rect width=\"6\" height=\"3\"/></svg>", -1, &error);
  CHECK (svg && !error);
  gdk_svg_get_size (svg, &width, &height);
  CHECK (width == 6 && height == 3);
  gdk_svg_free (svg);

  /* only as much of the text as it is told there is */
  unended = g_malloc (sizeof halves - 1 + 8);
  memcpy (unended, halves, sizeof halves - 1);
  memcpy (unended + sizeof halves - 1, "<rect w", 8);
  svg = gdk_svg_load_from_data (unended, sizeof halves - 1, &error);
  CHECK (svg && !error);
  gdk_svg_free (svg);
  g_free (unended);

  /* what is not a picture, or is one of no size */
  refused ("nothing", "", -1);
  refused ("nothing at all", halves, 0);
  refused ("not SVG", "GIF89a, it says", -1);
  refused ("another kind of XML", "<html><body><p>width=\"8\"</p></body></html>", -1);
  refused ("no size and nothing in it", "<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>", -1);
  refused ("less than no height", "<svg width=\"4\" height=\"-4\"><rect width=\"4\" height=\"4\"/></svg>", -1);

  /* far out of bounds; cut short at every length; and with each byte
   * in turn wrong */
  survives (wild, -1);
  for (i = 0; i < sizeof shapes; i++)
    survives (shapes, i);
  for (i = 0; i < sizeof shapes - 1; i++)
    {
      memcpy (damaged, shapes, sizeof shapes);
      damaged[i] ^= 0x15;
      survives (damaged, -1);
      damaged[i] = '9';
      survives (damaged, -1);
    }

  /* from a file, and from none */
  CHECK (g_file_set_contents (file, halves, -1, NULL));
  svg = gdk_svg_load (file, &error);
  CHECK (svg && !error);
  if (svg)
    {
      pixels = gdk_svg_render (svg, 8, 4);
      CHECK (pixels && is (pixels, 8, 0, 0, 255, 0, 0, 255) && is (pixels, 8, 7, 3, 0, 0, 127, 127));
      g_free (pixels);
      gdk_svg_free (svg);
    }
  unlink (file);
  svg = gdk_svg_load (file, &error);
  CHECK (!svg && error && error->domain == G_FILE_ERROR && error->code == G_FILE_ERROR_NOENT);
  g_clear_error (&error);

  if (failed)
    printf ("%d failed\n", failed);

  return failed != 0;
}
