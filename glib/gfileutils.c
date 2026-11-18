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
 * Files: reading one whole, writing one so that a failure leaves the
 * old one, and the names of files. The names of the functions are
 * GLib 2's.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "glib.h"

GQuark
g_file_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("g-file-error-quark");

  return quark;
}

/* The GFileError for a value of errno. */
GFileError
g_file_error_from_errno (gint err_no)
{
  switch (err_no)
    {
    case EEXIST: return G_FILE_ERROR_EXIST;
    case EISDIR: return G_FILE_ERROR_ISDIR;
    case EACCES: return G_FILE_ERROR_ACCES;
    case ENAMETOOLONG: return G_FILE_ERROR_NAMETOOLONG;
    case ENOENT: return G_FILE_ERROR_NOENT;
    case ENOTDIR: return G_FILE_ERROR_NOTDIR;
    case ENXIO: return G_FILE_ERROR_NXIO;
    case ENODEV: return G_FILE_ERROR_NODEV;
    case EROFS: return G_FILE_ERROR_ROFS;
    case ETXTBSY: return G_FILE_ERROR_TXTBSY;
    case EFAULT: return G_FILE_ERROR_FAULT;
    case ELOOP: return G_FILE_ERROR_LOOP;
    case ENOSPC: return G_FILE_ERROR_NOSPC;
    case ENOMEM: return G_FILE_ERROR_NOMEM;
    case EMFILE: return G_FILE_ERROR_MFILE;
    case ENFILE: return G_FILE_ERROR_NFILE;
    case EBADF: return G_FILE_ERROR_BADF;
    case EINVAL: return G_FILE_ERROR_INVAL;
    case EPIPE: return G_FILE_ERROR_PIPE;
    case EAGAIN: return G_FILE_ERROR_AGAIN;
    case EINTR: return G_FILE_ERROR_INTR;
    case EIO: return G_FILE_ERROR_IO;
    case EPERM: return G_FILE_ERROR_PERM;
    case ENOSYS: return G_FILE_ERROR_NOSYS;
    default: return G_FILE_ERROR_FAILED;
    }
}

/* An error for what errno says went wrong with a file. format has one
 * %s for the file's name and one for the reason, in that order. */
static void
set_file_error (GError **error, const gchar *filename, const gchar *format)
{
  gint saved = errno;
  gchar *name = g_filename_display_name (filename);

  g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved), format, name, g_strerror (saved));
  g_free (name);
}

/* Whether any of the tests is true of the file: there at all, a plain
 * file, a directory, one that can be run, or a symbolic link. All but
 * the last look at what a link leads to. */
gboolean
g_file_test (const gchar *filename, GFileTest test)
{
  struct stat s;

  g_return_val_if_fail (filename != NULL, FALSE);

  if ((test & G_FILE_TEST_EXISTS) && access (filename, F_OK) == 0)
    return TRUE;

  if ((test & G_FILE_TEST_IS_SYMLINK) && lstat (filename, &s) == 0 && S_ISLNK (s.st_mode))
    return TRUE;

  if ((test & (G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_DIR | G_FILE_TEST_IS_EXECUTABLE)) && stat (filename, &s) == 0)
    {
      if ((test & G_FILE_TEST_IS_REGULAR) && S_ISREG (s.st_mode))
        return TRUE;
      if ((test & G_FILE_TEST_IS_DIR) && S_ISDIR (s.st_mode))
        return TRUE;
      /* Run by this user, and a file: a directory can be "executed" too. */
      if ((test & G_FILE_TEST_IS_EXECUTABLE) && S_ISREG (s.st_mode) && access (filename, X_OK) == 0)
        return TRUE;
    }

  return FALSE;
}

/*
 * The whole of a file in *contents, to be freed with g_free(), with
 * a nul after it that is not counted in *length. FALSE and an error
 * if it cannot be read.
 */
