/* GLIB - Library of useful routines for C programming
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
 * Strings of UTF-8, by the names GLib 2 gave these functions.
 *
 * What is a letter, and what its capital is, is asked of the C
 * library: there are no tables of Unicode here. The C library answers
 * for the locale the program has set, so a program that wants more
 * than ASCII calls setlocale() first, as gtk_set_locale() does.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "glib.h"

static const gchar utf8_skip_data[256] = {
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,1,1,1,1,1,1,1,1
};

/* How many bytes the character that starts with a given byte has. A
 * byte that starts none counts for one, so a walk always moves on. */
const gchar * const g_utf8_skip = utf8_skip_data;

/*
 * The character at str, of which at most max bytes may be read, or
 * as far as a nul if max is negative.
 *
 * (gunichar) -1 for bytes that are not UTF-8, among them the long
 * ways of writing a short character, the surrogates, and anything
 * past U+10FFFF. (gunichar) -2 for a character cut short.
 */
static gunichar
utf8_decode (const gchar *str, gssize max)
{
  const guchar *p = (const guchar *) str;
  gunichar c, min;
  gint i, len;

  if (max == 0)
    return (gunichar) -2;

  c = p[0];
  if (c < 0x80)
    return c;
  else if (c < 0xc2)
    return (gunichar) -1;
  else if (c < 0xe0)
    {
      len = 2;
      c &= 0x1f;
      min = 0x80;
    }
  else if (c < 0xf0)
    {
      len = 3;
      c &= 0x0f;
      min = 0x800;
    }
  else if (c < 0xf5)
    {
      len = 4;
      c &= 0x07;
      min = 0x10000;
    }
  else
    return (gunichar) -1;

  for (i = 1; i < len; i++)
    {
      if (max >= 0 ? i >= max : p[i] == 0)
        return (gunichar) -2;
      if ((p[i] & 0xc0) != 0x80)
        return (gunichar) -1;
      c = c << 6 | (p[i] & 0x3f);
    }

  if (c < min || !g_unichar_validate (c))
    return (gunichar) -1;

  return c;
}

/* Whether c is a character: not a surrogate, and no more than U+10FFFF. */
gboolean
g_unichar_validate (gunichar c)
{
  return c <= 0x10ffff && (c < 0xd800 || c > 0xdfff);
}

/* The character at p. U+FFFD, the replacement character, where there
 * is none. */
gunichar
g_utf8_get_char (const gchar *p)
{
  gunichar c;

  g_return_val_if_fail (p != NULL, 0);

  c = utf8_decode (p, -1);

  return c >= (gunichar) -2 ? 0xfffd : c;
}

/* The same, reading no more than max_len bytes (to a nul, if that is
 * negative), and saying what is wrong: (gunichar) -1 for bytes that
 * are not UTF-8 and (gunichar) -2 for the start of a character that
 * max_len or the nul cuts short. */
gunichar
g_utf8_get_char_validated (const gchar *p, gssize max_len)
{
  g_return_val_if_fail (p != NULL, (gunichar) -1);

  return utf8_decode (p, max_len);
}

/*
 * Whether str is UTF-8 through and through. *end is where it stops
 * being so, or the end of what was checked.
 *
 * With a negative max_len the string runs to its nul. Otherwise
 * max_len bytes are checked, and a nul among them is not UTF-8.
 */
gboolean
g_utf8_validate (const gchar *str, gssize max_len, const gchar **end)
{
  const gchar *p = str;

  g_return_val_if_fail (str != NULL, FALSE);

  while (max_len < 0 ? *p != 0 : p - str < max_len)
    {
      gunichar c = utf8_decode (p, max_len < 0 ? -1 : max_len - (p - str));

      if (c >= (gunichar) -2 || (c == 0 && max_len >= 0))
        break;
      p += g_utf8_skip[*(const guchar *) p];
    }

  if (end)
    *end = p;

  return max_len < 0 ? *p == 0 : p - str == max_len;
}

/* The character before p. There must be one. */
gchar*
g_utf8_prev_char (const gchar *p)
{
  do
    p--;
  while ((*p & 0xc0) == 0x80);

  return (gchar *) p;
}

/* The start of the first character after p, before end if end is
 * given; NULL when there is none. p need not be at a start. */
