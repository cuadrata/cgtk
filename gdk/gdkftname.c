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
 * Font names, and what they resolve to.
 *
 * Parsing a name is done here, without help, because it has to
 * understand the X names the way the X server did. Resolving one is
 * fontconfig's job, so that "Sans" means here what it means to every
 * other program on the machine, and the user's configuration -- which
 * fonts, what hinting, what to reject -- applies here as it does there.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "gdk.h"
#include "gdkftfont.h"

/*
 * Families the X world used, and the generic family each stands for.
 * These are not left to fontconfig's own aliases, which send helvetica
 * to a metric clone and fixed to a proportional face: an old name means
 * the user's Sans, Serif or Monospace.
 */
static const struct
{
  const gchar *xlfd;
  const gchar *family;
}
family_aliases[] =
{
  { "helvetica",              "sans-serif" },
  { "arial",                  "sans-serif" },
  { "lucida",                 "sans-serif" },
  { "lucidasans",             "sans-serif" },
  { "lucida sans",            "sans-serif" },
  { "verdana",                "sans-serif" },
  { "nimbus sans l",          "sans-serif" },
  { "nimbus sans",            "sans-serif" },
  { "times",                  "serif" },
  { "times new roman",        "serif" },
  { "charter",                "serif" },
  { "new century schoolbook", "serif" },
  { "lucidabright",           "serif" },
  { "nimbus roman no9 l",     "serif" },
  { "utopia",                 "serif" },
  { "courier",                "monospace" },
  { "courier new",            "monospace" },
  { "nimbus mono l",          "monospace" },
  { "lucidatypewriter",       "monospace" },
  { "fixed",                  "monospace" },
  { "misc fixed",             "monospace" },
  { "clean",                  "monospace" },
  { "terminal",               "monospace" },
  { "screen",                 "monospace" }
};

/* How the generic families may be written in a modern name. */
static const struct
{
  const gchar *name;
  const gchar *family;
}
generic_names[] =
{
  { "sans",       "sans-serif" },
  { "sans-serif", "sans-serif" },
  { "serif",      "serif" },
  { "monospace",  "monospace" },
  { "mono",       "monospace" }
};

static const struct
{
  const gchar *word;
  gint weight;
}
weight_words[] =
{
  { "thin",       100 },
  { "extralight", 200 },
  { "ultralight", 200 },
  { "light",      300 },
  { "book",       400 },
  { "regular",    400 },
  { "normal",     400 },
  { "medium",     500 },
  { "demibold",   600 },
  { "semibold",   600 },
  { "bold",       700 },
  { "extrabold",  800 },
  { "ultrabold",  800 },
  { "black",      900 },
  { "heavy",      900 }
};

static const struct
{
  const gchar *word;
  gint width;
}
width_words[] =
{
  { "ultracondensed", 50 },
  { "extracondensed", 63 },
  { "condensed",      75 },
  { "semicondensed",  87 },
  { "semiexpanded",   113 },
  { "expanded",       125 },
  { "extraexpanded",  150 },
  { "ultraexpanded",  200 }
};

#define N_ELEMENTS(a) (sizeof (a) / sizeof ((a)[0]))

#define DEFAULT_POINTS      10.0   /* a modern name with no size */
#define DEFAULT_XLFD_PIXELS 12.0   /* an X name with no size */

static void
name_init (GdkFTName *name)
{
  name->families = NULL;
  name->weight = 400;
  name->slant = GDK_FT_SLANT_ROMAN;
  name->width = 100;
  name->monospace = FALSE;
  name->pixel_size = 0;
  name->points = 0;
}

/* "Semi-Bold" is "semibold": hyphens and case do not matter in style words. */
static gchar *
style_key (const gchar *word)
{
  gchar *key = g_malloc (strlen (word) + 1);
  gchar *q = key;

  for (; *word; word++)
    if (*word != '-')
      *q++ = tolower ((guchar) *word);
  *q = '\0';

  return key;
}

static gboolean
parse_weight (const gchar *word,
              gint        *weight)
{
  gchar *key = style_key (word);
  gsize i;

  for (i = 0; i < N_ELEMENTS (weight_words); i++)
    if (strcmp (key, weight_words[i].word) == 0)
      {
        *weight = weight_words[i].weight;
        g_free (key);
        return TRUE;
      }

  g_free (key);
  return FALSE;
}

/*
 * Not "roman": it is what a name means without a slant anyway, and
 * taking it as a style word would turn Times New Roman into Times New.
 */
static gboolean
parse_slant (const gchar *word,
             gint        *slant)
{
  if (g_strcasecmp (word, "italic") == 0)
    *slant = GDK_FT_SLANT_ITALIC;
  else if (g_strcasecmp (word, "oblique") == 0)
    *slant = GDK_FT_SLANT_OBLIQUE;
  else
    return FALSE;

  return TRUE;
}

static gboolean
parse_width (const gchar *word,
             gint        *width)
{
  gchar *key = style_key (word);
  gsize i;

  for (i = 0; i < N_ELEMENTS (width_words); i++)
    if (strcmp (key, width_words[i].word) == 0)
      {
        *width = width_words[i].width;
        g_free (key);
        return TRUE;
      }

  g_free (key);
  return FALSE;
}

/* "11", "10.5", "13px". Points are rounded to the nearest pixel. */
static gboolean
parse_size (const gchar *word,
            gdouble      dpi,
            gdouble     *pixel_size,
            gdouble     *points)
{
  gchar *end;
  gdouble value;

  if (!isdigit ((guchar) *word))
    return FALSE;

  value = strtod (word, &end);
  if (value <= 0 || value > 1000)
    return FALSE;

  if (g_strcasecmp (end, "px") == 0)
    {
      *pixel_size = floor (value + 0.5);
      *points = 0;
    }
  else if (*end == '\0')
    {
      *pixel_size = floor (value * dpi / 72.0 + 0.5);
      *points = value;
    }
  else
    return FALSE;

  if (*pixel_size < 1)
    *pixel_size = 1;

  return TRUE;
}

static const gchar *
xlfd_family (const gchar *family)
{
  gsize i;

  for (i = 0; i < N_ELEMENTS (family_aliases); i++)
    if (g_strcasecmp (family, family_aliases[i].xlfd) == 0)
      return family_aliases[i].family;

  return NULL;
}

static const gchar *
generic_family (const gchar *family)
{
  gsize i;

  for (i = 0; i < N_ELEMENTS (generic_names); i++)
    if (g_strcasecmp (family, generic_names[i].name) == 0)
      return generic_names[i].family;

  return NULL;
}

static gboolean
is_wild (const gchar *field)
{
  return *field == '\0' || strchr (field, '*') || strchr (field, '?');
}

static gchar **
one_family (const gchar *family)
{
  gchar **families = g_new0 (gchar *, 2);

  families[0] = g_strdup (family);

  return families;
}

/*
 * The X server's own aliases, which are not XLFDs at all: fixed, the
 * cell sizes of the misc fonts, and variable.
 */
