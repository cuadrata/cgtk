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
 * Programs started, with pipes to them or without, and waited for
 * here or by the main loop; and command lines taken apart into words
 * as a shell takes them. The names are GLib 2's.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "glib.h"

extern char **environ;

GQuark
g_spawn_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("g-exec-error-quark");

  return quark;
}

GQuark
g_spawn_exit_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("g-spawn-exit-error-quark");

  return quark;
}

GQuark
g_shell_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("g-shell-error-quark");

  return quark;
}

/* ---------------------------------------------------------------------
 * Words, as a shell has them.
 */

/*
 * What is at *p, with its quotes taken off, added to word. It runs to
 * the end of the text, or to the first space outside quotes if words
 * are being cut. A backslash and a newline together are nothing.
 */
static gboolean
unquote (const gchar **p, gboolean words, GString *word, GError **error)
{
  const gchar *s = *p;

  while (*s && !(words && strchr (" \t\n", *s)))
    {
      if (*s == '\\')
        {
          s++;
          if (!*s)
            {
              g_set_error (error, G_SHELL_ERROR, G_SHELL_ERROR_BAD_QUOTING,
                           "The text ends just after a backslash");
              return FALSE;
            }
          if (*s != '\n')
            g_string_append_c (word, *s);
          s++;
        }
      else if (*s == '\'')
        {
          const gchar *end = strchr (s + 1, '\'');

          if (!end)
            {
              g_set_error (error, G_SHELL_ERROR, G_SHELL_ERROR_BAD_QUOTING,
                           "The text ends before the quote that matches a ' was found");
              return FALSE;
            }
          for (s++; s < end; s++)
            g_string_append_c (word, *s);
          s++;
        }
      else if (*s == '"')
        {
          for (s++; *s && *s != '"'; s++)
            if (*s == '\\' && s[1] && strchr ("$`\"\\\n", s[1]))
              {
                s++;
                if (*s != '\n')
                  g_string_append_c (word, *s);
              }
            else
              g_string_append_c (word, *s);
          if (!*s)
            {
              g_set_error (error, G_SHELL_ERROR, G_SHELL_ERROR_BAD_QUOTING,
                           "The text ends before the quote that matches a \" was found");
              return FALSE;
            }
          s++;
        }
      else
        g_string_append_c (word, *s++);
    }
  *p = s;

  return TRUE;
}

/*
 * A command line taken apart into its words as a shell would take
 * it, without anything a shell would put in: no variables, no
 * commands, no file names for patterns. Quotes of both kinds and
 * backslashes are read as sh reads them, and a # where a word would
 * start has the rest of its line left out. The words are in *argvp,
 * to be freed with g_strfreev(). A line with no word in it is an
 * error.
 */
gboolean
g_shell_parse_argv (const gchar *command_line, gint *argcp, gchar ***argvp, GError **error)
{
  GPtrArray *words;
  GString *word;
  const gchar *p = command_line;
  guint i;

  g_return_val_if_fail (command_line != NULL, FALSE);

  words = g_ptr_array_new ();
  word = g_string_new (NULL);
  while (*p)
    {
      if (strchr (" \t\n", *p))
        p++;
      else if (*p == '#')
        p += strcspn (p, "\n");
      else if (p[0] == '\\' && p[1] == '\n')
        p += 2;
      else
        {
          g_string_truncate (word, 0);
          if (!unquote (&p, TRUE, word, error))
            {
              for (i = 0; i < words->len; i++)
                g_free (words->pdata[i]);
              g_ptr_array_free (words, TRUE);
              g_string_free (word, TRUE);
              return FALSE;
            }
          g_ptr_array_add (words, g_strdup (word->str));
        }
    }
  g_string_free (word, TRUE);

  if (words->len == 0)
    {
      g_ptr_array_free (words, TRUE);
      g_set_error (error, G_SHELL_ERROR, G_SHELL_ERROR_EMPTY_STRING,
                   "The text was empty, or had only spaces in it");
      return FALSE;
    }

  if (argcp)
    *argcp = words->len;
  g_ptr_array_add (words, NULL);
  if (argvp)
    {
      *argvp = (gchar **) words->pdata;
      g_ptr_array_free (words, FALSE);
    }
  else
    {
      for (i = 0; i < words->len; i++)
        g_free (words->pdata[i]);
      g_ptr_array_free (words, TRUE);
    }

  return TRUE;
}