gchar*
g_utf8_find_next_char (const gchar *p, const gchar *end)
{
  g_return_val_if_fail (p != NULL, NULL);

  if (end ? p >= end : *p == 0)
    return NULL;

  for (p++; end ? p < end : *p != 0; p++)
    if ((*p & 0xc0) != 0x80)
      return (gchar *) p;

  return end ? NULL : (gchar *) p;
}

/* The start of the last character before p, no further back than
 * str; NULL when there is none. p need not be at a start. */
gchar*
g_utf8_find_prev_char (const gchar *str, const gchar *p)
{
  g_return_val_if_fail (str != NULL && p != NULL, NULL);

  for (p--; p >= str; p--)
    if ((*p & 0xc0) != 0x80)
      return (gchar *) p;

  return NULL;
}

/* How many characters p has: to its nul if max is negative, otherwise
 * those that are whole within max bytes. */
glong
g_utf8_strlen (const gchar *p, gssize max)
{
  const gchar *start = p;
  glong len = 0;

  g_return_val_if_fail (p != NULL || max == 0, 0);

  if (max < 0)
    for (; *p; p += g_utf8_skip[*(const guchar *) p])
      len++;
  else
    while (p - start < max && *p)
      {
        p += g_utf8_skip[*(const guchar *) p];
        if (p - start <= max)
          len++;
      }

  return len;
}

/* Where the character at offset starts. A negative offset counts back
 * from str, which then must have that many characters before it. */
gchar*
g_utf8_offset_to_pointer (const gchar *str, glong offset)
{
  g_return_val_if_fail (str != NULL, NULL);

  for (; offset > 0; offset--)
    str += g_utf8_skip[*(const guchar *) str];
  for (; offset < 0; offset++)
    str = g_utf8_prev_char (str);

  return (gchar *) str;
}

/* How many characters lie between str and pos: negative when pos is
 * before str. */
glong
g_utf8_pointer_to_offset (const gchar *str, const gchar *pos)
{
  glong offset = 0;

  g_return_val_if_fail (str != NULL && pos != NULL, 0);

  if (pos < str)
    return -g_utf8_pointer_to_offset (pos, str);

  for (; str < pos; str += g_utf8_skip[*(const guchar *) str])
    offset++;

  return offset;
}

/* Copies the first n characters of src to dest and ends it with a
 * nul, as strncpy() does bytes. dest must have room for them. */
gchar*
g_utf8_strncpy (gchar *dest, const gchar *src, gsize n)
{
  const gchar *end = src;

  g_return_val_if_fail (dest != NULL && src != NULL, NULL);

  for (; n > 0 && *end; n--)
    end += g_utf8_skip[*(const guchar *) end];
  memcpy (dest, src, end - src);
  dest[end - src] = 0;

  return dest;
}

/* Writes c as UTF-8 to outbuf, which has room for six bytes, and
 * returns how many it took. With no outbuf, only counts. Nothing is
 * written for what is not a character, and nought returned. */
gint
g_unichar_to_utf8 (gunichar c, gchar *outbuf)
{
  gint len, i;
  guchar first;

  if (c < 0x80)
    {
      len = 1;
      first = 0;
    }
  else if (c < 0x800)
    {
      len = 2;
      first = 0xc0;
    }
  else if (c < 0x10000)
    {
      len = 3;
      first = 0xe0;
    }
  else if (c <= 0x10ffff)
    {
      len = 4;
      first = 0xf0;
    }
  else
    return 0;

  if (c >= 0xd800 && c <= 0xdfff)
    return 0;

  if (outbuf)
    {
      for (i = len - 1; i > 0; i--)
        {
          outbuf[i] = (c & 0x3f) | 0x80;
          c >>= 6;
        }
      outbuf[0] = c | first;
    }

  return len;
}

/* The bytes of needle in haystack, of which len bytes are searched,
 * or all to its nul if len is negative. The last of them if last. */
static gchar*
utf8_find (const gchar *haystack, gssize len, const gchar *needle, gsize n, gboolean last)
{
  const gchar *p, *found = NULL;
  gsize hay = len < 0 ? strlen (haystack) : (gsize) len;

  if (n == 0 || n > hay)
    return NULL;

  for (p = haystack; p <= haystack + hay - n; p++)
    if (*p == *needle && memcmp (p, needle, n) == 0)
      {
        found = p;
        if (!last)
          break;
      }

  return (gchar *) found;
}

/* The first c in p, of which len bytes are searched: all to its nul
 * if len is negative. NULL if c is not there. */
