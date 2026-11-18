/* cgtk-theme - choose and edit CGTK themes
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

/* Themes: finding them, and reading and changing their rc files. */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "theme.h"

/* ---------------------------------------------------------------------
 * The themes there are
 */

static const gchar *set_names[N_SETS] = { "fg", "bg", "base", "text" };
static const gchar *state_names[N_STATES] =
  { "NORMAL", "ACTIVE", "PRELIGHT", "SELECTED", "INSENSITIVE" };

gchar *
theme_user_dir (void)
{
  return gtk_rc_get_user_theme_dir ();
}

/* A theme's rc file: gtk-1.4/gtkrc, or the gtk/gtkrc of a theme written
 * for GTK+ 1.2. The GTK 2 and later themes in the same directories have
 * neither. */
static gchar *
theme_rc_file (const gchar *dir)
{
  static const gchar *subdirs[] = { "gtk-1.4", "gtk" };
  struct stat info;
  guint i;

  for (i = 0; i < sizeof (subdirs) / sizeof (subdirs[0]); i++)
    {
      gchar *rc = g_strconcat (dir, "/", subdirs[i], "/gtkrc", NULL);

      if (stat (rc, &info) == 0 && S_ISREG (info.st_mode))
        return rc;
      g_free (rc);
    }
  return NULL;
}

static gint
theme_info_compare (gconstpointer a, gconstpointer b)
{
  return g_strcasecmp (((const ThemeInfo *) a)->name, ((const ThemeInfo *) b)->name);
}

static GList *
theme_list_dir (GList *list, const gchar *dirname, gboolean user)
{
  struct dirent *entry;
  DIR *dir = opendir (dirname);

  if (!dir)
    return list;
  while ((entry = readdir (dir)))
    {
      ThemeInfo *info;
      gchar *path, *rc;

      if (entry->d_name[0] == '.' || theme_list_find (list, entry->d_name))
        continue;
      path = g_strconcat (dirname, "/", entry->d_name, NULL);
      rc = theme_rc_file (path);
      if (!rc)
        {
          g_free (path);
          continue;
        }
      info = g_new0 (ThemeInfo, 1);
      info->name = g_strdup (entry->d_name);
      info->dir = path;
      info->rc_path = rc;
      info->user = user;
      list = g_list_insert_sorted (list, info, theme_info_compare);
    }
  closedir (dir);
  return list;
}

/* Whether a theme's name makes it the dark version of another */
gboolean
theme_name_is_dark (const gchar *name)
{
  gsize len = strlen (name), suffix = strlen (THEME_DARK_SUFFIX);

  return len > suffix && strcmp (name + len - suffix, THEME_DARK_SUFFIX) == 0;
}

/* Every theme: the user's, then the system's, a user's theme hiding a
 * system one of the same name. Foo and Foo-Dark, wherever each is, are
 * paired. */
GList *
theme_list (void)
{
  gchar *user_dir = theme_user_dir ();
  gchar *system_dir = gtk_rc_get_theme_dir ();
  GList *list = NULL, *l;

  list = theme_list_dir (list, user_dir, TRUE);
  list = theme_list_dir (list, system_dir, FALSE);
  g_free (user_dir);
  g_free (system_dir);

  for (l = list; l; l = l->next)
    {
      ThemeInfo *dark = l->data, *light;
      gchar *name;

      if (!theme_name_is_dark (dark->name))
        continue;
      name = g_strndup (dark->name, strlen (dark->name) - strlen (THEME_DARK_SUFFIX));
      light = theme_list_find (list, name);
      g_free (name);
      if (light && !theme_name_is_dark (light->name))
        {
          light->dark = dark;
          dark->light = light;
        }
    }
  return list;
}

void
theme_list_free (GList *list)
{
  GList *l;

  for (l = list; l; l = l->next)
    {
      ThemeInfo *info = l->data;

      g_free (info->name);
      g_free (info->dir);
      g_free (info->rc_path);
      g_free (info);
    }
  g_list_free (list);
}

ThemeInfo *
theme_list_find (GList *list, const gchar *name)
{
  for (; list; list = list->next)
    if (strcmp (((ThemeInfo *) list->data)->name, name) == 0)
      return list->data;
  return NULL;
}

