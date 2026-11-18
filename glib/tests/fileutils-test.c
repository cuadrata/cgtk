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

/* Files, their names, and the XDG directories. */

#undef G_LOG_DOMAIN

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "glib.h"

#define SAME(call, expected) G_STMT_START { gchar *got_ = (call); \
  if (strcmp (got_, (expected)) != 0) g_error ("%s gave '%s', not '%s'", #call, got_, (expected)); \
  g_free (got_); } G_STMT_END

static gchar *dir;      /* a directory of this test's own */

static void
test_names (void)
{
  SAME (g_build_filename ("/usr", "share", "themes", NULL), "/usr/share/themes");
  SAME (g_build_filename ("/usr/", "/share/", "/themes", NULL), "/usr/share/themes");
  SAME (g_build_filename ("/", "usr", NULL), "/usr");
  SAME (g_build_filename ("/", NULL), "/");
  SAME (g_build_filename ("a", NULL), "a");
  SAME (g_build_filename ("a", "", "b", NULL), "a/b");
  SAME (g_build_filename ("", "a", NULL), "a");
  SAME (g_build_filename ("a", "b/", NULL), "a/b/");
  SAME (g_build_filename ("a", "/", NULL), "a/");
  SAME (g_build_filename ("//a//", "b", NULL), "//a/b");
  SAME (g_build_filename ("a", "/", "b", NULL), "a/b");
  SAME (g_build_filename (NULL), "");
  SAME (g_build_path ("::", "a::", "::b", "c", NULL), "a::b::c");
  SAME (g_build_path ("", "a", "b", NULL), "ab");

  SAME (g_path_get_basename ("/a/b/c"), "c");
  SAME (g_path_get_basename ("/a/b/c/"), "c");
  SAME (g_path_get_basename ("c"), "c");
  SAME (g_path_get_basename ("/"), "/");
  SAME (g_path_get_basename ("///"), "/");
  SAME (g_path_get_basename (""), ".");

  SAME (g_path_get_dirname ("/a/b/c"), "/a/b");
  SAME (g_path_get_dirname ("/a"), "/");
  SAME (g_path_get_dirname ("a"), ".");
  SAME (g_path_get_dirname ("a/b"), "a");
  SAME (g_path_get_dirname ("a//b"), "a");
  SAME (g_path_get_dirname ("/"), "/");
}

static void
test_uri (void)
{
  GError *error = NULL;
  gchar *s, *host;

  SAME (g_filename_to_uri ("/home/me/a file", NULL, NULL), "file:///home/me/a%20file");
  SAME (g_filename_to_uri ("/tmp/100%", "box", NULL), "file://box/tmp/100%25");
  SAME (g_filename_to_uri ("/caf\xc3\xa9", NULL, NULL), "file:///caf%C3%A9");
  s = g_filename_to_uri ("relative", NULL, &error);
  g_assert (!s && g_error_matches (error, G_CONVERT_ERROR, G_CONVERT_ERROR_NOT_ABSOLUTE_PATH));
  g_clear_error (&error);

  SAME (g_filename_from_uri ("file:///home/me/a%20file", NULL, NULL), "/home/me/a file");
  SAME (g_filename_from_uri ("file:/tmp/x", NULL, NULL), "/tmp/x");
  SAME (g_filename_from_uri ("FILE:///caf%c3%A9", NULL, NULL), "/caf\xc3\xa9");
  s = g_filename_from_uri ("file://box/tmp/x", &host, &error);
  g_assert (s && !error && strcmp (s, "/tmp/x") == 0 && host && strcmp (host, "box") == 0);
  g_free (s);
  g_free (host);
  s = g_filename_from_uri ("file:///tmp/x", &host, &error);
  g_assert (s && host == NULL);
  g_free (s);

  s = g_filename_from_uri ("http://example.org/x", NULL, &error);
  g_assert (!s && g_error_matches (error, G_CONVERT_ERROR, G_CONVERT_ERROR_BAD_URI));
  g_clear_error (&error);
  s = g_filename_from_uri ("file:///tmp/%zz", NULL, &error);
  g_assert (!s && g_error_matches (error, G_CONVERT_ERROR, G_CONVERT_ERROR_BAD_URI));
  g_clear_error (&error);
  s = g_filename_from_uri ("file:///tmp/%00", NULL, &error);
  g_assert (!s && g_error_matches (error, G_CONVERT_ERROR, G_CONVERT_ERROR_BAD_URI));
  g_clear_error (&error);
  s = g_filename_from_uri ("file:///tmp/%4", NULL, &error);
  g_assert (!s && error);
  g_clear_error (&error);

  /* The names of files are UTF-8, and one that is not can still be shown. */
  SAME (g_filename_to_utf8 ("caf\xc3\xa9", -1, NULL, NULL, NULL), "caf\xc3\xa9");
  s = g_filename_to_utf8 ("caf\xe9", -1, NULL, NULL, &error);
  g_assert (!s && g_error_matches (error, G_CONVERT_ERROR, G_CONVERT_ERROR_ILLEGAL_SEQUENCE));
  g_clear_error (&error);
  SAME (g_filename_display_name ("caf\xe9"), "caf\xef\xbf\xbd");
  SAME (g_filename_display_name ("plain"), "plain");

  putenv ("G_FILENAME_ENCODING=ISO-8859-1,UTF-8");
  SAME (g_filename_to_utf8 ("caf\xe9", -1, NULL, NULL, NULL), "caf\xc3\xa9");
  SAME (g_filename_from_utf8 ("caf\xc3\xa9", -1, NULL, NULL, NULL), "caf\xe9");
  putenv ("G_FILENAME_ENCODING=");
}

