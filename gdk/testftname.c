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

/* testftname - what font names are understood as.
 *
 *   testftname              check the built-in lists; exit non-zero on failure
 *   testftname [-d DPI] NAME...   print what each NAME is understood as
 *   testftname -a           every installed family and style, as a font
 *                           selection offers them, written as a name in
 *                           points and in pixels, parsed back and resolved
 *
 * Without -a it needs no display and no fonts: this is only the parsing
 * and the writing of names.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gdk.h"
#include "gdkftfont.h"

static const struct
{
  const gchar *name;
  gdouble dpi;
  const gchar *expect;          /* NULL: must not parse */
}
cases[] =
{
  /* modern names */
  { "Sans 10",                                   96, "sans-serif 13px" },
  { "Sans 10",                                   75, "sans-serif 10px" },
  { "Sans",                                      96, "sans-serif 13px" },
  { "  Sans 10\n",                               96, "sans-serif 13px" },
  { "DejaVu Sans Bold 11",                       96, "DejaVu Sans weight=700 15px" },
  { "Monospace Italic 9px",                      96, "monospace italic 9px" },
  { "Mono 9px",                                  96, "monospace 9px" },
  { "Liberation Serif Semi-Bold Condensed 12px", 96,
    "Liberation Serif weight=600 width=75 12px" },
  { "DejaVu Sans, Noto Sans CJK JP 10",          96,
    "DejaVu Sans,Noto Sans CJK JP 13px" },
  { "Serif Oblique 10.5",                        96, "serif oblique 14px" },
  { "Times New Roman 12",                        96, "Times New Roman 16px" },
  { "Latin Modern Roman Bold 12",                96,
    "Latin Modern Roman weight=700 16px" },
  { "Noto Sans Light, 9",                        96, "Noto Sans Light 12px" },
  { "",                                          96, NULL },

  /* X names, read at 75 dpi whatever the display, truncated */
  { "-adobe-helvetica-medium-r-normal--*-120-*-*-*-*-*-*", 96,
    "sans-serif 12px" },
  { "-adobe-helvetica-bold-r-*-*-10-*",          96, "sans-serif weight=700 10px" },
  { "-adobe-helvetica-medium-r-*-*-8-*",         96, "sans-serif 8px" },
  { "-*-times-bold-i-normal--*-140-*-*-p-*-iso8859-1", 96,
    "serif weight=700 italic 14px" },
  { "-bitstream-charter-medium-r-normal--*-120-100-100-p-*", 96,
    "serif 16px" },
  { "-misc-fixed-medium-r-semicondensed--13-120-75-75-c-60-iso8859-1", 96,
    "monospace mono 13px" },
  { "-*-*-medium-r-normal--14-*-*-*-*-*-*-*",    96, "sans-serif 14px" },
  { "-*-*-medium-r-normal--*-*-*-*-m-*-*-*",     96, "monospace mono 12px" },
  { "-*-dejavu sans mono-medium-r-*-*-*-*-*-*-m-*", 96,
    "dejavu sans mono,monospace mono 12px" },
  { "-*-helvetica-medium-r-normal--0-0-75-75-p-0-iso8859-1", 96,
    "sans-serif 12px" },
  { "-xos4-terminus-bold-r-normal--16-*-*-*-c-*-iso10646-1", 96,
    "terminus,xos4 terminus,monospace weight=700 mono 16px" },
  { "-*-*-*-*-*-*-*-*-*-*-*-*-*-*",              96, "sans-serif 12px" },
  { "-*-*courier*-medium-r-normal--14-*",        96, "monospace 14px" },

  /* X patterns beginning with a wildcard, as the server matched them */
  { "*helvetica-medium-r-normal--10-*",          96, "sans-serif 10px" },
  { "*-helvetica-bold-r-*-*-12-*",               96, "sans-serif weight=700 12px" },
  { "*-*-times-bold-i-*-*-*-140-*",              96,
    "serif weight=700 italic 14px" },
  { "*-medium-r-normal--14-*",                   96, "sans-serif 14px" },
  { "*-bold-r-*-*-*-*-*-*-m-*",                  96,
    "monospace weight=700 mono 12px" },
  { "*courier*",                                 96, "monospace 12px" },
  { "*",                                         96, "sans-serif 12px" },
  { "*-fixed-medium-r-*-*-13-*,*-r-*",           96, "monospace 13px" },

  /* a fontset: the first name says what is wanted */
  { "-adobe-helvetica-medium-r-normal--*-120-*-*-*-*-*-*,"
    "-*-*-medium-r-normal--*-120-*-*-m-*-*-*",   96, "sans-serif 12px" },

  /* the server's aliases */
  { "fixed",                                     96, "monospace 13px" },
  { "6x13",                                      96, "monospace 13px" },
  { "9x15bold",                                  96, "monospace weight=700 15px" },
  { "variable",                                  96, "sans-serif weight=700 12px" },
};

