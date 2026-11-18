/* testgmodule.c - test program for GMODULE
 * Copyright (C) 1998 Tim Janik
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

#undef  G_LOG_DOMAIN
#include        <gmodule.h>
#include        <string.h>

/* %p requires a void *, and ISO C cannot convert a function pointer to
 * one, so print a copy of its representation instead.
 */
static gpointer
func_ptr_bits (const void *fp, size_t size)
{
  gpointer p = NULL;
  if (size == sizeof p)
    memcpy (&p, fp, size);
  return p;
}
#define FUNC_PTR(f)     func_ptr_bits (&(f), sizeof (f))



G_MODULE_EXPORT void
g_clash_func (void)
{
  g_print ("GModule: Hello global clash\n");
}

typedef void (*SimpleFunc) (void);
typedef void (*GModuleFunc) (GModule *);

static SimpleFunc plugin_clash_func = NULL;

int
main (int   arg,
      char *argv[])
{
  GModule *module_self, *module_a, *module_b;
  gchar *string;
  gchar *plugin_a, *plugin_b;
  SimpleFunc f_a, f_b, f_self;
  GModuleFunc gmod_f;

  string = g_get_current_dir ();
  g_print ("testgmodule (%s):\n", string);

  plugin_a = g_strconcat (string, "/.libs/", "libgplugin_a.so", NULL);
  plugin_b = g_strconcat (string, "/.libs/", "libgplugin_b.so", NULL);
  g_free (string);

  /* module handles
   */
  g_print ("get main module handle\n");
  module_self = g_module_open (NULL, G_MODULE_BIND_LAZY);
  if (!module_self)
    {
      g_print ("error: %s\n", g_module_error ());
      return 1;
    }
  g_print ("check that not yet bound symbols in shared libraries of main module are retrievable:\n");
  string = "g_module_close";
  g_print ("retrive symbol `%s' from \"%s\":\n", string, g_basename (g_module_name (module_self)));
  if (!g_module_symbol (module_self, string, (gpointer) &f_self))
    {
      g_print ("error: %s\n", g_module_error ());
      return 1;
    }
  g_print ("retrived symbol `%s' as %p\n", string, FUNC_PTR (f_self));
  g_print ("load plugin from \"%s\"\n", plugin_a);
  module_a = g_module_open (plugin_a, G_MODULE_BIND_LAZY);
  if (!module_a)
    {
      g_print ("error: %s\n", g_module_error ());
      return 1;
    }
  g_print ("load plugin from \"%s\"\n", plugin_b);
  module_b = g_module_open (plugin_b, G_MODULE_BIND_LAZY);
  if (!module_b)
    {
      g_print ("error: %s\n", g_module_error ());
      return 1;
    }

  /* get plugin specific symbols and call them
   */
  string = "gplugin_a_func";
  g_print ("retrive symbol `%s' from \"%s\"\n", string, g_basename (g_module_name (module_a)));
  if (!g_module_symbol (module_a, string, (gpointer) &f_a))
    {
      g_print ("error: %s\n", g_module_error ());
      return 1;
    }
  string = "gplugin_b_func";
  g_print ("retrive symbol `%s' from \"%s\"\n", string, g_basename (g_module_name (module_b)));
  if (!g_module_symbol (module_b, string, (gpointer) &f_b))
    {
      g_print ("error: %s\n", g_module_error ());
      return 1;
    }
  g_print ("call plugin function(%p) A: ", FUNC_PTR (f_a));
  f_a ();
  g_print ("call plugin function(%p) B: ", FUNC_PTR (f_b));
  f_b ();

  /* get and call globally clashing functions
   */
  string = "g_clash_func";
  g_print ("retrive symbol `%s' from \"%s\"\n", string, g_basename (g_module_name (module_self)));
  if (!g_module_symbol (module_self, string, (gpointer) &f_self))
    {
      g_print ("error: %s\n", g_module_error ());
      return 1;
    }
  g_print ("retrive symbol `%s' from \"%s\"\n", string, g_basename (g_module_name (module_a)));
  if (!g_module_symbol (module_a, string, (gpointer) &f_a))
    {
      g_print ("error: %s\n", g_module_error ());
      return 1;
    }
  g_print ("retrive symbol `%s' from \"%s\"\n", string, g_basename (g_module_name (module_b)));
  if (!g_module_symbol (module_b, string, (gpointer) &f_b))
    {
      g_print ("error: %s\n", g_module_error ());
      return 1;
    }
  g_print ("call plugin function(%p) self: ", FUNC_PTR (f_self));
  f_self ();
  g_print ("call plugin function(%p) A: ", FUNC_PTR (f_a));
  f_a ();
  g_print ("call plugin function(%p) B: ", FUNC_PTR (f_b));
  f_b ();

  /* get and call clashing plugin functions
   */
  string = "gplugin_clash_func";
  g_print ("retrive symbol `%s' from \"%s\"\n", string, g_basename (g_module_name (module_self)));
  if (!g_module_symbol (module_self, string, (gpointer) &f_self))
    f_self = NULL;
  g_print ("retrived function `%s' from self: %p\n", string, FUNC_PTR (f_self));
  g_print ("retrive symbol `%s' from \"%s\"\n", string, g_basename (g_module_name (module_a)));
  if (!g_module_symbol (module_a, string, (gpointer) &f_a))
    {
      g_print ("error: %s\n", g_module_error ());
      return 1;
    }
  g_print ("retrive symbol `%s' from \"%s\"\n", string, g_basename (g_module_name (module_b)));
  if (!g_module_symbol (module_b, string, (gpointer) &f_b))
    {
      g_print ("error: %s\n", g_module_error ());
      return 1;
    }
  g_print ("call plugin function(%p) A: ", FUNC_PTR (f_a));
  plugin_clash_func = f_a;
  plugin_clash_func ();
  g_print ("call plugin function(%p) B: ", FUNC_PTR (f_b));
  plugin_clash_func = f_b;
  plugin_clash_func ();

  /* call gmodule function form A
   */
  string = "gplugin_a_module_func";
  g_print ("retrive symbol `%s' from \"%s\"\n", string, g_basename (g_module_name (module_a)));
  if (!g_module_symbol (module_a, string, (gpointer) &gmod_f))
    {
      g_print ("error: %s\n", g_module_error ());
      return 1;
    }
  g_print ("call plugin A's module function(%p):\n{\n", FUNC_PTR (gmod_f));
  gmod_f (module_b);
  g_print ("}\n");


  /* unload plugins
   */
  g_print ("unload plugin A:\n");
  if (!g_module_close (module_a))
    g_print ("error: %s\n", g_module_error ());
  g_print ("unload plugin B:\n");
  if (!g_module_close (module_b))
    g_print ("error: %s\n", g_module_error ());

  g_free (plugin_a);
  g_free (plugin_b);

  return 0;
}