ThemeInfo *
theme_list_find_rc (GList *list, const gchar *rc_path)
{
  for (; list; list = list->next)
    if (strcmp (((ThemeInfo *) list->data)->rc_path, rc_path) == 0)
      return list->data;
  return NULL;
}

/* ---------------------------------------------------------------------
 * Reading an rc file
 *
 * Only as much of gtkrc's grammar as the editor needs: styles, the
 * colours in them, their engine blocks and the options in those, and
 * which style "class GtkWidget" names. Everything else is passed over,
 * and stays in the text as it was.
 */

typedef struct { gint start, end; } Span;

typedef struct
{
  gchar *key;
  Span value;
} Option;

typedef struct
{
  gchar *name;
  gboolean has[N_SETS][N_STATES];
  Span span[N_SETS][N_STATES];
  GdkColor color[N_SETS][N_STATES];
  gint close;                   /* where its closing brace is */
  gchar *engine;
  gint engine_close;            /* where the engine block's closing brace is */
  GList *options;
} Style;

struct _Theme
{
  GString *text;
  GList *styles;
  Style *main;
};

enum { TOK_END, TOK_STRING, TOK_NUMBER, TOK_IDENT, TOK_PUNCT };

typedef struct
{
  const gchar *text;
  gint pos;
  gint type;
  Span span;
} Lexer;

static gint
lex (Lexer *lx)
{
  const gchar *t = lx->text;
  gint p = lx->pos;

  for (;;)
    {
      while (t[p] && isspace ((guchar) t[p]))
        p++;
      if (t[p] != '#')
        break;
      while (t[p] && t[p] != '\n')
        p++;
    }

  lx->span.start = p;
  if (!t[p])
    lx->type = TOK_END;
  else if (t[p] == '"')
    {
      p++;
      while (t[p] && t[p] != '"')
        p += (t[p] == '\\' && t[p + 1]) ? 2 : 1;
      if (t[p])
        p++;
      lx->type = TOK_STRING;
    }
  else if (isdigit ((guchar) t[p]) || t[p] == '-' || t[p] == '.')
    {
      p++;
      while (isalnum ((guchar) t[p]) || t[p] == '.')
        p++;
      lx->type = TOK_NUMBER;
    }
  else if (isalpha ((guchar) t[p]) || t[p] == '_')
    {
      while (isalnum ((guchar) t[p]) || t[p] == '_' || t[p] == '-')
        p++;
      lx->type = TOK_IDENT;
    }
  else
    {
      p++;
      lx->type = TOK_PUNCT;
    }
  lx->span.end = p;
  lx->pos = p;
  return lx->type;
}

static gboolean
lex_is (Lexer *lx, const gchar *word)
{
  gint n = lx->span.end - lx->span.start;

  return (gint) strlen (word) == n && strncmp (lx->text + lx->span.start, word, n) == 0;
}

static gchar *
lex_string (Lexer *lx)
{
  if (lx->type == TOK_STRING)
    return g_strndup (lx->text + lx->span.start + 1, lx->span.end - lx->span.start - 2);
  return g_strndup (lx->text + lx->span.start, lx->span.end - lx->span.start);
}

/* Past a block whose opening brace has been read. */
static void
lex_skip_block (Lexer *lx)
{
  gint depth = 1;

  while (depth > 0 && lex (lx) != TOK_END)
    if (lx->type == TOK_PUNCT)
      {
        if (lex_is (lx, "{"))
          depth++;
        else if (lex_is (lx, "}"))
          depth--;
      }
}

/* A colour: "#rrggbb" and the like, or { r, g, b } as fractions of one
 * or as numbers up to 65535. */
static gboolean
parse_color (Lexer *lx, GdkColor *color, Span *span)
{
  gushort *c[3];
  gint i;

  span->start = lx->span.start;
  if (lx->type == TOK_STRING)
    {
      gchar *spec = lex_string (lx);
      gboolean ok = gdk_color_parse (spec, color);

      g_free (spec);
      span->end = lx->span.end;
      return ok;
    }
  if (lx->type != TOK_PUNCT || !lex_is (lx, "{"))
    return FALSE;

  c[0] = &color->red;
  c[1] = &color->green;
  c[2] = &color->blue;
  for (i = 0; i < 3; i++)
    {
      gchar *number;
      gdouble value;

      if (lex (lx) != TOK_NUMBER)
        return FALSE;
      number = lex_string (lx);
      value = strtod (number, NULL);
      *c[i] = strchr (number, '.') ? CLAMP (value, 0.0, 1.0) * 65535.0
                                   : CLAMP (value, 0.0, 65535.0);
      g_free (number);
      lex (lx);
      if (i < 2 && !lex_is (lx, ","))
        return FALSE;
    }
  if (!lex_is (lx, "}"))
    return FALSE;
  span->end = lx->span.end;
  return TRUE;
}

