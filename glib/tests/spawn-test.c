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

/* Command lines taken apart, programs run and waited for, and a
 * child watched by the main loop. */

#undef G_LOG_DOMAIN

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "glib.h"

static void
words (const gchar *line, const gchar *expected)
{
  GError *error = NULL;
  gchar **argv, *joined;
  gint argc;

  if (!g_shell_parse_argv (line, &argc, &argv, &error))
    g_error ("'%s' is not taken apart: %s", line, error->message);
  joined = g_strjoinv ("|", argv);
  if (strcmp (joined, expected) != 0 || argc != (gint) g_strv_length (argv))
    g_error ("'%s' gives '%s', and not '%s'", line, joined, expected);
  g_free (joined);
  g_strfreev (argv);
}

static void
test_shell (void)
{
  GError *error = NULL;
  gchar **argv, *s, *t;

  words ("ls -l /tmp", "ls|-l|/tmp");
  words ("  a\tb\n c  ", "a|b|c");
  words ("echo 'a b' \"c d\" e\\ f", "echo|a b|c d|e f");
  words ("echo 'it''s' \"it's\" 'say \"hi\"'", "echo|its|it's|say \"hi\"");
  words ("echo \"a\\\"b\\\\c\\$d\\e\"", "echo|a\"b\\c$d\\e");
  words ("echo '' \"\" x", "echo|||x");
  words ("echo a#b # and the rest\nnext", "echo|a#b|next");
  words ("echo one\\\ntwo \\\n three", "echo|onetwo|three");
  words ("x='a b'c\"d e\"", "x=a bcd e");

  g_assert (!g_shell_parse_argv ("", NULL, &argv, &error));
  g_assert (g_error_matches (error, G_SHELL_ERROR, G_SHELL_ERROR_EMPTY_STRING));
  g_clear_error (&error);
  g_assert (!g_shell_parse_argv ("  # nothing\n", NULL, &argv, &error));
  g_assert (g_error_matches (error, G_SHELL_ERROR, G_SHELL_ERROR_EMPTY_STRING));
  g_clear_error (&error);
  g_assert (!g_shell_parse_argv ("echo 'open", NULL, &argv, &error));
  g_assert (g_error_matches (error, G_SHELL_ERROR, G_SHELL_ERROR_BAD_QUOTING));
  g_clear_error (&error);
  g_assert (!g_shell_parse_argv ("echo \"open", NULL, NULL, &error));
  g_assert (g_error_matches (error, G_SHELL_ERROR, G_SHELL_ERROR_BAD_QUOTING));
  g_clear_error (&error);
  g_assert (!g_shell_parse_argv ("echo end\\", NULL, NULL, &error));
  g_assert (g_error_matches (error, G_SHELL_ERROR, G_SHELL_ERROR_BAD_QUOTING));
  g_clear_error (&error);
  g_assert (g_shell_parse_argv ("only counted", NULL, NULL, &error));

  /* Quoted, and read back by this and by the shell itself. */
  s = g_shell_quote ("it's a \"test\" $HOME \\ `x`\n*");
  g_assert (strcmp (s, "'it'\\''s a \"test\" $HOME \\ `x`\n*'") == 0);
  t = g_shell_unquote (s, &error);
  g_assert (t && strcmp (t, "it's a \"test\" $HOME \\ `x`\n*") == 0);
  g_free (t);
  t = g_strconcat ("printf %s ", s, NULL);
  g_free (s);
  g_assert (g_shell_parse_argv (t, NULL, &argv, &error));
  g_free (t);
  g_assert (g_spawn_sync (NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &s, NULL, NULL, &error));
  g_assert (strcmp (s, "it's a \"test\" $HOME \\ `x`\n*") == 0);
  g_free (s);
  g_strfreev (argv);

  t = g_shell_unquote ("a' b 'c\\ d", &error);
  g_assert (t && strcmp (t, "a b c d") == 0);
  g_free (t);
  g_assert (!g_shell_unquote ("'open", &error));
  g_clear_error (&error);
}

static void
setup (gpointer data)
{
  if (write (1, data, strlen (data)) < 0)
    _exit (9);
}