/* A string as a shell has to be given it to take it for one word
 * and as it is: in single quotes. */
gchar*
g_shell_quote (const gchar *unquoted_string)
{
  GString *quoted;
  gchar *result;

  g_return_val_if_fail (unquoted_string != NULL, NULL);

  quoted = g_string_new ("'");
  for (; *unquoted_string; unquoted_string++)
    if (*unquoted_string == '\'')
      g_string_append (quoted, "'\\''");
    else
      g_string_append_c (quoted, *unquoted_string);
  g_string_append_c (quoted, '\'');
  result = quoted->str;
  g_string_free (quoted, FALSE);

  return result;
}

/* And a string a shell would take for one word, as the shell would
 * have it once the quotes are off. */
gchar*
g_shell_unquote (const gchar *quoted_string, GError **error)
{
  GString *word;
  gchar *result;

  g_return_val_if_fail (quoted_string != NULL, NULL);

  word = g_string_new (NULL);
  if (!unquote (&quoted_string, FALSE, word, error))
    {
      g_string_free (word, TRUE);
      return NULL;
    }
  result = word->str;
  g_string_free (word, FALSE);

  return result;
}

/* ---------------------------------------------------------------------
 * Programs.
 */

/* What a child that could not become the program says of it. */
enum
{
  CHILD_CHDIR_FAILED,
  CHILD_EXEC_FAILED,
  CHILD_FORK_FAILED
};

static gboolean
write_all (gint fd, gconstpointer data, gsize n)
{
  const gchar *p = data;

  while (n > 0)
    {
      gssize r = write (fd, p, n);

      if (r < 0 && errno == EINTR)
        continue;
      if (r <= 0)
        return FALSE;
      p += r;
      n -= r;
    }

  return TRUE;
}

/* As much as there is, up to n bytes. How much, or -1. */
static gssize
read_all (gint fd, gpointer data, gsize n)
{
  gchar *p = data;
  gsize got = 0;

  while (got < n)
    {
      gssize r = read (fd, p + got, n - got);

      if (r < 0 && errno == EINTR)
        continue;
      if (r < 0)
        return -1;
      if (r == 0)
        break;
      got += r;
    }

  return got;
}

static void
child_failed (gint fd, gint what, gint error_number)
{
  gint said[2];

  said[0] = what;
  said[1] = error_number;
  write_all (fd, said, sizeof said);
  _exit (1);
}

/*
 * The files that are open, from one on, are to be closed when the
 * program is run. Which are open is asked of poll(), a great many at
 * a time: the highest a file may be can be a million, and asking of
 * each by itself takes long.
 */
static void
close_on_exec_from (gint lowest)
{
  struct pollfd fds[128];
  glong max = sysconf (_SC_OPEN_MAX);
  gint fd, i, n;

  if (max < 0 || max > INT_MAX)
    max = INT_MAX;
  for (fd = lowest; fd < max; fd += n)
    {
      n = MIN ((glong) G_N_ELEMENTS (fds), max - fd);
      for (i = 0; i < n; i++)
        {
          fds[i].fd = fd + i;
          fds[i].events = 0;
          fds[i].revents = 0;
        }
      if (poll (fds, n, 0) < 0)
        for (i = 0; i < n; i++)
          fds[i].revents = 0;  /* not known: all of them, then */
      for (i = 0; i < n; i++)
        if (!(fds[i].revents & POLLNVAL))
          fcntl (fd + i, F_SETFD, FD_CLOEXEC);
    }
}

/* In the child: its directory and its files are seen to, and it
 * becomes the program or says why it could not. */
