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

/* UTF-8 strings, GError, and conversion between character sets. */

#undef G_LOG_DOMAIN

#include <locale.h>
#include <string.h>

#include "glib.h"

/* "a", e acute, the euro sign, and U+1F600, which takes four bytes. */
#define SAMPLE "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80"

static void
test_error (void)
{
  GError *error = NULL, *copy;
  GQuark domain = g_quark_from_static_string ("test");

  g_set_error (&error, domain, 7, "%s is %d", "seven", 7);
  g_assert (error != NULL);
  g_assert (g_error_matches (error, domain, 7));
  g_assert (!g_error_matches (error, domain, 8));
  g_assert (!g_error_matches (NULL, domain, 7));
  g_assert (strcmp (error->message, "seven is 7") == 0);

  copy = g_error_copy (error);
  g_assert (copy != error && copy->code == 7 && strcmp (copy->message, error->message) == 0);
  g_clear_error (&error);
  g_assert (error == NULL);
  g_clear_error (&error);

  g_propagate_error (&error, copy);
  g_assert (error == copy);
  g_error_free (error);

  /* Nobody listening: nothing is made, nothing leaks. */
  g_set_error (NULL, domain, 1, "unheard");
  g_propagate_error (NULL, g_error_new_literal (domain, 1, "unheard"));
}

static void
test_walk (void)
{
  const gchar *s = SAMPLE, *p, *end;
  gchar buf[16];

  g_assert (g_utf8_strlen (s, -1) == 4);
  g_assert (g_utf8_strlen (s, 1) == 1);
  g_assert (g_utf8_strlen (s, 2) == 1);     /* half an e acute */
  g_assert (g_utf8_strlen (s, 3) == 2);
  g_assert (g_utf8_strlen (s, 0) == 0);
  g_assert (g_utf8_strlen ("", -1) == 0);

  p = s;
  g_assert (g_utf8_get_char (p) == 'a');
  p = g_utf8_next_char (p);
  g_assert (g_utf8_get_char (p) == 0xe9);
  p = g_utf8_next_char (p);
  g_assert (g_utf8_get_char (p) == 0x20ac);
  p = g_utf8_next_char (p);
  g_assert (g_utf8_get_char (p) == 0x1f600);
  p = g_utf8_next_char (p);
  g_assert (*p == 0 && p == s + 10);

  p = g_utf8_prev_char (p);
  g_assert (p == s + 6);
  p = g_utf8_prev_char (p);
  g_assert (p == s + 3);

  g_assert (g_utf8_offset_to_pointer (s, 0) == s);
  g_assert (g_utf8_offset_to_pointer (s, 2) == s + 3);
  g_assert (g_utf8_offset_to_pointer (s, 4) == s + 10);
  g_assert (g_utf8_offset_to_pointer (s + 10, -2) == s + 3);
  g_assert (g_utf8_pointer_to_offset (s, s + 6) == 3);
  g_assert (g_utf8_pointer_to_offset (s + 6, s) == -3);

  g_assert (g_utf8_find_next_char (s + 4, NULL) == s + 6);   /* from inside the euro */
  g_assert (g_utf8_find_next_char (s + 6, s + 10) == NULL);
  g_assert (g_utf8_find_next_char (s + 10, NULL) == NULL);
  g_assert (g_utf8_find_prev_char (s, s + 5) == s + 3);
  g_assert (g_utf8_find_prev_char (s, s) == NULL);

  g_assert (g_utf8_strchr (s, -1, 0x20ac) == s + 3);
  g_assert (g_utf8_strchr (s, 3, 0x20ac) == NULL);
  g_assert (g_utf8_strchr (s, -1, 'z') == NULL);
  g_assert (g_utf8_strrchr ("a\xc3\xa9" "a\xc3\xa9", -1, 0xe9) != NULL);
  g_assert (strlen (g_utf8_strrchr ("a\xc3\xa9" "a\xc3\xa9", -1, 0xe9)) == 2);

  g_utf8_strncpy (buf, s, 2);
  g_assert (strcmp (buf, "a\xc3\xa9") == 0);
  g_utf8_strncpy (buf, s, 9);
  g_assert (strcmp (buf, s) == 0);

  g_assert (g_utf8_validate (s, -1, &end) && end == s + 10);
  g_assert (g_utf8_validate (s, 10, NULL));
  g_assert (!g_utf8_validate (s, 5, &end) && end == s + 3);
  g_assert (g_utf8_validate ("", -1, NULL));
  g_assert (!g_utf8_validate ("a\0b", 3, &end) && *end == 0);
}