static gboolean
parse_x_alias (const gchar *name,
               GdkFTName   *result)
{
  const gchar *p = name;
  gint cell_height = 0;

  if (g_strcasecmp (name, "fixed") == 0)
    {
      result->families = one_family ("monospace");
      result->pixel_size = 13;
      return TRUE;
    }

  if (g_strcasecmp (name, "variable") == 0)
    {
      result->families = one_family ("sans-serif");
      result->weight = 700;
      result->pixel_size = DEFAULT_XLFD_PIXELS;
      return TRUE;
    }

  /* 6x13, 9x15bold, 12x13ja, 10x20 */
  if (!isdigit ((guchar) *p))
    return FALSE;
  while (isdigit ((guchar) *p))
    p++;
  if (*p++ != 'x' || !isdigit ((guchar) *p))
    return FALSE;
  while (isdigit ((guchar) *p))
    cell_height = cell_height * 10 + (*p++ - '0');
  if (*p != '\0' && g_strcasecmp (p, "bold") != 0 &&
      g_strcasecmp (p, "ja") != 0 && g_strcasecmp (p, "ko") != 0)
    return FALSE;
  if (cell_height <= 0)
    return FALSE;

  result->families = one_family ("monospace");
  result->pixel_size = cell_height;
  if (g_strcasecmp (p, "bold") == 0)
    result->weight = 700;

  return TRUE;
}

/*
 * A family written with wildcards, "*courier*": if what is left without
 * them is a family X knew, that is the family meant. Anything else is
 * any family at all.
 */
static const gchar *
wild_family (const gchar *field)
{
  gchar *name = g_malloc (strlen (field) + 1);
  gchar *q = name;
  const gchar *family;

  for (; *field; field++)
    if (*field != '*' && *field != '?')
      *q++ = *field;
  *q = '\0';

  family = *name ? xlfd_family (name) : NULL;
  g_free (name);

  return family;
}

/*
 * An X name could also be a pattern starting with a wildcard,
 * "*helvetica-medium-r-normal--10-*", which the server matched against
 * the full names of its fonts: the star stood for the leading dash, the
 * foundry and the dash after it. Written out as the full name it stood
 * for, it is read as any other. When what follows the star is a
 * weight, "*-bold-r-*", the star stood for the family as well.
 */
static gchar *
xlfd_from_pattern (const gchar *pattern)
{
  const gchar *rest = pattern;
  const gchar *end;
  gchar *first;
  gint weight;
  gboolean no_family;

  /* "*", "*-" and "*-*-" all stand for the start of a name */
  while (*rest == '*' || *rest == '?')
    {
      rest++;
      if (*rest == '-')
        rest++;
    }

  end = rest + strcspn (rest, "-,");
  first = g_strndup (rest, end - rest);
  no_family = *first != '\0' && parse_weight (first, &weight);
  g_free (first);

  return g_strconcat (no_family ? "-*-*-" : "-*-", rest, NULL);
}

static gboolean
parse_xlfd (const gchar *name,
            GdkFTName   *result)
{
  gchar **fields;
  gchar *first;
  const gchar *family;
  gint n;

  /*
   * A fontset is a list of X names, one for each character set. The
   * first says what is wanted; the fonts covering the other character
   * sets are found by falling back.
   */
  first = g_strdup (name);
  if (strchr (first, ','))
    *strchr (first, ',') = '\0';
  g_strstrip (first);

  /* A leading dash gives an empty first field. */
  fields = g_strsplit (first, "-", 15);
  g_free (first);
  for (n = 0; fields[n]; n++)
    ;

  family = n > 2 ? fields[2] : "";
  if (n > 11 && (fields[11][0] == 'm' || fields[11][0] == 'M' ||
                 fields[11][0] == 'c' || fields[11][0] == 'C'))
    result->monospace = TRUE;

  if (is_wild (family) && wild_family (family))
    result->families = one_family (wild_family (family));
  else if (is_wild (family))
    result->families = one_family (result->monospace ? "monospace"
                                                       : "sans-serif");
  else if (xlfd_family (family))
    result->families = one_family (xlfd_family (family));
  else
    {
      /*
       * A family of its own, which fontconfig may well have -- under its
       * own name, or with its foundry in front, which is how FreeType
       * names bitmap fonts when built to ("xos4 Terminus"). If it asks
       * for fixed width, whatever stands in for it had better be too.
       */
      gint n_families = 0;

      result->families = g_new0 (gchar *, 4);
      result->families[n_families++] = g_strdup (family);
      if (n > 1 && !is_wild (fields[1]))
        result->families[n_families++] = g_strconcat (fields[1], " ", family, NULL);
      if (result->monospace)
        result->families[n_families++] = g_strdup ("monospace");
    }

  /* X's "medium" is the regular weight, not a modern name's 500. */
  if (n > 3 && !is_wild (fields[3]) && g_strcasecmp (fields[3], "medium") != 0)
    parse_weight (fields[3], &result->weight);

  if (n > 4)
    {
      if (g_strcasecmp (fields[4], "i") == 0)
        result->slant = GDK_FT_SLANT_ITALIC;
      else if (g_strcasecmp (fields[4], "o") == 0)
        result->slant = GDK_FT_SLANT_OBLIQUE;
    }

  /*
   * The pixel size says exactly what was wanted. Failing that, the point
   * size at the resolution given, or at 75 dpi, truncated as the server
   * did: 12 points at 75 dpi was a 12 pixel font. A size of 0 is how X
   * lists a font it can draw at any size, and means no size here.
   */
  if (n > 7 && !is_wild (fields[7]) && atoi (fields[7]) > 0)
    result->pixel_size = atoi (fields[7]);
  else if (n > 8 && !is_wild (fields[8]) && atoi (fields[8]) > 0)
    {
      gdouble dpi = GDK_FT_XLFD_DPI;

      if (n > 10 && !is_wild (fields[10]) && atoi (fields[10]) > 0)
        dpi = atoi (fields[10]);
      result->pixel_size = floor (atoi (fields[8]) * dpi / 720.0);
    }

  if (result->pixel_size < 1)
    result->pixel_size = DEFAULT_XLFD_PIXELS;

  g_strfreev (fields);

  return TRUE;
}

