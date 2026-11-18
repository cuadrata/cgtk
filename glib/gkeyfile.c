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
 * A file of groups, each with keys and their values, as .desktop
 * files are and many files of settings:
 *
 *   # a comment
 *   [Group]
 *   Key=value
 *   Key[de]=Wert
 *
 * The names are GLib 2's. What is read is kept as it was, comments
 * and the order of things too, so that a file read, changed and
 * written again has changed only where it was changed: the flags
 * that ask for comments and translations to be kept are taken, and
 * both are always kept.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>

#include "glib.h"

/* A line of a group: a key and its value, or with no key a comment
 * or an empty line, as it stood. */
typedef struct
{
  gchar *key;
  gchar *value;
} Entry;

/* The first group may have no name: it is what stands before any
 * group. Its entries are found by their keys in the index and added
 * at the last of them, so that a group of a thousand keys is read as
 * fast, a key, as one of ten. */
typedef struct
{
  gchar *name;
  GList *entries;
  GList *last;
  GHashTable *index;
} Group;

struct _GKeyFile
{
  GList *groups;
  gchar separator;
};

GQuark
g_key_file_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("g-key-file-error-quark");

  return quark;
}

GKeyFile*
g_key_file_new (void)
{
  GKeyFile *key_file = g_new0 (GKeyFile, 1);

  key_file->separator = ';';

  return key_file;
}

static void
entry_free (Entry *entry)
{
  g_free (entry->key);
  g_free (entry->value);
  g_free (entry);
}

static void
group_free (Group *group)
{
  GList *l;

  for (l = group->entries; l; l = l->next)
    entry_free (l->data);
  g_list_free (group->entries);
  g_hash_table_destroy (group->index);
  g_free (group->name);
  g_free (group);
}

static void
key_file_clear (GKeyFile *key_file)
{
  GList *l;

  for (l = key_file->groups; l; l = l->next)
    group_free (l->data);
  g_list_free (key_file->groups);
  key_file->groups = NULL;
}

void
g_key_file_free (GKeyFile *key_file)
{
  g_return_if_fail (key_file != NULL);

  key_file_clear (key_file);
  g_free (key_file);
}

/* What stands between the strings of a list: ';' unless this says
 * otherwise. */
void
g_key_file_set_list_separator (GKeyFile *key_file, gchar separator)
{
  g_return_if_fail (key_file != NULL);

  key_file->separator = separator;
}

static Group*
find_group (GKeyFile *key_file, const gchar *name)
{
  GList *l;

  for (l = key_file->groups; l; l = l->next)
    {
      Group *group = l->data;

      if (group->name ? name && strcmp (group->name, name) == 0 : !name)
        return group;
    }

  return NULL;
}

static Group*
add_group (GKeyFile *key_file, const gchar *name)
{
  Group *group = find_group (key_file, name);

  if (!group)
    {
      group = g_new0 (Group, 1);
      group->name = g_strdup (name);
      group->index = g_hash_table_new (g_str_hash, g_str_equal);
      key_file->groups = g_list_append (key_file->groups, group);
    }

  return group;
}

static Entry*
find_entry (Group *group, const gchar *key)
{
  return g_hash_table_lookup (group->index, key);
}

static void
append_entry (Group *group, Entry *entry)
{
  if (group->last)
    {
      g_list_append (group->last, entry);
      group->last = group->last->next;
    }
  else
    group->entries = group->last = g_list_append (NULL, entry);
}

/* A key is set, where it stood if it was there. A new one goes after
 * the last key of the group, before the empty lines that end it; or,
 * as a file is read, where it comes. */
static void
set_entry (Group *group, const gchar *key, const gchar *value, gboolean reading)
{
  Entry *entry = find_entry (group, key);
  GList *l;
  gint position = 0, i;

  if (entry)
    {
      g_free (entry->value);
      entry->value = g_strdup (value);
      return;
    }

  entry = g_new (Entry, 1);
  entry->key = g_strdup (key);
  entry->value = g_strdup (value);
  g_hash_table_insert (group->index, entry->key, entry);
  if (reading)
    {
      append_entry (group, entry);
      return;
    }
  for (l = group->entries, i = 1; l; l = l->next, i++)
    if (((Entry *) l->data)->key)
      position = i;
  group->entries = g_list_insert (group->entries, entry, position);
  group->last = g_list_last (group->entries);
}