static void
test_bad (void)
{
  static const gchar *bad[] = {
    "\x80",                 /* a byte that continues nothing */
    "\xc0\xaf",             /* a slash the long way */
    "\xe0\x80\xaf",         /* and a longer way */
    "\xed\xa0\x80",         /* a surrogate */
    "\xf4\x90\x80\x80",     /* past U+10FFFF */
    "\xf8\x88\x80\x80\x80", /* five bytes */
    "\xff",
    "a\xc3",                /* cut short */
    "\xe2\x82"
  };
  const gchar *end;
  guint i;

  for (i = 0; i < G_N_ELEMENTS (bad); i++)
    {
      g_assert (!g_utf8_validate (bad[i], -1, &end));
      g_assert (end == bad[i] + (bad[i][0] == 'a' ? 1 : 0));
      g_assert (g_utf8_get_char (end) == 0xfffd);
    }

  g_assert (g_utf8_get_char_validated ("\x80", -1) == (gunichar) -1);
  g_assert (g_utf8_get_char_validated ("\xe2\x82", -1) == (gunichar) -2);
  g_assert (g_utf8_get_char_validated ("\xe2\x82\xac", 2) == (gunichar) -2);
  g_assert (g_utf8_get_char_validated ("\xe2\x82\xac", 3) == 0x20ac);
  g_assert (g_utf8_get_char_validated ("\xe2\x82\xac", -1) == 0x20ac);
  g_assert (g_utf8_get_char_validated ("\xe2\x28\xac", -1) == (gunichar) -1);

  g_assert (g_unichar_validate (0x10ffff) && !g_unichar_validate (0x110000));
  g_assert (!g_unichar_validate (0xd800) && g_unichar_validate (0xe000));
}

static void
test_ucs4 (void)
{
  static const gunichar chars[] = { 'a', 0xe9, 0x20ac, 0x1f600, 0 }, lone[] = { 'a', 0xdc00, 0 };
  GError *error = NULL;
  gunichar *ucs, c;
  gchar *utf8, buf[8];
  glong read, written;

  for (c = 0; c < 0x110000; c += c < 0x3000 ? 1 : 97)
    {
      gint n = g_unichar_to_utf8 (c, buf);

      if (c >= 0xd800 && c <= 0xdfff)
        {
          g_assert (n == 0);
          continue;
        }
      g_assert (n == g_unichar_to_utf8 (c, NULL));
      buf[n] = 0;
      g_assert (c == 0 || g_utf8_validate (buf, -1, NULL));
      g_assert (g_utf8_get_char_validated (buf, n) == c);
      g_assert (c == 0 || g_utf8_next_char (buf) == buf + n);
    }
  g_assert (g_unichar_to_utf8 (0x110000, buf) == 0);

  ucs = g_utf8_to_ucs4 (SAMPLE, -1, &read, &written, &error);
  g_assert (ucs && !error && read == 10 && written == 4);
  g_assert (memcmp (ucs, chars, sizeof chars) == 0);
  utf8 = g_ucs4_to_utf8 (ucs, -1, &read, &written, &error);
  g_assert (utf8 && !error && read == 4 && written == 10 && strcmp (utf8, SAMPLE) == 0);
  g_free (utf8);
  g_free (ucs);

  ucs = g_utf8_to_ucs4 ("ab\xff" "c", -1, &read, NULL, &error);
  g_assert (!ucs && read == 2);
  g_assert (g_error_matches (error, G_CONVERT_ERROR, G_CONVERT_ERROR_ILLEGAL_SEQUENCE));
  g_clear_error (&error);

  /* Cut short: an error, unless the caller asks how far it got. */
  ucs = g_utf8_to_ucs4 ("ab\xe2\x82", -1, NULL, NULL, &error);
  g_assert (!ucs && g_error_matches (error, G_CONVERT_ERROR, G_CONVERT_ERROR_PARTIAL_INPUT));
  g_clear_error (&error);
  ucs = g_utf8_to_ucs4 ("ab\xe2\x82", -1, &read, &written, &error);
  g_assert (ucs && !error && read == 2 && written == 2);
  g_free (ucs);

  ucs = g_utf8_to_ucs4_fast ("a\xff\xc3\xa9", -1, &written);
  g_assert (written == 3 && ucs[0] == 'a' && ucs[1] == 0xfffd && ucs[2] == 0xe9 && ucs[3] == 0);
  g_free (ucs);

  utf8 = g_ucs4_to_utf8 (lone, -1, &read, NULL, &error);
  g_assert (!utf8 && read == 1 && g_error_matches (error, G_CONVERT_ERROR, G_CONVERT_ERROR_ILLEGAL_SEQUENCE));
  g_clear_error (&error);
}