static gboolean
parse_modern (const gchar *name,
              gdouble      dpi,
              GdkFTName   *result)
{
  gchar **words, **families;
  GString *family;
  gint n, last, i, n_families;

  words = g_strsplit (name, " ", 0);
  for (n = 0; words[n]; n++)
    ;

  /* The trailing words are the size and the style; the rest is family. */
  last = n;
  while (last > 1)
    {
      const gchar *word = words[last - 1];

      if (*word == '\0')
        {
          last--;
          continue;
        }
      if (result->pixel_size == 0 &&
          parse_size (word, dpi, &result->pixel_size, &result->points))
        {
          last--;
          continue;
        }
      if (parse_weight (word, &result->weight) ||
          parse_slant (word, &result->slant) ||
          parse_width (word, &result->width))
        {
          last--;
          continue;
        }
      break;
    }

  family = g_string_new ("");
  for (i = 0; i < last; i++)
    {
      if (*words[i] == '\0')
        continue;
      if (family->len > 0)
        g_string_append_c (family, ' ');
      g_string_append (family, words[i]);
    }
  g_strfreev (words);

  families = g_strsplit (family->str, ",", 0);
  g_string_free (family, TRUE);

  for (i = 0, n_families = 0; families[i]; i++)
    {
      gchar *f = g_strstrip (families[i]);

      if (*f == '\0')
        g_free (f);
      else if (generic_family (f))
        {
          families[n_families++] = g_strdup (generic_family (f));
          g_free (f);
        }
      else
        families[n_families++] = f;
    }
  families[n_families] = NULL;

  if (n_families == 0)
    {
      g_strfreev (families);
      return FALSE;
    }

  result->families = families;

  if (result->pixel_size <= 0)
    {
      result->pixel_size = floor (DEFAULT_POINTS * dpi / 72.0 + 0.5);
      result->points = DEFAULT_POINTS;
    }

  return TRUE;
}

gboolean
_gdk_ft_name_parse (const gchar *name,
                    gdouble      dpi,
                    GdkFTName   *result)
{
  gchar *trimmed;
  gboolean ok;

  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (result != NULL, FALSE);

  name_init (result);

  if (dpi <= 0)
    dpi = 96;

  trimmed = g_strstrip (g_strdup (name));

  if (*trimmed == '\0')
    ok = FALSE;
  else if (*trimmed == '-')
    ok = parse_xlfd (trimmed, result);
  else if (*trimmed == '*' || *trimmed == '?')
    {
      gchar *xlfd = xlfd_from_pattern (trimmed);

      ok = parse_xlfd (xlfd, result);
      g_free (xlfd);
    }
  else if (parse_x_alias (trimmed, result))
    ok = TRUE;
  else
    ok = parse_modern (trimmed, dpi, result);

  g_free (trimmed);

  return ok;
}

void
_gdk_ft_name_clear (GdkFTName *name)
{
  g_return_if_fail (name != NULL);

  g_strfreev (name->families);
  name->families = NULL;
}

/* What a name was understood as, for tests and for looking at. */
gchar *
_gdk_ft_name_to_string (const GdkFTName *name)
{
  GString *string = g_string_new ("");
  gchar *result;
  gsize i;

  g_return_val_if_fail (name != NULL, NULL);

  for (i = 0; name->families && name->families[i]; i++)
    {
      if (i > 0)
        g_string_append_c (string, ',');
      g_string_append (string, name->families[i]);
    }

  if (name->weight != 400)
    g_string_sprintfa (string, " weight=%d", name->weight);
  if (name->slant == GDK_FT_SLANT_ITALIC)
    g_string_append (string, " italic");
  else if (name->slant == GDK_FT_SLANT_OBLIQUE)
    g_string_append (string, " oblique");
  if (name->width != 100)
    g_string_sprintfa (string, " width=%d", name->width);
  if (name->monospace)
    g_string_append (string, " mono");
  g_string_sprintfa (string, " %gpx", name->pixel_size);

  result = string->str;
  g_string_free (string, FALSE);

  return result;
}


/* How a name says a weight and a width, one word to each. */
static const gchar *
weight_word (gint weight)
{
  switch (weight)
    {
    case 100: return "Thin";
    case 200: return "Extra-Light";
    case 300: return "Light";
    case 500: return "Medium";
    case 600: return "Semi-Bold";
    case 700: return "Bold";
    case 800: return "Extra-Bold";
    case 900: return "Black";
    default:  return NULL;
    }
}

static const gchar *
width_word (gint width)
{
  switch (width)
    {
    case 50:  return "Ultra-Condensed";
    case 63:  return "Extra-Condensed";
    case 75:  return "Condensed";
    case 87:  return "Semi-Condensed";
    case 113: return "Semi-Expanded";
    case 125: return "Expanded";
    case 150: return "Extra-Expanded";
    case 200: return "Ultra-Expanded";
    default:  return NULL;
    }
}

gchar *
_gdk_ft_style_name (gint weight,
                    gint slant,
                    gint width)
{
  GString *string = g_string_new ("");
  const gchar *words[3];
  gchar *result;
  gint i;

  words[0] = width_word (width);
  words[1] = weight_word (weight);
  words[2] = slant == GDK_FT_SLANT_ITALIC ? "Italic" :
             slant == GDK_FT_SLANT_OBLIQUE ? "Oblique" : NULL;

  for (i = 0; i < 3; i++)
    if (words[i])
      {
        if (string->len)
          g_string_append_c (string, ' ');
        g_string_append (string, words[i]);
      }
  if (!string->len)
    g_string_append (string, "Regular");

  result = string->str;
  g_string_free (string, FALSE);

  return result;
}

/* Whether a name, parsed, has the family it was written with first. */
static gboolean
name_has_family (const gchar *name,
                 const gchar *family)
{
  GdkFTName parsed;
  const gchar *want = generic_family (family) ? generic_family (family) : family;
  gboolean same;

  if (!_gdk_ft_name_parse (name, 96, &parsed))
    return FALSE;

  same = parsed.families && parsed.families[0] &&
    g_strcasecmp (parsed.families[0], want) == 0;
  _gdk_ft_name_clear (&parsed);

  return same;
}

gchar *
_gdk_ft_name_compose (const gchar *family,
                      gint         weight,
                      gint         slant,
                      gint         width,
                      gdouble      size,
                      gboolean     pixels)
{
  gchar *style, *size_text, *name;

  g_return_val_if_fail (family != NULL, NULL);

  style = _gdk_ft_style_name (weight, slant, width);
  if (strcmp (style, "Regular") == 0)
    {
      g_free (style);
      style = g_strdup ("");
    }

  if (pixels)
    size_text = g_strdup_printf ("%dpx", (gint) floor (size + 0.5));
  else
    size_text = g_strdup_printf ("%g", floor (size * 10 + 0.5) / 10);

  name = g_strconcat (family, *style ? " " : "", style, " ", size_text, NULL);

  /*
   * A family whose name ends in a style word or a number -- "Noto Sans
   * Light", "LM Roman 10" -- would lose it to the style or the size. A
   * comma after the family keeps it whole.
   */
  if (!name_has_family (name, family))
    {
      g_free (name);
      name = g_strconcat (family, ",", *style ? " " : "", style, " ",
                          size_text, NULL);
    }

  g_free (style);
  g_free (size_text);

  return name;
}


#include <fontconfig/fontconfig.h>

static gboolean fc_ready;
static gboolean fc_failed;

static gboolean
gdk_ft_fc_init (void)
{
  if (fc_ready)
    return TRUE;
  if (fc_failed)
    return FALSE;

  if (!FcInit ())
    {
      g_warning ("Could not initialise fontconfig; scalable fonts are off");
      fc_failed = TRUE;
      return FALSE;
    }

  fc_ready = TRUE;

  return TRUE;
}