static void
add_comment (Group *group, const gchar *text, gsize len)
{
  Entry *entry = g_new (Entry, 1);

  entry->key = NULL;
  entry->value = g_strndup (text, len);
  append_entry (group, entry);
}

/* A key may be anything that has no '=' in it, and may end in a
 * locale in square brackets. */
static gboolean
key_is_sound (const gchar *key)
{
  const gchar *open = strchr (key, '[');

  if (!*key || *key == '[' || strchr (key, '='))
    return FALSE;
  if (open && (key[strlen (key) - 1] != ']' || strchr (open + 1, '[') || !open[2]))
    return FALSE;

  return !open ? strchr (key, ']') == NULL : TRUE;
}

/*
 * What is in a key file is taken from length bytes of data, or all of
 * data to its nul if length is (gsize) -1, in place of what was there.
 * The text is to be UTF-8.
 */
gboolean
g_key_file_load_from_data (GKeyFile *key_file, const gchar *data, gsize length, GKeyFileFlags flags, GError **error)
{
  const gchar *line = data, *end, *next, *equals;
  Group *group = NULL;
  gint number = 0;

  g_return_val_if_fail (key_file != NULL, FALSE);
  g_return_val_if_fail (data != NULL || length == 0, FALSE);

  key_file_clear (key_file);
  if (length == (gsize) -1)
    length = strlen (data);
  if (length == 0)
    return TRUE;
  end = data + length;

  if (!g_utf8_validate (data, length, NULL))
    {
      g_set_error (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_UNKNOWN_ENCODING,
                   "Key file contains text that is not UTF-8");
      return FALSE;
    }

  for (; line < end; line = next)
    {
      const gchar *stop = memchr (line, '\n', end - line);
      gchar *key, *value;

      number++;
      next = stop ? stop + 1 : end;
      if (!stop)
        stop = end;
      if (stop > line && stop[-1] == '\r')
        stop--;
      while (line < stop && (*line == ' ' || *line == '\t'))
        line++;

      if (line == stop || *line == '#')
        {
          if (!group)
            group = add_group (key_file, NULL);
          add_comment (group, line, stop - line);
        }
      else if (*line == '[')
        {
          const gchar *close = stop;
          gchar *name;

          while (close > line && (close[-1] == ' ' || close[-1] == '\t'))
            close--;
          if (close - line < 3 || close[-1] != ']' || memchr (line + 1, '[', close - line - 2) ||
              memchr (line + 1, ']', close - line - 2))
            {
              g_set_error (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_PARSE,
                           "Line %d of the key file is not the name of a group", number);
              goto failed;
            }
          name = g_strndup (line + 1, close - line - 2);
          group = add_group (key_file, name);
          g_free (name);
        }
      else
        {
          equals = memchr (line, '=', stop - line);
          if (!equals)
            {
              g_set_error (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_PARSE,
                           "Line %d of the key file is not a key and its value, a group or a comment", number);
              goto failed;
            }
          if (!group || !group->name)
            {
              g_set_error (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND,
                           "Line %d of the key file has a key before any group", number);
              goto failed;
            }
          key = g_strchomp (g_strndup (line, equals - line));
          if (!key_is_sound (key))
            {
              g_set_error (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_PARSE,
                           "Line %d of the key file has a key that cannot be one: '%s'", number, key);
              g_free (key);
              goto failed;
            }
          for (equals++; equals < stop && (*equals == ' ' || *equals == '\t'); equals++)
            ;
          value = g_strndup (equals, stop - equals);
          set_entry (group, key, value, TRUE);
          g_free (key);
          g_free (value);
        }
    }

  return TRUE;

failed:
  key_file_clear (key_file);

  return FALSE;
}

gboolean
g_key_file_load_from_file (GKeyFile *key_file, const gchar *file, GKeyFileFlags flags, GError **error)
{
  gchar *data;
  gsize length;
  gboolean result;

  g_return_val_if_fail (key_file != NULL && file != NULL, FALSE);

  if (!g_file_get_contents (file, &data, &length, error))
    return FALSE;
  result = g_key_file_load_from_data (key_file, data, length, flags, error);
  g_free (data);

  return result;
}

/* The key file as text, to be freed. A group that was added to what
 * was read has an empty line before it. */