static void
test_case (void)
{
  gchar *s;

  /* ASCII is known in any locale. */
  s = g_utf8_strup ("Hello, World 1!", -1);
  g_assert (strcmp (s, "HELLO, WORLD 1!") == 0);
  g_free (s);
  s = g_utf8_strdown ("Hello, World 1!", 5);
  g_assert (strcmp (s, "hello") == 0);
  g_free (s);
  s = g_utf8_casefold ("MiXeD", -1);
  g_assert (strcmp (s, "mixed") == 0);
  g_free (s);

  /* What is not UTF-8 goes through as it is. */
  s = g_utf8_strup ("a\xff" "b", -1);
  g_assert (strcmp (s, "A\xff" "B") == 0);
  g_free (s);

  g_assert (g_unichar_isalpha ('a') && !g_unichar_isalpha ('1') && g_unichar_isdigit ('1'));
  g_assert (g_unichar_isspace (' ') && g_unichar_isupper ('A') && g_unichar_islower ('a'));
  g_assert (g_unichar_isxdigit ('f') && !g_unichar_isxdigit ('g') && g_unichar_ispunct ('!'));
  g_assert (g_unichar_toupper ('a') == 'A' && g_unichar_tolower ('A') == 'a');
  g_assert (g_unichar_toupper (0x10ffff) == 0x10ffff);

  g_assert (g_utf8_collate ("abc", "abd") < 0);
  g_assert (g_utf8_collate ("abc", "abc") == 0);
  g_assert (g_utf8_collate ("b", "a") > 0);

  /* More than ASCII needs a locale that knows it, which not every
   * system has. */
  if (setlocale (LC_ALL, "C.UTF-8") || setlocale (LC_ALL, "en_US.UTF-8"))
    {
      const gchar *charset;

      g_assert (g_get_charset (&charset) && charset);
      if (g_unichar_toupper (0xe9) == 0xc9)
        {
          s = g_utf8_strup ("\xc3\xa9t\xc3\xa9", -1);
          g_assert (strcmp (s, "\xc3\x89T\xc3\x89") == 0);
          g_free (s);
          g_assert (g_unichar_isalpha (0xe9) && g_unichar_islower (0xe9) && g_unichar_isupper (0xc9));
        }
      setlocale (LC_ALL, "C");
    }
}