/* What was asked for, with the configuration's substitutions made. */
static FcPattern *
gdk_ft_name_pattern (const GdkFTName *name,
                     gdouble          dpi)
{
  FcPattern *pattern = FcPatternCreate ();
  gboolean generic = FALSE;
  gint i;

  for (i = 0; name->families[i]; i++)
    {
      FcPatternAddString (pattern, FC_FAMILY,
                          (const FcChar8 *) name->families[i]);
      if (generic_family (name->families[i]))
        generic = TRUE;
    }
  if (name->monospace)
    {
      if (!generic)
        FcPatternAddString (pattern, FC_FAMILY, (const FcChar8 *) "monospace");
      FcPatternAddInteger (pattern, FC_SPACING, FC_MONO);
    }

  FcPatternAddInteger (pattern, FC_WEIGHT, FcWeightFromOpenType (name->weight));
  FcPatternAddInteger (pattern, FC_SLANT, name->slant);
  FcPatternAddInteger (pattern, FC_WIDTH, name->width);
  FcPatternAddDouble (pattern, FC_PIXEL_SIZE, name->pixel_size);
  FcPatternAddDouble (pattern, FC_SIZE, name->pixel_size * 72.0 / dpi);
  FcPatternAddDouble (pattern, FC_DPI, dpi);

  /*
   * Outlines, and not colour. Fontconfig weighs these above the family,
   * so a bitmap face or an emoji font is never chosen while an outline
   * font that will do exists.
   */
  FcPatternAddBool (pattern, FC_SCALABLE, FcTrue);
  FcPatternAddBool (pattern, FC_COLOR, FcFalse);

  FcConfigSubstitute (NULL, pattern, FcMatchPattern);
  FcDefaultSubstitute (pattern);

  return pattern;
}

/* A font as fontconfig describes it after matching, opened. */
static GdkFTFont *
gdk_ft_open_pattern (FcPattern *pattern)
{
  GdkFTOptions options;
  FcChar8 *file;
  FcMatrix *matrix;
  FcBool b;
  gdouble pixel_size;
  gint index, i;

  if (FcPatternGetBool (pattern, FC_SCALABLE, 0, &b) == FcResultMatch && !b)
    return NULL;
  if (FcPatternGetBool (pattern, FC_COLOR, 0, &b) == FcResultMatch && b)
    return NULL;

  if (FcPatternGetString (pattern, FC_FILE, 0, &file) != FcResultMatch)
    return NULL;
  if (FcPatternGetInteger (pattern, FC_INDEX, 0, &index) != FcResultMatch)
    index = 0;
  if (FcPatternGetDouble (pattern, FC_PIXEL_SIZE, 0, &pixel_size) != FcResultMatch ||
      pixel_size <= 0)
    return NULL;

  _gdk_ft_options_init (&options);

  if (FcPatternGetBool (pattern, FC_ANTIALIAS, 0, &b) == FcResultMatch)
    options.antialias = b;
  if (FcPatternGetBool (pattern, FC_HINTING, 0, &b) == FcResultMatch)
    options.hinting = b;
  if (FcPatternGetInteger (pattern, FC_HINT_STYLE, 0, &i) == FcResultMatch)
    options.hint_style = CLAMP (i, GDK_FT_HINT_NONE, GDK_FT_HINT_FULL);
  if (FcPatternGetBool (pattern, FC_AUTOHINT, 0, &b) == FcResultMatch)
    options.autohint = b;
  if (FcPatternGetBool (pattern, FC_EMBOLDEN, 0, &b) == FcResultMatch)
    options.embolden = b;
  if (FcPatternGetMatrix (pattern, FC_MATRIX, 0, &matrix) == FcResultMatch)
    {
      options.matrix[0] = matrix->xx;
      options.matrix[1] = matrix->xy;
      options.matrix[2] = matrix->yx;
      options.matrix[3] = matrix->yy;
    }

  return _gdk_ft_font_open ((const gchar *) file, index, pixel_size, &options);
}

/*
 * Falling back. Everything installed, in the order the configuration
 * prefers for this request, is looked at in turn for the first font
 * that has the character. The same character therefore always comes
 * from the same font, whatever was drawn before it. The list is only
 * made the first time a character is missing, which for most fonts in
 * most programs is never.
 */
typedef struct
{
  FcPattern *pattern;
  FcFontSet *sorted;
  GdkFTFont **fonts;
  guchar *unusable;
} GdkFTFallback;

static void
gdk_ft_fallback_sort (GdkFTFallback *fallback)
{
  FcResult result;

  fallback->sorted = FcFontSort (NULL, fallback->pattern, FcTrue, NULL, &result);
  if (!fallback->sorted)
    fallback->sorted = FcFontSetCreate ();

  fallback->fonts = g_new0 (GdkFTFont *, MAX (fallback->sorted->nfont, 1));
  fallback->unusable = g_new0 (guchar, MAX (fallback->sorted->nfont, 1));
}

static GdkFTFont *
gdk_ft_fallback (gpointer data,
                 guint32  codepoint)
{
  GdkFTFallback *fallback = data;
  gint i;

  if (!fallback->sorted)
    gdk_ft_fallback_sort (fallback);

  for (i = 0; i < fallback->sorted->nfont; i++)
    {
      FcPattern *candidate = fallback->sorted->fonts[i];
      FcCharSet *charset;

      if (fallback->unusable[i])
        continue;

      if (FcPatternGetCharSet (candidate, FC_CHARSET, 0, &charset) != FcResultMatch ||
          !FcCharSetHasChar (charset, codepoint))
        continue;

      if (!fallback->fonts[i])
        {
          FcPattern *prepared = FcFontRenderPrepare (NULL, fallback->pattern,
                                                     candidate);

          fallback->fonts[i] = prepared ? gdk_ft_open_pattern (prepared) : NULL;
          if (prepared)
            FcPatternDestroy (prepared);

          if (!fallback->fonts[i])
            {
              fallback->unusable[i] = TRUE;
              continue;
            }
        }

      /* The configuration's idea of coverage and the face's can differ. */
      if (_gdk_ft_font_glyph (fallback->fonts[i], codepoint))
        return _gdk_ft_font_ref (fallback->fonts[i]);
    }

  return NULL;
}

static void
gdk_ft_fallback_free (gpointer data)
{
  GdkFTFallback *fallback = data;
  gint i;

  if (fallback->sorted)
    {
      for (i = 0; i < fallback->sorted->nfont; i++)
        if (fallback->fonts[i])
          _gdk_ft_font_unref (fallback->fonts[i]);
      FcFontSetDestroy (fallback->sorted);
    }

  FcPatternDestroy (fallback->pattern);
  g_free (fallback->fonts);
  g_free (fallback->unusable);
  g_free (fallback);
}

/*
 * Bitmap fonts.
 *
 * A name reaches one only by naming a family that is installed as
 * bitmaps and nothing else: "Misc Fixed 13px", or an X name whose
 * family is not one of the ones translated to Sans, Serif or Monospace.
 * The generic families and the translated X names always get outlines,
 * so a bitmap font never takes over a program's default font.
 */