gboolean
g_file_get_contents (const gchar *filename, gchar **contents, gsize *length, GError **error)
{
  struct stat s;
  gchar *buf;
  gsize size, used = 0;
  gint fd;

  g_return_val_if_fail (filename != NULL && contents != NULL, FALSE);

  *contents = NULL;
  if (length)
    *length = 0;

  fd = open (filename, O_RDONLY);
  if (fd < 0)
    {
      set_file_error (error, filename, "The file '%s' could not be opened: %s");
      return FALSE;
    }

  /* What the file says its size is, to begin with: of a pipe or a
   * file in /proc that is nought, and it is read all the same. */
  size = fstat (fd, &s) == 0 && S_ISREG (s.st_mode) && s.st_size > 0 ? (gsize) s.st_size + 1 : 4096;
  buf = g_malloc (size);

  for (;;)
    {
      gssize n = read (fd, buf + used, size - used - 1);

      if (n < 0 && errno == EINTR)
        continue;
      if (n < 0)
        {
          set_file_error (error, filename, "The file '%s' could not be read: %s");
          close (fd);
          g_free (buf);
          return FALSE;
        }
      if (n == 0)
        break;
      used += n;
      if (used + 1 == size)
        {
          size *= 2;
          buf = g_realloc (buf, size);
        }
    }
  close (fd);

  buf[used] = 0;
  *contents = g_realloc (buf, used + 1);
  if (length)
    *length = used;

  return TRUE;
}

/*
 * Writes length bytes of contents (all to its nul, if length is
 * negative) as the file. They go to a new file beside it that then
 * takes its name, so that the file is the old one or the new one
 * whole, whatever happens on the way.
 *
 * The new file has the old one's permissions. A name that is a
 * symbolic link stays one: the file it leads to is written.
 */
gboolean
g_file_set_contents (const gchar *filename, const gchar *contents, gssize length, GError **error)
{
  struct stat s;
  gchar *target = NULL, *tmp;
  gsize len, done = 0;
  mode_t mode, mask;
  gboolean written;
  gint fd, saved;

  g_return_val_if_fail (filename != NULL, FALSE);
  g_return_val_if_fail (contents != NULL || length == 0, FALSE);

  len = length < 0 ? strlen (contents) : (gsize) length;

  if (lstat (filename, &s) == 0 && S_ISLNK (s.st_mode))
    {
      target = realpath (filename, NULL);
      if (!target)
        {
          set_file_error (error, filename, "The link '%s' leads nowhere that can be written: %s");
          return FALSE;
        }
      filename = target;
    }

  if (stat (filename, &s) == 0)
    mode = s.st_mode & 07777;
  else
    {
      mask = umask (0);
      umask (mask);
      mode = 0666 & ~mask;
    }

  tmp = g_strconcat (filename, ".XXXXXX", NULL);
  fd = mkstemp (tmp);
  if (fd < 0)
    {
      set_file_error (error, filename, "A file to write '%s' could not be made: %s");
      g_free (tmp);
      free (target);
      return FALSE;
    }

  while (done < len)
    {
      gssize n = write (fd, contents + done, len - done);

      if (n < 0 && errno == EINTR)
        continue;
      if (n < 0)
        break;
      done += n;
    }

  /* The name is given to the new file only once what is in it is
   * known to be kept. */
  written = done == len && fchmod (fd, mode) == 0 && fsync (fd) == 0;
  saved = errno;
  if (close (fd) == 0)
    errno = saved;
  else if (!written)
    errno = saved;
  else
    written = FALSE;

  if (!written || rename (tmp, filename) < 0)
    {
      set_file_error (error, filename, "The file '%s' could not be written: %s");
      unlink (tmp);
      g_free (tmp);
      free (target);
      return FALSE;
    }

  g_free (tmp);
  free (target);

  return TRUE;
}

/* Makes a directory and those above it that are not there yet. Nought
 * if it is there in the end; otherwise -1, and errno says why. */
gint
g_mkdir_with_parents (const gchar *pathname, gint mode)
{
  gchar *path, *p;

  if (!pathname || !*pathname)
    {
      errno = EINVAL;
      return -1;
    }

  path = g_strdup (pathname);
  p = path;
  do
    {
      /* To the end of the next part of the name. */
      while (*p == G_DIR_SEPARATOR)
        p++;
      while (*p && *p != G_DIR_SEPARATOR)
        p++;

      if (p > path)
        {
          gchar c = *p;

          *p = 0;
          if (mkdir (path, mode) < 0)
            {
              gint saved = errno;

              /* A part that is there already is no failure, whatever
               * was said of making it again: for a root that is read
               * only that is not EEXIST. */
              if (!g_file_test (path, G_FILE_TEST_IS_DIR))
                {
                  g_free (path);
                  errno = saved == EEXIST ? ENOTDIR : saved;
                  return -1;
                }
            }
          *p = c;
        }
    }
  while (*p);

  g_free (path);

  return 0;
}

/*
 * The parts joined with separator between them, in memory to be
 * freed. Where two parts meet, the separators they bring are made
 * one; those before the first part and after the last are kept.
 * Empty parts are passed over.
 */