gchar*
g_key_file_to_data (GKeyFile *key_file, gsize *length, GError **error)
{
  GString *text;
  GList *g, *e;
  gchar *data;

  g_return_val_if_fail (key_file != NULL, NULL);

  text = g_string_new (NULL);
  for (g = key_file->groups; g; g = g->next)
    {
      Group *group = g->data;

      if (group->name)
        {
          if (text->len > 1 && text->str[text->len - 2] != '\n')
            g_string_append_c (text, '\n');
          g_string_sprintfa (text, "[%s]\n", group->name);
        }
      for (e = group->entries; e; e = e->next)
        {
          Entry *entry = e->data;

          if (entry->key)
            {
              g_string_append (text, entry->key);
              g_string_append_c (text, '=');
            }
          g_string_append (text, entry->value);
          g_string_append_c (text, '\n');
        }
    }

  if (length)
    *length = text->len;
  data = text->str;
  g_string_free (text, FALSE);

  return data;
}

/* The key file written to a file, which is whole or as it was. */
gboolean
g_key_file_save_to_file (GKeyFile *key_file, const gchar *filename, GError **error)
{
  gchar *data;
  gsize length;
  gboolean result;

  g_return_val_if_fail (key_file != NULL && filename != NULL, FALSE);

  data = g_key_file_to_data (key_file, &length, NULL);
  result = g_file_set_contents (filename, data, length, error);
  g_free (data);

  return result;
}

/* The name of the first group, to be freed, or NULL. */
gchar*
g_key_file_get_start_group (GKeyFile *key_file)
{
  GList *l;

  g_return_val_if_fail (key_file != NULL, NULL);

  for (l = key_file->groups; l; l = l->next)
    if (((Group *) l->data)->name)
      return g_strdup (((Group *) l->data)->name);

  return NULL;
}

/* The names of the groups, in their order, ending in NULL and to be
 * freed with g_strfreev(). */
gchar**
g_key_file_get_groups (GKeyFile *key_file, gsize *length)
{
  gchar **names;
  GList *l;
  gsize n = 0;

  g_return_val_if_fail (key_file != NULL, NULL);

  names = g_new (gchar *, g_list_length (key_file->groups) + 1);
  for (l = key_file->groups; l; l = l->next)
    if (((Group *) l->data)->name)
      names[n++] = g_strdup (((Group *) l->data)->name);
  names[n] = NULL;
  if (length)
    *length = n;

  return names;
}

static Group*
need_group (GKeyFile *key_file, const gchar *group_name, GError **error)
{
  Group *group = find_group (key_file, group_name);

  if (!group)
    g_set_error (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND,
                 "Key file does not have group '%s'", group_name);

  return group;
}

static Entry*
need_entry (GKeyFile *key_file, const gchar *group_name, const gchar *key, GError **error)
{
  Group *group = need_group (key_file, group_name, error);
  Entry *entry;

  if (!group)
    return NULL;
  entry = find_entry (group, key);
  if (!entry)
    g_set_error (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND,
                 "Key file does not have key '%s' in group '%s'", key, group_name);

  return entry;
}

/* The keys of a group, in their order, to be freed with
 * g_strfreev(). */
gchar**
g_key_file_get_keys (GKeyFile *key_file, const gchar *group_name, gsize *length, GError **error)
{
  Group *group;
  gchar **keys;
  GList *l;
  gsize n = 0;

  g_return_val_if_fail (key_file != NULL && group_name != NULL, NULL);

  group = need_group (key_file, group_name, error);
  if (!group)
    return NULL;
  keys = g_new (gchar *, g_list_length (group->entries) + 1);
  for (l = group->entries; l; l = l->next)
    if (((Entry *) l->data)->key)
      keys[n++] = g_strdup (((Entry *) l->data)->key);
  keys[n] = NULL;
  if (length)
    *length = n;

  return keys;
}

gboolean
g_key_file_has_group (GKeyFile *key_file, const gchar *group_name)
{
  g_return_val_if_fail (key_file != NULL && group_name != NULL, FALSE);

  return find_group (key_file, group_name) != NULL;
}

/* An error only if there is no such group. */
gboolean
g_key_file_has_key (GKeyFile *key_file, const gchar *group_name, const gchar *key, GError **error)
{
  Group *group;

  g_return_val_if_fail (key_file != NULL && group_name != NULL && key != NULL, FALSE);

  group = need_group (key_file, group_name, error);

  return group && find_entry (group, key) != NULL;
}

/* A value as it stands in the file, to be freed. */
gchar*
g_key_file_get_value (GKeyFile *key_file, const gchar *group_name, const gchar *key, GError **error)
{
  Entry *entry;

  g_return_val_if_fail (key_file != NULL && group_name != NULL && key != NULL, NULL);

  entry = need_entry (key_file, group_name, key, error);

  return entry ? g_strdup (entry->value) : NULL;
}

