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

/* Key files read, asked, changed and written; and numbers written as
 * C writes them. */

#undef G_LOG_DOMAIN

#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "glib.h"

static const gchar desktop[] =
  "# A comment at the top\n"
  "\n"
  "[Desktop Entry]\n"
  "Type=Application\n"
  "Name=Text Editor\n"
  "Name[de]=Texteditor\n"
  "Name[pt_BR]=Editor de texto\n"
  "Name[sr@latin]=Ure\xc4\x91ivac\n"
  "  Exec = editor %f  \r\n"
  "# about the keywords\n"
  "Keywords=text;plain\\;simple;\\sspaced;\n"
  "Terminal=false\n"
  "Startup = true \n"
  "Count=42\n"
  "Ratio=2.5\n"
  "Lines=one\\ntwo\\tthree\\\\four\n"
  "\n"
  "[Desktop Action New]\n"
  "Name=New Window\n";

#define SAME(got, want) G_STMT_START { gchar *s_ = (got); \
  if (!s_ || strcmp (s_, (want)) != 0) g_error ("line %d: got '%s', not '%s'", __LINE__, s_ ? s_ : "(null)", (want)); \
  g_free (s_); } G_STMT_END
#define FAILS(call, code) G_STMT_START { call; \
  if (!g_error_matches (error, G_KEY_FILE_ERROR, (code))) g_error ("line %d: not the error expected", __LINE__); \
  g_clear_error (&error); } G_STMT_END

static void
test_read (void)
{
  GKeyFile *kf = g_key_file_new ();
  GError *error = NULL;
  gchar **list, *joined, *data;
  gsize n;

  g_assert (g_key_file_load_from_data (kf, desktop, (gsize) -1, G_KEY_FILE_KEEP_COMMENTS, &error));
  SAME (g_key_file_get_start_group (kf), "Desktop Entry");
  list = g_key_file_get_groups (kf, &n);
  g_assert (n == 2 && strcmp (list[1], "Desktop Action New") == 0 && !list[2]);
  g_strfreev (list);
  list = g_key_file_get_keys (kf, "Desktop Entry", &n, &error);
  g_assert (n == 12 && strcmp (list[0], "Type") == 0 && strcmp (list[4], "Name[sr@latin]") == 0 && strcmp (list[5], "Exec") == 0);
  g_strfreev (list);
  g_assert (g_key_file_has_group (kf, "Desktop Entry") && !g_key_file_has_group (kf, "desktop entry"));
  g_assert (g_key_file_has_key (kf, "Desktop Entry", "Exec", &error) && !g_key_file_has_key (kf, "Desktop Entry", "exec", &error));
  g_assert (!error);

  SAME (g_key_file_get_string (kf, "Desktop Entry", "Name", &error), "Text Editor");
  SAME (g_key_file_get_value (kf, "Desktop Entry", "Exec", &error), "editor %f  ");
  SAME (g_key_file_get_string (kf, "Desktop Entry", "Lines", &error), "one\ntwo\tthree\\four");
  SAME (g_key_file_get_value (kf, "Desktop Entry", "Lines", &error), "one\\ntwo\\tthree\\\\four");
  g_assert (!g_key_file_get_boolean (kf, "Desktop Entry", "Terminal", &error) && !error);
  g_assert (g_key_file_get_boolean (kf, "Desktop Entry", "Startup", &error) && !error);
  g_assert (g_key_file_get_integer (kf, "Desktop Entry", "Count", &error) == 42 && !error);
  g_assert (g_key_file_get_double (kf, "Desktop Entry", "Ratio", &error) == 2.5 && !error);
  g_assert (g_key_file_get_double (kf, "Desktop Entry", "Count", &error) == 42.0 && !error);

  list = g_key_file_get_string_list (kf, "Desktop Entry", "Keywords", &n, &error);
  joined = g_strjoinv ("|", list);
  g_assert (n == 3 && strcmp (joined, "text|plain;simple| spaced") == 0);
  g_free (joined);
  g_strfreev (list);

  /* The nearest translation there is, and the key itself where there is none. */
  SAME (g_key_file_get_locale_string (kf, "Desktop Entry", "Name", "de_AT.UTF-8", &error), "Texteditor");
  SAME (g_key_file_get_locale_string (kf, "Desktop Entry", "Name", "pt_BR", &error), "Editor de texto");
  SAME (g_key_file_get_locale_string (kf, "Desktop Entry", "Name", "pt_PT", &error), "Text Editor");
  SAME (g_key_file_get_locale_string (kf, "Desktop Entry", "Name", "sr_RS@latin", &error), "Ure\xc4\x91ivac");
  SAME (g_key_file_get_locale_string (kf, "Desktop Entry", "Name", "C", &error), "Text Editor");
  SAME (g_key_file_get_locale_string (kf, "Desktop Entry", "Type", "de", &error), "Application");
  setenv ("LANGUAGE", "fr:de:en", 1);
  SAME (g_key_file_get_locale_string (kf, "Desktop Entry", "Name", NULL, &error), "Texteditor");
  g_assert (!error);

  FAILS (g_key_file_get_string (kf, "No Such", "Name", &error), G_KEY_FILE_ERROR_GROUP_NOT_FOUND);
  FAILS (g_key_file_get_string (kf, "Desktop Entry", "None", &error), G_KEY_FILE_ERROR_KEY_NOT_FOUND);
  FAILS (g_key_file_get_integer (kf, "Desktop Entry", "Name", &error), G_KEY_FILE_ERROR_INVALID_VALUE);
  FAILS (g_key_file_get_integer (kf, "Desktop Entry", "Ratio", &error), G_KEY_FILE_ERROR_INVALID_VALUE);
  FAILS (g_key_file_get_boolean (kf, "Desktop Entry", "Count", &error), G_KEY_FILE_ERROR_INVALID_VALUE);
  FAILS (g_key_file_get_double (kf, "Desktop Entry", "Name", &error), G_KEY_FILE_ERROR_INVALID_VALUE);
  FAILS (g_key_file_get_keys (kf, "No Such", NULL, &error), G_KEY_FILE_ERROR_GROUP_NOT_FOUND);

  /* Written, it is what was read, but that spaces about a key are gone. */
  data = g_key_file_to_data (kf, &n, &error);
  g_assert (n == strlen (data) && strstr (data, "# A comment at the top\n\n[Desktop Entry]\nType=Application\n"));
  g_assert (strstr (data, "Exec=editor %f  \n# about the keywords\nKeywords=text;plain\\;simple;\\sspaced;\n"));
  g_assert (strstr (data, "four\n\n[Desktop Action New]\nName=New Window\n"));
  g_assert (g_key_file_load_from_data (kf, data, n, 0, &error));
  SAME (g_key_file_to_data (kf, NULL, NULL), data);
  g_free (data);
  g_key_file_free (kf);
}

