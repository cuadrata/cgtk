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
 * MT safe ; except for g_on_error_stack_trace, but who wants thread safety
 * then
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "glib.h"
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>

#include <time.h>
#include <unistd.h>

#include <sys/select.h>

#include <string.h> /* for bzero on BSD systems */


#  define SELECT_MASK fd_set


static void stack_trace (char **args);

extern volatile gboolean glib_on_error_halt;
volatile gboolean glib_on_error_halt = TRUE;

void
g_on_error_query (const gchar *prg_name)
{
  static const gchar *query1 = "[E]xit, [H]alt";
  static const gchar *query2 = ", show [S]tack trace";
  static const gchar *query3 = " or [P]roceed";
  gchar buf[16];

  if (!prg_name)
    prg_name = g_get_prgname ();

 retry:

  if (prg_name)
    fprintf (stdout,
             "%s (pid:%u): %s%s%s: ",
             prg_name,
             (guint) getpid (),
             query1,
             query2,
             query3);
  else
    fprintf (stdout,
             "(process:%u): %s%s: ",
             (guint) getpid (),
             query1,
             query3);
  fflush (stdout);

  if (!isatty(0) || !isatty(1) || !fgets (buf, 8, stdin))
    strcpy (buf, "E\n");

  if ((buf[0] == 'E' || buf[0] == 'e')
      && buf[1] == '\n')
    _exit (0);
  else if ((buf[0] == 'P' || buf[0] == 'p')
           && buf[1] == '\n')
    return;
  else if (prg_name
           && (buf[0] == 'S' || buf[0] == 's')
           && buf[1] == '\n')
    {
      g_on_error_stack_trace (prg_name);
      goto retry;
    }
  else if ((buf[0] == 'H' || buf[0] == 'h')
           && buf[1] == '\n')
    {
      while (glib_on_error_halt)
        ;
      glib_on_error_halt = TRUE;
      return;
    }
  else
    goto retry;
}

void
g_on_error_stack_trace (const gchar *prg_name)
{
  pid_t pid;
  gchar buf[16];
  gchar *args[4] = { "gdb", NULL, NULL, NULL };

  if (!prg_name)
    return;

  sprintf (buf, "%u", (guint) getpid ());

  args[1] = (gchar*) prg_name;
  args[2] = buf;

  pid = fork ();
  if (pid == 0)
    {
      stack_trace (args);
      _exit (0);
    }
  else if (pid == (pid_t) -1)
    {
      perror ("unable to fork gdb");
      return;
    }

  while (glib_on_error_halt)
    ;
  glib_on_error_halt = TRUE;
}

static gboolean stack_trace_done = FALSE;

static void
stack_trace_sigchld (int signum)
{
  stack_trace_done = TRUE;
}

