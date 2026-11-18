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
 * Text from one character set to another, with iconv(), and the
 * names of files to and from UTF-8 and file: URIs. The names are
 * GLib 2's.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <iconv.h>
#include <langinfo.h>
#include <stdlib.h>
#include <string.h>

#include "glib.h"

GQuark
g_convert_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("g_convert_error");

  return quark;
}

/* The character set of the locale, in *charset if that is given.
 * TRUE when it is UTF-8. */
gboolean
g_get_charset (const gchar **charset)
{
  const gchar *codeset = nl_langinfo (CODESET);

  if (!codeset || !*codeset)
    codeset = "US-ASCII";
  if (charset)
    *charset = codeset;

  return g_ascii_strcasecmp (codeset, "UTF-8") == 0 || g_ascii_strcasecmp (codeset, "UTF8") == 0;
}

static iconv_t
open_converter (const gchar *to_codeset, const gchar *from_codeset, GError **error)
{
  iconv_t cd = iconv_open (to_codeset, from_codeset);

  if (cd == (iconv_t) -1)
    {
      if (errno == EINVAL)
        g_set_error (error, G_CONVERT_ERROR, G_CONVERT_ERROR_NO_CONVERSION,
                     "There is no conversion from %s to %s", from_codeset, to_codeset);
      else
        g_set_error (error, G_CONVERT_ERROR, G_CONVERT_ERROR_FAILED,
                     "The converter from %s to %s could not be opened: %s",
                     from_codeset, to_codeset, g_strerror (errno));
    }

  return cd;
}

/*
 * iconv() says how many characters it wrote as something else because
 * the character set it writes has not got them. With glibc that is
 * none unless it was asked to with //TRANSLIT: it stops at such a
 * character with EILSEQ. musl's writes a * and counts. Here such a
 * character is one that cannot be converted, as GLib 2 has it, unless
 * the caller named a way of its own after //.
 */
static gboolean
strict (const gchar *to_codeset)
{
  return strstr (to_codeset, "//") == NULL;
}

/* iconv()'s second argument is char ** on some systems and
 * const char ** on others. */
#define ICONV(cd, in, left, out, room) iconv (cd, (void *) (in), left, out, room)

/*
 * All of len bytes at str through cd, from its first state, into
 * *dest, which is made as large as it has to be, with room to end the
 * text after. Returns how many bytes went through, with errno as
 * iconv() left it when that is not all of them. *inexact is how many
 * characters were written as something else.
 */
static gsize
convert_all (iconv_t cd, const gchar *str, gsize len, gchar **dest, gsize *size, gsize *used, gsize *inexact)
{
  for (;;)
    {
      const gchar *in = str;
      gchar *out = *dest;
      gsize left = len, room = *size - 64, n;

      iconv (cd, NULL, NULL, NULL, NULL);
      n = ICONV (cd, &in, &left, &out, &room);
      if (n == (gsize) -1 && errno == E2BIG)
        {
          *size *= 2;
          *dest = g_realloc (*dest, *size);
          continue;
        }
      *used = out - *dest;
      *inexact = n == (gsize) -1 ? 0 : n;

      return in - str;
    }
}

/* Where in str the first character is that cd writes as something
 * else: found by giving it the text a character at a time. */
static gsize
first_inexact (iconv_t cd, const gchar *str, gsize len)
{
  gsize pos = 0, n = 1;

  iconv (cd, NULL, NULL, NULL, NULL);
  while (pos + n <= len)
    {
      gchar scratch[64], *out = scratch;
      const gchar *in = str + pos;
      gsize left = n, room = sizeof scratch, r = ICONV (cd, &in, &left, &out, &room);

      if (r != (gsize) -1 && r > 0)
        break;
      if (in == str + pos)
        {
          if (r == (gsize) -1 && errno != EINVAL)
            break;
          n++;          /* more of this character is needed */
        }
      else
        {
          pos = in - str;
          n = 1;
        }
    }

  return pos;
}

/* Ends the text in *dest: the bytes some character sets need to go
 * back to their first state, and a nul wide enough for any. */
static void
convert_end (iconv_t cd, gchar **dest, gsize *used)
{
  gchar *out = *dest + *used;
  gsize room = 60;

  iconv (cd, NULL, NULL, &out, &room);
  *used = out - *dest;
  *dest = g_realloc (*dest, *used + 4);
  memset (*dest + *used, 0, 4);
}

/*
 * len bytes of str (all to its nul, if len is negative) from one
 * character set to another, in memory to be freed with g_free().
 * The result ends in a nul, wide enough for any character set.
 *
 * NULL and an error where str has something that is not text in
 * from_codeset, or that to_codeset cannot write. *bytes_read is how
 * much of str was good. A character cut short at the end is an error
 * too, unless bytes_read is asked for: then what came before it is
 * returned, as there may be more to follow.
 */