static void
test_bad (void)
{
  GKeyFile *kf = g_key_file_new ();
  GError *error = NULL;

  FAILS (g_key_file_load_from_data (kf, "Key=before any group\n", (gsize) -1, 0, &error), G_KEY_FILE_ERROR_GROUP_NOT_FOUND);
  FAILS (g_key_file_load_from_data (kf, "[G]\nno equals sign\n", (gsize) -1, 0, &error), G_KEY_FILE_ERROR_PARSE);
  FAILS (g_key_file_load_from_data (kf, "[G\nK=v\n", (gsize) -1, 0, &error), G_KEY_FILE_ERROR_PARSE);
  FAILS (g_key_file_load_from_data (kf, "[]\n", (gsize) -1, 0, &error), G_KEY_FILE_ERROR_PARSE);
  FAILS (g_key_file_load_from_data (kf, "[a[b]]\n", (gsize) -1, 0, &error), G_KEY_FILE_ERROR_PARSE);
  FAILS (g_key_file_load_from_data (kf, "[G]\n=v\n", (gsize) -1, 0, &error), G_KEY_FILE_ERROR_PARSE);
  FAILS (g_key_file_load_from_data (kf, "[G]\nK[de=v\n", (gsize) -1, 0, &error), G_KEY_FILE_ERROR_PARSE);
  FAILS (g_key_file_load_from_data (kf, "[G]\nK=caf\xe9\n", (gsize) -1, 0, &error), G_KEY_FILE_ERROR_UNKNOWN_ENCODING);
  FAILS (g_key_file_load_from_data (kf, "[G]\nK=a\0b\n", 10, 0, &error), G_KEY_FILE_ERROR_UNKNOWN_ENCODING);
  g_assert (!g_key_file_has_group (kf, "G"));

  /* Nothing, a file that does not end in a newline, and the same group twice. */
  g_assert (g_key_file_load_from_data (kf, "", 0, 0, &error) && !g_key_file_get_start_group (kf));
  g_assert (g_key_file_load_from_data (kf, "[G]\nA=1\n[H]\nB=2\n[G]\nA=3\nC=4", (gsize) -1, 0, &error));
  g_assert (g_key_file_get_integer (kf, "G", "A", &error) == 3 && g_key_file_get_integer (kf, "G", "C", &error) == 4);
  g_assert (g_key_file_load_from_data (kf, "[G]\nK=bad\\q\nL=end\\\n", (gsize) -1, 0, &error));
  FAILS (g_key_file_get_string (kf, "G", "K", &error), G_KEY_FILE_ERROR_INVALID_VALUE);
  FAILS (g_key_file_get_string (kf, "G", "L", &error), G_KEY_FILE_ERROR_INVALID_VALUE);
  SAME (g_key_file_get_value (kf, "G", "K", &error), "bad\\q");

  if (!g_key_file_load_from_file (kf, "/nonexistent/file", 0, &error))
    g_assert (g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT));
  g_clear_error (&error);
  g_key_file_free (kf);
}