static void
do_exec (gint report, const gchar *working_directory, const gchar *file, gchar **argv, gchar **shell_argv,
         gchar **envp, GSpawnFlags flags, gint in, gint out, gint err,
         GSpawnChildSetupFunc child_setup, gpointer user_data)
{
  gint null;

  if (working_directory && chdir (working_directory) < 0)
    child_failed (report, CHILD_CHDIR_FAILED, errno);

  if (in >= 0)
    dup2 (in, 0);
  else if (!(flags & G_SPAWN_CHILD_INHERITS_STDIN) && (null = open ("/dev/null", O_RDONLY)) >= 0)
    {
      dup2 (null, 0);
      if (null > 2)
        close (null);
    }
  if (out >= 0)
    dup2 (out, 1);
  else if ((flags & G_SPAWN_STDOUT_TO_DEV_NULL) && (null = open ("/dev/null", O_WRONLY)) >= 0)
    {
      dup2 (null, 1);
      if (null > 2)
        close (null);
    }
  if (err >= 0)
    dup2 (err, 2);
  else if ((flags & G_SPAWN_STDERR_TO_DEV_NULL) && (null = open ("/dev/null", O_WRONLY)) >= 0)
    {
      dup2 (null, 2);
      if (null > 2)
        close (null);
    }

  if (!(flags & G_SPAWN_LEAVE_DESCRIPTORS_OPEN))
    close_on_exec_from (3);
  else
    {
      if (in > 2)
        close (in);
      if (out > 2)
        close (out);
      if (err > 2)
        close (err);
    }

  /* A program whose parent takes no notice of a broken pipe would
   * take none either, and write to one for ever. */
  signal (SIGPIPE, SIG_DFL);

  if (child_setup)
    child_setup (user_data);

  if (!envp)
    envp = environ;
  execve (file, argv, envp);
  if (errno == ENOEXEC)
    execve ("/bin/sh", shell_argv, envp);  /* a script that does not say what runs it */

  child_failed (report, CHILD_EXEC_FAILED, errno);
}

static gint
exec_error_code (gint error_number)
{
  switch (error_number)
    {
    case EACCES: return G_SPAWN_ERROR_ACCES;
    case EPERM: return G_SPAWN_ERROR_PERM;
    case E2BIG: return G_SPAWN_ERROR_TOO_BIG;
    case ENOEXEC: return G_SPAWN_ERROR_NOEXEC;
    case ENAMETOOLONG: return G_SPAWN_ERROR_NAMETOOLONG;
    case ENOENT: return G_SPAWN_ERROR_NOENT;
    case ENOMEM: return G_SPAWN_ERROR_NOMEM;
    case ENOTDIR: return G_SPAWN_ERROR_NOTDIR;
    case ELOOP: return G_SPAWN_ERROR_LOOP;
    case ETXTBSY: return G_SPAWN_ERROR_TXTBUSY;
    case EIO: return G_SPAWN_ERROR_IO;
    case ENFILE: return G_SPAWN_ERROR_NFILE;
    case EMFILE: return G_SPAWN_ERROR_MFILE;
    case EINVAL: return G_SPAWN_ERROR_INVAL;
    case EISDIR: return G_SPAWN_ERROR_ISDIR;
    default: return G_SPAWN_ERROR_FAILED;
    }
}

static void
close_pair (gint pair[2])
{
  if (pair[0] >= 0)
    close (pair[0]);
  if (pair[1] >= 0)
    close (pair[1]);
  pair[0] = pair[1] = -1;
}

static gboolean
make_pipe (gint pair[2], gboolean wanted, GError **error)
{
  if (wanted && pipe (pair) < 0)
    {
      pair[0] = pair[1] = -1;
      g_set_error (error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
                   "Failed to create a pipe to the child process (%s)", g_strerror (errno));
      return FALSE;
    }

  return TRUE;
}

