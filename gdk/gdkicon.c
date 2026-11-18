/* GDK - The GIMP Drawing Kit
 * A program's icon, found by its name where icons are installed
 *
 * Copyright (C) 2026 The CGTK authors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <https://www.gnu.org/licenses/>.
 */

/* A program's icon is installed as its desktop file names it, "cgterm":
 * PNG files of several sizes, icons/hicolor/48x48/apps/cgterm.png and
 * so on, and perhaps one drawing, icons/hicolor/scalable/apps/cgterm.svg,
 * under the user's data directory or one of the system's. This finds
 * them, for a backend that has a window's icon to give in pixels.
 *
 * The PNG files are taken if there are any, each size from the first
 * directory that has it: they are what was drawn for those sizes.
 * With none the drawing is drawn in the sizes a window manager and a
 * panel ask for; with neither, a PNG or SVG file of the name in a
 * pixmaps directory is taken. A name that is a whole path is that
 * file.
 */

#include "config.h"

#include <string.h>

#include "gdk.h"
#include "gdkprivate.h"

/* The sizes looked for, and those a drawing is drawn in. An icon of
 * more than the last is not taken: a window's icons go to the window
 * system together, and one of 256 is as much as all of these.
 */
static const gint png_sizes[] = { 16, 22, 24, 32, 48, 64, 96, 128 };
static const gint svg_sizes[] = { 16, 24, 32, 48, 64, 128 };

#define MOST_SIDE 128

/* Every name asked for so far, found or not. */
static GSList *icons = NULL;

static void
icon_add (GdkThemedIcon *icon,
          guchar        *pixels,
          gint           width,
          gint           height)
{
  if (!pixels)
    return;
  if (width > MOST_SIDE || height > MOST_SIDE)
    {
      g_free (pixels);
      return;
    }
  icon->images = g_renew (GdkIconImage, icon->images, icon->n_images + 1);
  icon->images[icon->n_images].width = width;
  icon->images[icon->n_images].height = height;
  icon->images[icon->n_images].pixels = pixels;
  icon->n_images++;
}

static gboolean
icon_add_png (GdkThemedIcon *icon,
              const gchar   *filename)
{
  gint width, height, before = icon->n_images;
  guchar *pixels = gdk_png_load_straight (filename, &width, &height, NULL);

  icon_add (icon, pixels, width, height);

  return icon->n_images > before;
}

/* The drawing in each of the sizes, as large as fits and in the
 * middle.
 */
static gboolean
icon_add_svg (GdkThemedIcon *icon,
              const gchar   *filename)
{
  GdkSvg *svg = gdk_svg_load (filename, NULL);
  gdouble width, height, scale;
  guint i;

  if (!svg)
    return FALSE;
  gdk_svg_get_size (svg, &width, &height);
  for (i = 0; i < G_N_ELEMENTS (svg_sizes); i++)
    {
      gint size = svg_sizes[i];

      scale = MIN (size / width, size / height);
      icon_add (icon,
                gdk_svg_render_area_straight (svg, (width - size / scale) / 2,
                                              (height - size / scale) / 2, scale, size, size),
                size, size);
    }
  gdk_svg_free (svg);

  return TRUE;
}

static gboolean
icon_add_file (GdkThemedIcon *icon,
               const gchar   *filename)
{
  gsize length = strlen (filename);

  if (length > 4 && g_strcasecmp (filename + length - 4, ".svg") == 0)
    return icon_add_svg (icon, filename);

  return icon_add_png (icon, filename);
}

static void
icon_find (GdkThemedIcon *icon)
{
  const gchar * const *system = g_get_system_data_dirs ();
  const gchar **dirs;
  gchar *filename;
  gint n_dirs = 0, i;
  guint s;

  if (g_path_is_absolute (icon->name))
    {
      icon_add_file (icon, icon->name);
      return;
    }
  /* a name, and not a way to anywhere else */
  if (!icon->name[0] || strchr (icon->name, G_DIR_SEPARATOR))
    return;

  while (system[n_dirs])
    n_dirs++;
  dirs = g_new (const gchar*, n_dirs + 1);
  dirs[0] = g_get_user_data_dir ();
  for (i = 0; i < n_dirs; i++)
    dirs[i + 1] = system[i];
  n_dirs++;

  for (s = 0; s < G_N_ELEMENTS (png_sizes); s++)
    for (i = 0; i < n_dirs; i++)
      {
        gboolean found;

        filename = g_strdup_printf ("%s/icons/hicolor/%dx%d/apps/%s.png",
                                    dirs[i], png_sizes[s], png_sizes[s], icon->name);
        found = icon_add_png (icon, filename);
        g_free (filename);
        if (found)
          break;
      }

  for (i = 0; i < n_dirs && icon->n_images == 0; i++)
    {
      filename = g_strdup_printf ("%s/icons/hicolor/scalable/apps/%s.svg", dirs[i], icon->name);
      icon_add_svg (icon, filename);
      g_free (filename);
    }

  for (i = 0; i < n_dirs && icon->n_images == 0; i++)
    {
      filename = g_strdup_printf ("%s/pixmaps/%s.png", dirs[i], icon->name);
      if (!icon_add_png (icon, filename))
        {
          g_free (filename);
          filename = g_strdup_printf ("%s/pixmaps/%s.svg", dirs[i], icon->name);
          icon_add_svg (icon, filename);
        }
      g_free (filename);
    }

  g_free (dirs);
}

/* The icon of that name: its pictures, with straight alpha, as
 * gdk_png_load_straight() gives them and a window system takes them,
 * smallest first if they were found by size. It is looked for once
 * and kept, and is GDK's; one that is not to be found has no pictures.
 * A backend may keep a pixmap and mask it has made of it in it.
 */
GdkThemedIcon*
_gdk_themed_icon (const gchar *name)
{
  GdkThemedIcon *icon;
  GSList *list;

  g_return_val_if_fail (name != NULL, NULL);

  for (list = icons; list; list = list->next)
    {
      icon = list->data;
      if (strcmp (icon->name, name) == 0)
        return icon;
    }

  icon = g_new0 (GdkThemedIcon, 1);
  icon->name = g_strdup (name);
  icon_find (icon);
  icons = g_slist_prepend (icons, icon);

  return icon;
}
