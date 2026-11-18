/* GDK - The GIMP Drawing Kit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
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
 * Modified by the GTK+ Team and others 1997-1999. See the AUTHORS
 * file for a list of people on the GTK+ Team. See the ChangeLog
 * files for a list of changes. These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/.
 */

/*
 * Starting and stopping GDK, and the odds and ends that belong to no
 * window system: the program's arguments, timers, the threads lock.
 * The window system is opened by whichever backend gdk_init picks.
 */

#include "config.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "gdk.h"
#include "gdkprivate.h"
#include "gdkbackend.h"

static void gdk_exit_func (void);

#ifdef G_ENABLE_DEBUG
static const GDebugKey gdk_debug_keys[] = {
  {"events",        GDK_DEBUG_EVENTS},
  {"misc",          GDK_DEBUG_MISC},
  {"dnd",           GDK_DEBUG_DND},
  {"color-context", GDK_DEBUG_COLOR_CONTEXT},
  {"xim",           GDK_DEBUG_XIM}
};

static const int gdk_ndebug_keys = sizeof(gdk_debug_keys)/sizeof(GDebugKey);

#endif /* G_ENABLE_DEBUG */

static int gdk_initialized = 0;                     /* 1 if the library is initialized,
                                                     * 0 otherwise.
                                                     */

static struct timeval start;                        /* The time at which the library was
                                                     *  last initialized.
                                                     */
static guint32 timer_val;                           /* The timeout length as specified by
                                                     *  the user in milliseconds.
                                                     */

static const char *
get_option (char ***argv,
            gint    argc,
            int    *i_inout)
{
  gchar *equal_pos;
  const gchar *result = NULL;
  gint i = *i_inout;
  const gchar *orig = (*argv)[i];

  equal_pos = strchr ((*argv)[i], '=');
  (*argv)[i] = NULL;

  if (equal_pos)
    {
      result = equal_pos + 1;
    }
  else
    {
      if ((i + 1) < argc && (*argv)[i + 1])
        {
          i++;
          result = (*argv)[i];
          (*argv)[i] = NULL;
        }
      else
        {
          g_warning ("Option '%s' requires an argument.", orig);
        }
    }

  *i_inout = i;
  return result;
}

gboolean
gdk_init_check (int      *argc,
                char ***argv)
{
  gint synchronize;
  gint i, j, k;
  const gchar *option;

  if (gdk_initialized)
    return TRUE;

  if (g_thread_supported ())
    gdk_threads_mutex = g_mutex_new ();

  gettimeofday (&start, NULL);

  gdk_display_name = NULL;

  if (!_gdk_backend)
    _gdk_backend = _gdk_backend_choose ();
  if (!_gdk_backend)
    {
      g_warning ("GDK was built with no backend");
      return FALSE;
    }

  synchronize = FALSE;

#ifdef G_ENABLE_DEBUG
  {
    gchar *debug_string = getenv("GDK_DEBUG");
    if (debug_string != NULL)
      gdk_debug_flags = g_parse_debug_string (debug_string,
                                              (GDebugKey *) gdk_debug_keys,
                                              gdk_ndebug_keys);
  }
#endif  /* G_ENABLE_DEBUG */

  if (argc && argv)
    {
      if (*argc > 0)
        {
          gchar *d;

          d = strrchr((*argv)[0],'/');
          if (d != NULL)
            g_set_prgname (d + 1);
          else
            g_set_prgname ((*argv)[0]);
        }

      for (i = 1; i < *argc;)
        {
#ifdef G_ENABLE_DEBUG
          if ((strcmp ("--gdk-debug", (*argv)[i]) == 0) ||
              (strncmp ("--gdk-debug=", (*argv)[i], 12) == 0))
            {
              option = get_option (argv, *argc, &i);

              if (option)
                gdk_debug_flags |= g_parse_debug_string (option,
                                                         (GDebugKey *) gdk_debug_keys,
                                                         gdk_ndebug_keys);
            }
          else if ((strcmp ("--gdk-no-debug", (*argv)[i]) == 0) ||
                   (strncmp ("--gdk-no-debug=", (*argv)[i], 15) == 0))
            {
              option = get_option (argv, *argc, &i);

              if (option)
                gdk_debug_flags &= ~g_parse_debug_string (option,
                                                          (GDebugKey *) gdk_debug_keys,
                                                          gdk_ndebug_keys);
            }
          else
#endif /* G_ENABLE_DEBUG */
            if ((strcmp ("--display", (*argv)[i]) == 0) ||
                (strncmp ("--display=", (*argv)[i], 10) == 0))
              {
                option = get_option (argv, *argc, &i);

                if (option)
                  {
                    if (gdk_display_name)
                      g_free (gdk_display_name);

                    gdk_display_name = g_strdup (option);
                  }
              }
            else if (strcmp ("--sync", (*argv)[i]) == 0)
              {
                (*argv)[i] = NULL;
                synchronize = TRUE;
              }
            else if (strcmp ("--no-xshm", (*argv)[i]) == 0)
              {
                (*argv)[i] = NULL;
                gdk_use_xshm = FALSE;
              }
            else if ((strcmp ("--name", (*argv)[i]) == 0) ||
                     (strncmp ("--name=", (*argv)[i], 7) == 0))
              {
                option = get_option (argv, *argc, &i);

                if (option)
                  g_set_prgname (option);
              }
            else if ((strcmp ("--class", (*argv)[i]) == 0) ||
                     (strncmp ("--class=", (*argv)[i], 8) == 0))
              {
                option = get_option (argv, *argc, &i);

                if (option)
                  {
                    if (gdk_progclass)
                      g_free (gdk_progclass);

                    gdk_progclass = g_strdup (option);
                  }
              }
            /* The input method styles of GTK+ 1.2: taken and left alone, as
             * the input method decides how text being composed is shown. */
            else if ((strcmp ("--xim-preedit", (*argv)[i]) == 0) ||
                     (strncmp ("--xim-preedit=", (*argv)[i], 14) == 0) ||
                     (strcmp ("--xim-status", (*argv)[i]) == 0) ||
                     (strncmp ("--xim-status=", (*argv)[i], 13) == 0))
              {
                get_option (argv, *argc, &i);
              }

          i++;
        }

      for (i = 1; i < *argc; i++)
        {
          for (k = i; k < *argc; k++)
            if ((*argv)[k] != NULL)
              break;

          if (k > i)
            {
              k -= i;
              for (j = i + k; j < *argc; j++)
                (*argv)[j-k] = (*argv)[j];
              *argc -= k;
            }
        }
    }
  else
    {
      g_set_prgname ("<unknown>");
    }

  GDK_NOTE (MISC, g_message ("progname: \"%s\"", g_get_prgname ()));

  if (gdk_progclass == NULL)
    {
      gdk_progclass = g_strdup (g_get_prgname ());
      gdk_progclass[0] = toupper (gdk_progclass[0]);
    }

  gdk_visual_init ();
  if (!_gdk_backend->init (gdk_display_name, synchronize, gdk_progclass))
    {
      g_warning ("cannot open display: %s", gdk_get_display ());
      return FALSE;
    }
  _gdk_events_init ();
  _gdk_broadcast_init ();

  g_atexit (gdk_exit_func);

  gdk_initialized = 1;

  return TRUE;
}