/* The size as written, in points, or 0 for pixels. */
static const struct
{
  const gchar *name;
  gdouble points;
}
points_cases[] =
{
  { "Sans 10",                   10 },
  { "Sans 10.5",                 10.5 },
  { "Sans 13px",                 0 },
  { "Sans",                      10 },
  { "-adobe-helvetica-bold-r-*-*-10-*", 0 },
  { "-adobe-helvetica-medium-r-normal--*-120-*-*-*-*-*-*", 0 },
  { "fixed",                     0 },
};

/* Names written from a family, a style and a size. */
static const struct
{
  const gchar *family;
  gint weight, slant, width;
  gdouble size;
  gboolean pixels;
  const gchar *expect;
}
compose_cases[] =
{
  { "DejaVu Sans", 700, GDK_FT_SLANT_ITALIC, 100, 10, FALSE,
    "DejaVu Sans Bold Italic 10" },
  { "DejaVu Sans", 400, GDK_FT_SLANT_ROMAN, 100, 10, FALSE, "DejaVu Sans 10" },
  { "DejaVu Sans", 700, GDK_FT_SLANT_ROMAN, 75, 9.5, FALSE,
    "DejaVu Sans Condensed Bold 9.5" },
  { "Sans", 400, GDK_FT_SLANT_ROMAN, 100, 13, TRUE, "Sans 13px" },
  { "Monospace", 200, GDK_FT_SLANT_OBLIQUE, 100, 12.26, FALSE,
    "Monospace Extra-Light Oblique 12.3" },
  { "LM Roman 10", 400, GDK_FT_SLANT_ROMAN, 100, 12, FALSE, "LM Roman 10 12" },
  { "LM Roman 10", 400, GDK_FT_SLANT_ROMAN, 100, 12, TRUE, "LM Roman 10 12px" },
  { "Noto Sans Light", 400, GDK_FT_SLANT_ROMAN, 100, 12, FALSE,
    "Noto Sans Light, 12" },
};

/*
 * A bitmap family is listed without the foundry FreeType may put in
 * front of its name: "Fixed" is drawn from a face FreeType calls "Misc
 * Fixed".
 */
static gboolean
same_family (const gchar *face,
             const gchar *listed)
{
  gsize f = strlen (face), l = strlen (listed);

  return g_strcasecmp (face, listed) == 0 ||
         (f > l && face[f - l - 1] == ' ' && g_strcasecmp (face + f - l, listed) == 0);
}

/*
 * Every family and style installed, written as a name, must parse back
 * to what it was written from, and must resolve to something drawable.
 */
static gint
check_installed (void)
{
  gboolean *bitmap;
  gchar **families = _gdk_ft_list_families (FALSE, &bitmap);
  gint failures = 0, names = 0, other_family = 0, n_bitmap = 0;
  gint f, s, pass;

  for (f = 0; families[f]; f++)
    {
      gint n_styles;
      GdkFTName *styles = _gdk_ft_list_styles (families[f], &n_styles);

      if (n_styles == 0)
        {
          printf ("FAIL: \"%s\" has no styles\n", families[f]);
          failures++;
        }

      /*
       * A bitmap family: every size of every style, in pixels, must be
       * that family, that style, at that size, drawn from a bitmap.
       */
      if (bitmap[f])
        {
          n_bitmap++;
          for (s = 0; s < n_styles; s++)
            {
              gint n_sizes, z;
              gdouble *sizes = _gdk_ft_list_sizes (families[f], styles[s].weight,
                                                   styles[s].slant,
                                                   styles[s].width, &n_sizes);

              if (n_sizes == 0)
                {
                  printf ("FAIL: \"%s\" style %d has no sizes\n", families[f], s);
                  failures++;
                }
              for (z = 0; z < n_sizes; z++)
                {
                  gchar *name = _gdk_ft_name_compose (families[f], styles[s].weight,
                                                      styles[s].slant,
                                                      styles[s].width,
                                                      sizes[z], TRUE);
                  GdkFTSet *set = _gdk_ft_set_open_name (name, 96, 1);
                  GdkFTFont *font = set ? _gdk_ft_set_primary (set) : NULL;

                  names++;
                  if (!font || !font->bitmap || font->pixel_size != sizes[z] ||
                      !same_family (font->family, families[f]))
                    {
                      printf ("FAIL: \"%s\" drew from %s %s at %gpx%s\n", name,
                              font ? font->family : "nothing",
                              font ? font->style : "",
                              font ? font->pixel_size : 0,
                              font && !font->bitmap ? ", outlines" : "");
                      failures++;
                    }
                  if (set)
                    _gdk_ft_set_free (set);
                  g_free (name);
                }
              g_free (sizes);
            }
          g_free (styles);
          continue;
        }

      for (s = 0; s < n_styles; s++)
        for (pass = 0; pass < 2; pass++)
          {
            gdouble size = pass ? 13 : 10;
            gchar *name = _gdk_ft_name_compose (families[f], styles[s].weight,
                                                styles[s].slant, styles[s].width,
                                                size, pass);
            GdkFTName parsed;
            GdkFTSet *set;

            names++;
            if (!_gdk_ft_name_parse (name, 96, &parsed) ||
                g_strcasecmp (parsed.families[0], families[f]) != 0 ||
                parsed.weight != styles[s].weight ||
                parsed.slant != styles[s].slant ||
                parsed.width != styles[s].width ||
                (pass ? parsed.pixel_size != 13 || parsed.points != 0
                      : parsed.points != 10))
              {
                gchar *got = _gdk_ft_name_to_string (&parsed);

                printf ("FAIL: \"%s\" parses as %s\n", name, got);
                g_free (got);
                failures++;
              }
            _gdk_ft_name_clear (&parsed);

            set = _gdk_ft_set_open_name (name, 96, 1);
            if (!set)
              {
                printf ("FAIL: \"%s\" resolves to nothing\n", name);
                failures++;
              }
            else
              {
                if (g_strcasecmp (_gdk_ft_set_primary (set)->family, families[f]) != 0)
                  other_family++;
                _gdk_ft_set_free (set);
              }
            g_free (name);
          }

      g_free (styles);
    }

  printf ("%d families (%d bitmap), %d names, %d failed; %d drew from a "
          "face naming its family otherwise\n", f, n_bitmap, names, failures,
          other_family);
  g_strfreev (families);
  g_free (bitmap);

  return failures;
}