static void
parse_engine (Lexer *lx, Style *style)
{
  while (lex (lx) != TOK_END)
    {
      if (lx->type == TOK_PUNCT && lex_is (lx, "}"))
        {
          style->engine_close = lx->span.start;
          return;
        }
      if (lx->type == TOK_PUNCT && lex_is (lx, "{"))
        {
          lex_skip_block (lx);
          continue;
        }
      if (lx->type == TOK_IDENT)
        {
          gchar *key = lex_string (lx);
          Lexer save = *lx;

          if (lex (lx) == TOK_PUNCT && lex_is (lx, "=") && lex (lx) != TOK_END &&
              lx->type != TOK_PUNCT)
            {
              Option *option = g_new (Option, 1);

              option->key = key;
              option->value = lx->span;
              style->options = g_list_append (style->options, option);
              continue;
            }
          g_free (key);
          *lx = save;
        }
    }
}

static void
parse_style_body (Lexer *lx, Style *style)
{
  while (lex (lx) != TOK_END)
    {
      gint set;

      if (lx->type == TOK_PUNCT && lex_is (lx, "}"))
        {
          style->close = lx->span.start;
          return;
        }
      if (lx->type == TOK_PUNCT && lex_is (lx, "{"))
        {
          lex_skip_block (lx);
          continue;
        }
      if (lx->type != TOK_IDENT)
        continue;

      if (lex_is (lx, "engine"))
        {
          if (lex (lx) == TOK_STRING)
            {
              g_free (style->engine);
              style->engine = lex_string (lx);
              if (lex (lx) == TOK_PUNCT && lex_is (lx, "{"))
                parse_engine (lx, style);
            }
          continue;
        }

      for (set = 0; set < N_SETS; set++)
        if (lex_is (lx, set_names[set]))
          break;
      if (set < N_SETS && lex (lx) == TOK_PUNCT && lex_is (lx, "[") &&
          lex (lx) == TOK_IDENT)
        {
          gint state;

          for (state = 0; state < N_STATES; state++)
            if (lex_is (lx, state_names[state]))
              break;
          if (state < N_STATES && lex (lx) == TOK_PUNCT && lex_is (lx, "]") &&
              lex (lx) == TOK_PUNCT && lex_is (lx, "=") && lex (lx) != TOK_END &&
              parse_color (lx, &style->color[set][state], &style->span[set][state]))
            style->has[set][state] = TRUE;
        }
    }
  style->close = lx->pos;
}

static void
style_free (Style *style)
{
  GList *l;

  for (l = style->options; l; l = l->next)
    {
      g_free (((Option *) l->data)->key);
      g_free (l->data);
    }
  g_list_free (style->options);
  g_free (style->engine);
  g_free (style->name);
  g_free (style);
}

static void
theme_parse (Theme *theme)
{
  Lexer lx;
  gchar *main_name = NULL;
  GList *l;

  for (l = theme->styles; l; l = l->next)
    style_free (l->data);
  g_list_free (theme->styles);
  theme->styles = NULL;
  theme->main = NULL;

  lx.text = theme->text->str;
  lx.pos = 0;
  while (lex (&lx) != TOK_END)
    {
      if (lx.type != TOK_IDENT)
        continue;
      if (lex_is (&lx, "style"))
        {
          Style *style;

          if (lex (&lx) != TOK_STRING)
            continue;
          style = g_new0 (Style, 1);
          style->name = lex_string (&lx);
          style->engine_close = -1;
          lex (&lx);
          if (lex_is (&lx, "="))                /* style "a" = "b" */
            {
              lex (&lx);
              lex (&lx);
            }
          if (lx.type == TOK_PUNCT && lex_is (&lx, "{"))
            {
              parse_style_body (&lx, style);
              theme->styles = g_list_append (theme->styles, style);
            }
          else
            style_free (style);
        }
      else if (lex_is (&lx, "class"))
        {
          gchar *pattern;

          if (lex (&lx) != TOK_STRING)
            continue;
          pattern = lex_string (&lx);
          if (lex (&lx) == TOK_IDENT && lex_is (&lx, "style") && lex (&lx) == TOK_STRING &&
              strcmp (pattern, "GtkWidget") == 0 && !main_name)
            main_name = lex_string (&lx);
          g_free (pattern);
        }
    }

  for (l = theme->styles; l; l = l->next)
    if (main_name && strcmp (((Style *) l->data)->name, main_name) == 0)
      theme->main = l->data;
  if (!theme->main && theme->styles)
    theme->main = theme->styles->data;
  g_free (main_name);
}