static void
test_contents (void)
{
  gchar *file = g_build_filename (dir, "file", NULL), *link = g_build_filename (dir, "link", NULL);
  gchar *sub = g_build_filename (dir, "a", "b", "c", NULL), *under = g_build_filename (dir, "file", "x", NULL);
  gchar *big, *contents;
  GError *error = NULL;
  struct stat s;
  gsize length, i;

  g_assert (!g_file_test (file, G_FILE_TEST_EXISTS));
  g_assert (!g_file_get_contents (file, &contents, &length, &error));
  g_assert (contents == NULL && length == 0);
  g_assert (g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT));
  g_clear_error (&error);

  g_assert (g_file_set_contents (file, "one\ntwo\n", -1, &error) && !error);
  g_assert (g_file_test (file, G_FILE_TEST_EXISTS) && g_file_test (file, G_FILE_TEST_IS_REGULAR));
  g_assert (!g_file_test (file, G_FILE_TEST_IS_DIR | G_FILE_TEST_IS_SYMLINK | G_FILE_TEST_IS_EXECUTABLE));
  g_assert (g_file_get_contents (file, &contents, &length, &error) && !error);
  g_assert (length == 8 && strcmp (contents, "one\ntwo\n") == 0);
  g_free (contents);

  /* By length, with a nul inside; and nothing at all. */
  g_assert (g_file_set_contents (file, "a\0b", 3, &error));
  g_assert (g_file_get_contents (file, &contents, &length, NULL));
  g_assert (length == 3 && memcmp (contents, "a\0b\0", 4) == 0);
  g_free (contents);
  g_assert (g_file_set_contents (file, "", 0, &error));
  g_assert (g_file_get_contents (file, &contents, &length, NULL) && length == 0 && contents[0] == 0);
  g_free (contents);

  /* More than one read's worth. */
  big = g_malloc (300000);
  for (i = 0; i < 300000; i++)
    big[i] = 'a' + i % 26;
  g_assert (g_file_set_contents (file, big, 300000, &error));
  g_assert (g_file_get_contents (file, &contents, &length, NULL));
  g_assert (length == 300000 && memcmp (contents, big, 300000) == 0 && contents[300000] == 0);
  g_free (contents);
  g_free (big);

  /* The file keeps its permissions, and a link stays a link. */
  g_assert (chmod (file, 0640) == 0);
  g_assert (g_file_set_contents (file, "kept", -1, &error));
  g_assert (stat (file, &s) == 0 && (s.st_mode & 07777) == 0640);
  g_assert (symlink ("file", link) == 0);
  g_assert (g_file_test (link, G_FILE_TEST_IS_SYMLINK) && g_file_test (link, G_FILE_TEST_IS_REGULAR));
  g_assert (g_file_set_contents (link, "through the link", -1, &error) && !error);
  g_assert (g_file_test (link, G_FILE_TEST_IS_SYMLINK));
  g_assert (g_file_get_contents (file, &contents, NULL, NULL) && strcmp (contents, "through the link") == 0);
  g_free (contents);
  g_assert (stat (file, &s) == 0 && (s.st_mode & 07777) == 0640);

  /* A file where no file can be. */
  g_assert (!g_file_set_contents (under, "x", -1, &error));
  g_assert (g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOTDIR));
  g_clear_error (&error);
  g_assert (!g_file_get_contents (dir, &contents, NULL, &error));
  g_assert (g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_ISDIR));
  g_clear_error (&error);

  /* What reports no size is read all the same. */
  if (g_file_get_contents ("/proc/self/status", &contents, &length, NULL))
    {
      g_assert (length > 0 && strlen (contents) == length);
      g_free (contents);
    }

  g_assert (g_mkdir_with_parents (sub, 0700) == 0);
  g_assert (g_file_test (sub, G_FILE_TEST_IS_DIR));
  g_assert (g_mkdir_with_parents (sub, 0700) == 0);
  g_assert (g_mkdir_with_parents (under, 0700) == -1 && errno == ENOTDIR);
  g_assert (g_mkdir_with_parents ("", 0700) == -1 && errno == EINVAL);

  g_assert (g_file_test ("/bin/sh", G_FILE_TEST_IS_EXECUTABLE));
  g_assert (!g_file_test (dir, G_FILE_TEST_IS_EXECUTABLE));
  contents = g_find_program_in_path ("sh");
  g_assert (contents && g_path_is_absolute (contents) && g_file_test (contents, G_FILE_TEST_IS_EXECUTABLE));
  g_free (contents);
  g_assert (g_find_program_in_path ("no-such-program-anywhere") == NULL);
  SAME (g_find_program_in_path ("/bin/sh"), "/bin/sh");
  g_assert (g_find_program_in_path (file) == NULL);

  g_assert (g_file_error_from_errno (ENOENT) == G_FILE_ERROR_NOENT);
  g_assert (g_file_error_from_errno (-1) == G_FILE_ERROR_FAILED);

  g_free (file);
  g_free (link);
  g_free (sub);
  g_free (under);
}

