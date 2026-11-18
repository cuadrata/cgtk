/* GTK - The GIMP Toolkit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * Themes added by The Rasterman <raster@redhat.com>
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
#include <stdlib.h>
#include <string.h>
#include <gmodule.h>
#include "gtkthemes.h"
#include "gtkthemebuiltin.h"
#include "gtkmain.h"
#include "gtkrc.h"
#include "gtkselection.h"
#include "gtksignal.h"
#include "gtkwidget.h"
#include "config.h"
#include "gtkintl.h"

typedef struct _GtkThemeEnginePrivate GtkThemeEnginePrivate;

struct _GtkThemeEnginePrivate {
  GtkThemeEngine engine;

  GModule *library;
  void *name;

  void (*init) (GtkThemeEngine *);
  void (*exit) (void);

  guint refcount;
};

static GHashTable *engine_hash = NULL;

/* Engines built into the library, found by name before the module
 * path is searched. */
static const struct
{
  const gchar *name;
  void (*init) (GtkThemeEngine *);
} builtin_engines[] = {
  { "altus", gtk_theme_altus_init },
  { "clarus", gtk_theme_clarus_init },
  { "fabrica", gtk_theme_fabrica_init },
  { "fenestramm", gtk_theme_fenestramm_init },
  { "gelidus", gtk_theme_gelidus_init },
  { "metallum", gtk_theme_metallum_init },
  { "unicus", gtk_theme_unicus_init },
  { "priscus", gtk_theme_priscus_init }
};

static GtkThemeEnginePrivate *
gtk_theme_engine_get_builtin (const gchar *name)
{
  GtkThemeEnginePrivate *result;
  guint i;

  for (i = 0; i < sizeof (builtin_engines) / sizeof (builtin_engines[0]); i++)
    if (strcmp (builtin_engines[i].name, name) == 0)
      {
        result = g_new0 (GtkThemeEnginePrivate, 1);

        result->refcount = 1;
        result->name = g_strdup (name);
        result->init = builtin_engines[i].init;

        result->init ((GtkThemeEngine *)result);

        return result;
      }

  return NULL;
}

GtkThemeEngine*
gtk_theme_engine_get (const gchar *name)
{
  GtkThemeEnginePrivate *result;

  if (!engine_hash)
    engine_hash = g_hash_table_new (g_str_hash, g_str_equal);

  /* get the library name for the theme */

  result = g_hash_table_lookup (engine_hash, name);

  if (result)
    result->refcount++;
  else if ((result = gtk_theme_engine_get_builtin (name)))
    g_hash_table_insert (engine_hash, result->name, result);
  else
    {
       gchar fullname[1024];
       gchar *engine_path;
       GModule *library;

       g_snprintf (fullname, 1024, "lib%s.so", name);
       engine_path = gtk_rc_find_module_in_path (fullname);

       if (!engine_path)
         {
           g_warning (_("Unable to locate loadable module in module_path: \"%s\","),
                      fullname);

           return NULL;
         }

       /* load the lib */

       GTK_NOTE (MISC, g_message ("Loading Theme %s\n", engine_path));

       library = g_module_open (engine_path, 0);
       g_free(engine_path);
       if (!library)
         {
           g_warning ("%s", g_module_error ());
           return NULL;
         }
       else
         {
            result = g_new0 (GtkThemeEnginePrivate, 1);

            result->refcount = 1;
            result->name = g_strdup (name);
            result->library = library;

            /* extract symbols from the lib */
            if (!g_module_symbol (library, "theme_init",
                                  (gpointer *)&result->init) ||
                !g_module_symbol (library, "theme_exit",
                                  (gpointer *)&result->exit))
              {
                g_warning ("%s", g_module_error ());
                g_free (result);
                return NULL;
              }

            /* call the theme's init (theme_init) function to let it */
            /* setup anything it needs to set up. */
            result->init((GtkThemeEngine *)result);

            g_hash_table_insert (engine_hash, result->name, result);
         }
    }

  return (GtkThemeEngine *)result;
}

void
gtk_theme_engine_ref (GtkThemeEngine *engine)
{
  g_return_if_fail (engine != NULL);

  ((GtkThemeEnginePrivate *)engine)->refcount++;
}

void
gtk_theme_engine_unref (GtkThemeEngine *engine)
{
  GtkThemeEnginePrivate *private;
  private = (GtkThemeEnginePrivate *)engine;

  g_return_if_fail (engine != NULL);
  g_return_if_fail (private->refcount > 0);

  private->refcount--;

  if (private->refcount == 0)
    {
      if (private->exit)
        private->exit ();

      g_hash_table_remove (engine_hash, private->name);

      if (private->library)
        g_module_close (private->library);
      g_free (private->name);
      g_free (private);
    }
}
