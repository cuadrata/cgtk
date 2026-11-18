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
 * Visuals. There is one: 24 bit true colour, 8 bits each of red, green
 * and blue, which is what every framebuffer holds. Whatever the screen
 * is, the backend converts on the way out, so a program sees the same
 * visual everywhere.
 */

#include "gdk.h"
#include "gdkprivate.h"

static GdkVisualPrivate system_visual;
static GList *visuals = NULL;
static gint available_depths[1] = { 24 };
static GdkVisualType available_types[1] = { GDK_VISUAL_TRUE_COLOR };

void
gdk_visual_init (void)
{
  GdkVisual *visual = &system_visual.visual;

  visual->type = GDK_VISUAL_TRUE_COLOR;
  visual->depth = 24;
  visual->byte_order = (G_BYTE_ORDER == G_LITTLE_ENDIAN) ? GDK_LSB_FIRST : GDK_MSB_FIRST;
  visual->colormap_size = 256;
  visual->bits_per_rgb = 8;
  visual->red_mask = 0xff0000;
  visual->red_shift = 16;
  visual->red_prec = 8;
  visual->green_mask = 0x00ff00;
  visual->green_shift = 8;
  visual->green_prec = 8;
  visual->blue_mask = 0x0000ff;
  visual->blue_shift = 0;
  visual->blue_prec = 8;

  visuals = g_list_append (NULL, visual);
}

GdkVisual*
gdk_visual_ref (GdkVisual *visual)
{
  return visual;
}

void
gdk_visual_unref (GdkVisual *visual)
{
}

gint
gdk_visual_get_best_depth (void)
{
  return system_visual.visual.depth;
}

GdkVisualType
gdk_visual_get_best_type (void)
{
  return system_visual.visual.type;
}

GdkVisual*
gdk_visual_get_system (void)
{
  return &system_visual.visual;
}

GdkVisual*
gdk_visual_get_best (void)
{
  return &system_visual.visual;
}

GdkVisual*
gdk_visual_get_best_with_depth (gint depth)
{
  if (depth == system_visual.visual.depth)
    return &system_visual.visual;
  return NULL;
}

GdkVisual*
gdk_visual_get_best_with_type (GdkVisualType visual_type)
{
  if (visual_type == system_visual.visual.type)
    return &system_visual.visual;
  return NULL;
}

GdkVisual*
gdk_visual_get_best_with_both (gint          depth,
                               GdkVisualType visual_type)
{
  if (depth == system_visual.visual.depth &&
      visual_type == system_visual.visual.type)
    return &system_visual.visual;
  return NULL;
}

void
gdk_query_depths  (gint **depths,
                   gint  *count)
{
  *count = 1;
  *depths = available_depths;
}

void
gdk_query_visual_types (GdkVisualType **visual_types,
                        gint           *count)
{
  *count = 1;
  *visual_types = available_types;
}

GList*
gdk_list_visuals (void)
{
  return g_list_copy (visuals);
}