/*
 * A program started. argv is the program and what it is given, envp
 * its environment or NULL for this one's, working_directory where it
 * starts or NULL for here. child_setup, if there is one, is called
 * in the child before it becomes the program: it may do only what
 * may be done after fork(). A pipe is made for each of standard_input,
 * standard_output and standard_error that is not NULL, and its end
 * here is put there, to be closed by the caller.
 *
 * Without G_SPAWN_DO_NOT_REAP_CHILD the program is nobody's child
 * and need not be waited for. With it, *child_pid is to be waited
 * for, by waitpid() or g_child_watch_add().
 *
 * FALSE and an error if the program could not be started, which is
 * known when this returns: a program that is not there is found out
 * here, not later.
 */
gboolean
g_spawn_async_with_pipes (const gchar *working_directory, gchar **argv, gchar **envp, GSpawnFlags flags,
                          GSpawnChildSetupFunc child_setup, gpointer user_data, GPid *child_pid,
                          gint *standard_input, gint *standard_output, gint *standard_error, GError **error)
{
  gint in[2] = { -1, -1 }, out[2] = { -1, -1 }, err[2] = { -1, -1 }, report[2] = { -1, -1 }, told[2] = { -1, -1 };
  gint said[2], status, n_args, i;
  gchar *file = NULL, **shell_argv = NULL, **args = argv;
  GPid pid = -1, grandchild = -1;
  gssize got;

  g_return_val_if_fail (argv != NULL && argv[0] != NULL, FALSE);
  g_return_val_if_fail (standard_input == NULL || !(flags & G_SPAWN_CHILD_INHERITS_STDIN), FALSE);
  g_return_val_if_fail (standard_output == NULL || !(flags & G_SPAWN_STDOUT_TO_DEV_NULL), FALSE);
  g_return_val_if_fail (standard_error == NULL || !(flags & G_SPAWN_STDERR_TO_DEV_NULL), FALSE);

  if (flags & G_SPAWN_FILE_AND_ARGV_ZERO)
    args = argv + 1;

  /* The program is looked for here, where memory may be asked for. */
  if ((flags & G_SPAWN_SEARCH_PATH) && !strchr (argv[0], '/'))
    {
      file = g_find_program_in_path (argv[0]);
      if (!file)
        {
          g_set_error (error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT,
                       "Failed to execute child process \"%s\" (%s)", argv[0], g_strerror (ENOENT));
          return FALSE;
        }
    }
  else
    file = g_strdup (argv[0]);

  for (n_args = 0; args[n_args]; n_args++)
    ;
  /* For a script that does not say what runs it: sh, the script, and what the script is given. */
  shell_argv = g_new (gchar *, n_args + 3);
  shell_argv[0] = "/bin/sh";
  shell_argv[1] = file;
  for (i = 1; i < n_args; i++)
    shell_argv[i + 1] = args[i];
  shell_argv[MAX (n_args, 1) + 1] = NULL;

  if (!make_pipe (report, TRUE, error) || !make_pipe (told, !(flags & G_SPAWN_DO_NOT_REAP_CHILD), error) ||
      !make_pipe (in, standard_input != NULL, error) || !make_pipe (out, standard_output != NULL, error) ||
      !make_pipe (err, standard_error != NULL, error))
    goto failed;

  pid = fork ();
  if (pid < 0)
    {
      g_set_error (error, G_SPAWN_ERROR, G_SPAWN_ERROR_FORK, "Failed to fork (%s)", g_strerror (errno));
      goto failed;
    }
  if (pid == 0)
    {
      /* Only the far ends are the child's. The pipe it reports by is
       * closed when it becomes the program, which is how that is
       * known here. */
      close (report[0]);
      fcntl (report[1], F_SETFD, FD_CLOEXEC);
      if (in[1] >= 0)
        close (in[1]);
      if (out[0] >= 0)
        close (out[0]);
      if (err[0] >= 0)
        close (err[0]);

      if (!(flags & G_SPAWN_DO_NOT_REAP_CHILD))
        {
          /* The program is this child's child, and this one goes at
           * once: the program is then nobody's to wait for. */
          close (told[0]);
          grandchild = fork ();
          if (grandchild < 0)
            child_failed (report[1], CHILD_FORK_FAILED, errno);
          if (grandchild > 0)
            {
              write_all (told[1], &grandchild, sizeof grandchild);
              _exit (0);
            }
          close (told[1]);
        }

      do_exec (report[1], working_directory, file, args, shell_argv, envp, flags, in[0], out[1], err[1],
               child_setup, user_data);
    }

  close (report[1]);
  report[1] = -1;
  if (in[0] >= 0)
    close (in[0]);
  if (out[1] >= 0)
    close (out[1]);
  if (err[1] >= 0)
    close (err[1]);
  in[0] = out[1] = err[1] = -1;

  if (!(flags & G_SPAWN_DO_NOT_REAP_CHILD))
    {
      close (told[1]);
      told[1] = -1;
      while (waitpid (pid, &status, 0) < 0 && errno == EINTR)
        ;
      if (read_all (told[0], &grandchild, sizeof grandchild) != sizeof grandchild)
        grandchild = -1;
      pid = grandchild;
    }

  got = read_all (report[0], said, sizeof said);
  if (got != 0)
    {
      if (got != sizeof said)
        g_set_error (error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
                     "Failed to read from the child process (%s)", g_strerror (got < 0 ? errno : EIO));
      else if (said[0] == CHILD_CHDIR_FAILED)
        g_set_error (error, G_SPAWN_ERROR, G_SPAWN_ERROR_CHDIR,
                     "Failed to change to directory \"%s\" (%s)", working_directory, g_strerror (said[1]));
      else if (said[0] == CHILD_FORK_FAILED)
        g_set_error (error, G_SPAWN_ERROR, G_SPAWN_ERROR_FORK, "Failed to fork (%s)", g_strerror (said[1]));
      else
        g_set_error (error, G_SPAWN_ERROR, exec_error_code (said[1]),
                     "Failed to execute child process \"%s\" (%s)", argv[0], g_strerror (said[1]));
      if ((flags & G_SPAWN_DO_NOT_REAP_CHILD) && pid > 0)
        while (waitpid (pid, &status, 0) < 0 && errno == EINTR)
          ;
      goto failed;
    }

  close_pair (report);
  close_pair (told);
  g_free (file);
  g_free (shell_argv);
  if (child_pid)
    *child_pid = pid;
  if (standard_input)
    *standard_input = in[1];
  if (standard_output)
    *standard_output = out[0];
  if (standard_error)
    *standard_error = err[0];

  return TRUE;

failed:
  close_pair (report);
  close_pair (told);
  close_pair (in);
  close_pair (out);
  close_pair (err);
  g_free (file);
  g_free (shell_argv);

  return FALSE;
}