Theme *
theme_load (const gchar *rc_path)
{
  Theme *theme;
  GString *text;
  FILE *file = fopen (rc_path, "r");
  gchar buf[4096];
  size_t n;

  if (!file)
    return NULL;
  text = g_string_new (NULL);
  while ((n = fread (buf, 1, sizeof (buf) - 1, file)) > 0)
    {
      buf[n] = '\0';
      g_string_append (text, buf);
    }
  fclose (file);
  theme = theme_new (text->str);
  g_string_free (text, TRUE);
  return theme;
}

Theme *
theme_new (const gchar *text)
{
  Theme *theme = g_new0 (Theme, 1);

  theme->text = g_string_new (text);

  /* With no style at all, a main one for the editor to fill in. */
  theme_parse (theme);
  if (!theme->main)
    {
      g_string_append (theme->text,
                       "\nstyle \"cgtk-theme\"\n{\n}\nclass \"GtkWidget\" style \"cgtk-theme\"\n");
      theme_parse (theme);
    }
  return theme;
}

void
theme_free (Theme *theme)
{
  GList *l;

  if (!theme)
    return;
  for (l = theme->styles; l; l = l->next)
    style_free (l->data);
  g_list_free (theme->styles);
  g_string_free (theme->text, TRUE);
  g_free (theme);
}

const gchar *
theme_text (Theme *theme)
{
  return theme->text->str;
}

const gchar *
theme_engine (Theme *theme)
{
  return theme->main->engine;
}

void
theme_prepend (Theme *theme, const gchar *text)
{
  g_string_prepend (theme->text, text);
  theme_parse (theme);
}

/* ---------------------------------------------------------------------
 * Changing it
 */

typedef struct
{
  Span span;
  gchar *text;
} Edit;

static gint
edit_compare (gconstpointer a, gconstpointer b)
{
  return ((const Edit *) b)->span.start - ((const Edit *) a)->span.start;
}

/* Replace spans of the text, last first so the earlier ones stay put. */
static void
theme_apply_edits (Theme *theme, GList *edits)
{
  GList *l;

  edits = g_list_sort (edits, edit_compare);
  for (l = edits; l; l = l->next)
    {
      Edit *edit = l->data;

      g_string_erase (theme->text, edit->span.start, edit->span.end - edit->span.start);
      g_string_insert (theme->text, edit->span.start, edit->text);
      g_free (edit->text);
      g_free (edit);
    }
  g_list_free (edits);
  theme_parse (theme);
}

static GList *
edit_add (GList *edits, gint start, gint end, gchar *text)
{
  Edit *edit = g_new (Edit, 1);

  edit->span.start = start;
  edit->span.end = end;
  edit->text = text;
  return g_list_prepend (edits, edit);
}

/* A line put in before the closing brace at pos: above it when the
 * brace starts its line, else on a line of its own before it. */
static GList *
edit_add_line (GList *edits, Theme *theme, gint pos, gchar *line)
{
  gint start = pos;

  while (start > 0 && (theme->text->str[start - 1] == ' ' || theme->text->str[start - 1] == '\t'))
    start--;
  if (start == 0 || theme->text->str[start - 1] == '\n')
    return edit_add (edits, start, start, line);

  edits = edit_add (edits, pos, pos, g_strconcat ("\n", line, NULL));
  g_free (line);
  return edits;
}

static gboolean
color_equal (const GdkColor *a, const GdkColor *b)
{
  return (a->red >> 8) == (b->red >> 8) && (a->green >> 8) == (b->green >> 8) &&
         (a->blue >> 8) == (b->blue >> 8);
}