gchar*
g_convert (const gchar *str, gssize len, const gchar *to_codeset, const gchar *from_codeset,
           gsize *bytes_read, gsize *bytes_written, GError **error)
{
  iconv_t cd;
  gchar *dest;
  gsize in_len, size, used, done, inexact;
  gboolean failed = FALSE;

  g_return_val_if_fail (str != NULL, NULL);
  g_return_val_if_fail (to_codeset != NULL && from_codeset != NULL, NULL);

  if (bytes_read)
    *bytes_read = 0;
  if (bytes_written)
    *bytes_written = 0;

  cd = open_converter (to_codeset, from_codeset, error);
  if (cd == (iconv_t) -1)
    return NULL;

  in_len = len < 0 ? strlen (str) : (gsize) len;
  size = in_len + 128;
  dest = g_malloc (size);

  done = convert_all (cd, str, in_len, &dest, &size, &used, &inexact);
  if (done < in_len)
    {
      if (errno == EINVAL && bytes_read)
        /* Cut short, and the caller will see to that: what is before
         * the cut is converted again for what iconv() has to say of
         * it, which it does not when it stops. */
        done = convert_all (cd, str, done, &dest, &size, &used, &inexact);
      else
        {
          if (errno == EINVAL)
            g_set_error (error, G_CONVERT_ERROR, G_CONVERT_ERROR_PARTIAL_INPUT,
                         "The text ends in the middle of a character");
          else if (errno == EILSEQ)
            g_set_error (error, G_CONVERT_ERROR, G_CONVERT_ERROR_ILLEGAL_SEQUENCE,
                         "Byte %lu of the text cannot be converted from %s to %s",
                         (gulong) done, from_codeset, to_codeset);
          else
            g_set_error (error, G_CONVERT_ERROR, G_CONVERT_ERROR_FAILED,
                         "The conversion failed: %s", g_strerror (errno));
          failed = TRUE;
        }
    }
  if (!failed && inexact > 0 && strict (to_codeset))
    {
      done = first_inexact (cd, str, done);
      g_set_error (error, G_CONVERT_ERROR, G_CONVERT_ERROR_ILLEGAL_SEQUENCE,
                   "Byte %lu of the text cannot be converted from %s to %s",
                   (gulong) done, from_codeset, to_codeset);
      failed = TRUE;
    }

  if (bytes_read)
    *bytes_read = done;

  if (failed)
    {
      iconv_close (cd);
      g_free (dest);
      return NULL;
    }

  convert_end (cd, &dest, &used);
  iconv_close (cd);
  if (bytes_written)
    *bytes_written = used;

  return dest;
}

/*
 * As g_convert(), but a character to_codeset cannot write does not
 * stop it: fallback, which is UTF-8, is written in its place, or
 * \u and its number in hexadecimal if there is no fallback. Text
 * that is not in from_codeset at all is still an error.
 */
gchar*
g_convert_with_fallback (const gchar *str, gssize len, const gchar *to_codeset, const gchar *from_codeset,
                         const gchar *fallback, gsize *bytes_read, gsize *bytes_written, GError **error)
{
  GError *first = NULL;
  gchar *utf8, *dest, *result;
  const gchar *p;
  gsize utf8_len, size, used = 0;
  iconv_t cd;

  g_return_val_if_fail (str != NULL, NULL);
  g_return_val_if_fail (to_codeset != NULL && from_codeset != NULL, NULL);

  /* Most text converts as it is. */
  result = g_convert (str, len, to_codeset, from_codeset, bytes_read, bytes_written, &first);
  if (result || !g_error_matches (first, G_CONVERT_ERROR, G_CONVERT_ERROR_ILLEGAL_SEQUENCE))
    {
      if (first)
        g_propagate_error (error, first);
      return result;
    }
  g_error_free (first);

  /* Through UTF-8, and from there a character at a time. */
  utf8 = g_convert (str, len, "UTF-8", from_codeset, bytes_read, &utf8_len, error);
  if (!utf8)
    return NULL;

  cd = open_converter (to_codeset, "UTF-8", error);
  if (cd == (iconv_t) -1)
    {
      g_free (utf8);
      return NULL;
    }

  size = utf8_len + 128;
  dest = g_malloc (size);

  for (p = utf8; p < utf8 + utf8_len; p += g_utf8_skip[*(const guchar *) p])
    {
      gchar *text = NULL, *out;
      const gchar *in = p, *sub;
      gsize left = g_utf8_skip[*(const guchar *) p], room, r;

      /* No one character takes this much. */
      if (size - used < 128)
        {
          size *= 2;
          dest = g_realloc (dest, size);
        }
      out = dest + used;
      room = 64;
      r = ICONV (cd, &in, &left, &out, &room);
      if (r == 0 || (r != (gsize) -1 && !strict (to_codeset)))
        {
          used = out - dest;
          continue;
        }

      /* This one cannot be written: what was put for it, if anything
       * was, is taken back, and the fallback goes there. */
      if (fallback)
        sub = fallback;
      else
        sub = text = g_strdup_printf (g_utf8_get_char (p) < 0x10000 ? "\\u%04x" : "\\U%08x",
                                      (guint) g_utf8_get_char (p));
      in = sub;
      left = strlen (sub);
      if (size - used < left * 8 + 128)
        {
          size += left * 8 + 128;
          dest = g_realloc (dest, size);
        }
      out = dest + used;
      room = size - used - 64;
      r = ICONV (cd, &in, &left, &out, &room);
      if (r != 0)
        {
          g_set_error (error, G_CONVERT_ERROR, G_CONVERT_ERROR_ILLEGAL_SEQUENCE,
                       "The fallback '%s' cannot be written in %s", sub, to_codeset);
          g_free (text);
          g_free (utf8);
          g_free (dest);
          iconv_close (cd);
          return NULL;
        }
      used = out - dest;
      g_free (text);
    }
  g_free (utf8);

  convert_end (cd, &dest, &used);
  iconv_close (cd);
  if (bytes_written)
    *bytes_written = used;

  return dest;
}

