/* GDK - The GIMP Drawing Kit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
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

#include <stdio.h>
#include "gdktypes.h"
#include "gdkprivate.h"
#include "config.h"

guint             gdk_debug_flags = 0;
gint              gdk_use_xshm = TRUE;
gchar            *gdk_display_name = NULL;
gint              gdk_screen;
GdkWindowPrivate  gdk_root_parent = { 0 };
GdkAtom           gdk_selection_property;
gchar            *gdk_progclass = NULL;
gint              gdk_error_code = 0;
gint              gdk_error_warnings = TRUE;
gint              gdk_null_window_warnings = TRUE;
GList            *gdk_default_filters = NULL;

GdkWindowPrivate *gdk_xgrab_window = NULL;  /* Window that currently holds the
                                             *  x pointer grab
                                             */

GMutex *gdk_threads_mutex = NULL;          /* Global GDK lock */