static gchar *
color_spec (const GdkColor *color)
{
  return g_strdup_printf ("\"#%02x%02x%02x\"",
                          color->red >> 8, color->green >> 8, color->blue >> 8);
}

gboolean
theme_get_color (Theme *theme, gint set, gint state, GdkColor *color)
{
  if (!theme->main->has[set][state])
    return FALSE;
  *color = theme->main->color[set][state];
  return TRUE;
}

/* A colour of the main style, and of every other style that had the
 * same colour there: a menu that kept the window's colour keeps it, and
 * one with a colour of its own keeps that. */
void
theme_set_color (Theme *theme, gint set, gint state, const GdkColor *color)
{
  Style *main = theme->main;
  GList *edits = NULL, *l;

  if (main->has[set][state])
    {
      GdkColor old = main->color[set][state];

      for (l = theme->styles; l; l = l->next)
        {
          Style *style = l->data;

          if (style->has[set][state] && color_equal (&style->color[set][state], &old))
            edits = edit_add (edits, style->span[set][state].start,
                              style->span[set][state].end, color_spec (color));
        }
    }
  else
    {
      gchar *spec = color_spec (color);

      edits = edit_add_line (edits, theme, main->close,
                             g_strdup_printf ("  %s[%s] = %s\n", set_names[set],
                                              state_names[state], spec));
      g_free (spec);
    }
  theme_apply_edits (theme, edits);
}

#define PLACE(style, set, state)        GINT_TO_POINTER (((style) << 8) | ((set) << 4) | (state))

GList *
theme_find_color (Theme *theme, gint set, gint state, const GdkColor *color)
{
  GList *places = NULL, *l;
  gint index = 0;

  for (l = theme->styles; l; l = l->next, index++)
    {
      Style *style = l->data;
      gint s, t;

      for (s = 0; s < N_SETS; s++)
        for (t = 0; t < N_STATES; t++)
          if ((set < 0 || (s == set && t == state)) && style->has[s][t] &&
              color_equal (&style->color[s][t], color))
            places = g_list_append (places, PLACE (index, s, t));
    }
  return places;
}

void
theme_set_places (Theme *theme, GList *places, const GdkColor *color)
{
  GList *edits = NULL;

  for (; places; places = places->next)
    {
      gint place = GPOINTER_TO_INT (places->data);
      Style *style = g_list_nth_data (theme->styles, place >> 8);
      gint set = (place >> 4) & 0xf, state = place & 0xf;

      if (style && style->has[set][state])
        edits = edit_add (edits, style->span[set][state].start,
                          style->span[set][state].end, color_spec (color));
    }
  theme_apply_edits (theme, edits);
}

static Option *
theme_find_option (Theme *theme, const gchar *key)
{
  GList *l;

  for (l = theme->main->options; l; l = l->next)
    if (strcmp (((Option *) l->data)->key, key) == 0)
      return l->data;
  return NULL;
}

gchar *
theme_get_option (Theme *theme, const gchar *key)
{
  Option *option = theme_find_option (theme, key);

  if (!option)
    return NULL;
  return g_strndup (theme->text->str + option->value.start,
                    option->value.end - option->value.start);
}

/* An option in the main style's engine block. */
void
theme_set_option (Theme *theme, const gchar *key, const gchar *value)
{
  Option *option = theme_find_option (theme, key);
  GList *edits = NULL;

  if (option)
    edits = edit_add (edits, option->value.start, option->value.end, g_strdup (value));
  else if (theme->main->engine_close >= 0)
    edits = edit_add_line (edits, theme, theme->main->engine_close,
                           g_strdup_printf ("    %s = %s\n", key, value));
  theme_apply_edits (theme, edits);
}

/* ---------------------------------------------------------------------
 * Writing files: whole, then renamed into place, so that a program
 * reading one never sees half of it, and sees a new file each time.
 */

static gboolean
make_dirs (const gchar *path)
{
  gchar *dir = g_strdup (path);
  gchar *p;

  for (p = dir + 1; *p; p++)
    if (*p == '/')
      {
        *p = '\0';
        if (mkdir (dir, 0755) != 0 && errno != EEXIST)
          {
            g_free (dir);
            return FALSE;
          }
        *p = '/';
      }
  g_free (dir);
  return TRUE;
}