static gint round_weight (gint weight);
static gint round_width  (gint width);

#define FAMILY_MISSING  0
#define FAMILY_SCALABLE 1
#define FAMILY_BITMAP   2

/* A bitmap face that can be drawn: characters in Unicode, and not colour. */
static gboolean
gdk_ft_bitmap_usable (FcPattern *face)
{
  FcCharSet *charset;
  FcBool b;

  if (FcPatternGetBool (face, FC_SCALABLE, 0, &b) == FcResultMatch && b)
    return FALSE;
  if (FcPatternGetBool (face, FC_COLOR, 0, &b) == FcResultMatch && b)
    return FALSE;
  if (FcPatternGetCharSet (face, FC_CHARSET, 0, &charset) != FcResultMatch ||
      FcCharSetCount (charset) == 0)
    return FALSE;

  return TRUE;
}

/*
 * Where bitmap fonts are found.
 *
 * Distributions now have the font configuration reject bitmap fonts,
 * with an exception here and there -- Terminus ships one -- to keep them
 * out of substitution and fallback, where a pixelated face standing in
 * for Helvetica is a real nuisance. Here a bitmap font is only ever
 * drawn when its own name asks for it: never for a generic family, never
 * to fill in a missing character. That reason does not apply, and
 * honouring the rule would take away the X world's fonts that the X
 * server itself still draws.
 *
 * So bitmap fonts are looked for in a second configuration: the same
 * directories, read from the same caches, without the rules that accept
 * and reject. Outlines are still found as the configuration says.
 */
static FcConfig *bitmap_config;
static gboolean bitmap_config_tried;

static void
xml_append_escaped (GString     *xml,
                    const gchar *text)
{
  for (; *text; text++)
    switch (*text)
      {
      case '&':  g_string_append (xml, "&amp;");  break;
      case '<':  g_string_append (xml, "&lt;");   break;
      case '>':  g_string_append (xml, "&gt;");   break;
      case '"':  g_string_append (xml, "&quot;"); break;
      default:   g_string_append_c (xml, *text);  break;
      }
}

static void
xml_append_list (GString     *xml,
                 FcStrList   *list,
                 const gchar *element)
{
  FcChar8 *item;

  if (!list)
    return;
  while ((item = FcStrListNext (list)) != NULL)
    {
      g_string_sprintfa (xml, "  <%s>", element);
      xml_append_escaped (xml, (const gchar *) item);
      g_string_sprintfa (xml, "</%s>\n", element);
    }
  FcStrListDone (list);
}

static FcConfig *
gdk_ft_bitmap_config (void)
{
  FcConfig *current = FcConfigGetCurrent ();
  GString *xml;

  if (bitmap_config_tried)
    return bitmap_config ? bitmap_config : current;
  bitmap_config_tried = TRUE;

  xml = g_string_new ("<?xml version=\"1.0\"?>\n<fontconfig>\n");
  xml_append_list (xml, FcConfigGetFontDirs (current), "dir");
  xml_append_list (xml, FcConfigGetCacheDirs (current), "cachedir");
  g_string_append (xml, "</fontconfig>\n");

  bitmap_config = FcConfigCreate ();
  if (!bitmap_config ||
      !FcConfigParseAndLoadFromMemory (bitmap_config,
                                       (const FcChar8 *) xml->str, FcFalse) ||
      !FcConfigBuildFonts (bitmap_config))
    {
      /* Then bitmap fonts are only what the configuration lets through. */
      if (bitmap_config)
        FcConfigDestroy (bitmap_config);
      bitmap_config = NULL;
    }
  g_string_free (xml, TRUE);

  return bitmap_config ? bitmap_config : current;
}

/* The faces of a family, as the configuration has them. */
static FcFontSet *
gdk_ft_family_faces (const gchar *family)
{
  FcPattern *pattern = FcPatternCreate ();
  FcObjectSet *objects;
  FcFontSet *faces;

  FcPatternAddString (pattern, FC_FAMILY, (const FcChar8 *) family);
  objects = FcObjectSetBuild (FC_WEIGHT, FC_SLANT, FC_WIDTH, FC_SCALABLE,
                              FC_COLOR, NULL);
  faces = FcFontList (NULL, pattern, objects);
  FcObjectSetDestroy (objects);
  FcPatternDestroy (pattern);

  return faces;
}

/* Whether the configuration has a family with outlines. */
static gboolean
gdk_ft_has_outlines (const gchar *family)
{
  FcFontSet *faces = gdk_ft_family_faces (family);
  gboolean outlines = FALSE;
  gint i;

  for (i = 0; faces && i < faces->nfont && !outlines; i++)
    {
      FcBool scalable, color;

      if (FcPatternGetBool (faces->fonts[i], FC_COLOR, 0, &color) == FcResultMatch &&
          color)
        continue;
      outlines = FcPatternGetBool (faces->fonts[i], FC_SCALABLE, 0,
                                   &scalable) == FcResultMatch && scalable;
    }
  if (faces)
    FcFontSetDestroy (faces);

  return outlines;
}

/* A face's style, rounded to the steps a name can say. */
static void
gdk_ft_face_style (FcPattern *face,
                   GdkFTName *style)
{
  gint value;

  style->weight = 400;
  style->slant = GDK_FT_SLANT_ROMAN;
  style->width = 100;
  if (FcPatternGetInteger (face, FC_WEIGHT, 0, &value) == FcResultMatch)
    style->weight = round_weight (FcWeightToOpenType (value));
  if (FcPatternGetInteger (face, FC_SLANT, 0, &value) == FcResultMatch)
    style->slant = value >= GDK_FT_SLANT_OBLIQUE ? GDK_FT_SLANT_OBLIQUE :
                   value >= GDK_FT_SLANT_ITALIC ? GDK_FT_SLANT_ITALIC :
                   GDK_FT_SLANT_ROMAN;
  if (FcPatternGetInteger (face, FC_WIDTH, 0, &value) == FcResultMatch)
    style->width = round_width (value);
}

static gint
gdk_ft_style_distance (const GdkFTName *have,
                       const GdkFTName *want)
{
  return ABS (have->weight - want->weight) +
         10 * ABS (have->width - want->width) +
         (have->slant != want->slant ? 10000 : 0);
}

/*
 * The bitmap faces installed, read once.
 *
 * FreeType can put a PCF font's foundry in front of its family ("xos4
 * Terminus"), so the same font in two formats, or read by two builds of
 * FreeType, comes under two names. Each face is filed under its family
 * without it, so it is one family, as it was to the X server; and under
 * the longer name only where the shorter belongs to a family with
 * outlines, which would hide it -- here the bitmap Courier stays "Adobe
 * Courier". A name may use either.
 */
typedef struct
{
  gchar *family;
  gchar *long_family;           /* as FreeType named it */
  GdkFTName style;              /* weight, slant and width, rounded */
  gdouble size;                 /* the pixel size it was drawn at */
  gboolean monospace;
  gint coverage;                /* characters it has */
  gchar *file;
  gint index;
  gboolean unusable;            /* FreeType could not open it */
} GdkFTBitmap;