gboolean
g_spawn_async (const gchar *working_directory, gchar **argv, gchar **envp, GSpawnFlags flags,
               GSpawnChildSetupFunc child_setup, gpointer user_data, GPid *child_pid, GError **error)
{
  return g_spawn_async_with_pipes (working_directory, argv, envp, flags, child_setup, user_data, child_pid,
                                   NULL, NULL, NULL, error);
}

/* What one pipe has had read from it. */
typedef struct
{
  gint fd;
  gchar *data;
  gsize len, size;
} Catch;

/* FALSE when there is no more to come. */
static gboolean
catch_some (Catch *c)
{
  gssize r;

  if (c->size - c->len < 4096)
    {
      c->size = c->size * 2 + 4096;
      c->data = g_realloc (c->data, c->size);
    }
  r = read (c->fd, c->data + c->len, c->size - c->len - 1);
  if (r < 0 && (errno == EINTR || errno == EAGAIN))
    return TRUE;
  if (r <= 0)
    {
      close (c->fd);
      c->fd = -1;
      return FALSE;
    }
  c->len += r;

  return TRUE;
}

/*
 * A program run and waited for. What it wrote is in *standard_output
 * and *standard_error, where those are not NULL, each ending in a nul
 * and to be freed; where they are NULL the program writes where this
 * one does, unless a flag sends it to /dev/null. *exit_status is as
 * waitpid() has it: see g_spawn_check_exit_status().
 */