void
gdk_init (int *argc, char ***argv)
{
  if (!gdk_init_check (argc, argv))
    {
      g_warning ("cannot open display: %s", gdk_get_display ());
      exit(1);
    }
}

void
gdk_exit (int errorcode)
{
  /* de-initialisation is done by the gdk_exit_funct(),
     no need to do this here (Alex J.) */
  exit (errorcode);
}

void
gdk_set_use_xshm (gboolean use_xshm)
{
  gdk_use_xshm = use_xshm;
}

gboolean
gdk_get_use_xshm (void)
{
  return gdk_use_xshm;
}

guint32
gdk_time_get (void)
{
  struct timeval end;
  struct timeval elapsed;
  guint32 milliseconds;

  gettimeofday (&end, NULL);

  if (start.tv_usec > end.tv_usec)
    {
      end.tv_usec += 1000000;
      end.tv_sec--;
    }
  elapsed.tv_sec = end.tv_sec - start.tv_sec;
  elapsed.tv_usec = end.tv_usec - start.tv_usec;

  milliseconds = (elapsed.tv_sec * 1000) + (elapsed.tv_usec / 1000);

  return milliseconds;
}

guint32
gdk_timer_get (void)
{
  return timer_val;
}

void
gdk_timer_set (guint32 milliseconds)
{
  timer_val = milliseconds;
}

void
gdk_timer_enable (void)
{
}

void
gdk_timer_disable (void)
{
}

static void
gdk_exit_func (void)
{
  static gboolean in_gdk_exit_func = FALSE;

  /* This is to avoid an infinite loop if a program segfaults in
     an atexit() handler (and yes, it does happen, especially if a program
     has trounced over memory too badly for even g_message to work) */
  if (in_gdk_exit_func == TRUE)
    return;
  in_gdk_exit_func = TRUE;

  if (gdk_initialized)
    {
      gdk_image_exit ();
      _gdk_broadcast_exit ();
      if (_gdk_backend)
        _gdk_backend->exit ();
      gdk_initialized = 0;
    }
}

gchar *
gdk_get_display (void)
{
  return gdk_display_name ? gdk_display_name : (gchar *) "";
}

void
gdk_threads_enter (void)
{
  GDK_THREADS_ENTER ();
}

void
gdk_threads_leave (void)
{
  GDK_THREADS_LEAVE ();
}
