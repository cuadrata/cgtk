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

#include "glib.h"
#include <unistd.h>
#include <sys/time.h>


typedef struct _GRealTimer GRealTimer;

struct _GRealTimer
{
  struct timeval start;
  struct timeval end;

  guint active : 1;
};

GTimer*
g_timer_new (void)
{
  GRealTimer *timer;

  timer = g_new (GRealTimer, 1);
  timer->active = TRUE;

  gettimeofday (&timer->start, NULL);

  return ((GTimer*) timer);
}

void
g_timer_destroy (GTimer *timer)
{
  g_return_if_fail (timer != NULL);

  g_free (timer);
}

void
g_timer_start (GTimer *timer)
{
  GRealTimer *rtimer;

  g_return_if_fail (timer != NULL);

  rtimer = (GRealTimer*) timer;
  rtimer->active = TRUE;

  gettimeofday (&rtimer->start, NULL);
}

void
g_timer_stop (GTimer *timer)
{
  GRealTimer *rtimer;

  g_return_if_fail (timer != NULL);

  rtimer = (GRealTimer*) timer;
  rtimer->active = FALSE;

  gettimeofday (&rtimer->end, NULL);
}

void
g_timer_reset (GTimer *timer)
{
  GRealTimer *rtimer;

  g_return_if_fail (timer != NULL);

  rtimer = (GRealTimer*) timer;

  gettimeofday (&rtimer->start, NULL);
}

gdouble
g_timer_elapsed (GTimer *timer,
                 gulong *microseconds)
{
  GRealTimer *rtimer;
  gdouble total;
  struct timeval elapsed;

  g_return_val_if_fail (timer != NULL, 0);

  rtimer = (GRealTimer*) timer;

  if (rtimer->active)
    gettimeofday (&rtimer->end, NULL);

  if (rtimer->start.tv_usec > rtimer->end.tv_usec)
    {
      rtimer->end.tv_usec += 1000000;
      rtimer->end.tv_sec--;
    }

  elapsed.tv_usec = rtimer->end.tv_usec - rtimer->start.tv_usec;
  elapsed.tv_sec = rtimer->end.tv_sec - rtimer->start.tv_sec;

  total = elapsed.tv_sec + ((gdouble) elapsed.tv_usec / 1e6);
  if (total < 0)
    {
      total = 0;

      if (microseconds)
        *microseconds = 0;
    }
  else
    if (microseconds)
      *microseconds = elapsed.tv_usec;


  return total;
}