gboolean
g_spawn_sync (const gchar *working_directory, gchar **argv, gchar **envp, GSpawnFlags flags,
              GSpawnChildSetupFunc child_setup, gpointer user_data, gchar **standard_output,
              gchar **standard_error, gint *exit_status, GError **error)
{
  Catch caught[2] = { { -1, NULL, 0, 0 }, { -1, NULL, 0, 0 } };
  struct pollfd fds[2];
  gint status = 0, which[2], n, i;
  GPid pid;

  g_return_val_if_fail (!(flags & G_SPAWN_DO_NOT_REAP_CHILD), FALSE);

  if (standard_output)
    *standard_output = NULL;
  if (standard_error)
    *standard_error = NULL;

  if (!g_spawn_async_with_pipes (working_directory, argv, envp, flags | G_SPAWN_DO_NOT_REAP_CHILD, child_setup,
                                 user_data, &pid, NULL, standard_output ? &caught[0].fd : NULL,
                                 standard_error ? &caught[1].fd : NULL, error))
    return FALSE;

  while (caught[0].fd >= 0 || caught[1].fd >= 0)
    {
      for (n = 0, i = 0; i < 2; i++)
        if (caught[i].fd >= 0)
          {
            which[n] = i;
            fds[n].fd = caught[i].fd;
            fds[n].events = POLLIN;
            fds[n++].revents = 0;
          }
      if (poll (fds, n, -1) < 0)
        {
          if (errno == EINTR)
            continue;
          break;
        }
      /* Only what has something: the other may have none for as long as this one is not read. */
      for (i = 0; i < n; i++)
        if (fds[i].revents)
          catch_some (&caught[which[i]]);
    }
  for (i = 0; i < 2; i++)
    if (caught[i].fd >= 0)
      close (caught[i].fd);

  /* One that is not there to wait for has been waited for by a
   * program that ignores its children: how it ended is not known. */
  while (waitpid (pid, &status, 0) < 0 && errno == EINTR)
    ;

  for (i = 0; i < 2; i++)
    {
      gchar **to = i ? standard_error : standard_output;

      if (!to)
        continue;
      if (!caught[i].data)
        caught[i].data = g_malloc (1);
      caught[i].data[caught[i].len] = 0;
      *to = caught[i].data;
    }
  if (exit_status)
    *exit_status = status;

  return TRUE;
}

/* A command line run, its program looked for in the path, and not
 * waited for. */
gboolean
g_spawn_command_line_async (const gchar *command_line, GError **error)
{
  gchar **argv;
  gboolean result;

  g_return_val_if_fail (command_line != NULL, FALSE);

  if (!g_shell_parse_argv (command_line, NULL, &argv, error))
    return FALSE;
  result = g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, error);
  g_strfreev (argv);

  return result;
}

/* And one waited for. */
gboolean
g_spawn_command_line_sync (const gchar *command_line, gchar **standard_output, gchar **standard_error,
                           gint *exit_status, GError **error)
{
  gchar **argv;
  gboolean result;

  g_return_val_if_fail (command_line != NULL, FALSE);

  if (!g_shell_parse_argv (command_line, NULL, &argv, error))
    return FALSE;
  result = g_spawn_sync (NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, standard_output, standard_error,
                         exit_status, error);
  g_strfreev (argv);

  return result;
}

/* TRUE if a program ended well, by what waitpid() said of it; else
 * an error that says how it ended. */
gboolean
g_spawn_check_exit_status (gint exit_status, GError **error)
{
  if (WIFEXITED (exit_status))
    {
      if (WEXITSTATUS (exit_status) == 0)
        return TRUE;
      g_set_error (error, G_SPAWN_EXIT_ERROR, WEXITSTATUS (exit_status),
                   "Child process exited with code %d", WEXITSTATUS (exit_status));
    }
  else if (WIFSIGNALED (exit_status))
    g_set_error (error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
                 "Child process killed by signal %d", WTERMSIG (exit_status));
  else
    g_set_error (error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED, "Child process exited abnormally");

  return FALSE;
}

/* There is nothing to close where a child is a number. */
void
g_spawn_close_pid (GPid pid)
{
}