static void
test_convert (void)
{
  GError *error = NULL;
  gsize read, written;
  gchar *s;

  s = g_convert ("caf\xe9", -1, "UTF-8", "ISO-8859-1", &read, &written, &error);
  g_assert (s && !error && read == 4 && written == 5 && strcmp (s, "caf\xc3\xa9") == 0);
  g_free (s);

  s = g_convert ("caf\xc3\xa9", -1, "ISO-8859-1", "UTF-8", NULL, &written, &error);
  g_assert (s && !error && written == 4 && strcmp (s, "caf\xe9") == 0);
  g_free (s);

  /* Part of a string, by its length. */
  s = g_convert ("caf\xe9 au lait", 4, "UTF-8", "ISO-8859-1", NULL, NULL, &error);
  g_assert (s && strcmp (s, "caf\xc3\xa9") == 0);
  g_free (s);

  /* Wide enough a nul for UTF-16. */
  s = g_convert ("hi", -1, "UTF-16LE", "UTF-8", NULL, &written, &error);
  g_assert (s && written == 4 && memcmp (s, "h\0i\0\0\0", 6) == 0);
  g_free (s);

  s = g_convert ("x", -1, "UTF-8", "no-such-character-set", &read, &written, &error);
  g_assert (!s && g_error_matches (error, G_CONVERT_ERROR, G_CONVERT_ERROR_NO_CONVERSION));
  g_clear_error (&error);

  s = g_convert ("ab\xff", -1, "ISO-8859-1", "UTF-8", &read, NULL, &error);
  g_assert (!s && read == 2 && g_error_matches (error, G_CONVERT_ERROR, G_CONVERT_ERROR_ILLEGAL_SEQUENCE));
  g_clear_error (&error);

  s = g_convert ("ab\xc3", -1, "ISO-8859-1", "UTF-8", NULL, NULL, &error);
  g_assert (!s && g_error_matches (error, G_CONVERT_ERROR, G_CONVERT_ERROR_PARTIAL_INPUT));
  g_clear_error (&error);
  s = g_convert ("ab\xc3", -1, "ISO-8859-1", "UTF-8", &read, NULL, &error);
  g_assert (s && !error && read == 2 && strcmp (s, "ab") == 0);
  g_free (s);

  /* The euro sign is not in Latin-1. */
  s = g_convert ("5 \xe2\x82\xac", -1, "ISO-8859-1", "UTF-8", &read, NULL, &error);
  g_assert (!s && read == 2 && g_error_matches (error, G_CONVERT_ERROR, G_CONVERT_ERROR_ILLEGAL_SEQUENCE));
  g_clear_error (&error);
  s = g_convert_with_fallback ("5 \xe2\x82\xac!", -1, "ISO-8859-1", "UTF-8", "EUR", NULL, &written, &error);
  g_assert (s && !error && written == 6 && strcmp (s, "5 EUR!") == 0);
  g_free (s);
  s = g_convert_with_fallback ("5 \xe2\x82\xac \xc3\xa9", -1, "ISO-8859-1", "UTF-8", NULL, NULL, NULL, &error);
  g_assert (s && !error && strcmp (s, "5 \\u20ac \xe9") == 0);
  g_free (s);
  s = g_convert_with_fallback ("plain", -1, "ISO-8859-1", "UTF-8", "?", NULL, NULL, &error);
  g_assert (s && strcmp (s, "plain") == 0);
  g_free (s);
  s = g_convert_with_fallback ("ab\xff", -1, "ISO-8859-1", "UTF-8", "?", NULL, NULL, &error);
  g_assert (!s && g_error_matches (error, G_CONVERT_ERROR, G_CONVERT_ERROR_ILLEGAL_SEQUENCE));
  g_clear_error (&error);
}

static void
test_locale (void)
{
  GError *error = NULL;
  gsize read, written;
  gchar *s;

  /* In the "C" locale, whatever it calls its character set. */
  s = g_locale_to_utf8 ("plain text", -1, &read, &written, &error);
  g_assert (s && !error && read == 10 && written == 10 && strcmp (s, "plain text") == 0);
  g_free (s);
  s = g_locale_from_utf8 ("plain text", 5, NULL, NULL, &error);
  g_assert (s && !error && strcmp (s, "plain") == 0);
  g_free (s);

  if (setlocale (LC_ALL, "C.UTF-8") || setlocale (LC_ALL, "en_US.UTF-8"))
    {
      s = g_locale_to_utf8 ("caf\xc3\xa9", -1, &read, &written, &error);
      g_assert (s && !error && read == 5 && written == 5 && strcmp (s, "caf\xc3\xa9") == 0);
      g_free (s);
      s = g_locale_to_utf8 ("caf\xe9", -1, &read, &written, &error);
      g_assert (!s && read == 3 && written == 0);
      g_assert (g_error_matches (error, G_CONVERT_ERROR, G_CONVERT_ERROR_ILLEGAL_SEQUENCE));
      g_clear_error (&error);
      setlocale (LC_ALL, "C");
    }
}

int
main (void)
{
  test_error ();
  test_walk ();
  test_bad ();
  test_ucs4 ();
  test_case ();
  test_convert ();
  test_locale ();

  return 0;
}
