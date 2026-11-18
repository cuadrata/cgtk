/* GLIB - Library of useful routines for C programming
 * Copyright (C) 1995-1997  Peter Mattis, Spencer Kimball and Josh MacDonald
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
 * Modified by the GLib Team and others 1997-1999. See the AUTHORS
 * file for a list of people on the GLib Team. See the ChangeLog
 * files for a list of changes. These files are distributed with
 * GLib at ftp://ftp.gtk.org/pub/gtk/.
 */

/*
 * MT safe
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>            /* POSIX: strcasecmp(), strncasecmp() */
#include <locale.h>
#include <errno.h>
#include <ctype.h>              /* For tolower() */
#include <signal.h>
#include "glib.h"

gchar*
g_strdup (const gchar *str)
{
  gchar *new_str;

  if (str)
    {
      new_str = g_new (char, strlen (str) + 1);
      strcpy (new_str, str);
    }
  else
    new_str = NULL;

  return new_str;
}

gpointer
g_memdup (gconstpointer mem,
          guint         byte_size)
{
  gpointer new_mem;

  if (mem)
    {
      new_mem = g_malloc (byte_size);
      memcpy (new_mem, mem, byte_size);
    }
  else
    new_mem = NULL;

  return new_mem;
}

gchar*
g_strndup (const gchar *str,
           guint        n)
{
  gchar *new_str;

  if (str)
    {
      new_str = g_new (gchar, n + 1);
      strncpy (new_str, str, n);
      new_str[n] = '\0';
    }
  else
    new_str = NULL;

  return new_str;
}

gchar*
g_strnfill (guint length,
            gchar fill_char)
{
  register gchar *str, *s, *end;

  str = g_new (gchar, length + 1);
  s = str;
  end = str + length;
  while (s < end)
    *(s++) = fill_char;
  *s = 0;

  return str;
}

gchar*
g_strdup_vprintf (const gchar *format,
                  va_list      args1)
{
  gchar *buffer;
  va_list args2;

  G_VA_COPY (args2, args1);

  buffer = g_new (gchar, g_printf_string_upper_bound (format, args1));

  vsprintf (buffer, format, args2);
  va_end (args2);

  return buffer;
}

gchar*
g_strdup_printf (const gchar *format,
                 ...)
{
  gchar *buffer;
  va_list args;

  va_start (args, format);
  buffer = g_strdup_vprintf (format, args);
  va_end (args);

  return buffer;
}

gchar*
g_strconcat (const gchar *string1, ...)
{
  guint   l;
  va_list args;
  gchar   *s;
  gchar   *concat;

  g_return_val_if_fail (string1 != NULL, NULL);

  l = 1 + strlen (string1);
  va_start (args, string1);
  s = va_arg (args, gchar*);
  while (s)
    {
      l += strlen (s);
      s = va_arg (args, gchar*);
    }
  va_end (args);

  concat = g_new (gchar, l);
  concat[0] = 0;

  strcat (concat, string1);
  va_start (args, string1);
  s = va_arg (args, gchar*);
  while (s)
    {
      strcat (concat, s);
      s = va_arg (args, gchar*);
    }
  va_end (args);

  return concat;
}

gdouble
g_strtod (const gchar *nptr,
          gchar **endptr)
{
  gchar *fail_pos_1;
  gchar *fail_pos_2;
  gdouble val_1;
  gdouble val_2 = 0;

  g_return_val_if_fail (nptr != NULL, 0);

  fail_pos_1 = NULL;
  fail_pos_2 = NULL;

  val_1 = strtod (nptr, &fail_pos_1);

  if (fail_pos_1 && fail_pos_1[0] != 0)
    {
      gchar *old_locale;

      old_locale = g_strdup (setlocale (LC_NUMERIC, NULL));
      setlocale (LC_NUMERIC, "C");
      val_2 = strtod (nptr, &fail_pos_2);
      setlocale (LC_NUMERIC, old_locale);
      g_free (old_locale);
    }

  if (!fail_pos_1 || fail_pos_1[0] == 0 || fail_pos_1 >= fail_pos_2)
    {
      if (endptr)
        *endptr = fail_pos_1;
      return val_1;
    }
  else
    {
      if (endptr)
        *endptr = fail_pos_2;
      return val_2;
    }
}