/* Text that is UTF-8 already: a copy of it, once it has been looked
 * at. */
static gchar*
utf8_copy (const gchar *str, gssize len, gsize *bytes_read, gsize *bytes_written, GError **error)
{
  const gchar *end;
  gsize n = len < 0 ? strlen (str) : (gsize) len;
  gboolean valid = g_utf8_validate (str, n, &end);

  if (bytes_read)
    *bytes_read = end - str;
  if (bytes_written)
    *bytes_written = valid ? n : 0;

  if (!valid)
    {
      g_set_error (error, G_CONVERT_ERROR, G_CONVERT_ERROR_ILLEGAL_SEQUENCE,
                   "The text is not UTF-8 at byte %lu", (gulong) (end - str));
      return NULL;
    }

  return g_strndup (str, n);
}

/* Text in the locale's character set, as the system gives it in
 * messages and the user in the environment, to UTF-8. */
gchar*
g_locale_to_utf8 (const gchar *opsysstring, gssize len, gsize *bytes_read, gsize *bytes_written, GError **error)
{
  const gchar *charset;

  g_return_val_if_fail (opsysstring != NULL, NULL);

  if (g_get_charset (&charset))
    return utf8_copy (opsysstring, len, bytes_read, bytes_written, error);

  return g_convert (opsysstring, len, "UTF-8", charset, bytes_read, bytes_written, error);
}

/* And UTF-8 to the locale's character set. */
gchar*
g_locale_from_utf8 (const gchar *utf8string, gssize len, gsize *bytes_read, gsize *bytes_written, GError **error)
{
  const gchar *charset;

  g_return_val_if_fail (utf8string != NULL, NULL);

  if (g_get_charset (&charset))
    return utf8_copy (utf8string, len, bytes_read, bytes_written, error);

  return g_convert (utf8string, len, charset, "UTF-8", bytes_read, bytes_written, error);
}

/*
 * The character set the names of files are in: UTF-8, unless
 * G_FILENAME_ENCODING in the environment names another, or says
 * @locale for the locale's. Of a list there, the first is used.
 * NULL for UTF-8; otherwise to be freed.
 */
static gchar*
filename_charset (void)
{
  const gchar *env = g_getenv ("G_FILENAME_ENCODING"), *charset;
  gchar *name;

  if (!env || !*env)
    return NULL;

  name = g_strndup (env, strcspn (env, ","));
  if (strcmp (name, "@locale") == 0)
    {
      g_free (name);
      if (g_get_charset (&charset))
        return NULL;
      name = g_strdup (charset);
    }
  if (g_ascii_strcasecmp (name, "UTF-8") == 0)
    {
      g_free (name);
      return NULL;
    }

  return name;
}

/* The name of a file as the system has it, to UTF-8 for showing or
 * keeping, */
gchar*
g_filename_to_utf8 (const gchar *opsysstring, gssize len, gsize *bytes_read, gsize *bytes_written, GError **error)
{
  gchar *charset, *result;

  g_return_val_if_fail (opsysstring != NULL, NULL);

  charset = filename_charset ();
  if (!charset)
    return utf8_copy (opsysstring, len, bytes_read, bytes_written, error);

  result = g_convert (opsysstring, len, "UTF-8", charset, bytes_read, bytes_written, error);
  g_free (charset);

  return result;
}