static void
test_spawn (void)
{
  gchar *sh[] = { "sh", "-c", NULL, NULL }, *none[] = { "/nonexistent/program", NULL };
  gchar *zero[] = { "/bin/sh", "called-this", "-c", "printf %s \"$0\"", NULL };
  gchar *env[] = { "PATH=/usr/bin:/bin", "ONLY=this", NULL };
  GError *error = NULL;
  gchar *out, *err, *big;
  gint status, in, fd, i;
  gssize n;
  GPid pid;

  /* What it wrote to each, and how it ended. */
  sh[2] = "echo out; echo err >&2; exit 3";
  g_assert (g_spawn_sync (NULL, sh, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &out, &err, &status, &error));
  g_assert (strcmp (out, "out\n") == 0 && strcmp (err, "err\n") == 0);
  g_assert (WIFEXITED (status) && WEXITSTATUS (status) == 3);
  g_assert (!g_spawn_check_exit_status (status, &error));
  g_assert (g_error_matches (error, G_SPAWN_EXIT_ERROR, 3));
  g_clear_error (&error);
  g_free (out);
  g_free (err);

  sh[2] = "kill -TERM $$";
  g_assert (g_spawn_sync (NULL, sh, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, &status, &error));
  g_assert (WIFSIGNALED (status) && WTERMSIG (status) == SIGTERM);
  g_assert (!g_spawn_check_exit_status (status, &error));
  g_assert (g_error_matches (error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED));
  g_clear_error (&error);
  sh[2] = "exit 0";
  g_assert (g_spawn_sync (NULL, sh, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, &status, &error));
  g_assert (g_spawn_check_exit_status (status, &error));

  /* A great deal on both at once: neither waits for the other. */
  sh[2] = "i=0; while [ $i -lt 4000 ]; do echo 0123456789012345678901234567890123456789; "
          "echo 0123456789012345678901234567890123456789 >&2; i=$((i+1)); done";
  g_assert (g_spawn_sync (NULL, sh, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &out, &err, &status, &error));
  g_assert (strlen (out) == 4000 * 41 && strlen (err) == 4000 * 41 && status == 0);
  g_free (out);
  g_free (err);

  /* Its directory, its environment, and what is done in it first. */
  sh[2] = "pwd; echo \"${ONLY}${HOME-none}\"";
  g_assert (g_spawn_sync ("/", sh, env, G_SPAWN_SEARCH_PATH, setup, "first\n", &out, NULL, NULL, &error));
  g_assert (strcmp (out, "first\n/\nthisnone\n") == 0);
  g_free (out);
  g_assert (g_spawn_sync (NULL, zero, NULL, G_SPAWN_FILE_AND_ARGV_ZERO, NULL, NULL, &out, NULL, NULL, &error));
  g_assert (strcmp (out, "called-this") == 0);
  g_free (out);

  /* A program that is not there is found out at once, with or without a path to look in. */
  g_assert (!g_spawn_sync (NULL, none, NULL, 0, NULL, NULL, NULL, NULL, NULL, &error));
  g_assert (g_error_matches (error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT));
  g_clear_error (&error);
  none[0] = "no-such-program-anywhere";
  g_assert (!g_spawn_async (NULL, none, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error));
  g_assert (g_error_matches (error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT));
  g_clear_error (&error);
  none[0] = "/";
  g_assert (!g_spawn_async (NULL, none, NULL, 0, NULL, NULL, NULL, &error));
  g_assert (g_error_matches (error, G_SPAWN_ERROR, G_SPAWN_ERROR_ACCES));
  g_clear_error (&error);
  sh[2] = "exit 0";
  g_assert (!g_spawn_sync ("/nonexistent/directory", sh, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, &error));
  g_assert (g_error_matches (error, G_SPAWN_ERROR, G_SPAWN_ERROR_CHDIR));
  g_clear_error (&error);

  /* Pipes to it and from it. */
  sh[2] = "tr a-z A-Z";
  g_assert (g_spawn_async_with_pipes (NULL, sh, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL,
                                      &pid, &in, &fd, NULL, &error));
  g_assert (write (in, "shout\n", 6) == 6);
  close (in);
  big = g_malloc (64);
  for (n = 0, i = 0; (i = read (fd, big + n, 63 - n)) > 0; n += i)
    ;
  big[n] = 0;
  g_assert (strcmp (big, "SHOUT\n") == 0);
  g_free (big);
  close (fd);
  g_assert (waitpid (pid, &status, 0) == pid && status == 0);
  g_spawn_close_pid (pid);

  /* No file of this program's is open in it but the three, and one that is not waited for is nobody's child. */
  fd = dup (1);
  sh[2] = "ls /dev/fd/ 2>/dev/null | wc -l; sleep 0.1";
  g_assert (g_spawn_sync (NULL, sh, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &out, NULL, NULL, &error));
  g_assert (atoi (out) <= 5);
  g_free (out);
  close (fd);
  sh[2] = "exit 0";
  g_assert (g_spawn_async (NULL, sh, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &pid, &error));
  g_assert (pid > 0 && waitpid (pid, &status, 0) < 0);

  g_assert (g_spawn_command_line_sync ("sh -c 'echo \"a  b\"'", &out, NULL, &status, &error));
  g_assert (strcmp (out, "a  b\n") == 0 && status == 0);
  g_free (out);
  g_assert (!g_spawn_command_line_async ("'unfinished", &error));
  g_assert (g_error_matches (error, G_SHELL_ERROR, G_SHELL_ERROR_BAD_QUOTING));
  g_clear_error (&error);
}

static gint ended, order;
static GMainLoop *loop;

static void
watched (GPid pid, gint status, gpointer data)
{
  g_assert (WIFEXITED (status) && WEXITSTATUS (status) == GPOINTER_TO_INT (data));
  order = order * 10 + GPOINTER_TO_INT (data);
  if (++ended == 3)
    g_main_quit (loop);
}

static gboolean
too_long (gpointer data)
{
  g_error ("the children were not seen to end");
  return FALSE;
}

static void
test_watch (void)
{
  gchar *sh[] = { "sh", "-c", NULL, NULL };
  GError *error = NULL;
  GPid pid;

  loop = g_main_new (FALSE);
  /* One that ends later, one sooner, and one that has ended before it is watched. */
  sh[2] = "sleep 0.4; exit 1";
  g_assert (g_spawn_async (NULL, sh, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, &error));
  g_child_watch_add (pid, watched, GINT_TO_POINTER (1));
  sh[2] = "sleep 0.2; exit 2";
  g_assert (g_spawn_async (NULL, sh, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, &error));
  g_child_watch_add (pid, watched, GINT_TO_POINTER (2));
  sh[2] = "exit 3";
  g_assert (g_spawn_async (NULL, sh, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, &error));
  usleep (100000);
  g_child_watch_add (pid, watched, GINT_TO_POINTER (3));
  g_timeout_add (10000, too_long, NULL);
  g_main_run (loop);
  g_assert (ended == 3 && order == 321);
  g_main_destroy (loop);
}

int
main (void)
{
  test_shell ();
  test_spawn ();
  test_watch ();

  return 0;
}
