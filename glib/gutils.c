/* GLIB - Library of useful routines for C programming
 * Copyright (C) 1995-1998  Peter Mattis, Spencer Kimball and Josh MacDonald
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

#include "glibconfig.h"

#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/param.h>


/* implement Glib's inline functions
 */
#define G_INLINE_FUNC extern
#define G_CAN_INLINE 1
#include "glib.h"

#ifdef  MAXPATHLEN
#define G_PATH_LENGTH   (MAXPATHLEN + 1)
#elif   defined (PATH_MAX)
#define G_PATH_LENGTH   (PATH_MAX + 1)
#else   /* !MAXPATHLEN */
#define G_PATH_LENGTH   (2048 + 1)
#endif  /* !MAXPATHLEN && !PATH_MAX */

const guint glib_major_version = GLIB_MAJOR_VERSION;
const guint glib_minor_version = GLIB_MINOR_VERSION;
const guint glib_micro_version = GLIB_MICRO_VERSION;
const guint glib_interface_age = GLIB_INTERFACE_AGE;
const guint glib_binary_age = GLIB_BINARY_AGE;


void
g_atexit (GVoidFunc func)
{
  if (atexit ((void (*)(void)) func))
    g_error ("Could not register atexit() function: %s", g_strerror (errno));
}

gint
g_snprintf (gchar       *str,
            gulong       n,
            gchar const *fmt,
            ...)
{
  va_list args;
  gint retval;

  g_return_val_if_fail (str != NULL, 0);
  g_return_val_if_fail (n > 0, 0);
  g_return_val_if_fail (fmt != NULL, 0);

  va_start (args, fmt);
  retval = vsnprintf (str, n, fmt, args);
  va_end (args);

  if (retval < 0)
    {
      str[n-1] = '\0';
      retval = strlen (str);
    }

  return retval;
}

gint
g_vsnprintf (gchar       *str,
             gulong       n,
             gchar const *fmt,
             va_list      args)
{
  gint retval;

  g_return_val_if_fail (str != NULL, 0);
  g_return_val_if_fail (n > 0, 0);
  g_return_val_if_fail (fmt != NULL, 0);

  retval = vsnprintf (str, n, fmt, args);

  if (retval < 0)
    {
      str[n-1] = '\0';
      retval = strlen (str);
    }

  return retval;
}

guint
g_parse_debug_string  (const gchar *string,
                       GDebugKey   *keys,
                       guint        nkeys)
{
  guint i;
  guint result = 0;

  g_return_val_if_fail (string != NULL, 0);

  if (!g_strcasecmp (string, "all"))
    {
      for (i=0; i<nkeys; i++)
        result |= keys[i].value;
    }
  else
    {
      gchar *str = g_strdup (string);
      gchar *p = str;
      gchar *q;
      gboolean done = FALSE;

      while (*p && !done)
        {
          q = strchr (p, ':');
          if (!q)
            {
              q = p + strlen(p);
              done = TRUE;
            }

          *q = 0;

          for (i=0; i<nkeys; i++)
            if (!g_strcasecmp(keys[i].key, p))
              result |= keys[i].value;

          p = q+1;
        }

      g_free (str);
    }

  return result;
}

gchar*
g_basename (const gchar    *file_name)
{
  register gchar *base;

  g_return_val_if_fail (file_name != NULL, NULL);

  base = strrchr (file_name, G_DIR_SEPARATOR);
  if (base)
    return base + 1;


  return (gchar*) file_name;
}

gboolean
g_path_is_absolute (const gchar *file_name)
{
  g_return_val_if_fail (file_name != NULL, FALSE);

  if (file_name[0] == G_DIR_SEPARATOR)
    return TRUE;


  return FALSE;
}

gchar*
g_path_skip_root (gchar *file_name)
{
  g_return_val_if_fail (file_name != NULL, NULL);

  if (file_name[0] == G_DIR_SEPARATOR)
    return file_name + 1;


  return NULL;
}

gchar*
g_dirname (const gchar     *file_name)
{
  register gchar *base;
  register guint len;

  g_return_val_if_fail (file_name != NULL, NULL);

  base = strrchr (file_name, G_DIR_SEPARATOR);
  if (!base)
    return g_strdup (".");
  while (base > file_name && *base == G_DIR_SEPARATOR)
    base--;
  len = (guint) 1 + base - file_name;

  base = g_new (gchar, len + 1);
  g_memmove (base, file_name, len);
  base[len] = 0;

  return base;
}

gchar*
g_get_current_dir (void)
{
  gchar *buffer;
  gchar *dir;

  buffer = g_new (gchar, G_PATH_LENGTH);
  *buffer = 0;

  dir = getcwd (buffer, G_PATH_LENGTH - 1);

  if (!dir || !*buffer)
    {
      /* hm, should we g_error() out here?
       * this can happen if e.g. "./" has mode \0000
       */
      buffer[0] = G_DIR_SEPARATOR;
      buffer[1] = 0;
    }

  dir = g_strdup (buffer);
  g_free (buffer);

  return dir;
}