static void
test_xdg (void)
{
  const gchar * const *dirs;
  gchar *expected;

  /* Set before anything is asked: the answers stay as first given. */
  expected = g_build_filename (g_get_home_dir (), ".config", NULL);
  g_assert (strcmp (g_get_user_config_dir (), expected) == 0);      /* relative, so not used */
  g_free (expected);
  g_assert (strcmp (g_get_user_data_dir (), "/xdg/data") == 0);
  expected = g_build_filename (g_get_home_dir (), ".cache", NULL);
  g_assert (strcmp (g_get_user_cache_dir (), expected) == 0);
  g_assert (strcmp (g_get_user_runtime_dir (), expected) == 0);     /* none, so the cache */
  g_free (expected);
  g_assert (g_get_user_config_dir () == g_get_user_config_dir ());

  dirs = g_get_system_data_dirs ();
  g_assert (dirs[0] && strcmp (dirs[0], "/one") == 0);
  g_assert (dirs[1] && strcmp (dirs[1], "/two/") == 0);
  g_assert (dirs[2] == NULL);
  dirs = g_get_system_config_dirs ();
  g_assert (dirs[0] && strcmp (dirs[0], "/etc/xdg") == 0 && dirs[1] == NULL);
}

int
main (void)
{
  gchar *command;

  dir = g_strdup_printf ("%s/glib-fileutils-test-%d", g_get_tmp_dir (), (gint) getpid ());
  g_assert (g_mkdir_with_parents (dir, 0700) == 0);

  putenv ("XDG_CONFIG_HOME=relative/config");
  putenv ("XDG_DATA_HOME=/xdg/data");
  putenv ("XDG_CACHE_HOME=");
  putenv ("XDG_RUNTIME_DIR=");
  putenv ("XDG_DATA_DIRS=/one:relative:/two/");
  putenv ("XDG_CONFIG_DIRS=");

  test_names ();
  test_uri ();
  test_contents ();
  test_xdg ();

  command = g_strdup_printf ("rm -rf '%s'", dir);
  if (system (command) != 0)
    g_warning ("%s was left behind", dir);
  g_free (command);
  g_free (dir);

  return 0;
}