static void
stack_trace (char **args)
{
  pid_t pid;
  int in_fd[2];
  int out_fd[2];
  SELECT_MASK fdset;
  SELECT_MASK readset;
  struct timeval tv;
  int sel, index, state;
  char buffer[256];
  char c;

  stack_trace_done = FALSE;
  signal (SIGCHLD, stack_trace_sigchld);

  if ((pipe (in_fd) == -1) || (pipe (out_fd) == -1))
    {
      perror ("unable to open pipe");
      _exit (0);
    }

  pid = fork ();
  if (pid == 0)
    {
      /* set stdin to the in pipe, stdout and stderr to the out pipe */
      if (dup2 (in_fd[0], 0) < 0 || dup2 (out_fd[1], 1) < 0 ||
          dup2 (out_fd[1], 2) < 0)
        _exit (0);

      execvp (args[0], args);      /* exec gdb */
      perror ("exec failed");
      _exit (0);
    }
  else if (pid == (pid_t) -1)
    {
      perror ("unable to fork");
      _exit (0);
    }

  FD_ZERO (&fdset);
  FD_SET (out_fd[0], &fdset);

  if (write (in_fd[1], "backtrace\np x = 0\nquit\n", 23) != 23)
    {
      perror ("unable to talk to the debugger");
      _exit (0);
    }

  index = 0;
  state = 0;

  while (1)
    {
      readset = fdset;
      tv.tv_sec = 1;
      tv.tv_usec = 0;

      sel = select (FD_SETSIZE, &readset, NULL, NULL, &tv);
      if (sel == -1)
        break;

      if ((sel > 0) && (FD_ISSET (out_fd[0], &readset)))
        {
          if (read (out_fd[0], &c, 1))
            {
              switch (state)
                {
                case 0:
                  if (c == '#')
                    {
                      state = 1;
                      index = 0;
                      buffer[index++] = c;
                    }
                  break;
                case 1:
                  buffer[index++] = c;
                  if ((c == '\n') || (c == '\r'))
                    {
                      buffer[index] = 0;
                      fprintf (stdout, "%s", buffer);
                      state = 0;
                      index = 0;
                    }
                  break;
                default:
                  break;
                }
            }
        }
      else if (stack_trace_done)
        break;
    }

  close (in_fd[0]);
  close (in_fd[1]);
  close (out_fd[0]);
  close (out_fd[1]);
  _exit (0);
}

/*
 * GError: what went wrong, for a caller that can do something about
 * it. By GLib 2's names, and used as there: a function that can fail
 * takes a GError ** last, which may be NULL, and on failure puts an
 * error there that the caller frees.
 */

GError*
g_error_new_literal (GQuark domain, gint code, const gchar *message)
{
  GError *error;

  g_return_val_if_fail (domain != 0 && message != NULL, NULL);

  error = g_new (GError, 1);
  error->domain = domain;
  error->code = code;
  error->message = g_strdup (message);

  return error;
}

GError*
g_error_new (GQuark domain, gint code, const gchar *format, ...)
{
  GError *error;
  va_list args;

  g_return_val_if_fail (domain != 0 && format != NULL, NULL);

  error = g_new (GError, 1);
  error->domain = domain;
  error->code = code;
  va_start (args, format);
  error->message = g_strdup_vprintf (format, args);
  va_end (args);

  return error;
}

void
g_error_free (GError *error)
{
  g_return_if_fail (error != NULL);

  g_free (error->message);
  g_free (error);
}

GError*
g_error_copy (const GError *error)
{
  g_return_val_if_fail (error != NULL, NULL);

  return g_error_new_literal (error->domain, error->code, error->message);
}

/* Whether error is there and is this one. */
gboolean
g_error_matches (const GError *error, GQuark domain, gint code)
{
  return error && error->domain == domain && error->code == code;
}

/* Puts a new error in *err, unless err is NULL: then nobody wants to
 * know. *err must have none in it yet. */
void
g_set_error (GError **err, GQuark domain, gint code, const gchar *format, ...)
{
  va_list args;
  gchar *message;

  if (!err)
    return;

  va_start (args, format);
  message = g_strdup_vprintf (format, args);
  va_end (args);

  if (*err)
    g_warning ("An error was set over another, which said: %s\nThe new one says: %s", (*err)->message, message);
  else
    *err = g_error_new_literal (domain, code, message);
  g_free (message);
}

/* Hands src on to *dest, or frees it if dest is NULL. */
void
g_propagate_error (GError **dest, GError *src)
{
  g_return_if_fail (src != NULL);

  if (!dest)
    g_error_free (src);
  else if (*dest)
    {
      g_warning ("An error was set over another, which said: %s\nThe new one says: %s", (*dest)->message, src->message);
      g_error_free (src);
    }
  else
    *dest = src;
}

/* Frees the error in *err, if there is one, and leaves NULL there. */
void
g_clear_error (GError **err)
{
  if (err && *err)
    {
      g_error_free (*err);
      *err = NULL;
    }
}