gchar*
g_utf8_strchr (const gchar *p, gssize len, gunichar c)
{
  gchar ch[6];

  g_return_val_if_fail (p != NULL, NULL);

  return utf8_find (p, len, ch, g_unichar_to_utf8 (c, ch), FALSE);
}

/* The last c in p, likewise. */
gchar*
g_utf8_strrchr (const gchar *p, gssize len, gunichar c)
{
  gchar ch[6];

  g_return_val_if_fail (p != NULL, NULL);

  return utf8_find (p, len, ch, g_unichar_to_utf8 (c, ch), TRUE);
}

/*
 * str as characters, ended with a nought, without looking at what it
 * is given: bytes that are not UTF-8 come out as U+FFFD. len bytes
 * are read, or all to the nul if len is negative.
 */
gunichar*
g_utf8_to_ucs4_fast (const gchar *str, glong len, glong *items_written)
{
  const gchar *p, *end;
  gunichar *result;
  glong n = 0;

  g_return_val_if_fail (str != NULL, NULL);

  end = str + (len < 0 ? (glong) strlen (str) : len);
  result = g_new (gunichar, end - str + 1);

  for (p = str; p < end; n++)
    {
      gunichar c = utf8_decode (p, end - p);

      if (c >= (gunichar) -2)
        {
          c = 0xfffd;
          p++;
        }
      else
        p += g_utf8_skip[*(const guchar *) p];
      result[n] = c;
    }
  result[n] = 0;

  if (items_written)
    *items_written = n;

  return result;
}

/*
 * str as characters, ended with a nought, or NULL and an error where
 * it is not UTF-8. *items_read is how many bytes were good. A
 * character cut short at the end is an error unless items_read is
 * asked for: then the bytes before it are returned, and the caller
 * may have more to follow.
 */
gunichar*
g_utf8_to_ucs4 (const gchar *str, glong len, glong *items_read, glong *items_written, GError **error)
{
  const gchar *p, *end;
  gunichar *result;
  glong n = 0;

  g_return_val_if_fail (str != NULL, NULL);

  end = str + (len < 0 ? (glong) strlen (str) : len);
  result = g_new (gunichar, end - str + 1);

  for (p = str; p < end; n++)
    {
      gunichar c = utf8_decode (p, end - p);

      if (c == (gunichar) -2 && items_read)
        break;
      if (c >= (gunichar) -2)
        {
          if (c == (gunichar) -2)
            g_set_error (error, G_CONVERT_ERROR, G_CONVERT_ERROR_PARTIAL_INPUT,
                         "The text ends in the middle of a character");
          else
            g_set_error (error, G_CONVERT_ERROR, G_CONVERT_ERROR_ILLEGAL_SEQUENCE,
                         "The text is not UTF-8 at byte %ld", (glong) (p - str));
          if (items_read)
            *items_read = p - str;
          g_free (result);
          return NULL;
        }
      result[n] = c;
      p += g_utf8_skip[*(const guchar *) p];
    }
  result[n] = 0;

  if (items_read)
    *items_read = p - str;
  if (items_written)
    *items_written = n;

  return result;
}

/* len characters as UTF-8, or those before the first nought if len is
 * negative. NULL and an error if one of them is not a character. */
gchar*
g_ucs4_to_utf8 (const gunichar *str, glong len, glong *items_read, glong *items_written, GError **error)
{
  gchar *result, *p;
  glong i, n = 0;

  g_return_val_if_fail (str != NULL, NULL);

  for (i = 0; len < 0 ? str[i] != 0 : i < len; i++)
    {
      gint l = g_unichar_to_utf8 (str[i], NULL);

      if (l == 0)
        {
          g_set_error (error, G_CONVERT_ERROR, G_CONVERT_ERROR_ILLEGAL_SEQUENCE,
                       "U+%X is not a character", (guint) str[i]);
          if (items_read)
            *items_read = i;
          return NULL;
        }
      n += l;
    }
  len = i;

  p = result = g_malloc (n + 1);
  for (i = 0; i < len; i++)
    p += g_unichar_to_utf8 (str[i], p);
  *p = 0;

  if (items_read)
    *items_read = len;
  if (items_written)
    *items_written = n;

  return result;
}

/*
 * What kind of character c is, as the C library has it. Where
 * wchar_t cannot hold every character (it is 16 bits on some
 * systems), only those it can hold are asked about.
 */
#define FITS(c) ((c) <= (gunichar) WCHAR_MAX)