gboolean
theme_write_file (const gchar *path, const gchar *text)
{
  gchar *tmp = g_strconcat (path, ".new", NULL);
  FILE *file;
  gboolean ok;

  if (!make_dirs (path) || !(file = fopen (tmp, "w")))
    {
      g_free (tmp);
      return FALSE;
    }
  ok = fputs (text, file) >= 0;
  ok = (fclose (file) == 0) && ok;
  if (ok)
    ok = rename (tmp, path) == 0;
  if (!ok)
    unlink (tmp);
  g_free (tmp);
  return ok;
}

/* ---------------------------------------------------------------------
 * The user's rc file, $XDG_CONFIG_HOME/gtk-1.0/gtkrc, and gtkrc.mine
 * beside it
 */

#define GTKRC_MARK "# -- THEME AUTO-WRITTEN DO NOT EDIT"

gchar *
gtkrc_path (void)
{
  return gtk_rc_get_user_file ();
}

/* Where settings of the user's own are kept: gtkrc.mine beside the rc
 * file */
static gchar *
gtkrc_mine_path (void)
{
  gchar *rc = gtkrc_path ();
  gchar *dir = g_dirname (rc);
  gchar *mine = g_strconcat (dir, "/gtkrc.mine", NULL);

  g_free (dir);
  g_free (rc);
  return mine;
}

static gchar *
quoted (const gchar *s)
{
  GString *out = g_string_new ("\"");
  gchar *result;

  for (; *s; s++)
    {
      if (*s == '"' || *s == '\\')
        g_string_append_c (out, '\\');
      g_string_append_c (out, *s);
    }
  g_string_append_c (out, '"');
  result = out->str;
  g_string_free (out, FALSE);
  return result;
}

/* The theme it includes, and the font of its "user-font" style: what
 * cgtk-theme writes, and gtk-theme-switch did. */
void
gtkrc_read (gchar **theme_rc, gchar **font)
{
  gchar *path = gtkrc_path ();
  gchar *mine = gtkrc_mine_path ();
  gboolean in_font_style = FALSE;
  Lexer lx;
  Theme *rc;

  *theme_rc = NULL;
  *font = NULL;
  rc = theme_load (path);
  g_free (path);
  if (!rc)
    {
      g_free (mine);
      return;
    }

  lx.text = rc->text->str;
  lx.pos = 0;
  while (lex (&lx) != TOK_END)
    {
      if (lx.type == TOK_IDENT && lex_is (&lx, "include") && lex (&lx) == TOK_STRING)
        {
          gchar *file = lex_string (&lx);

          if (!*theme_rc && strcmp (file, mine) != 0)
            *theme_rc = file;
          else
            g_free (file);
        }
      else if (lx.type == TOK_IDENT && lex_is (&lx, "style") && lex (&lx) == TOK_STRING)
        in_font_style = lex_is (&lx, "\"user-font\"");
      else if (in_font_style && lx.type == TOK_IDENT && lex_is (&lx, "font") &&
               lex (&lx) == TOK_PUNCT && lex_is (&lx, "=") && lex (&lx) == TOK_STRING)
        {
          g_free (*font);
          *font = lex_string (&lx);
        }
      else if (lx.type == TOK_PUNCT && lex_is (&lx, "}"))
        in_font_style = FALSE;
    }
  theme_free (rc);
  g_free (mine);
}

gchar *
gtkrc_render (const gchar *theme_rc, const gchar *font)
{
  gchar *home = gtkrc_mine_path ();
  gchar *q_home = quoted (home);
  GString *out = g_string_new (GTKRC_MARK "\n");
  gchar *result;

  g_string_sprintfa (out, "# Written by cgtk-theme: choose a theme with it, and keep\n"
                          "# settings of your own in %s,\n"
                          "# which this includes.\n", home);
  if (theme_rc)
    {
      gchar *q = quoted (theme_rc);

      g_string_sprintfa (out, "include %s\n", q);
      g_free (q);
    }
  if (font && *font)
    {
      gchar *q = quoted (font);

      g_string_sprintfa (out, "\nstyle \"user-font\"\n{\n  font = %s\n}\n"
                              "widget_class \"*\" style \"user-font\"\n", q);
      g_free (q);
    }
  g_string_sprintfa (out, "\ninclude %s\n" GTKRC_MARK "\n", q_home);
  g_free (q_home);
  g_free (home);
  result = out->str;
  g_string_free (out, FALSE);
  return result;
}