static void
test_write (void)
{
  static const gchar * const list[] = { "one", "two;three", " four", NULL };
  GKeyFile *kf = g_key_file_new ();
  GError *error = NULL;
  gchar *file, **back;
  gsize n;

  g_assert (g_key_file_load_from_data (kf, "[Window]\nWidth=640\n\n[Recent]\nFile=/tmp/a\n", (gsize) -1, 0, &error));
  g_key_file_set_integer (kf, "Window", "Height", 480);
  g_key_file_set_integer (kf, "Window", "Width", 800);
  g_key_file_set_boolean (kf, "Window", "Maximized", TRUE);
  g_key_file_set_double (kf, "View", "Zoom", 1.25);
  g_key_file_set_string (kf, "View", "Title", " two\nlines\\");
  g_key_file_set_locale_string (kf, "View", "Title", "de", "Zwei");
  g_key_file_set_string_list (kf, "View", "Names", list, 3);
  SAME (g_key_file_to_data (kf, NULL, NULL),
        "[Window]\nWidth=800\nHeight=480\nMaximized=true\n\n[Recent]\nFile=/tmp/a\n\n"
        "[View]\nZoom=1.25\nTitle=\\stwo\\nlines\\\\\nTitle[de]=Zwei\nNames=one;two\\;three;\\sfour;\n");
  SAME (g_key_file_get_string (kf, "View", "Title", &error), " two\nlines\\");
  back = g_key_file_get_string_list (kf, "View", "Names", &n, &error);
  g_assert (n == 3 && strcmp (back[1], "two;three") == 0 && strcmp (back[2], " four") == 0);
  g_strfreev (back);

  g_assert (g_key_file_remove_key (kf, "Window", "Height", &error));
  g_assert (!g_key_file_remove_key (kf, "Window", "Height", &error));
  g_clear_error (&error);
  g_assert (g_key_file_remove_group (kf, "Recent", &error) && !g_key_file_has_group (kf, "Recent"));
  g_assert (!g_key_file_remove_group (kf, "Recent", &error));
  g_clear_error (&error);

  /* To a file and back. */
  file = g_strdup_printf ("%s/cgtk-keyfile-test-%d", g_get_tmp_dir (), (gint) getpid ());
  g_assert (g_key_file_save_to_file (kf, file, &error));
  g_key_file_free (kf);
  kf = g_key_file_new ();
  g_assert (g_key_file_load_from_file (kf, file, G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, &error));
  g_assert (g_key_file_get_integer (kf, "Window", "Width", &error) == 800);
  g_assert (g_key_file_get_double (kf, "View", "Zoom", &error) == 1.25 && !error);
  SAME (g_key_file_get_locale_string (kf, "View", "Title", "de_DE", &error), "Zwei");
  unlink (file);
  g_free (file);

  g_key_file_set_list_separator (kf, ',');
  g_key_file_set_value (kf, "View", "Sizes", "8,10,12");
  back = g_key_file_get_string_list (kf, "View", "Sizes", &n, &error);
  g_assert (n == 3 && strcmp (back[2], "12") == 0);
  g_strfreev (back);
  g_key_file_free (kf);
}

/* A number is read and written the same in a locale that has a comma for the point. */
static void
test_numbers (void)
{
  gchar buffer[40], *end;
  gint pass;

  for (pass = 0; pass < 2; pass++)
    {
      g_assert (g_ascii_strtod ("2.5", &end) == 2.5 && !*end);
      g_assert (g_ascii_strtod ("  -0.125e2x", &end) == -12.5 && *end == 'x');
      g_assert (g_ascii_strtod ("7,5", &end) == 7.0 && *end == ',');
      g_assert (g_ascii_strtod (".5 ", &end) == 0.5 && *end == ' ');
      g_assert (g_ascii_strtod ("3.", &end) == 3.0 && !*end);
      g_assert (g_ascii_strtod ("1e", &end) == 1.0 && *end == 'e');
      g_assert (g_ascii_strtod ("abc", &end) == 0.0 && *end == 'a');
      g_assert (g_ascii_strtod ("0x10", &end) == 16.0 && !*end);
      g_assert (strcmp (g_ascii_dtostr (buffer, sizeof buffer, 2.5), "2.5") == 0);
      g_assert (strcmp (g_ascii_dtostr (buffer, sizeof buffer, -1e-7), "-9.9999999999999995e-08") == 0);
      g_assert (g_ascii_strtod (g_ascii_dtostr (buffer, sizeof buffer, 0.1), NULL) == 0.1);
      if (!setlocale (LC_ALL, "de_DE.UTF-8") && !setlocale (LC_ALL, "fr_FR.UTF-8") && !setlocale (LC_ALL, "de_DE"))
        break;  /* no such locale here to try */
    }
  setlocale (LC_ALL, "C");
}

int
main (void)
{
  test_read ();
  test_bad ();
  test_write ();
  test_numbers ();

  return 0;
}