gchar*
g_strerror (gint errnum)
{
  static GStaticPrivate msg_private = G_STATIC_PRIVATE_INIT;
  char *text = strerror (errnum);
  char *msg;

  if (text)
    return text;

  msg = g_static_private_get (&msg_private);
  if (!msg)
    {
      msg = g_new (gchar, 64);
      g_static_private_set (&msg_private, msg, g_free);
    }

  sprintf (msg, "unknown error (%d)", errnum);

  return msg;
}

/* POSIX.1-2008 has strsignal; the build asks for SUSv3, which does not. */
extern char *strsignal (int sig);

gchar*
g_strsignal (gint signum)
{
  return strsignal (signum);
}

/* The length of what format prints with args, and its nul. */
guint
g_printf_string_upper_bound (const gchar* format,
                             va_list      args)
{
  gint length;

  if (!format)
    return 1;

  length = vsnprintf (NULL, 0, format, args);

  return length < 0 ? 1 : length + 1;
}

void
g_strdown (gchar *string)
{
  register guchar *s;

  g_return_if_fail (string != NULL);

  s = (guchar *) string;

  while (*s)
    {
      *s = tolower (*s);
      s++;
    }
}

void
g_strup (gchar *string)
{
  register guchar *s;

  g_return_if_fail (string != NULL);

  s = (guchar *) string;

  while (*s)
    {
      *s = toupper (*s);
      s++;
    }
}

void
g_strreverse (gchar *string)
{
  g_return_if_fail (string != NULL);

  if (*string)
    {
      register gchar *h, *t;

      h = string;
      t = string + strlen (string) - 1;

      while (h < t)
        {
          register gchar c;

          c = *h;
          *h = *t;
          h++;
          *t = c;
          t--;
        }
    }
}