static gchar*
build_path (const gchar *separator, const gchar *first, va_list args)
{
  GString *path = g_string_new (NULL);
  gsize sep = strlen (separator);
  const gchar *part = first, *next;
  gboolean any = FALSE;
  gchar *result;

  for (; part; part = next)
    {
      const gchar *start = part, *end;

      next = va_arg (args, const gchar *);
      if (!*part)
        continue;

      end = part + strlen (part);
      if (sep)
        {
          /* Not the first part: without the separators before it. */
          if (any)
            while (strncmp (start, separator, sep) == 0)
              start += sep;
          /* Not the last: without those after it. */
          if (next)
            while (end - start >= (gssize) sep && strncmp (end - sep, separator, sep) == 0)
              end -= sep;
          /* A part that was separators and nothing else adds
           * nothing between others; last, it ends the path with one,
           * and first, it is the root. */
          if (end == start && any)
            {
              if (!next && ((gsize) path->len < sep || strcmp (path->str + path->len - sep, separator) != 0))
                g_string_append (path, separator);
              continue;
            }
          if (any && ((gsize) path->len < sep || strcmp (path->str + path->len - sep, separator) != 0))
            g_string_append (path, separator);
        }
      if (end == start && !any && next)
        g_string_append (path, separator);
      else
        {
          gchar *piece = g_strndup (start, end - start);

          g_string_append (path, piece);
          g_free (piece);
        }
      any = TRUE;
    }

  result = path->str;
  g_string_free (path, FALSE);

  return result;
}

/* g_build_path ("/", "/usr", "share/", "/themes", NULL) is
 * "/usr/share/themes". The list ends with a NULL. */
gchar*
g_build_path (const gchar *separator, const gchar *first_element, ...)
{
  va_list args;
  gchar *path;

  g_return_val_if_fail (separator != NULL, NULL);

  va_start (args, first_element);
  path = build_path (separator, first_element, args);
  va_end (args);

  return path;
}

/* The same with the separator of directories. */
gchar*
g_build_filename (const gchar *first_element, ...)
{
  va_list args;
  gchar *path;

  va_start (args, first_element);
  path = build_path (G_DIR_SEPARATOR_S, first_element, args);
  va_end (args);

  return path;
}

/* The last part of a file's name, in memory to be freed: "c" of
 * "/a/b/c" and of "/a/b/c/", "/" of "/", and "." of nothing. */
gchar*
g_path_get_basename (const gchar *file_name)
{
  const gchar *end, *start;

  g_return_val_if_fail (file_name != NULL, NULL);

  if (!*file_name)
    return g_strdup (".");

  end = file_name + strlen (file_name);
  while (end > file_name && end[-1] == G_DIR_SEPARATOR)
    end--;
  if (end == file_name)
    return g_strdup (G_DIR_SEPARATOR_S);

  start = end;
  while (start > file_name && start[-1] != G_DIR_SEPARATOR)
    start--;

  return g_strndup (start, end - start);
}

/* The directory a file is in, in memory to be freed: "/a/b" of
 * "/a/b/c", "/" of "/a", and "." of a name with no directory. */
gchar*
g_path_get_dirname (const gchar *file_name)
{
  const gchar *end;

  g_return_val_if_fail (file_name != NULL, NULL);

  end = strrchr (file_name, G_DIR_SEPARATOR);
  if (!end)
    return g_strdup (".");

  while (end > file_name && end[-1] == G_DIR_SEPARATOR)
    end--;
  if (end == file_name)
    return g_strdup (G_DIR_SEPARATOR_S);

  return g_strndup (file_name, end - file_name);
}

/* Where the program a shell would run for this name is, in memory to
 * be freed, or NULL if there is none. A name with a slash in it is
 * not looked for: it is the answer if it can be run. */
gchar*
g_find_program_in_path (const gchar *program)
{
  const gchar *path, *p;

  g_return_val_if_fail (program != NULL, NULL);

  if (strchr (program, G_DIR_SEPARATOR))
    return g_file_test (program, G_FILE_TEST_IS_EXECUTABLE) ? g_strdup (program) : NULL;

  path = g_getenv ("PATH");
  if (!path)
    path = "/bin:/usr/bin";

  for (p = path; ; p += strcspn (p, ":") + 1)
    {
      gsize n = strcspn (p, ":");
      gchar *dir = n ? g_strndup (p, n) : g_strdup ("."), *file = g_build_filename (dir, program, NULL);

      g_free (dir);
      if (g_file_test (file, G_FILE_TEST_IS_EXECUTABLE))
        return file;
      g_free (file);
      if (!p[n])
        break;
    }

  return NULL;
}