gchar*
g_getenv (const gchar *variable)
{
  g_return_val_if_fail (variable != NULL, NULL);

  return getenv (variable);
}


G_LOCK_DEFINE_STATIC (g_utils_global);

static  gchar   *g_tmp_dir = NULL;
static  gchar   *g_user_name = NULL;
static  gchar   *g_real_name = NULL;
static  gchar   *g_home_dir = NULL;

/* HOLDS: g_utils_global_lock */
static void
g_get_any_init (void)
{
  if (!g_tmp_dir)
    {
      g_tmp_dir = g_strdup (g_getenv ("TMPDIR"));
      if (!g_tmp_dir)
        g_tmp_dir = g_strdup (g_getenv ("TMP"));
      if (!g_tmp_dir)
        g_tmp_dir = g_strdup (g_getenv ("TEMP"));

#ifdef P_tmpdir
      if (!g_tmp_dir)
        {
          int k;
          g_tmp_dir = g_strdup (P_tmpdir);
          k = strlen (g_tmp_dir);
          if (g_tmp_dir[k-1] == G_DIR_SEPARATOR)
            g_tmp_dir[k-1] = '\0';
        }
#endif

      if (!g_tmp_dir)
        {
          g_tmp_dir = g_strdup ("/tmp");
        }

      if (!g_home_dir)
        g_home_dir = g_strdup (g_getenv ("HOME"));


      {
        struct passwd *pw = NULL;
        gpointer buffer = NULL;

        struct passwd pwd;
#    ifdef _SC_GETPW_R_SIZE_MAX
        /* This returns the maximum length, or -1 if there is no fixed
         * limit (POSIX allows that, and musl does it); the loop below
         * grows the buffer on ERANGE, so start small in that case. */
        glong sc_bufsize = sysconf (_SC_GETPW_R_SIZE_MAX);
        guint bufsize = sc_bufsize > 0 ? (guint) sc_bufsize : 64;
#    else /* _SC_GETPW_R_SIZE_MAX */
        guint bufsize = 64;
#    endif /* _SC_GETPW_R_SIZE_MAX */
        gint error;

        do
          {
            g_free (buffer);
            buffer = g_malloc (bufsize);
            errno = 0;

            error = getpwuid_r (getuid (), &pwd, buffer, bufsize, &pw);
            error = error < 0 ? errno : error;

            if (!pw)
              {
                /* we bail out prematurely if the user id can't be found
                 * (should be pretty rare case actually), or if the buffer
                 * should be sufficiently big and lookups are still not
                 * successfull.
                 */
                if (error == 0 || error == ENOENT)
                  {
                    g_warning ("getpwuid_r(): failed due to: No such user %d.",
                               getuid ());
                    break;
                  }
                if (bufsize > 32 * 1024)
                  {
                    g_warning ("getpwuid_r(): failed due to: %s.",
                               g_strerror (error));
                    break;
                  }

                bufsize *= 2;
              }
          }
        while (!pw);

        if (!pw)
          {
            setpwent ();
            pw = getpwuid (getuid ());
            endpwent ();
          }
        if (pw)
          {
            g_user_name = g_strdup (pw->pw_name);
            g_real_name = g_strdup (pw->pw_gecos);
            if (!g_home_dir)
              g_home_dir = g_strdup (pw->pw_dir);
          }
        g_free (buffer);
      }


      if (!g_user_name)
        g_user_name = g_strdup ("somebody");
      if (!g_real_name)
        g_real_name = g_strdup ("Unknown");
      else
        {
          gchar *p;

          for (p = g_real_name; *p; p++)
            if (*p == ',')
              {
                *p = 0;
                p = g_strdup (g_real_name);
                g_free (g_real_name);
                g_real_name = p;
                break;
              }
        }
    }
}

gchar*
g_get_user_name (void)
{
  G_LOCK (g_utils_global);
  if (!g_tmp_dir)
    g_get_any_init ();
  G_UNLOCK (g_utils_global);

  return g_user_name;
}

gchar*
g_get_real_name (void)
{
  G_LOCK (g_utils_global);
  if (!g_tmp_dir)
    g_get_any_init ();
  G_UNLOCK (g_utils_global);

  return g_real_name;
}

/* Return the home directory of the user. If there is a HOME
 * environment variable, its value is returned, otherwise use some
 * system-dependent way of finding it out. If no home directory can be
 * deduced, return NULL.
 */

gchar*
g_get_home_dir (void)
{
  G_LOCK (g_utils_global);
  if (!g_tmp_dir)
    g_get_any_init ();
  G_UNLOCK (g_utils_global);

  return g_home_dir;
}