/* And a value put as it is to stand there. The group is made if it
 * is not there. */
void
g_key_file_set_value (GKeyFile *key_file, const gchar *group_name, const gchar *key, const gchar *value)
{
  g_return_if_fail (key_file != NULL && group_name != NULL && value != NULL);
  g_return_if_fail (key != NULL && key_is_sound (key));
  g_return_if_fail (*group_name && !strpbrk (group_name, "[]\n"));

  set_entry (add_group (key_file, group_name), key, value, FALSE);
}

/*
 * A value as the string it stands for: \s is a space, \n a newline,
 * \t a tab, \r a return and \\ a backslash. With a separator, the
 * string ends at the first one that has no backslash before it, and
 * *rest is what follows that; a backslash before a separator is not
 * of the string.
 */
static gchar*
value_to_string (const gchar *value, gchar separator, const gchar **rest, GError **error)
{
  GString *string = g_string_new (NULL);
  gchar *result;

  for (; *value && !(separator && *value == separator); value++)
    if (*value != '\\')
      g_string_append_c (string, *value);
    else
      {
        value++;
        if (*value == 's')
          g_string_append_c (string, ' ');
        else if (*value == 'n')
          g_string_append_c (string, '\n');
        else if (*value == 't')
          g_string_append_c (string, '\t');
        else if (*value == 'r')
          g_string_append_c (string, '\r');
        else if (*value == '\\' || (separator && *value == separator))
          g_string_append_c (string, *value);
        else
          {
            if (*value)
              g_set_error (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                           "Key file contains invalid escape sequence '\\%c'", *value);
            else
              g_set_error (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                           "Key file contains escape character at end of line");
            g_string_free (string, TRUE);
            return NULL;
          }
      }
  if (rest)
    *rest = *value ? value + 1 : value;
  result = string->str;
  g_string_free (string, FALSE);

  return result;
}

static void
string_to_value (GString *value, const gchar *string, gchar separator)
{
  const gchar *p;

  for (p = string; *p; p++)
    if (*p == ' ' && p == string)
      g_string_append (value, "\\s");
    else if (*p == '\n')
      g_string_append (value, "\\n");
    else if (*p == '\t')
      g_string_append (value, "\\t");
    else if (*p == '\r')
      g_string_append (value, "\\r");
    else if (*p == '\\' || (separator && *p == separator))
      {
        g_string_append_c (value, '\\');
        g_string_append_c (value, *p);
      }
    else
      g_string_append_c (value, *p);
}

gchar*
g_key_file_get_string (GKeyFile *key_file, const gchar *group_name, const gchar *key, GError **error)
{
  Entry *entry;

  g_return_val_if_fail (key_file != NULL && group_name != NULL && key != NULL, NULL);

  entry = need_entry (key_file, group_name, key, error);

  return entry ? value_to_string (entry->value, 0, NULL, error) : NULL;
}

void
g_key_file_set_string (GKeyFile *key_file, const gchar *group_name, const gchar *key, const gchar *string)
{
  GString *value;

  g_return_if_fail (key_file != NULL && string != NULL);

  value = g_string_new (NULL);
  string_to_value (value, string, 0);
  g_key_file_set_value (key_file, group_name, key, value->str);
  g_string_free (value, TRUE);
}

/*
 * The locales to look for a translation under, the nearest first,
 * for a locale, or for the user's if that is NULL: of ll_CC.set@mod
 * those are ll_CC@mod, ll_CC, ll@mod and ll. The user's are those of
 * LANGUAGE, which may be several, or else that of LC_ALL, LC_MESSAGES
 * or LANG.
 */