gchar
g_ascii_tolower (gchar c)
{
  return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

gchar
g_ascii_toupper (gchar c)
{
  return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
}

gint
g_ascii_strcasecmp (const gchar *s1,
                    const gchar *s2)
{
  gint c1, c2;

  g_return_val_if_fail (s1 != NULL, 0);
  g_return_val_if_fail (s2 != NULL, 0);

  while (*s1 && *s2)
    {
      c1 = (guchar) g_ascii_tolower (*s1);
      c2 = (guchar) g_ascii_tolower (*s2);
      if (c1 != c2)
        return (c1 - c2);
      s1++; s2++;
    }

  return (((gint) (guchar) *s1) - ((gint) (guchar) *s2));
}

gint
g_ascii_strncasecmp (const gchar *s1,
                     const gchar *s2,
                     gsize        n)
{
  gint c1, c2;

  g_return_val_if_fail (s1 != NULL, 0);
  g_return_val_if_fail (s2 != NULL, 0);

  while (n && *s1 && *s2)
    {
      n -= 1;
      c1 = (guchar) g_ascii_tolower (*s1);
      c2 = (guchar) g_ascii_tolower (*s2);
      if (c1 != c2)
        return (c1 - c2);
      s1++; s2++;
    }

  if (n)
    return (((gint) (guchar) *s1) - ((gint) (guchar) *s2));
  else
    return 0;
}

gint
g_strcasecmp (const gchar *s1,
              const gchar *s2)
{
  g_return_val_if_fail (s1 != NULL, 0);
  g_return_val_if_fail (s2 != NULL, 0);

  return strcasecmp (s1, s2);
}

gint
g_strncasecmp (const gchar *s1,
               const gchar *s2,
               guint n)
{
  return strncasecmp (s1, s2, n);
}

gchar*
g_strdelimit (gchar       *string,
              const gchar *delimiters,
              gchar        new_delim)
{
  register gchar *c;

  g_return_val_if_fail (string != NULL, NULL);

  if (!delimiters)
    delimiters = G_STR_DELIMITERS;

  for (c = string; *c; c++)
    {
      if (strchr (delimiters, *c))
        *c = new_delim;
    }

  return string;
}

gchar*
g_strescape (gchar *string)
{
  gchar *q;
  gchar *escaped;
  guint backslashes = 0;
  gchar *p = string;

  g_return_val_if_fail (string != NULL, NULL);

  while (*p != '\000')
    backslashes += (*p++ == '\\');

  if (!backslashes)
    return g_strdup (string);

  escaped = g_new (gchar, strlen (string) + backslashes + 1);

  p = string;
  q = escaped;

  while (*p != '\000')
    {
      if (*p == '\\')
        *q++ = '\\';
      *q++ = *p++;
    }
  *q = '\000';

  return escaped;
}

/* blame Elliot for these next five routines */
gchar*
g_strchug (gchar *string)
{
  guchar *start;

  g_return_val_if_fail (string != NULL, NULL);

  for (start = (guchar *) string; *start && isspace (*start); start++)
    ;

  g_memmove(string, start, strlen ((gchar *) start) + 1);

  return string;
}

gchar*
g_strchomp (gchar *string)
{
  gchar *s;

  g_return_val_if_fail (string != NULL, NULL);

  if (!*string)
    return string;

  for (s = string + strlen (string) - 1; s >= string && isspace ((guchar)*s);
       s--)
    *s = '\0';

  return string;
}

gchar**
g_strsplit (const gchar *string,
            const gchar *delimiter,
            gint         max_tokens)
{
  GSList *string_list = NULL, *slist;
  gchar **str_array, *s;
  guint i, n = 1;

  g_return_val_if_fail (string != NULL, NULL);
  g_return_val_if_fail (delimiter != NULL, NULL);

  if (max_tokens < 1)
    max_tokens = G_MAXINT;

  s = strstr (string, delimiter);
  if (s)
    {
      guint delimiter_len = strlen (delimiter);

      do
        {
          guint len;
          gchar *new_string;

          len = s - string;
          new_string = g_new (gchar, len + 1);
          strncpy (new_string, string, len);
          new_string[len] = 0;
          string_list = g_slist_prepend (string_list, new_string);
          n++;
          string = s + delimiter_len;
          s = strstr (string, delimiter);
        }
      while (--max_tokens && s);
    }
  if (*string)
    {
      n++;
      string_list = g_slist_prepend (string_list, g_strdup (string));
    }

  str_array = g_new (gchar*, n);

  i = n - 1;

  str_array[i--] = NULL;
  for (slist = string_list; slist; slist = slist->next)
    str_array[i--] = slist->data;

  g_slist_free (string_list);

  return str_array;
}

void
g_strfreev (gchar **str_array)
{
  if (str_array)
    {
      int i;

      for(i = 0; str_array[i] != NULL; i++)
        g_free(str_array[i]);

      g_free (str_array);
    }
}

gchar*
g_strjoinv (const gchar  *separator,
            gchar       **str_array)
{
  gchar *string;

  g_return_val_if_fail (str_array != NULL, NULL);

  if (separator == NULL)
    separator = "";

  if (*str_array)
    {
      guint i, len;
      guint separator_len;

      separator_len = strlen (separator);
      len = 1 + strlen (str_array[0]);
      for(i = 1; str_array[i] != NULL; i++)
        len += separator_len + strlen(str_array[i]);

      string = g_new (gchar, len);
      *string = 0;
      strcat (string, *str_array);
      for (i = 1; str_array[i] != NULL; i++)
        {
          strcat (string, separator);
          strcat (string, str_array[i]);
        }
      }
  else
    string = g_strdup ("");

  return string;
}

gchar*
g_strjoin (const gchar  *separator,
           ...)
{
  gchar *string, *s;
  va_list args;
  guint len;
  guint separator_len;

  if (separator == NULL)
    separator = "";

  separator_len = strlen (separator);

  va_start (args, separator);

  s = va_arg (args, gchar*);

  if (s)
    {
      len = strlen (s);

      s = va_arg (args, gchar*);
      while (s)
        {
          len += separator_len + strlen (s);
          s = va_arg (args, gchar*);
        }
      va_end (args);

      string = g_new (gchar, len + 1);
      *string = 0;

      va_start (args, separator);

      s = va_arg (args, gchar*);
      strcat (string, s);

      s = va_arg (args, gchar*);
      while (s)
        {
          strcat (string, separator);
          strcat (string, s);
          s = va_arg (args, gchar*);
        }
    }
  else
    string = g_strdup ("");

  va_end (args);

  return string;
}

/* How many strings an array has before the NULL that ends it. */
guint
g_strv_length (gchar **str_array)
{
  guint n = 0;

  g_return_val_if_fail (str_array != NULL, 0);

  while (str_array[n])
    n++;

  return n;
}

/*
 * strtod() for a number written as C writes it, with a full stop for
 * the point, whatever the locale has for it. Files and protocols have
 * such numbers, and a program in a locale with a comma must still
 * read them.
 */
gdouble
g_ascii_strtod (const gchar *nptr, gchar **endptr)
{
  const gchar *point = localeconv ()->decimal_point, *p = nptr, *start, *dot = NULL, *q;
  gsize point_len = strlen (point), n = 0;
  gchar *copy;
  gdouble value;

  g_return_val_if_fail (nptr != NULL, 0);

  if (strcmp (point, ".") == 0)
    return strtod (nptr, endptr);

  while (*p == ' ' || (*p >= '\t' && *p <= '\r'))
    p++;
  start = p;
  if (*p == '+' || *p == '-')
    p++;
  /* What is no decimal number, strtod() reads the same in every locale. */
  if (!((*p >= '0' && *p <= '9') || (*p == '.' && p[1] >= '0' && p[1] <= '9')) ||
      (*p == '0' && (p[1] == 'x' || p[1] == 'X')))
    return strtod (nptr, endptr);

  while (*p >= '0' && *p <= '9')
    p++;
  if (*p == '.')
    for (dot = p++; *p >= '0' && *p <= '9'; p++)
      ;
  if (*p == 'e' || *p == 'E')
    {
      q = p + 1;
      if (*q == '+' || *q == '-')
        q++;
      if (*q >= '0' && *q <= '9')
        for (p = q; *p >= '0' && *p <= '9'; p++)
          ;
    }

  /* The number, with what the locale has where its point is. */
  copy = g_malloc ((p - start) + point_len + 1);
  for (q = start; q < p; q++)
    if (q == dot)
      {
        memcpy (copy + n, point, point_len);
        n += point_len;
      }
    else
      copy[n++] = *q;
  copy[n] = 0;
  value = strtod (copy, NULL);
  g_free (copy);
  if (endptr)
    *endptr = (gchar *) p;

  return value;
}

/* And a number written so, in a buffer, with as many digits as it
 * takes to read the same number back: 17 at most. */
gchar*
g_ascii_dtostr (gchar *buffer, gint buf_len, gdouble d)
{
  const gchar *point = localeconv ()->decimal_point;
  gsize point_len = strlen (point);
  gchar *p;

  g_return_val_if_fail (buffer != NULL && buf_len > 0, NULL);

  g_snprintf (buffer, buf_len, "%.17g", d);
  if (strcmp (point, ".") != 0 && (p = strstr (buffer, point)))
    {
      *p = '.';
      memmove (p + 1, p + point_len, strlen (p + point_len) + 1);
    }

  return buffer;
}