/* Return a directory to be used to store temporary files. This is the
 * value of the TMPDIR, TMP or TEMP environment variables (they are
 * checked in that order). If none of those exist, use P_tmpdir from
 * stdio.h. If that isn't defined, return "/tmp" on POSIXly systems,
 * and C:\ on Windows.
 */

gchar*
g_get_tmp_dir (void)
{
  G_LOCK (g_utils_global);
  if (!g_tmp_dir)
    g_get_any_init ();
  G_UNLOCK (g_utils_global);

  return g_tmp_dir;
}

/*
 * Where a user's files of each kind go, by the XDG Base Directory
 * Specification, and by GLib 2's names. A variable in the environment
 * that is empty, or is not a path from the root, is not used, as the
 * specification says. What is returned belongs to GLib and stays as
 * it was first asked for.
 */

static gchar *g_user_dirs[4];
static gchar **g_system_dirs[2];

static const gchar*
g_user_dir (gint which, const gchar *variable, const gchar *under_home)
{
  G_LOCK (g_utils_global);
  if (!g_user_dirs[which])
    {
      const gchar *env = g_getenv (variable);

      if (env && g_path_is_absolute (env))
        g_user_dirs[which] = g_strdup (env);
      else if (under_home)
        {
          if (!g_tmp_dir)
            g_get_any_init ();
          g_user_dirs[which] = g_build_filename (g_home_dir ? g_home_dir : g_tmp_dir, under_home, NULL);
        }
    }
  G_UNLOCK (g_utils_global);

  return g_user_dirs[which];
}

/* Settings: $XDG_CONFIG_HOME, or .config at home. */
const gchar*
g_get_user_config_dir (void)
{
  return g_user_dir (0, "XDG_CONFIG_HOME", ".config");
}

/* What a program keeps for the user: $XDG_DATA_HOME, or .local/share
 * at home. */
const gchar*
g_get_user_data_dir (void)
{
  return g_user_dir (1, "XDG_DATA_HOME", ".local/share");
}

/* What can be made again: $XDG_CACHE_HOME, or .cache at home. */
const gchar*
g_get_user_cache_dir (void)
{
  return g_user_dir (2, "XDG_CACHE_HOME", ".cache");
}

/* Sockets and the like, for as long as the user is logged in:
 * $XDG_RUNTIME_DIR, and where that is not set, the cache. */
const gchar*
g_get_user_runtime_dir (void)
{
  const gchar *dir = g_user_dir (3, "XDG_RUNTIME_DIR", NULL);

  return dir ? dir : g_get_user_cache_dir ();
}

static const gchar* const *
g_system_dir_list (gint which, const gchar *variable, const gchar *otherwise)
{
  G_LOCK (g_utils_global);
  if (!g_system_dirs[which])
    {
      const gchar *env = g_getenv (variable);
      gchar **all = g_strsplit (env && *env ? env : otherwise, ":", 0);
      gint i, n = 0;

      /* Those that are paths from the root. */
      for (i = 0; all[i]; i++)
        if (g_path_is_absolute (all[i]))
          all[n++] = all[i];
        else
          g_free (all[i]);
      all[n] = NULL;
      g_system_dirs[which] = all;
    }
  G_UNLOCK (g_utils_global);

  return (const gchar * const *) g_system_dirs[which];
}

/* Where what is installed for every user is looked for, in order:
 * $XDG_DATA_DIRS, or /usr/local/share and /usr/share. The list ends
 * with a NULL. */
const gchar* const *
g_get_system_data_dirs (void)
{
  return g_system_dir_list (0, "XDG_DATA_DIRS", "/usr/local/share:/usr/share");
}

/* And the settings for every user: $XDG_CONFIG_DIRS, or /etc/xdg. */
const gchar* const *
g_get_system_config_dirs (void)
{
  return g_system_dir_list (1, "XDG_CONFIG_DIRS", "/etc/xdg");
}

static gchar *g_prgname = NULL;

gchar*
g_get_prgname (void)
{
  gchar* retval;

  G_LOCK (g_utils_global);
  retval = g_prgname;
  G_UNLOCK (g_utils_global);

  return retval;
}

void
g_set_prgname (const gchar *prgname)
{
  gchar *c;

  G_LOCK (g_utils_global);
  c = g_prgname;
  g_prgname = g_strdup (prgname);
  g_free (c);
  G_UNLOCK (g_utils_global);
}

guint
g_direct_hash (gconstpointer v)
{
  return GPOINTER_TO_UINT (v);
}

gint
g_direct_equal (gconstpointer v1,
                gconstpointer v2)
{
  return v1 == v2;
}

gint
g_int_equal (gconstpointer v1,
             gconstpointer v2)
{
  return *((const gint*) v1) == *((const gint*) v2);
}

guint
g_int_hash (gconstpointer v)
{
  return *(const gint*) v;
}