/* ---------------------------------------------------------------------
 * A child watched by the main loop.
 */

typedef struct
{
  GPid pid;
  gint status;
  gboolean gone;
  GChildWatchFunc function;
} ChildWatch;

static gint child_wake[2] = { -1, -1 };
static GPollFD child_poll;
static struct sigaction child_before;

/* A child has ended: the main loop is woken, which may be between
 * looking and waiting. */
static void
child_signal (int signum)
{
  int saved = errno;

  if (write (child_wake[1], "", 1) < 0)
    {
      /* full: it will wake as it is */
    }
  if (child_before.sa_handler != SIG_DFL && child_before.sa_handler != SIG_IGN)
    child_before.sa_handler (signum);
  errno = saved;
}

static gboolean
child_gone (ChildWatch *watch)
{
  if (!watch->gone)
    {
      pid_t r = waitpid (watch->pid, &watch->status, WNOHANG);

      /* One that is no child of this program, or that another has
       * waited for, has ended for all that can be known. */
      if (r == watch->pid || (r < 0 && errno != EINTR))
        {
          if (r < 0)
            watch->status = 0;
          watch->gone = TRUE;
        }
    }

  return watch->gone;
}

static gboolean
child_prepare (gpointer source_data, GTimeVal *current_time, gint *timeout, gpointer user_data)
{
  *timeout = -1;

  return child_gone (source_data);
}

static gboolean
child_check (gpointer source_data, GTimeVal *current_time, gpointer user_data)
{
  gchar woken[64];

  if (child_poll.revents)
    while (read (child_wake[0], woken, sizeof woken) > 0)
      ;

  return child_gone (source_data);
}

static gboolean
child_dispatch (gpointer source_data, GTimeVal *dispatch_time, gpointer user_data)
{
  ChildWatch *watch = source_data;

  watch->function (watch->pid, watch->status, user_data);

  return FALSE;
}

static GSourceFuncs child_funcs = { child_prepare, child_check, child_dispatch, g_free };

/*
 * function is called from the main loop, once, when the child has
 * ended, with what waitpid() said of it; the child is waited for
 * here and by no one else. It is to be a child started with
 * G_SPAWN_DO_NOT_REAP_CHILD, or by fork(). What is returned is for
 * g_source_remove().
 *
 * The first watch takes SIGCHLD for the program, and calls what had
 * it before. A program that takes it back, or ignores it, has
 * watches that are slow or see nothing.
 */
guint
g_child_watch_add_full (gint priority, GPid pid, GChildWatchFunc function, gpointer data, GDestroyNotify notify)
{
  ChildWatch *watch;

  g_return_val_if_fail (function != NULL, 0);
  g_return_val_if_fail (pid > 0, 0);

  if (child_wake[0] < 0)
    {
      struct sigaction action;
      gint i;

      if (pipe (child_wake) < 0)
        g_error ("Cannot create a pipe for watching children: %s", g_strerror (errno));
      for (i = 0; i < 2; i++)
        {
          fcntl (child_wake[i], F_SETFL, fcntl (child_wake[i], F_GETFL) | O_NONBLOCK);
          fcntl (child_wake[i], F_SETFD, FD_CLOEXEC);
        }
      child_poll.fd = child_wake[0];
      child_poll.events = G_IO_IN;
      g_main_add_poll (&child_poll, G_PRIORITY_DEFAULT);

      memset (&action, 0, sizeof action);
      action.sa_handler = child_signal;
      sigemptyset (&action.sa_mask);
      action.sa_flags = SA_RESTART | SA_NOCLDSTOP;
      sigaction (SIGCHLD, &action, &child_before);
    }

  watch = g_new0 (ChildWatch, 1);
  watch->pid = pid;
  watch->function = function;

  return g_source_add (priority, FALSE, &child_funcs, watch, data, notify);
}

guint
g_child_watch_add (GPid pid, GChildWatchFunc function, gpointer data)
{
  return g_child_watch_add_full (G_PRIORITY_DEFAULT, pid, function, data, NULL);
}