int
main (int argc, char **argv)
{
  gdouble dpi = 96;
  gint failures = 0;
  gsize i;
  gint a;

  if (argc == 2 && strcmp (argv[1], "-a") == 0)
    return check_installed () ? 1 : 0;

  if (argc > 1)
    {
      for (a = 1; a < argc; a++)
        {
          GdkFTName name;

          if (strcmp (argv[a], "-d") == 0 && a + 1 < argc)
            {
              dpi = atof (argv[++a]);
              continue;
            }
          if (_gdk_ft_name_parse (argv[a], dpi, &name))
            {
              gchar *s = _gdk_ft_name_to_string (&name);

              printf ("%s\n    %s\n", argv[a], s);
              g_free (s);
              _gdk_ft_name_clear (&name);
            }
          else
            printf ("%s\n    (not a font name)\n", argv[a]);
        }
      return 0;
    }

  for (i = 0; i < sizeof cases / sizeof cases[0]; i++)
    {
      GdkFTName name;
      gchar *got = NULL;

      if (_gdk_ft_name_parse (cases[i].name, cases[i].dpi, &name))
        {
          got = _gdk_ft_name_to_string (&name);
          _gdk_ft_name_clear (&name);
        }

      if ((got == NULL) != (cases[i].expect == NULL) ||
          (got && strcmp (got, cases[i].expect) != 0))
        {
          printf ("FAIL: \"%s\" at %g dpi\n    got      %s\n    expected %s\n",
                  cases[i].name, cases[i].dpi, got ? got : "(nothing)",
                  cases[i].expect ? cases[i].expect : "(nothing)");
          failures++;
        }

      g_free (got);
    }

  for (i = 0; i < sizeof points_cases / sizeof points_cases[0]; i++)
    {
      GdkFTName name;

      if (!_gdk_ft_name_parse (points_cases[i].name, 96, &name) ||
          fabs (name.points - points_cases[i].points) > 0.001)
        {
          printf ("FAIL: \"%s\" is %g points as written, expected %g\n",
                  points_cases[i].name, name.points, points_cases[i].points);
          failures++;
        }
      _gdk_ft_name_clear (&name);
    }

  for (i = 0; i < sizeof compose_cases / sizeof compose_cases[0]; i++)
    {
      gchar *got = _gdk_ft_name_compose (compose_cases[i].family,
                                         compose_cases[i].weight,
                                         compose_cases[i].slant,
                                         compose_cases[i].width,
                                         compose_cases[i].size,
                                         compose_cases[i].pixels);

      if (strcmp (got, compose_cases[i].expect) != 0)
        {
          printf ("FAIL: composed \"%s\", expected \"%s\"\n", got,
                  compose_cases[i].expect);
          failures++;
        }
      g_free (got);
    }

  printf ("%d names, %d sizes, %d composed, %d failed\n",
          (int) (sizeof cases / sizeof cases[0]),
          (int) (sizeof points_cases / sizeof points_cases[0]),
          (int) (sizeof compose_cases / sizeof compose_cases[0]), failures);

  return failures ? 1 : 0;
}