static gchar**
locale_variants (const gchar *locale)
{
  GPtrArray *variants = g_ptr_array_new ();
  gchar **wanted;
  guint i;

  if (!locale || !*locale)
    {
      locale = g_getenv ("LANGUAGE");
      if (!locale || !*locale)
        locale = g_getenv ("LC_ALL");
      if (!locale || !*locale)
        locale = g_getenv ("LC_MESSAGES");
      if (!locale || !*locale)
        locale = g_getenv ("LANG");
      if (!locale)
        locale = "";
    }
  wanted = g_strsplit (locale, ":", 0);
  for (i = 0; wanted[i]; i++)
    {
      gchar *language = g_strdup (wanted[i]), *territory, *codeset, *modifier;

      modifier = strchr (language, '@');
      if (modifier)
        *modifier++ = 0;
      codeset = strchr (language, '.');
      if (codeset)
        *codeset = 0;
      territory = strchr (language, '_');
      if (territory)
        *territory++ = 0;
      if (*language && strcmp (language, "C") != 0 && strcmp (language, "POSIX") != 0)
        {
          if (territory && modifier)
            g_ptr_array_add (variants, g_strconcat (language, "_", territory, "@", modifier, NULL));
          if (territory)
            g_ptr_array_add (variants, g_strconcat (language, "_", territory, NULL));
          if (modifier)
            g_ptr_array_add (variants, g_strconcat (language, "@", modifier, NULL));
          g_ptr_array_add (variants, g_strdup (language));
        }
      g_free (language);
    }
  g_strfreev (wanted);
  g_ptr_array_add (variants, NULL);
  wanted = (gchar **) variants->pdata;
  g_ptr_array_free (variants, FALSE);

  return wanted;
}

/* The entry of a key in the nearest translation there is, or of the
 * key itself. */
static Entry*
locale_entry (GKeyFile *key_file, const gchar *group_name, const gchar *key, const gchar *locale, GError **error)
{
  Group *group = need_group (key_file, group_name, error);
  gchar **variants;
  Entry *entry = NULL;
  guint i;

  if (!group)
    return NULL;
  variants = locale_variants (locale);
  for (i = 0; variants[i] && !entry; i++)
    {
      gchar *translated = g_strdup_printf ("%s[%s]", key, variants[i]);

      entry = find_entry (group, translated);
      g_free (translated);
    }
  g_strfreev (variants);

  return entry ? entry : need_entry (key_file, group_name, key, error);
}

/* A string in the language of a locale, or of the user if locale is
 * NULL, where the file has it; else as the key has it. */
gchar*
g_key_file_get_locale_string (GKeyFile *key_file, const gchar *group_name, const gchar *key,
                              const gchar *locale, GError **error)
{
  Entry *entry;

  g_return_val_if_fail (key_file != NULL && group_name != NULL && key != NULL, NULL);

  entry = locale_entry (key_file, group_name, key, locale, error);

  return entry ? value_to_string (entry->value, 0, NULL, error) : NULL;
}

void
g_key_file_set_locale_string (GKeyFile *key_file, const gchar *group_name, const gchar *key,
                              const gchar *locale, const gchar *string)
{
  gchar *translated;

  g_return_if_fail (key != NULL && locale != NULL);

  translated = g_strdup_printf ("%s[%s]", key, locale);
  g_key_file_set_string (key_file, group_name, translated, string);
  g_free (translated);
}

/* true or false, and 1 or 0. */
gboolean
g_key_file_get_boolean (GKeyFile *key_file, const gchar *group_name, const gchar *key, GError **error)
{
  Entry *entry;
  gchar *value;
  gboolean result = FALSE;

  g_return_val_if_fail (key_file != NULL && group_name != NULL && key != NULL, FALSE);

  entry = need_entry (key_file, group_name, key, error);
  if (!entry)
    return FALSE;
  value = g_strchomp (g_strdup (entry->value));
  if (strcmp (value, "true") == 0 || strcmp (value, "1") == 0)
    result = TRUE;
  else if (strcmp (value, "false") != 0 && strcmp (value, "0") != 0)
    g_set_error (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                 "Value '%s' cannot be interpreted as a boolean", entry->value);
  g_free (value);

  return result;
}

void
g_key_file_set_boolean (GKeyFile *key_file, const gchar *group_name, const gchar *key, gboolean value)
{
  g_key_file_set_value (key_file, group_name, key, value ? "true" : "false");
}

gint
g_key_file_get_integer (GKeyFile *key_file, const gchar *group_name, const gchar *key, GError **error)
{
  Entry *entry;
  gchar *end;
  glong number;

  g_return_val_if_fail (key_file != NULL && group_name != NULL && key != NULL, 0);

  entry = need_entry (key_file, group_name, key, error);
  if (!entry)
    return 0;
  errno = 0;
  number = strtol (entry->value, &end, 10);
  while (*end == ' ' || *end == '\t')
    end++;
  if (end == entry->value || *end || errno || number < INT_MIN || number > INT_MAX)
    {
      g_set_error (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                   "Value '%s' cannot be interpreted as a number", entry->value);
      return 0;
    }

  return number;
}