gboolean g_unichar_isalnum  (gunichar c) { return FITS (c) && iswalnum  ((wint_t) c) != 0; }
gboolean g_unichar_isalpha  (gunichar c) { return FITS (c) && iswalpha  ((wint_t) c) != 0; }
gboolean g_unichar_iscntrl  (gunichar c) { return FITS (c) && iswcntrl  ((wint_t) c) != 0; }
gboolean g_unichar_isdigit  (gunichar c) { return FITS (c) && iswdigit  ((wint_t) c) != 0; }
gboolean g_unichar_isgraph  (gunichar c) { return FITS (c) && iswgraph  ((wint_t) c) != 0; }
gboolean g_unichar_islower  (gunichar c) { return FITS (c) && iswlower  ((wint_t) c) != 0; }
gboolean g_unichar_isprint  (gunichar c) { return FITS (c) && iswprint  ((wint_t) c) != 0; }
gboolean g_unichar_ispunct  (gunichar c) { return FITS (c) && iswpunct  ((wint_t) c) != 0; }
gboolean g_unichar_isspace  (gunichar c) { return FITS (c) && iswspace  ((wint_t) c) != 0; }
gboolean g_unichar_isupper  (gunichar c) { return FITS (c) && iswupper  ((wint_t) c) != 0; }
gboolean g_unichar_isxdigit (gunichar c) { return FITS (c) && iswxdigit ((wint_t) c) != 0; }

gunichar
g_unichar_toupper (gunichar c)
{
  return FITS (c) ? (gunichar) towupper ((wint_t) c) : c;
}

gunichar
g_unichar_tolower (gunichar c)
{
  return FITS (c) ? (gunichar) towlower ((wint_t) c) : c;
}

/* str with each character put through change. Bytes that are not
 * UTF-8 are kept as they are. */
static gchar*
utf8_each (const gchar *str, gssize len, gunichar (*change) (gunichar))
{
  const gchar *p, *end;
  GString *out;
  gchar *result;

  g_return_val_if_fail (str != NULL, NULL);

  end = str + (len < 0 ? (gssize) strlen (str) : len);
  out = g_string_new (NULL);

  for (p = str; p < end; )
    {
      gunichar c = utf8_decode (p, end - p);
      gchar ch[6];
      gint n;

      if (c >= (gunichar) -2)
        g_string_append_c (out, *p++);
      else
        {
          n = g_unichar_to_utf8 (change (c), ch);
          ch[n] = 0;
          g_string_append (out, ch);
          p += g_utf8_skip[*(const guchar *) p];
        }
    }

  result = out->str;
  g_string_free (out, FALSE);

  return result;
}

/* str in capitals, and in small letters, one character for each as
 * the C library gives them: a sharp s stays one. len bytes are read,
 * or all to the nul if len is negative. */
gchar*
g_utf8_strup (const gchar *str, gssize len)
{
  return utf8_each (str, len, g_unichar_toupper);
}

gchar*
g_utf8_strdown (const gchar *str, gssize len)
{
  return utf8_each (str, len, g_unichar_tolower);
}

/* str in a form in which two strings that differ only in case are
 * the same string. Here that is its small letters. */
gchar*
g_utf8_casefold (const gchar *str, gssize len)
{
  return utf8_each (str, len, g_unichar_tolower);
}

/* A wide string for the C library, from UTF-8. */
static wchar_t*
utf8_to_wide (const gchar *str)
{
  glong n, i;
  gunichar *ucs = g_utf8_to_ucs4_fast (str, -1, &n);
  wchar_t *wide = g_new (wchar_t, n + 1);

  for (i = 0; i < n; i++)
    wide[i] = FITS (ucs[i]) ? (wchar_t) ucs[i] : (wchar_t) 0xfffd;
  wide[n] = 0;
  g_free (ucs);

  return wide;
}

/* Which of two strings sorts first in the locale's order, as
 * strcoll() tells of strings in the locale's own encoding: less than,
 * equal to or greater than nought. */
gint
g_utf8_collate (const gchar *str1, const gchar *str2)
{
  wchar_t *w1, *w2;
  gint result;

  g_return_val_if_fail (str1 != NULL && str2 != NULL, 0);

  w1 = utf8_to_wide (str1);
  w2 = utf8_to_wide (str2);
  result = wcscoll (w1, w2);
  g_free (w1);
  g_free (w2);

  return result;
}