static GdkFTBitmap *bitmaps;
static gint n_bitmaps = -1;

static const gchar *gdk_ft_family_of    (FcPattern   *pattern);
static gboolean     family_is_file_name (const gchar *family);

/* The family without the foundry FreeType may have put in front. */
static const gchar *
gdk_ft_short_family (FcPattern   *face,
                     const gchar *family)
{
  FcChar8 *foundry;
  gsize length;

  if (FcPatternGetString (face, FC_FOUNDRY, 0, &foundry) != FcResultMatch)
    return family;
  length = strlen ((const gchar *) foundry);
  if (g_strncasecmp (family, (const gchar *) foundry, length) != 0 ||
      family[length] != ' ' || family[length + 1] == '\0')
    return family;

  return family + length + 1;
}

static void
gdk_ft_load_bitmaps (void)
{
  FcPattern *pattern;
  FcObjectSet *objects;
  FcFontSet *fonts;
  GHashTable *outlines;
  gint i;

  if (n_bitmaps >= 0)
    return;
  n_bitmaps = 0;

  pattern = FcPatternCreate ();
  FcPatternAddBool (pattern, FC_SCALABLE, FcFalse);
  objects = FcObjectSetBuild (FC_FAMILY, FC_FAMILYLANG, FC_FOUNDRY, FC_FILE,
                              FC_INDEX, FC_WEIGHT, FC_SLANT, FC_WIDTH,
                              FC_PIXEL_SIZE, FC_SPACING, FC_CHARSET,
                              FC_SCALABLE, FC_COLOR, NULL);
  fonts = FcFontList (gdk_ft_bitmap_config (), pattern, objects);
  FcObjectSetDestroy (objects);
  FcPatternDestroy (pattern);

  bitmaps = g_new0 (GdkFTBitmap, fonts ? MAX (fonts->nfont, 1) : 1);
  outlines = g_hash_table_new (g_str_hash, g_str_equal);

  for (i = 0; fonts && i < fonts->nfont; i++)
    {
      FcPattern *face = fonts->fonts[i];
      const gchar *family = gdk_ft_family_of (face);
      const gchar *short_family;
      GdkFTBitmap *b;
      FcCharSet *charset;
      FcChar8 *file;
      gpointer known;
      gint spacing;

      if (!gdk_ft_bitmap_usable (face) || !family || family[0] == '.' ||
          family_is_file_name (family) ||
          FcPatternGetString (face, FC_FILE, 0, &file) != FcResultMatch)
        continue;

      /* Whether the short name is taken by outlines, asked once each. */
      short_family = gdk_ft_short_family (face, family);
      if (short_family != family &&
          !g_hash_table_lookup_extended (outlines, short_family, NULL, &known))
        {
          known = GINT_TO_POINTER (gdk_ft_has_outlines (short_family));
          g_hash_table_insert (outlines, g_strdup (short_family), known);
        }
      if (short_family != family && GPOINTER_TO_INT (known))
        short_family = family;

      b = &bitmaps[n_bitmaps++];
      b->family = g_strdup (short_family);
      b->long_family = g_strdup (family);
      gdk_ft_face_style (face, &b->style);
      if (FcPatternGetDouble (face, FC_PIXEL_SIZE, 0, &b->size) != FcResultMatch)
        b->size = 0;
      b->size = floor (b->size + 0.5);
      b->monospace = FcPatternGetInteger (face, FC_SPACING, 0, &spacing) ==
                     FcResultMatch && spacing >= FC_MONO;
      if (FcPatternGetCharSet (face, FC_CHARSET, 0, &charset) == FcResultMatch)
        b->coverage = FcCharSetCount (charset);
      b->file = g_strdup ((const gchar *) file);
      if (FcPatternGetInteger (face, FC_INDEX, 0, &b->index) != FcResultMatch)
        b->index = 0;
    }

  if (fonts)
    FcFontSetDestroy (fonts);
  g_hash_table_foreach (outlines, (GHFunc) g_free, NULL);
  g_hash_table_destroy (outlines);
}

static gboolean
gdk_ft_bitmap_is (const GdkFTBitmap *b,
                  const gchar       *family)
{
  return g_strcasecmp (family, b->family) == 0 ||
         g_strcasecmp (family, b->long_family) == 0;
}

/*
 * Whether a family is installed with outlines, as the configuration
 * has them, or only as bitmaps, wherever they are, or not at all.
 */
static gint
gdk_ft_family_kind (const gchar *family)
{
  gint i;

  if (gdk_ft_has_outlines (family))
    return FAMILY_SCALABLE;

  gdk_ft_load_bitmaps ();
  for (i = 0; i < n_bitmaps; i++)
    if (gdk_ft_bitmap_is (&bitmaps[i], family))
      return FAMILY_BITMAP;

  return FAMILY_MISSING;
}

/*
 * Of a bitmap family's faces, the one a name asks for: the closest
 * style; then the closest size, the smaller of two as near, as the X
 * server chose; then the one with the most characters, so that of two
 * files of the same font the ISO10646 one is drawn rather than the
 * Latin-1 one. One FreeType cannot open is passed over for the next.
 */
static gboolean
gdk_ft_bitmap_better (const GdkFTBitmap *a,
                      const GdkFTBitmap *b,
                      const GdkFTName   *want)
{
  gint sa = gdk_ft_style_distance (&a->style, want);
  gint sb = gdk_ft_style_distance (&b->style, want);
  gdouble da = ABS (a->size - want->pixel_size);
  gdouble db = ABS (b->size - want->pixel_size);

  if (sa != sb)
    return sa < sb;
  if (da != db)
    return da < db;
  if (a->size != b->size)
    return a->size < b->size;
  return a->coverage > b->coverage;
}

static GdkFTFont *
gdk_ft_open_bitmap (const gchar     *family,
                    const GdkFTName *name,
                    gboolean        *monospace)
{
  gdk_ft_load_bitmaps ();

  for (;;)
    {
      GdkFTBitmap *best = NULL;
      GdkFTFont *font;
      gint i;

      for (i = 0; i < n_bitmaps; i++)
        if (!bitmaps[i].unusable && gdk_ft_bitmap_is (&bitmaps[i], family) &&
            (!best || gdk_ft_bitmap_better (&bitmaps[i], best, name)))
          best = &bitmaps[i];

      if (!best)
        return NULL;

      font = _gdk_ft_font_open (best->file, best->index,
                                best->size > 0 ? best->size : name->pixel_size,
                                NULL);
      if (font)
        {
          *monospace = best->monospace;
          return font;
        }
      best->unusable = TRUE;
    }
}

/*
 * The family a name's families decide on: the first of its own that is
 * installed, before any generic one. NULL unless that one is installed
 * only as bitmaps.
 */