void
g_key_file_set_integer (GKeyFile *key_file, const gchar *group_name, const gchar *key, gint value)
{
  gchar number[32];

  g_snprintf (number, sizeof number, "%d", value);
  g_key_file_set_value (key_file, group_name, key, number);
}

gdouble
g_key_file_get_double (GKeyFile *key_file, const gchar *group_name, const gchar *key, GError **error)
{
  Entry *entry;
  gchar *end;
  gdouble number;

  g_return_val_if_fail (key_file != NULL && group_name != NULL && key != NULL, 0);

  entry = need_entry (key_file, group_name, key, error);
  if (!entry)
    return 0;
  number = g_ascii_strtod (entry->value, &end);
  while (*end == ' ' || *end == '\t')
    end++;
  if (end == entry->value || *end)
    {
      g_set_error (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                   "Value '%s' cannot be interpreted as a float number", entry->value);
      return 0;
    }

  return number;
}

void
g_key_file_set_double (GKeyFile *key_file, const gchar *group_name, const gchar *key, gdouble value)
{
  gchar number[40];

  g_key_file_set_value (key_file, group_name, key, g_ascii_dtostr (number, sizeof number, value));
}

static gchar**
value_to_list (GKeyFile *key_file, const gchar *value, gsize *length, GError **error)
{
  GPtrArray *strings = g_ptr_array_new ();
  gchar **list;
  guint i;

  while (*value)
    {
      gchar *string = value_to_string (value, key_file->separator, &value, error);

      if (!string)
        {
          for (i = 0; i < strings->len; i++)
            g_free (strings->pdata[i]);
          g_ptr_array_free (strings, TRUE);
          return NULL;
        }
      g_ptr_array_add (strings, string);
    }
  if (length)
    *length = strings->len;
  g_ptr_array_add (strings, NULL);
  list = (gchar **) strings->pdata;
  g_ptr_array_free (strings, FALSE);

  return list;
}

/* The strings of a value that is a list of them, to be freed with
 * g_strfreev(). */
gchar**
g_key_file_get_string_list (GKeyFile *key_file, const gchar *group_name, const gchar *key,
                            gsize *length, GError **error)
{
  Entry *entry;

  g_return_val_if_fail (key_file != NULL && group_name != NULL && key != NULL, NULL);

  if (length)
    *length = 0;
  entry = need_entry (key_file, group_name, key, error);

  return entry ? value_to_list (key_file, entry->value, length, error) : NULL;
}

/* A separator stands after each string, the last too. */
void
g_key_file_set_string_list (GKeyFile *key_file, const gchar *group_name, const gchar *key,
                            const gchar * const list[], gsize length)
{
  GString *value;
  gsize i;

  g_return_if_fail (key_file != NULL && (list != NULL || length == 0));

  value = g_string_new (NULL);
  for (i = 0; i < length && list[i]; i++)
    {
      string_to_value (value, list[i], key_file->separator);
      g_string_append_c (value, key_file->separator);
    }
  g_key_file_set_value (key_file, group_name, key, value->str);
  g_string_free (value, TRUE);
}

gchar**
g_key_file_get_locale_string_list (GKeyFile *key_file, const gchar *group_name, const gchar *key,
                                   const gchar *locale, gsize *length, GError **error)
{
  Entry *entry;

  g_return_val_if_fail (key_file != NULL && group_name != NULL && key != NULL, NULL);

  if (length)
    *length = 0;
  entry = locale_entry (key_file, group_name, key, locale, error);

  return entry ? value_to_list (key_file, entry->value, length, error) : NULL;
}

gboolean
g_key_file_remove_key (GKeyFile *key_file, const gchar *group_name, const gchar *key, GError **error)
{
  Group *group;
  Entry *entry;

  g_return_val_if_fail (key_file != NULL && group_name != NULL && key != NULL, FALSE);

  entry = need_entry (key_file, group_name, key, error);
  if (!entry)
    return FALSE;
  group = find_group (key_file, group_name);
  g_hash_table_remove (group->index, entry->key);
  group->entries = g_list_remove (group->entries, entry);
  group->last = g_list_last (group->entries);
  entry_free (entry);

  return TRUE;
}

gboolean
g_key_file_remove_group (GKeyFile *key_file, const gchar *group_name, GError **error)
{
  Group *group;

  g_return_val_if_fail (key_file != NULL && group_name != NULL, FALSE);

  group = need_group (key_file, group_name, error);
  if (!group)
    return FALSE;
  key_file->groups = g_list_remove (key_file->groups, group);
  group_free (group);

  return TRUE;
}