/* and UTF-8 to the name the system knows the file by. */
gchar*
g_filename_from_utf8 (const gchar *utf8string, gssize len, gsize *bytes_read, gsize *bytes_written, GError **error)
{
  gchar *charset, *result;

  g_return_val_if_fail (utf8string != NULL, NULL);

  charset = filename_charset ();
  if (!charset)
    return utf8_copy (utf8string, len, bytes_read, bytes_written, error);

  result = g_convert (utf8string, len, charset, "UTF-8", bytes_read, bytes_written, error);
  g_free (charset);

  return result;
}

/* The name of a file for showing: always UTF-8, with U+FFFD where
 * the name has bytes that are no text. Not for opening the file. */
gchar*
g_filename_display_name (const gchar *filename)
{
  gchar *name, *result;
  gunichar *ucs;

  g_return_val_if_fail (filename != NULL, NULL);

  name = g_filename_to_utf8 (filename, -1, NULL, NULL, NULL);
  if (name)
    return name;

  ucs = g_utf8_to_ucs4_fast (filename, -1, NULL);
  result = g_ucs4_to_utf8 (ucs, -1, NULL, NULL, NULL);
  g_free (ucs);

  return result;
}

/* The bytes a URI may carry as they are in a path. */
static gboolean
uri_plain (guchar c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         (c != 0 && strchr ("-._~!$&'()*+,;=:@/", c) != NULL);
}

/* The file: URI of a file, whose name must begin at the root:
 * file:///home/me/a%20file. hostname goes between the slashes if it
 * is given. NULL and an error for a name that does not. */
gchar*
g_filename_to_uri (const gchar *filename, const gchar *hostname, GError **error)
{
  static const gchar hex[] = "0123456789ABCDEF";
  const guchar *p;
  GString *uri;
  gchar *result;

  g_return_val_if_fail (filename != NULL, NULL);

  if (!g_path_is_absolute (filename))
    {
      g_set_error (error, G_CONVERT_ERROR, G_CONVERT_ERROR_NOT_ABSOLUTE_PATH,
                   "The name '%s' does not begin at the root", filename);
      return NULL;
    }

  uri = g_string_new ("file://");
  if (hostname)
    g_string_append (uri, hostname);
  for (p = (const guchar *) filename; *p; p++)
    if (uri_plain (*p))
      g_string_append_c (uri, *p);
    else
      {
        g_string_append_c (uri, '%');
        g_string_append_c (uri, hex[*p >> 4]);
        g_string_append_c (uri, hex[*p & 15]);
      }

  result = uri->str;
  g_string_free (uri, FALSE);

  return result;
}

static gint
hex_value (gchar c)
{
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* The name of the file a file: URI is of, and its host in *hostname
 * if that is asked for and the URI has one. NULL and an error for
 * what is not a file: URI. */
gchar*
g_filename_from_uri (const gchar *uri, gchar **hostname, GError **error)
{
  const gchar *p, *path;
  gchar *result, *q;

  g_return_val_if_fail (uri != NULL, NULL);

  if (hostname)
    *hostname = NULL;

  if (g_ascii_strncasecmp (uri, "file:", 5) != 0 || uri[5] != '/')
    {
      g_set_error (error, G_CONVERT_ERROR, G_CONVERT_ERROR_BAD_URI,
                   "'%s' is not the URI of a file", uri);
      return NULL;
    }

  /* file:/path, file:///path and file://host/path */
  path = uri + 5;
  if (path[1] == '/')
    {
      const gchar *host = path + 2;

      path = strchr (host, '/');
      if (!path)
        {
          g_set_error (error, G_CONVERT_ERROR, G_CONVERT_ERROR_BAD_URI,
                       "The URI '%s' has no path", uri);
          return NULL;
        }
      if (hostname && path > host)
        *hostname = g_strndup (host, path - host);
    }

  q = result = g_malloc (strlen (path) + 1);
  for (p = path; *p && *p != '#' && *p != '?'; p++)
    if (*p == '%')
      {
        gint hi = hex_value (p[1]), lo = hi < 0 ? -1 : hex_value (p[2]);

        if (lo < 0 || (hi == 0 && lo == 0))
          {
            g_set_error (error, G_CONVERT_ERROR, G_CONVERT_ERROR_BAD_URI,
                         "The URI '%s' has a bad escape", uri);
            g_free (result);
            if (hostname)
              {
                g_free (*hostname);
                *hostname = NULL;
              }
            return NULL;
          }
        *q++ = hi << 4 | lo;
        p += 2;
      }
    else
      *q++ = *p;
  *q = 0;

  return result;
}