static const gchar *
gdk_ft_bitmap_family (const GdkFTName *name)
{
  gint i;

  for (i = 0; name->families[i] && !generic_family (name->families[i]); i++)
    switch (gdk_ft_family_kind (name->families[i]))
      {
      case FAMILY_SCALABLE:
        return NULL;
      case FAMILY_BITMAP:
        return name->families[i];
      default:
        break;
      }

  return NULL;
}

GdkFTSet *
_gdk_ft_set_open_name (const gchar *name,
                       gdouble      dpi,
                       gint         scale)
{
  GdkFTFallback *fallback;
  GdkFTName parsed;
  GdkFTFont *font = NULL;
  GdkFTSet *set;
  FcPattern *match;
  FcResult result;
  const gchar *bitmap_family;
  gint i;

  g_return_val_if_fail (name != NULL, NULL);

  if (dpi <= 0)
    dpi = 96;
  if (scale < 1)
    scale = 1;

  if (!gdk_ft_fc_init ())
    return NULL;

  if (!_gdk_ft_name_parse (name, dpi, &parsed))
    return NULL;

  /*
   * Bigger by the scale: the pixel size is the real one, so the font
   * configuration's rules about sizes see what will be on the screen,
   * and the resolution goes with it so the size in points stays the
   * one the name gave.
   */
  parsed.pixel_size *= scale;
  dpi *= scale;

  fallback = g_new0 (GdkFTFallback, 1);

  bitmap_family = gdk_ft_bitmap_family (&parsed);
  if (bitmap_family)
    {
      gboolean monospace = FALSE;

      font = gdk_ft_open_bitmap (bitmap_family, &parsed, &monospace);

      /*
       * What a bitmap font does not have comes from outlines, fixed width
       * ones if it is fixed width. The family itself is no use to that
       * search, which is only over outlines.
       */
      g_strfreev (parsed.families);
      parsed.families = g_new0 (gchar *, 2);
      parsed.families[0] = g_strdup (monospace ? "monospace" : "sans-serif");
    }

  fallback->pattern = gdk_ft_name_pattern (&parsed, dpi);
  _gdk_ft_name_clear (&parsed);

  if (!font)
    {
      match = FcFontMatch (NULL, fallback->pattern, &result);
      if (match)
        {
          font = gdk_ft_open_pattern (match);
          FcPatternDestroy (match);
        }
    }

  /*
   * The best match could not be opened: take the first thing in the
   * configuration's order that can be, rather than nothing.
   */
  if (!font)
    {
      gdk_ft_fallback_sort (fallback);

      for (i = 0; !font && i < fallback->sorted->nfont; i++)
        {
          FcPattern *prepared = FcFontRenderPrepare (NULL, fallback->pattern,
                                                     fallback->sorted->fonts[i]);

          if (prepared)
            {
              font = gdk_ft_open_pattern (prepared);
              FcPatternDestroy (prepared);
            }
        }
    }

  if (!font)
    {
      gdk_ft_fallback_free (fallback);
      return NULL;
    }

  set = _gdk_ft_set_new (font, gdk_ft_fallback, fallback, gdk_ft_fallback_free);
  _gdk_ft_font_unref (font);

  return set;
}

/*
 * Listing what is installed.
 */

/* A face's family, in English if it has an English name. */
static const gchar *
gdk_ft_family_of (FcPattern *pattern)
{
  FcChar8 *family, *lang;
  gint i;

  for (i = 0; FcPatternGetString (pattern, FC_FAMILYLANG, i, &lang) == FcResultMatch; i++)
    if (strcmp ((const gchar *) lang, "en") == 0 &&
        FcPatternGetString (pattern, FC_FAMILY, i, &family) == FcResultMatch)
      return (const gchar *) family;

  if (FcPatternGetString (pattern, FC_FAMILY, 0, &family) == FcResultMatch)
    return (const gchar *) family;

  return NULL;
}

static gint
gdk_ft_compare_names (gconstpointer a,
                      gconstpointer b)
{
  return g_strcasecmp (*(const gchar **) a, *(const gchar **) b);
}

/* A name FreeType made from a file's, for a font that has no family. */
static gboolean
family_is_file_name (const gchar *family)
{
  return strstr (family, ".pcf") || strstr (family, ".bdf") ||
         strstr (family, ".otb");
}

/*
 * Every family with outlines, as the configuration has them, and, for a
 * caller who can tell them apart, every family installed only as
 * bitmaps. A family with outlines is that, whatever bitmaps it has.
 */
static void
gdk_ft_add_family (const gchar *family,
                   gint         kind,
                   GHashTable  *seen,
                   GPtrArray   *names)
{
  gchar *copy;

  if (g_hash_table_lookup (seen, family))
    return;
  copy = g_strdup (family);
  g_hash_table_insert (seen, copy, GINT_TO_POINTER (kind));
  g_ptr_array_add (names, copy);
}

gchar **
_gdk_ft_list_families (gboolean   monospace,
                       gboolean **bitmap)
{
  FcPattern *pattern;
  FcObjectSet *objects;
  FcFontSet *fonts;
  GHashTable *seen;             /* family -> FAMILY_SCALABLE or FAMILY_BITMAP */
  GPtrArray *names;
  gchar **result;
  gint i;

  if (bitmap)
    *bitmap = NULL;

  if (!gdk_ft_fc_init ())
    {
      if (bitmap)
        *bitmap = g_new0 (gboolean, 1);
      return g_new0 (gchar *, 1);
    }

  seen = g_hash_table_new (g_str_hash, g_str_equal);
  names = g_ptr_array_new ();

  pattern = FcPatternCreate ();
  FcPatternAddBool (pattern, FC_SCALABLE, FcTrue);
  objects = FcObjectSetBuild (FC_FAMILY, FC_FAMILYLANG, FC_COLOR, FC_SPACING,
                              NULL);
  fonts = FcFontList (NULL, pattern, objects);
  FcObjectSetDestroy (objects);
  FcPatternDestroy (pattern);

  for (i = 0; fonts && i < fonts->nfont; i++)
    {
      FcPattern *face = fonts->fonts[i];
      const gchar *family = gdk_ft_family_of (face);
      gint spacing;
      FcBool color;

      /* Colour fonts are never drawn here, and dotted names are hidden. */
      if ((FcPatternGetBool (face, FC_COLOR, 0, &color) == FcResultMatch && color) ||
          !family || family[0] == '.')
        continue;
      if (monospace &&
          !(FcPatternGetInteger (face, FC_SPACING, 0, &spacing) == FcResultMatch &&
            spacing >= FC_MONO))
        continue;
      gdk_ft_add_family (family, FAMILY_SCALABLE, seen, names);
    }
  if (fonts)
    FcFontSetDestroy (fonts);

  if (bitmap)
    {
      gdk_ft_load_bitmaps ();
      for (i = 0; i < n_bitmaps; i++)
        if (!monospace || bitmaps[i].monospace)
          gdk_ft_add_family (bitmaps[i].family, FAMILY_BITMAP, seen, names);
    }

  qsort (names->pdata, names->len, sizeof (gpointer), gdk_ft_compare_names);

  if (bitmap)
    {
      *bitmap = g_new0 (gboolean, names->len + 1);
      for (i = 0; i < (gint) names->len; i++)
        (*bitmap)[i] = GPOINTER_TO_INT (g_hash_table_lookup (seen,
                                        names->pdata[i])) == FAMILY_BITMAP;
    }
  g_hash_table_destroy (seen);

  g_ptr_array_add (names, NULL);
  result = (gchar **) names->pdata;
  g_ptr_array_free (names, FALSE);

  return result;
}

gchar *
_gdk_ft_resolve_family (const gchar *family)
{
  FcPattern *pattern, *match;
  FcResult result;
  const gchar *generic;
  gchar *resolved = NULL;

  g_return_val_if_fail (family != NULL, NULL);

  if (!gdk_ft_fc_init ())
    return g_strdup (family);

  /*
   * Bitmaps are only ever what their own family's name asks for, which
   * may be the longer name FreeType gave them: it resolves to the name
   * they are listed under.
   */
  if (!generic_family (family) && gdk_ft_family_kind (family) == FAMILY_BITMAP)
    {
      gint i;

      for (i = 0; i < n_bitmaps; i++)
        if (gdk_ft_bitmap_is (&bitmaps[i], family))
          return g_strdup (bitmaps[i].family);
    }

  generic = generic_family (family);
  pattern = FcPatternCreate ();
  FcPatternAddString (pattern, FC_FAMILY,
                      (const FcChar8 *) (generic ? generic : family));
  FcPatternAddBool (pattern, FC_SCALABLE, FcTrue);
  FcPatternAddBool (pattern, FC_COLOR, FcFalse);
  FcConfigSubstitute (NULL, pattern, FcMatchPattern);
  FcDefaultSubstitute (pattern);

  match = FcFontMatch (NULL, pattern, &result);
  if (match)
    {
      const gchar *name = gdk_ft_family_of (match);

      if (name)
        resolved = g_strdup (name);
      FcPatternDestroy (match);
    }
  FcPatternDestroy (pattern);

  return resolved ? resolved : g_strdup (family);
}

/* The nearest step a name can say, for a weight and a width. */
static gint
round_weight (gint weight)
{
  weight = ((weight + 50) / 100) * 100;

  return CLAMP (weight, 100, 900);
}

static gint
round_width (gint width)
{
  gint best = 100, i;

  for (i = 0; i < (gint) N_ELEMENTS (width_words); i++)
    if (ABS (width_words[i].width - width) < ABS (best - width))
      best = width_words[i].width;

  return best;
}

static gint
gdk_ft_compare_styles (gconstpointer a,
                       gconstpointer b)
{
  const GdkFTName *x = a, *y = b;

  if ((x->width != 100) != (y->width != 100))
    return x->width != 100 ? 1 : -1;
  if (x->width != y->width)
    return x->width - y->width;
  if (x->weight != y->weight)
    return x->weight - y->weight;
  return x->slant - y->slant;
}

GdkFTName *
_gdk_ft_list_styles (const gchar *family,
                     gint        *n_styles)
{
  FcFontSet *fonts;
  GArray *styles;
  gchar *resolved;
  gboolean bitmap;
  gint i, j;

  g_return_val_if_fail (family != NULL, NULL);
  g_return_val_if_fail (n_styles != NULL, NULL);

  *n_styles = 0;

  if (!gdk_ft_fc_init ())
    return NULL;

  styles = g_array_new (FALSE, TRUE, sizeof (GdkFTName));

  /*
   * A family that is not installed has the styles of what stands in; a
   * bitmap-only family has the styles of its bitmaps.
   */
  resolved = _gdk_ft_resolve_family (family);
  bitmap = gdk_ft_family_kind (resolved) == FAMILY_BITMAP;
  fonts = bitmap ? NULL : gdk_ft_family_faces (resolved);

  for (i = 0; bitmap ? i < n_bitmaps : (fonts && i < fonts->nfont); i++)
    {
      GdkFTName style;
      gboolean dup = FALSE;

      memset (&style, 0, sizeof style);
      if (bitmap)
        {
          if (!gdk_ft_bitmap_is (&bitmaps[i], resolved))
            continue;
          style = bitmaps[i].style;
        }
      else
        {
          FcPattern *face = fonts->fonts[i];
          FcBool b;

          if ((FcPatternGetBool (face, FC_COLOR, 0, &b) == FcResultMatch && b) ||
              (FcPatternGetBool (face, FC_SCALABLE, 0, &b) == FcResultMatch && !b))
            continue;
          gdk_ft_face_style (face, &style);
        }

      for (j = 0; j < (gint) styles->len; j++)
        {
          GdkFTName *other = &g_array_index (styles, GdkFTName, j);

          if (other->weight == style.weight && other->slant == style.slant &&
              other->width == style.width)
            dup = TRUE;
        }
      if (!dup)
        g_array_append_val (styles, style);
    }

  if (fonts)
    FcFontSetDestroy (fonts);
  g_free (resolved);

  qsort (styles->data, styles->len, sizeof (GdkFTName), gdk_ft_compare_styles);
  *n_styles = styles->len;

  /* GLib 1.2's g_array_free returns nothing: take the data first. */
  {
    GdkFTName *result = (GdkFTName *) styles->data;

    g_array_free (styles, FALSE);
    return result;
  }
}

static gint
gdk_ft_compare_sizes (gconstpointer a,
                      gconstpointer b)
{
  gdouble x = *(const gdouble *) a, y = *(const gdouble *) b;

  return x < y ? -1 : x > y ? 1 : 0;
}

gdouble *
_gdk_ft_list_sizes (const gchar *family,
                    gint         weight,
                    gint         slant,
                    gint         width,
                    gint        *n_sizes)
{
  GArray *sizes;
  gint i, j;

  g_return_val_if_fail (family != NULL, NULL);
  g_return_val_if_fail (n_sizes != NULL, NULL);

  *n_sizes = 0;

  if (!gdk_ft_fc_init () || generic_family (family) ||
      gdk_ft_family_kind (family) != FAMILY_BITMAP)
    return NULL;

  sizes = g_array_new (FALSE, TRUE, sizeof (gdouble));

  for (i = 0; i < n_bitmaps; i++)
    {
      GdkFTBitmap *b = &bitmaps[i];
      gboolean dup = FALSE;

      if (!gdk_ft_bitmap_is (b, family) || b->style.weight != weight ||
          b->style.slant != slant || b->style.width != width)
        continue;
      for (j = 0; j < (gint) sizes->len; j++)
        if (g_array_index (sizes, gdouble, j) == b->size)
          dup = TRUE;
      if (!dup)
        g_array_append_val (sizes, b->size);
    }

  qsort (sizes->data, sizes->len, sizeof (gdouble), gdk_ft_compare_sizes);
  *n_sizes = sizes->len;

  {
    gdouble *result = (gdouble *) sizes->data;

    g_array_free (sizes, FALSE);
    return result;
  }
}
