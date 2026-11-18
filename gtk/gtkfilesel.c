/* GTK - The GIMP Toolkit
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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <fnmatch.h>
#include <time.h>

#include "gdk/gdkkeysyms.h"
#include "gtkarrow.h"
#include "gtkbutton.h"
#include "gtkcheckmenuitem.h"
#include "gtkhpaned.h"
#include "gtktable.h"
#include "gtkentry.h"
#include "gtkfilesel.h"
#include "gtkhbox.h"
#include "gtkhbbox.h"
#include "gtklabel.h"
#include "gtklist.h"
#include "gtklistitem.h"
#include "gtkmain.h"
#include "gtkscrolledwindow.h"
#include "gtksignal.h"
#include "gtkvbox.h"
#include "gtkmenu.h"
#include "gtkmenuitem.h"
#include "gtkoptionmenu.h"
#include "gtkclist.h"
#include "gtkdialog.h"
#include "gtkintl.h"

#define FILE_SEL_WIDTH   660
#define FILE_SEL_HEIGHT  420
#define PLACES_WIDTH     150

/* The Hurd doesn't define either PATH_MAX or MAXPATHLEN, so we put this
 * in here, since the rest of the code in the file does require some
 * fixed maximum.
 */
#ifndef MAXPATHLEN
#  ifdef PATH_MAX
#    define MAXPATHLEN PATH_MAX
#  else
#    define MAXPATHLEN 2048
#  endif
#endif

/* I've put this here so it doesn't get confused with the
 * file completion interface */
typedef struct _HistoryCallbackArg HistoryCallbackArg;

struct _HistoryCallbackArg
{
  gchar *directory;
  GtkWidget *menu_item;
};


typedef struct _CompletionState    CompletionState;
typedef struct _CompletionDir      CompletionDir;
typedef struct _CompletionDirSent  CompletionDirSent;
typedef struct _CompletionDirEntry CompletionDirEntry;
typedef struct _CompletionUserDir  CompletionUserDir;
typedef struct _PossibleCompletion PossibleCompletion;

/* Non-external file completion decls and structures */

/* A contant telling PRCS how many directories to cache. Its actually
 * kept in a list, so the geometry isn't important. */
#define CMPL_DIRECTORY_CACHE_SIZE 10

/* A constant used to determine whether a substring was an exact
 * match by first_diff_index()
 */
#define PATTERN_MATCH -1
/* The arguments used by all fnmatch() calls below
 */
#define FNMATCH_FLAGS (FNM_PATHNAME | (cmpl_show_hidden ? 0 : FNM_PERIOD))

/* Whether names that begin with a full stop are listed with the
 * rest: set by the selector that is listing, as it begins.
 */
static gboolean cmpl_show_hidden = FALSE;

#define CMPL_ERRNO_TOO_LONG ((1<<16)-1)

/* This structure contains all the useful information about a directory
 * for the purposes of filename completion. These structures are cached
 * in the CompletionState struct. CompletionDir's are reference counted.
 */
struct _CompletionDirSent
{
  ino_t inode;
  time_t mtime;
  dev_t device;

  gint entry_count;
  gchar *name_buffer; /* memory segment containing names of all entries */

  struct _CompletionDirEntry *entries;
};

struct _CompletionDir
{
  CompletionDirSent *sent;

  gchar *fullname;
  gint fullname_len;

  struct _CompletionDir *cmpl_parent;
  gint cmpl_index;
  gchar *cmpl_text;
};

/* This structure contains pairs of directory entry names with a flag saying
 * whether or not they are a valid directory. NOTE: This information is used
 * to provide the caller with information about whether to update its completions
 * or try to open a file. Since directories are cached by the directory mtime,
 * a symlink which points to an invalid file (which will not be a directory),
 * will not be reevaluated if that file is created, unless the containing
 * directory is touched. I consider this case to be worth ignoring (josh).
 */
struct _CompletionDirEntry
{
  gint is_dir;
  gchar *entry_name;
};

struct _CompletionUserDir
{
  gchar *login;
  gchar *homedir;
};

struct _PossibleCompletion
{
  /* accessible fields, all are accessed externally by functions
   * declared above
   */
  gchar *text;
  gint is_a_completion;
  gint is_directory;

  /* Private fields
   */
  gint text_alloc;
};

struct _CompletionState
{
  gint last_valid_char;
  gchar *updated_text;
  gint updated_text_len;
  gint updated_text_alloc;
  gint re_complete;

  gchar *user_dir_name_buffer;
  gint user_directories_len;

  gchar *last_completion_text;

  gint user_completion_index; /* if >= 0, currently completing ~user */

  struct _CompletionDir *completion_dir; /* directory completing from */
  struct _CompletionDir *active_completion_dir;

  struct _PossibleCompletion the_completion;

  struct _CompletionDir *reference_dir; /* initial directory */

  GList* directory_storage;
  GList* directory_sent_storage;

  struct _CompletionUserDir *user_directories;
};


/* File completion functions which would be external, were they used
 * outside of this file.
 */

static CompletionState*    cmpl_init_state        (void);
static void                cmpl_free_state        (CompletionState *cmpl_state);
static gint                cmpl_state_okay        (CompletionState* cmpl_state);
static gchar*              cmpl_strerror          (gint);

static PossibleCompletion* cmpl_completion_matches(gchar           *text_to_complete,
                                                   gchar          **remaining_text,
                                                   CompletionState *cmpl_state);

/* Returns a name for consideration, possibly a completion, this name
 * will be invalid after the next call to cmpl_next_completion.
 */
static char*               cmpl_this_completion   (PossibleCompletion*);

/* True if this completion matches the given text. Otherwise, this
 * output can be used to have a list of non-completions.
 */
static gint                cmpl_is_a_completion   (PossibleCompletion*);

/* True if the completion is a directory
 */
static gint                cmpl_is_directory      (PossibleCompletion*);

/* Obtains the next completion, or NULL
 */
static PossibleCompletion* cmpl_next_completion   (CompletionState*);

/* Updating completions: the return value of cmpl_updated_text() will
 * be text_to_complete completed as much as possible after the most
 * recent call to cmpl_completion_matches. For the present
 * application, this is the suggested replacement for the user's input
 * string. You must CALL THIS AFTER ALL cmpl_text_completions have
 * been received.
 */
static gchar*              cmpl_updated_text       (CompletionState* cmpl_state);

/* After updating, to see if the completion was a directory, call
 * this. If it was, you should consider re-calling completion_matches.
 */
static gint                cmpl_updated_dir        (CompletionState* cmpl_state);

/* Current location: if using file completion, return the current
 * directory, from which file completion begins. More specifically,
 * the cwd concatenated with all exact completions up to the last
 * directory delimiter('/').
 */
static gchar*              cmpl_reference_position (CompletionState* cmpl_state);

/* backing up: if cmpl_completion_matches returns NULL, you may query
 * the index of the last completable character into cmpl_updated_text.
 */
static gint                cmpl_last_valid_char    (CompletionState* cmpl_state);

/* When the user selects a non-directory, call cmpl_completion_fullname
 * to get the full name of the selected file.
 */
static gchar*              cmpl_completion_fullname (gchar*, CompletionState* cmpl_state);


/* Directory operations. */
static CompletionDir* open_ref_dir         (gchar* text_to_complete,
                                            gchar** remaining_text,
                                            CompletionState* cmpl_state);
static gboolean       check_dir            (gchar *dir_name,
                                            struct stat *result,
                                            gboolean *stat_subdirs);
static CompletionDir* open_dir             (gchar* dir_name,
                                            CompletionState* cmpl_state);
static CompletionDir* open_user_dir        (gchar* text_to_complete,
                                            CompletionState *cmpl_state);
static CompletionDir* open_relative_dir    (gchar* dir_name, CompletionDir* dir,
                                            CompletionState *cmpl_state);
static CompletionDirSent* open_new_dir     (gchar* dir_name,
                                            struct stat* sbuf,
                                            gboolean stat_subdirs);
static gint           correct_dir_fullname (CompletionDir* cmpl_dir);
static gint           correct_parent       (CompletionDir* cmpl_dir,
                                            struct stat *sbuf);
static gchar*         find_parent_dir_fullname    (gchar* dirname);
static CompletionDir* attach_dir           (CompletionDirSent* sent,
                                            gchar* dir_name,
                                            CompletionState *cmpl_state);
static void           free_dir_sent (CompletionDirSent* sent);
static void           free_dir      (CompletionDir  *dir);
static void           prune_memory_usage(CompletionState *cmpl_state);

/* Completion operations */
static PossibleCompletion* attempt_homedir_completion(gchar* text_to_complete,
                                                      CompletionState *cmpl_state);
static PossibleCompletion* attempt_file_completion(CompletionState *cmpl_state);
static CompletionDir* find_completion_dir(gchar* text_to_complete,
                                          gchar** remaining_text,
                                          CompletionState* cmpl_state);
static PossibleCompletion* append_completion_text(gchar* text,
                                                  CompletionState* cmpl_state);
static gint get_pwdb(CompletionState* cmpl_state);
static gint first_diff_index(gchar* pat, gchar* text);
static gint compare_user_dir(const void* a, const void* b);
static gint compare_cmpl_dir(const void* a, const void* b);
static void update_cmpl(PossibleCompletion* poss,
                        CompletionState* cmpl_state);

static void gtk_file_selection_class_init    (GtkFileSelectionClass *klass);
static void gtk_file_selection_init          (GtkFileSelection      *filesel);
static void gtk_file_selection_destroy       (GtkObject             *object);
static void gtk_file_selection_show          (GtkWidget             *widget);
static void file_sel_buttons_misplaced       (GtkWidget             *box);
static gint gtk_file_selection_key_press     (GtkWidget             *widget,
                                              GdkEventKey           *event,
                                              gpointer               user_data);

static void gtk_file_selection_file_button (GtkWidget *widget,
                                            gint row,
                                            gint column,
                                            GdkEventButton *bevent,
                                            gpointer user_data);

static void gtk_file_selection_dir_button (GtkWidget *widget,
                                           gint row,
                                           gint column,
                                           GdkEventButton *bevent,
                                           gpointer data);

static void gtk_file_selection_populate      (GtkFileSelection      *fs,
                                              gchar                 *rel_path,
                                              gint                   try_complete);
static void gtk_file_selection_abort         (GtkFileSelection      *fs);

static void gtk_file_selection_update_history_menu (GtkFileSelection       *fs,
                                                    gchar                  *current_dir);

static void gtk_file_selection_create_dir (GtkWidget *widget, gpointer data);
static void gtk_file_selection_delete_file (GtkWidget *widget, gpointer data);
static void gtk_file_selection_rename_file (GtkWidget *widget, gpointer data);



static GtkWindowClass *parent_class = NULL;

/* Saves errno when something cmpl does fails. */
static gint cmpl_errno;

GtkType
gtk_file_selection_get_type (void)
{
  static GtkType file_selection_type = 0;

  if (!file_selection_type)
    {
      static const GtkTypeInfo filesel_info =
      {
        "GtkFileSelection",
        sizeof (GtkFileSelection),
        sizeof (GtkFileSelectionClass),
        (GtkClassInitFunc) gtk_file_selection_class_init,
        (GtkObjectInitFunc) gtk_file_selection_init,
        /* reserved_1 */ NULL,
        /* reserved_2 */ NULL,
        (GtkClassInitFunc) NULL,
      };

      file_selection_type = gtk_type_unique (GTK_TYPE_WINDOW, &filesel_info);
    }

  return file_selection_type;
}

static void
gtk_file_selection_class_init (GtkFileSelectionClass *class)
{
  GtkObjectClass *object_class;

  object_class = (GtkObjectClass*) class;

  parent_class = gtk_type_class (GTK_TYPE_WINDOW);

  object_class->destroy = gtk_file_selection_destroy;
  GTK_WIDGET_CLASS (class)->show = gtk_file_selection_show;
}

/*
 * The box of OK and Cancel.
 *
 * In GTK+ 1.2 the selector's two buttons were side by side in one
 * box, and programs written for it take the OK button's parent for a
 * box and pack a button of their own in beside them: XMMS's Play
 * files has Play, Add and Close. The two stand one over the other
 * now, each beside a field, and a third among them would not.
 *
 * So the box is one that lies either way. With two buttons in it, or
 * fewer, it is a column, each button in the middle of a row as high
 * as the rows of the fields beside it. With more it is a row, all the
 * buttons of one size, and the selector stands it under the fields at
 * the right (file_sel_place_buttons()), where such a row was. It is a
 * GtkBox, and how a button is packed into it is not looked at.
 */
#define BUTTONS_COLUMN_SPACING 4
#define BUTTONS_ROW_SPACING    6

static gint
file_sel_buttons_count (GtkBox *box)
{
  GList *list;
  gint n = 0;

  for (list = box->children; list; list = list->next)
    if (GTK_WIDGET_VISIBLE (((GtkBoxChild *) list->data)->widget))
      n++;

  return n;
}

static void
file_sel_buttons_size_request (GtkWidget      *widget,
                               GtkRequisition *requisition)
{
  GtkBox *box = GTK_BOX (widget);
  GtkRequisition child;
  GList *list;
  gint n = 0, width = 0, height = 0;

  for (list = box->children; list; list = list->next)
    {
      GtkWidget *button = ((GtkBoxChild *) list->data)->widget;

      if (!GTK_WIDGET_VISIBLE (button))
        continue;
      gtk_widget_size_request (button, &child);
      width = MAX (width, child.width);
      height = MAX (height, child.height);
      n++;
    }

  if (n > 2)
    {
      requisition->width = n * width + (n - 1) * BUTTONS_ROW_SPACING;
      requisition->height = height;
    }
  else
    {
      requisition->width = width;
      requisition->height = n * height + MAX (n - 1, 0) * BUTTONS_COLUMN_SPACING;
    }
  requisition->width += GTK_CONTAINER (box)->border_width * 2;
  requisition->height += GTK_CONTAINER (box)->border_width * 2;

  /* a button more or less may have made it the other of the two */
  file_sel_buttons_misplaced (widget);
}

static void
file_sel_buttons_size_allocate (GtkWidget     *widget,
                                GtkAllocation *allocation)
{
  GtkBox *box = GTK_BOX (widget);
  GtkRequisition requisition;
  GtkAllocation child;
  GList *list;
  gint border = GTK_CONTAINER (box)->border_width;
  gint n = file_sel_buttons_count (box);
  gint x, y, width, height, each;

  widget->allocation = *allocation;
  if (n == 0)
    return;

  x = allocation->x + border;
  y = allocation->y + border;
  width = MAX (1, (gint) allocation->width - border * 2);
  height = MAX (1, (gint) allocation->height - border * 2);

  if (n > 2)
    {
      /* a row: what is over when the width is shared goes to the last */
      width -= (n - 1) * BUTTONS_ROW_SPACING;
      each = width / n;
      for (list = box->children; list; list = list->next)
        {
          GtkWidget *button = ((GtkBoxChild *) list->data)->widget;

          if (!GTK_WIDGET_VISIBLE (button))
            continue;
          child.x = x;
          child.y = y;
          child.width = MAX (1, n == 1 ? width : each);
          child.height = height;
          gtk_widget_size_allocate (button, &child);
          x += each + BUTTONS_ROW_SPACING;
          width -= each;
          n--;
        }
    }
  else
    {
      /* a column: rows as a GtkVBox of equal rows makes them, which is
       * what the fields beside it are in */
      height -= (n - 1) * BUTTONS_COLUMN_SPACING;
      each = height / n;
      for (list = box->children; list; list = list->next)
        {
          GtkWidget *button = ((GtkBoxChild *) list->data)->widget;
          gint row = n == 1 ? height : each;

          if (!GTK_WIDGET_VISIBLE (button))
            continue;
          gtk_widget_get_child_requisition (button, &requisition);
          child.x = x;
          child.width = width;
          child.height = MAX (1, MIN (requisition.height, row));
          child.y = y + (row - (gint) child.height) / 2;
          gtk_widget_size_allocate (button, &child);
          y += each + BUTTONS_COLUMN_SPACING;
          height -= each;
          n--;
        }
    }
}

static void
file_sel_buttons_class_init (GtkBoxClass *class)
{
  GtkWidgetClass *widget_class = (GtkWidgetClass*) class;

  widget_class->size_request = file_sel_buttons_size_request;
  widget_class->size_allocate = file_sel_buttons_size_allocate;
}

static GtkType
file_sel_buttons_get_type (void)
{
  static GtkType buttons_type = 0;

  if (!buttons_type)
    {
      static const GtkTypeInfo buttons_info =
      {
        "GtkFileSelectionButtons",
        sizeof (GtkBox),
        sizeof (GtkBoxClass),
        (GtkClassInitFunc) file_sel_buttons_class_init,
        (GtkObjectInitFunc) NULL,
        /* reserved_1 */ NULL,
        /* reserved_2 */ NULL,
        (GtkClassInitFunc) NULL,
      };

      buttons_type = gtk_type_unique (GTK_TYPE_BOX, &buttons_info);
    }

  return buttons_type;
}

/* What a program has not got at through the widget's own fields: the
 * places at the side, the kinds of file to show, and where the
 * selector has been.
 */
typedef struct
{
  gchar *name;       /* as the menu has it */
  gchar **patterns;  /* NULL for every file */
} FileFilter;

typedef struct
{
  gboolean is_dir;
  gdouble size;
  time_t mtime;
} FileRow;

typedef struct
{
  GtkWidget *places_list;
  GtkWidget *filter_pulldown;
  GtkWidget *back_button;
  GtkWidget *forward_button;
  GtkWidget *up_button;
  GtkWidget *popup;          /* the menu of the list of files */
  GtkWidget *popup_create;
  GtkWidget *popup_line;     /* between the three above and the rest */
  GtkWidget *popup_hidden;
  GtkWidget *places_popup;   /* and that of a place the user added */
  GList *filters;            /* of FileFilter */
  FileFilter *filter;        /* the one in use; NULL shows every file */
  GList *places;             /* the directory of each row of places_list */
  gint fixed_places;         /* how many of them are not the user's */
  gint place_row;            /* the row its menu is for */
  gchar *place_target;       /* the folder the list's menu is on, if any */
  GtkWidget *place_remove;   /* the places menu's Remove from Places */
  GList *back;               /* directories it was in, the last first */
  GList *forward;            /* and those gone back from */
  gchar *current;
  GtkWidget *button_box;     /* of OK and Cancel, and what a program adds */
  GtkWidget *foot;           /* where it stands as a column, beside the fields */
  GtkWidget *row_slot;       /* and where as a row, under them */
  guint place_idle;          /* if it is to be moved from one to the other */
  guint show_hidden : 1;
  guint moving : 1;          /* going back or forward */
} FileSelData;

static const gchar file_sel_key[] = "gtk-file-selection-data";

#define FILE_SEL_DATA(fs) ((FileSelData *) gtk_object_get_data (GTK_OBJECT (fs), file_sel_key))

static void
file_sel_free_strings (GList **list)
{
  GList *l;

  for (l = *list; l; l = l->next)
    g_free (l->data);
  g_list_free (*list);
  *list = NULL;
}

/* The directory is listed again, and what was typed is still there. */
static void
file_sel_refresh (GtkFileSelection *fs)
{
  gchar *typed = g_strdup (gtk_entry_get_text (GTK_ENTRY (fs->selection_entry)));

  gtk_file_selection_populate (fs, "", FALSE);
  gtk_entry_set_text (GTK_ENTRY (fs->selection_entry), typed);
  g_free (typed);
}

static void
file_sel_go (GtkFileSelection *fs,
             const gchar      *dir)
{
  gchar *path = *dir && dir[strlen (dir) - 1] == '/' ? g_strdup (dir) : g_strconcat (dir, "/", NULL);

  gtk_file_selection_populate (fs, path, FALSE);
  g_free (path);
}

/* The place the selector is at, if it is at one, is the one that is
 * marked. */
static void
file_sel_mark_place (GtkFileSelection *fs)
{
  FileSelData *data = FILE_SEL_DATA (fs);
  GList *l;
  gint row;

  gtk_clist_unselect_all (GTK_CLIST (data->places_list));
  for (l = data->places, row = 0; l && data->current; l = l->next, row++)
    if (strcmp (l->data, data->current) == 0)
      {
        gtk_clist_select_row (GTK_CLIST (data->places_list), row, 0);
        break;
      }
}

/* The selector shows a directory: if it is another than before, the
 * one before is where Back goes. */
static void
file_sel_moved (GtkFileSelection *fs,
                const gchar      *dir)
{
  FileSelData *data = FILE_SEL_DATA (fs);

  if (!data)
    return;
  if (!data->current || strcmp (data->current, dir) != 0)
    {
      if (data->current && !data->moving)
        {
          data->back = g_list_prepend (data->back, data->current);
          file_sel_free_strings (&data->forward);
        }
      else
        g_free (data->current);
      data->current = g_strdup (dir);
    }
  gtk_widget_set_sensitive (data->back_button, data->back != NULL);
  gtk_widget_set_sensitive (data->forward_button, data->forward != NULL);
  gtk_widget_set_sensitive (data->up_button, strcmp (dir, "/") != 0);
  file_sel_mark_place (fs);
}

static void
file_sel_step (GtkFileSelection  *fs,
               GList            **from,
               GList            **to)
{
  FileSelData *data = FILE_SEL_DATA (fs);
  gchar *dir;

  if (!*from)
    return;
  dir = (*from)->data;
  *from = g_list_remove (*from, dir);
  if (data->current)
    *to = g_list_prepend (*to, g_strdup (data->current));
  data->moving = TRUE;
  file_sel_go (fs, dir);
  data->moving = FALSE;
  g_free (dir);
  if (data->current)
    file_sel_moved (fs, data->current);
}

static void
file_sel_back (GtkWidget        *widget,
               GtkFileSelection *fs)
{
  FileSelData *data = FILE_SEL_DATA (fs);

  file_sel_step (fs, &data->back, &data->forward);
}

static void
file_sel_forward (GtkWidget        *widget,
                  GtkFileSelection *fs)
{
  FileSelData *data = FILE_SEL_DATA (fs);

  file_sel_step (fs, &data->forward, &data->back);
}

static void
file_sel_up (GtkWidget        *widget,
             GtkFileSelection *fs)
{
  gtk_file_selection_populate (fs, "../", FALSE);
}

/* Return in the entry goes into a directory that is named there,
 * and is OK for anything else. */
static void
file_sel_entry_activate (GtkWidget        *entry,
                         GtkFileSelection *fs)
{
  gchar *text = gtk_entry_get_text (GTK_ENTRY (entry));
  struct stat st;

  if (text && *text && stat (cmpl_completion_fullname (text, fs->cmpl_state), &st) == 0 && S_ISDIR (st.st_mode))
    {
      gchar *dir = g_strdup (text);

      file_sel_go (fs, dir);
      g_free (dir);
    }
  else
    gtk_button_clicked (GTK_BUTTON (fs->ok_button));
}

/* ---- the kinds of file ---- */

static gboolean
file_sel_filter_matches (FileFilter  *filter,
                         const gchar *name)
{
  gint i;

  if (!filter || !filter->patterns)
    return TRUE;
  for (i = 0; filter->patterns[i]; i++)
    if (fnmatch (filter->patterns[i], name, 0) == 0)
      return TRUE;

  return FALSE;
}

static void
file_sel_filter_chosen (GtkWidget        *item,
                        GtkFileSelection *fs)
{
  FileSelData *data = FILE_SEL_DATA (fs);
  FileFilter *filter = gtk_object_get_data (GTK_OBJECT (item), "filter");

  if (data->filter != filter)
    {
      data->filter = filter;
      file_sel_refresh (fs);
    }
}

static void
file_sel_update_filters (GtkFileSelection *fs)
{
  FileSelData *data = FILE_SEL_DATA (fs);
  GtkWidget *menu = gtk_menu_new (), *item;
  GList *l;
  gint n = 0, chosen = 0;

  if (!data->filters)
    {
      gchar *all = g_strconcat (_("All Files"), " (*)", NULL);

      item = gtk_menu_item_new_with_label (all);
      g_free (all);
      gtk_menu_append (GTK_MENU (menu), item);
      gtk_widget_show (item);
    }
  for (l = data->filters; l; l = l->next, n++)
    {
      FileFilter *filter = l->data;

      item = gtk_menu_item_new_with_label (filter->name);
      gtk_object_set_data (GTK_OBJECT (item), "filter", filter);
      gtk_signal_connect (GTK_OBJECT (item), "activate",
                          GTK_SIGNAL_FUNC (file_sel_filter_chosen), fs);
      gtk_menu_append (GTK_MENU (menu), item);
      gtk_widget_show (item);
      if (filter == data->filter)
        chosen = n;
    }
  gtk_option_menu_set_menu (GTK_OPTION_MENU (data->filter_pulldown), menu);
  gtk_option_menu_set_history (GTK_OPTION_MENU (data->filter_pulldown), chosen);
}

void
gtk_file_selection_add_filter (GtkFileSelection *filesel,
                               const gchar      *name,
                               const gchar      *patterns)
{
  FileSelData *data;
  FileFilter *filter;

  g_return_if_fail (filesel != NULL);
  g_return_if_fail (GTK_IS_FILE_SELECTION (filesel));
  g_return_if_fail (name != NULL);

  data = FILE_SEL_DATA (filesel);
  if (!data)
    return;
  filter = g_new0 (FileFilter, 1);
  filter->name = g_strdup (name);
  if (patterns && *patterns && strcmp (patterns, "*") != 0)
    {
      gchar *spaced = g_strdelimit (g_strdup (patterns), ";", ' '), **all;
      gint i, n = 0;

      all = g_strsplit (spaced, " ", 0);
      for (i = 0; all[i]; i++)
        if (*all[i])
          all[n++] = all[i];
        else
          g_free (all[i]);
      all[n] = NULL;
      filter->patterns = all;
      g_free (spaced);
    }
  data->filters = g_list_append (data->filters, filter);
  if (!data->filter)
    data->filter = filter;
  file_sel_update_filters (filesel);
  file_sel_refresh (filesel);
}

void
gtk_file_selection_set_show_hidden (GtkFileSelection *filesel,
                                    gboolean          show_hidden)
{
  FileSelData *data;

  g_return_if_fail (filesel != NULL);
  g_return_if_fail (GTK_IS_FILE_SELECTION (filesel));

  data = FILE_SEL_DATA (filesel);
  if (data && data->show_hidden != (show_hidden != FALSE))
    {
      data->show_hidden = show_hidden != FALSE;
      gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (data->popup_hidden), data->show_hidden);
      file_sel_refresh (filesel);
    }
}

void
gtk_file_selection_set_select_multiple (GtkFileSelection *filesel,
                                        gboolean          select_multiple)
{
  g_return_if_fail (filesel != NULL);
  g_return_if_fail (GTK_IS_FILE_SELECTION (filesel));

  gtk_clist_set_selection_mode (GTK_CLIST (filesel->file_list),
                                select_multiple ? GTK_SELECTION_EXTENDED : GTK_SELECTION_SINGLE);
}

/* The files that are chosen, each with its directory: the rows that
 * are selected, or what is in the entry if none is. NULL ends them,
 * and g_strfreev() frees them. */
gchar**
gtk_file_selection_get_selections (GtkFileSelection *filesel)
{
  GtkCList *clist;
  gchar **names, *text;
  GList *l;
  gint n = 0;

  g_return_val_if_fail (filesel != NULL, NULL);
  g_return_val_if_fail (GTK_IS_FILE_SELECTION (filesel), NULL);

  clist = GTK_CLIST (filesel->file_list);
  names = g_new (gchar *, g_list_length (clist->selection) + 2);
  for (l = clist->selection; l; l = l->next)
    if (gtk_clist_get_text (clist, GPOINTER_TO_INT (l->data), 0, &text))
      names[n++] = g_strdup (cmpl_completion_fullname (text, filesel->cmpl_state));
  if (n == 0)
    names[n++] = g_strdup (gtk_file_selection_get_filename (filesel));
  names[n] = NULL;

  return names;
}

/* ---- the list of files ---- */

static void
file_sel_size_text (gdouble  size,
                    gchar   *text,
                    gint     len)
{
  if (size < 1024.0)
    g_snprintf (text, len, "%.0f B", size);
  else if (size < 1024.0 * 1024.0)
    g_snprintf (text, len, "%.1f KB", size / 1024.0);
  else if (size < 1024.0 * 1024.0 * 1024.0)
    g_snprintf (text, len, "%.1f MB", size / (1024.0 * 1024.0));
  else
    g_snprintf (text, len, "%.1f GB", size / (1024.0 * 1024.0 * 1024.0));
}

/* Directories come before files, whichever way the rest is sorted. */
static gint
file_sel_compare (GtkCList      *clist,
                  gconstpointer  ptr1,
                  gconstpointer  ptr2)
{
  const GtkCListRow *row1 = ptr1, *row2 = ptr2;
  const FileRow *file1 = row1->data, *file2 = row2->data;
  gint result = 0;

  if (!file1 || !file2)
    return file1 ? 1 : file2 ? -1 : 0;
  if (file1->is_dir != file2->is_dir)
    return (file1->is_dir ? -1 : 1) * (clist->sort_type == GTK_SORT_ASCENDING ? 1 : -1);
  if (clist->sort_column == 1)
    result = file1->size < file2->size ? -1 : file1->size > file2->size;
  else if (clist->sort_column == 2)
    result = file1->mtime < file2->mtime ? -1 : file1->mtime > file2->mtime;
  if (result == 0)
    result = strcoll (GTK_CELL_TEXT (row1->cell[0])->text, GTK_CELL_TEXT (row2->cell[0])->text);

  return result;
}

/* The title of the column the list is sorted by has an arrow in it,
 * that points the way the list runs. */
static void
file_sel_show_sort (GtkCList *clist)
{
  static const gchar *titles[3] = { N_("Name"), N_("Size"), N_("Date Modified") };
  gint i;

  for (i = 0; i < 3; i++)
    {
      GtkWidget *box = gtk_hbox_new (FALSE, 4), *label = gtk_label_new (_(titles[i]));

      gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);
      if (i == clist->sort_column)
        gtk_box_pack_end (GTK_BOX (box),
                          gtk_arrow_new (clist->sort_type == GTK_SORT_ASCENDING ? GTK_ARROW_DOWN : GTK_ARROW_UP, GTK_SHADOW_IN),
                          FALSE, FALSE, 0);
      gtk_widget_show_all (box);
      gtk_clist_set_column_widget (clist, i, box);
    }
}

static void
file_sel_click_column (GtkCList *clist,
                       gint      column,
                       gpointer  data)
{
  if (column == clist->sort_column)
    gtk_clist_set_sort_type (clist, clist->sort_type == GTK_SORT_ASCENDING ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING);
  else
    {
      gtk_clist_set_sort_column (clist, column);
      gtk_clist_set_sort_type (clist, GTK_SORT_ASCENDING);
    }
  gtk_clist_sort (clist);
  file_sel_show_sort (clist);
}

/* The right button on the list has a menu, for the row it is on. */
static gint
file_sel_list_press (GtkWidget        *widget,
                     GdkEventButton   *event,
                     GtkFileSelection *fs)
{
  FileSelData *data = FILE_SEL_DATA (fs);
  gint row, column;

  if (event->button != 3 || event->window != GTK_CLIST (widget)->clist_window)
    return FALSE;
  g_free (data->place_target);
  data->place_target = NULL;
  if (gtk_clist_get_selection_info (GTK_CLIST (widget), event->x, event->y, &row, &column))
    {
      FileRow *file = gtk_clist_get_row_data (GTK_CLIST (widget), row);
      gchar *name;

      gtk_clist_select_row (GTK_CLIST (widget), row, 0);
      /* On a folder, Add to Places is for that folder. */
      if (file && file->is_dir && data->current &&
          gtk_clist_get_text (GTK_CLIST (widget), row, 0, &name) &&
          strcmp (name, ".") != 0 && strcmp (name, "./") != 0 &&
          strcmp (name, "..") != 0 && strcmp (name, "../") != 0)
        {
          gsize n;

          data->place_target = g_build_filename (data->current, name, NULL);
          n = strlen (data->place_target);
          while (n > 1 && data->place_target[n - 1] == '/')
            data->place_target[--n] = 0;
        }
    }
  gtk_menu_popup (GTK_MENU (data->popup), NULL, NULL, NULL, NULL, event->button, event->time);
  gtk_signal_emit_stop_by_name (GTK_OBJECT (widget), "button_press_event");

  return TRUE;
}

static void
file_sel_hidden_toggled (GtkWidget        *item,
                         GtkFileSelection *fs)
{
  gtk_file_selection_set_show_hidden (fs, GTK_CHECK_MENU_ITEM (item)->active);
}

/* ---- the places ---- */

static gchar*
file_sel_places_file (void)
{
  return g_build_filename (g_get_user_config_dir (), "cgtk", "bookmarks", NULL);
}

static void
file_sel_add_place (GtkFileSelection *fs,
                    const gchar      *label,
                    const gchar      *dir)
{
  FileSelData *data = FILE_SEL_DATA (fs);
  gchar *text[1], *name = label ? g_strdup (label) : g_path_get_basename (dir);

  text[0] = name;
  gtk_clist_append (GTK_CLIST (data->places_list), text);
  data->places = g_list_append (data->places, g_strdup (dir));
  g_free (name);
}

/* Home and the top of the file system, and then the user's own: a
 * file of file: URIs, one a line, each with a name after it if it is
 * to be called something else than its directory is. */
static void
file_sel_fill_places (GtkFileSelection *fs)
{
  FileSelData *data = FILE_SEL_DATA (fs);
  gchar *file = file_sel_places_file (), *contents, **lines;
  gint i;

  gtk_clist_clear (GTK_CLIST (data->places_list));
  file_sel_free_strings (&data->places);
  file_sel_add_place (fs, _("Home"), g_get_home_dir ());
  file_sel_add_place (fs, _("File System"), "/");
  data->fixed_places = 2;

  if (g_file_get_contents (file, &contents, NULL, NULL))
    {
      lines = g_strsplit (contents, "\n", 0);
      for (i = 0; lines[i]; i++)
        {
          gchar *label = strchr (lines[i], ' '), *dir;

          if (label)
            *label++ = 0;
          dir = g_filename_from_uri (lines[i], NULL, NULL);
          if (dir)
            file_sel_add_place (fs, label && *label ? label : NULL, dir);
          g_free (dir);
        }
      g_strfreev (lines);
      g_free (contents);
    }
  g_free (file);
  file_sel_mark_place (fs);
}

static void
file_sel_save_places (GtkFileSelection *fs)
{
  FileSelData *data = FILE_SEL_DATA (fs);
  GString *text = g_string_new (NULL);
  gchar *file = file_sel_places_file (), *dir = g_path_get_dirname (file), *uri;
  GList *l;

  for (l = g_list_nth (data->places, data->fixed_places); l; l = l->next)
    if ((uri = g_filename_to_uri (l->data, NULL, NULL)))
      {
        g_string_append (text, uri);
        g_string_append_c (text, '\n');
        g_free (uri);
      }
  if (g_mkdir_with_parents (dir, 0700) == 0)
    g_file_set_contents (file, text->str, text->len, NULL);
  g_string_free (text, TRUE);
  g_free (dir);
  g_free (file);
}

/* The folder the right button was on, or else the directory the
 * selector is in. */
static void
file_sel_place_add (GtkWidget        *item,
                    GtkFileSelection *fs)
{
  FileSelData *data = FILE_SEL_DATA (fs);
  gchar *dir;
  GList *l;

  dir = g_strdup (data->place_target ? data->place_target : data->current);
  if (!dir)
    return;
  file_sel_fill_places (fs);  /* as the file has them now: another program may have added one */
  for (l = data->places; l; l = l->next)
    if (strcmp (l->data, dir) == 0)
      break;
  if (!l)
    {
      file_sel_add_place (fs, NULL, dir);
      file_sel_save_places (fs);
      file_sel_mark_place (fs);
    }
  g_free (dir);
}

static void
file_sel_place_remove (GtkWidget        *item,
                       GtkFileSelection *fs)
{
  FileSelData *data = FILE_SEL_DATA (fs);
  GList *link = data->place_row < 0 ? NULL : g_list_nth (data->places, data->place_row);

  if (!link || data->place_row < data->fixed_places)
    return;
  g_free (link->data);
  data->places = g_list_remove_link (data->places, link);
  g_list_free_1 (link);
  file_sel_save_places (fs);
  file_sel_fill_places (fs);
}

/* The left button on a place goes there, and the right button on one
 * the user added has a menu to take it away. */
static gint
file_sel_place_press (GtkWidget        *widget,
                      GdkEventButton   *event,
                      GtkFileSelection *fs)
{
  FileSelData *data = FILE_SEL_DATA (fs);
  gint row, column;
  gboolean on_row;

  if (event->window != GTK_CLIST (widget)->clist_window)
    return FALSE;
  on_row = gtk_clist_get_selection_info (GTK_CLIST (widget), event->x, event->y, &row, &column) &&
           row < (gint) g_list_length (data->places);
  if (on_row && event->button == 1 && event->type == GDK_BUTTON_PRESS)
    {
      gchar *dir = g_strdup (g_list_nth_data (data->places, row));

      if (dir)
        file_sel_go (fs, dir);
      g_free (dir);
    }
  else if (event->button == 3 && event->type == GDK_BUTTON_PRESS)
    {
      /* Anywhere on the list: Add to Places is for the directory the
       * selector is in, and Remove from Places for a place of the
       * user's that the button is on. */
      data->place_row = on_row ? row : -1;
      g_free (data->place_target);
      data->place_target = NULL;
      gtk_widget_set_sensitive (data->place_remove, on_row && row >= data->fixed_places);
      gtk_menu_popup (GTK_MENU (data->places_popup), NULL, NULL, NULL, NULL, event->button, event->time);
    }
  else
    return FALSE;
  gtk_signal_emit_stop_by_name (GTK_OBJECT (widget), "button_press_event");

  return TRUE;
}

static GtkWidget*
file_sel_menu_item (GtkWidget        *menu,
                    const gchar      *label,
                    GtkSignalFunc     func,
                    GtkFileSelection *fs)
{
  GtkWidget *item = label ? gtk_menu_item_new_with_label (label) : gtk_menu_item_new ();

  if (func)
    gtk_signal_connect (GTK_OBJECT (item), "activate", func, fs);
  gtk_menu_append (GTK_MENU (menu), item);
  gtk_widget_show (item);

  return item;
}

static GtkWidget*
file_sel_arrow_button (GtkWidget        *box,
                       GtkArrowType      type,
                       GtkSignalFunc     func,
                       GtkFileSelection *fs)
{
  GtkWidget *button = gtk_button_new (), *arrow = gtk_arrow_new (type, GTK_SHADOW_OUT);

  gtk_container_add (GTK_CONTAINER (button), arrow);
  gtk_widget_set_usize (button, 28, -1);
  gtk_signal_connect (GTK_OBJECT (button), "clicked", func, fs);
  gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);
  gtk_widget_show_all (button);

  return button;
}

static void
gtk_file_selection_init (GtkFileSelection *filesel)
{
  FileSelData *data = g_new0 (FileSelData, 1);
  GtkWidget *hbox, *label, *paned, *scrolled_win, *item;
  GtkWidget *label_box, *field_box, *button_box;
  gchar *titles[3];

  gtk_object_set_data (GTK_OBJECT (filesel), file_sel_key, data);
  filesel->cmpl_state = cmpl_init_state ();

  /* The dialog-sized vertical box  */
  filesel->main_vbox = gtk_vbox_new (FALSE, 6);
  gtk_container_set_border_width (GTK_CONTAINER (filesel), 8);
  gtk_container_add (GTK_CONTAINER (filesel), filesel->main_vbox);
  gtk_widget_show (filesel->main_vbox);
  gtk_window_set_default_size (GTK_WINDOW (filesel), FILE_SEL_WIDTH, FILE_SEL_HEIGHT);

  /* The menus of the right button: on the list of files, and on a
   * place of the user's. */
  data->popup = gtk_menu_new ();
  gtk_object_ref (GTK_OBJECT (data->popup));
  gtk_object_sink (GTK_OBJECT (data->popup));
  data->places_popup = gtk_menu_new ();
  gtk_object_ref (GTK_OBJECT (data->places_popup));
  gtk_object_sink (GTK_OBJECT (data->places_popup));
  file_sel_menu_item (data->places_popup, _("Add to Places"), GTK_SIGNAL_FUNC (file_sel_place_add), filesel);
  data->place_remove = file_sel_menu_item (data->places_popup, _("Remove from Places"), GTK_SIGNAL_FUNC (file_sel_place_remove), filesel);

  /* Along the top: where it is looking, with the directories above
   * that in a menu; back, forward and up; and Create Dir. */
  hbox = gtk_hbox_new (FALSE, 4);
  gtk_box_pack_start (GTK_BOX (filesel->main_vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  label = gtk_label_new (_("Look in:"));
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  filesel->history_pulldown = gtk_option_menu_new ();
  gtk_box_pack_start (GTK_BOX (hbox), filesel->history_pulldown, TRUE, TRUE, 0);
  gtk_widget_show (filesel->history_pulldown);

  data->back_button = file_sel_arrow_button (hbox, GTK_ARROW_LEFT, GTK_SIGNAL_FUNC (file_sel_back), filesel);
  data->forward_button = file_sel_arrow_button (hbox, GTK_ARROW_RIGHT, GTK_SIGNAL_FUNC (file_sel_forward), filesel);
  data->up_button = file_sel_arrow_button (hbox, GTK_ARROW_UP, GTK_SIGNAL_FUNC (file_sel_up), filesel);

  filesel->button_area = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), filesel->button_area, FALSE, FALSE, 0);
  gtk_widget_show (filesel->button_area);

  data->popup_line = file_sel_menu_item (data->popup, NULL, NULL, filesel);
  file_sel_menu_item (data->popup, _("Add to Places"), GTK_SIGNAL_FUNC (file_sel_place_add), filesel);
  item = gtk_check_menu_item_new_with_label (_("Show Hidden Files"));
  gtk_check_menu_item_set_show_toggle (GTK_CHECK_MENU_ITEM (item), TRUE);
  gtk_signal_connect (GTK_OBJECT (item), "toggled", GTK_SIGNAL_FUNC (file_sel_hidden_toggled), filesel);
  gtk_menu_append (GTK_MENU (data->popup), item);
  gtk_widget_show (item);
  data->popup_hidden = item;
  gtk_file_selection_show_fileop_buttons (filesel);

  /* The places at the side, and beside them what is in the directory. */
  paned = gtk_hpaned_new ();
  gtk_box_pack_start (GTK_BOX (filesel->main_vbox), paned, TRUE, TRUE, 0);
  gtk_widget_show (paned);

  data->places_list = gtk_clist_new (1);
  gtk_clist_set_column_auto_resize (GTK_CLIST (data->places_list), 0, TRUE);
  gtk_signal_connect (GTK_OBJECT (data->places_list), "button_press_event",
                      GTK_SIGNAL_FUNC (file_sel_place_press), filesel);
  scrolled_win = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_win), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER (scrolled_win), data->places_list);
  gtk_widget_set_usize (scrolled_win, PLACES_WIDTH, -1);
  gtk_paned_pack1 (GTK_PANED (paned), scrolled_win, FALSE, TRUE);
  gtk_widget_show_all (scrolled_win);

  titles[0] = titles[1] = titles[2] = "";
  filesel->file_list = gtk_clist_new_with_titles (3, titles);
  file_sel_show_sort (GTK_CLIST (filesel->file_list));
  gtk_clist_set_column_width (GTK_CLIST (filesel->file_list), 0, 240);
  gtk_clist_set_column_auto_resize (GTK_CLIST (filesel->file_list), 1, TRUE);
  gtk_clist_set_column_auto_resize (GTK_CLIST (filesel->file_list), 2, TRUE);
  gtk_clist_set_column_min_width (GTK_CLIST (filesel->file_list), 1, 64);
  gtk_clist_set_column_min_width (GTK_CLIST (filesel->file_list), 2, 110);
  gtk_clist_set_column_justification (GTK_CLIST (filesel->file_list), 1, GTK_JUSTIFY_RIGHT);
  gtk_clist_set_compare_func (GTK_CLIST (filesel->file_list), file_sel_compare);
  gtk_signal_connect (GTK_OBJECT (filesel->file_list), "select_row",
                      (GtkSignalFunc) gtk_file_selection_file_button,
                      (gpointer) filesel);
  gtk_signal_connect (GTK_OBJECT (filesel->file_list), "click_column",
                      GTK_SIGNAL_FUNC (file_sel_click_column), NULL);
  gtk_signal_connect (GTK_OBJECT (filesel->file_list), "button_press_event",
                      GTK_SIGNAL_FUNC (file_sel_list_press), filesel);
  scrolled_win = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_win), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
  gtk_container_add (GTK_CONTAINER (scrolled_win), filesel->file_list);
  gtk_paned_pack2 (GTK_PANED (paned), scrolled_win, TRUE, TRUE);
  gtk_widget_show_all (scrolled_win);

  /* The directories had a list of their own, beside the files. They
   * are in the one list now, before the files; the other is kept,
   * and filled as it was, for programs that read it, and is not
   * shown. */
  filesel->dir_list = gtk_clist_new (1);
  gtk_object_ref (GTK_OBJECT (filesel->dir_list));
  gtk_object_sink (GTK_OBJECT (filesel->dir_list));
  gtk_signal_connect (GTK_OBJECT (filesel->dir_list), "select_row",
                      (GtkSignalFunc) gtk_file_selection_dir_button,
                      (gpointer) filesel);

  /* action area for packing buttons into. */
  filesel->action_area = gtk_hbox_new (TRUE, 0);
  gtk_box_pack_start (GTK_BOX (filesel->main_vbox), filesel->action_area,
                      FALSE, FALSE, 0);
  gtk_widget_show (filesel->action_area);

  /* What could not be read is said here, and nothing else. */
  filesel->selection_text = label = gtk_label_new ("");
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (filesel->main_vbox), label, FALSE, FALSE, 0);

  /* Below: the name of the file with OK beside it, and the kinds of
   * file with Cancel beside them. They are three columns, of the
   * labels, the fields and the buttons, each a box that gives its rows
   * the same height, so that the rows run straight across.
   *
   * The buttons are in a box of their own, OK first, as they were in
   * GTK+ 1.2; see above. If a program puts a third in it, it is a row,
   * and stands in the place kept for it under these. */
  data->foot = hbox = gtk_hbox_new (FALSE, 6);
  gtk_box_pack_start (GTK_BOX (filesel->main_vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  data->row_slot = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (filesel->main_vbox), data->row_slot, FALSE, FALSE, 0);

  label_box = gtk_vbox_new (TRUE, 4);
  gtk_box_pack_start (GTK_BOX (hbox), label_box, FALSE, FALSE, 0);
  gtk_widget_show (label_box);

  field_box = gtk_vbox_new (TRUE, 4);
  gtk_box_pack_start (GTK_BOX (hbox), field_box, TRUE, TRUE, 0);
  gtk_widget_show (field_box);

  data->button_box = button_box = gtk_widget_new (file_sel_buttons_get_type (), NULL);
  gtk_box_pack_start (GTK_BOX (hbox), button_box, FALSE, FALSE, 0);
  gtk_widget_show (button_box);

  label = gtk_label_new (_("File name:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (label_box), label, TRUE, TRUE, 0);
  gtk_widget_show (label);

  filesel->selection_entry = gtk_entry_new ();
  gtk_signal_connect (GTK_OBJECT (filesel->selection_entry), "key_press_event",
                      (GtkSignalFunc) gtk_file_selection_key_press, filesel);
  gtk_box_pack_start (GTK_BOX (field_box), filesel->selection_entry, TRUE, FALSE, 0);
  gtk_widget_show (filesel->selection_entry);

  filesel->ok_button = gtk_button_new_with_label (_("OK"));
  GTK_WIDGET_SET_FLAGS (filesel->ok_button, GTK_CAN_DEFAULT);
  gtk_widget_set_usize (filesel->ok_button, 90, -1);
  gtk_box_pack_start (GTK_BOX (button_box), filesel->ok_button, TRUE, FALSE, 0);
  gtk_widget_grab_default (filesel->ok_button);
  gtk_widget_show (filesel->ok_button);

  gtk_signal_connect_object (GTK_OBJECT (filesel->selection_entry), "focus_in_event",
                             (GtkSignalFunc) gtk_widget_grab_default,
                             GTK_OBJECT (filesel->ok_button));
  gtk_signal_connect (GTK_OBJECT (filesel->selection_entry), "activate",
                      GTK_SIGNAL_FUNC (file_sel_entry_activate), filesel);

  label = gtk_label_new (_("Files of type:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (label_box), label, TRUE, TRUE, 0);
  gtk_widget_show (label);

  data->filter_pulldown = gtk_option_menu_new ();
  gtk_box_pack_start (GTK_BOX (field_box), data->filter_pulldown, TRUE, FALSE, 0);
  gtk_widget_show (data->filter_pulldown);
  file_sel_update_filters (filesel);

  filesel->cancel_button = gtk_button_new_with_label (_("Cancel"));
  GTK_WIDGET_SET_FLAGS (filesel->cancel_button, GTK_CAN_DEFAULT);
  gtk_box_pack_start (GTK_BOX (button_box), filesel->cancel_button, TRUE, FALSE, 0);
  gtk_widget_show (filesel->cancel_button);

  file_sel_fill_places (filesel);

  if (!cmpl_state_okay (filesel->cmpl_state))
    {
      gchar err_buf[256];

      sprintf (err_buf, _("Directory unreadable: %s"), cmpl_strerror (cmpl_errno));

      gtk_label_set_text (GTK_LABEL (filesel->selection_text), err_buf);
      gtk_widget_show (filesel->selection_text);
    }
  else
    {
      gtk_file_selection_populate (filesel, "", FALSE);
    }

  gtk_widget_grab_focus (filesel->selection_entry);
}

GtkWidget*
gtk_file_selection_new (const gchar *title)
{
  GtkFileSelection *filesel;

  filesel = gtk_type_new (GTK_TYPE_FILE_SELECTION);
  gtk_window_set_title (GTK_WINDOW (filesel), title);

  return GTK_WIDGET (filesel);
}

void
gtk_file_selection_show_fileop_buttons (GtkFileSelection *filesel)
{
  FileSelData *data;

  g_return_if_fail (filesel != NULL);
  g_return_if_fail (GTK_IS_FILE_SELECTION (filesel));

  data = FILE_SEL_DATA (filesel);
  if (!data)
    return;

  /* Create Dir is a button at the top, and with delete and rename in
   * the menu of the list of files. */
  gtk_widget_show (data->popup_line);
  if (!filesel->fileop_c_dir)
    {
      filesel->fileop_c_dir = gtk_button_new_with_label (_("Create Dir"));
      gtk_signal_connect (GTK_OBJECT (filesel->fileop_c_dir), "clicked",
                          (GtkSignalFunc) gtk_file_selection_create_dir,
                          (gpointer) filesel);
      gtk_box_pack_start (GTK_BOX (filesel->button_area),
                          filesel->fileop_c_dir, TRUE, TRUE, 0);
      gtk_widget_show (filesel->fileop_c_dir);

      data->popup_create = gtk_menu_item_new_with_label (_("Create Dir"));
      gtk_signal_connect (GTK_OBJECT (data->popup_create), "activate",
                          (GtkSignalFunc) gtk_file_selection_create_dir,
                          (gpointer) filesel);
      gtk_menu_insert (GTK_MENU (data->popup), data->popup_create, 0);
      gtk_widget_show (data->popup_create);
    }

  if (!filesel->fileop_ren_file)
    {
      filesel->fileop_ren_file = gtk_menu_item_new_with_label (_("Rename File"));
      gtk_signal_connect (GTK_OBJECT (filesel->fileop_ren_file), "activate",
                          (GtkSignalFunc) gtk_file_selection_rename_file,
                          (gpointer) filesel);
      gtk_menu_insert (GTK_MENU (data->popup), filesel->fileop_ren_file, 1);
      gtk_widget_show (filesel->fileop_ren_file);
    }

  if (!filesel->fileop_del_file)
    {
      filesel->fileop_del_file = gtk_menu_item_new_with_label (_("Delete File"));
      gtk_signal_connect (GTK_OBJECT (filesel->fileop_del_file), "activate",
                          (GtkSignalFunc) gtk_file_selection_delete_file,
                          (gpointer) filesel);
      gtk_menu_insert (GTK_MENU (data->popup), filesel->fileop_del_file, 2);
      gtk_widget_show (filesel->fileop_del_file);
    }

  gtk_widget_queue_resize(GTK_WIDGET(filesel));
}

void
gtk_file_selection_hide_fileop_buttons (GtkFileSelection *filesel)
{
  FileSelData *data;

  g_return_if_fail (filesel != NULL);
  g_return_if_fail (GTK_IS_FILE_SELECTION (filesel));

  data = FILE_SEL_DATA (filesel);
  if (!data)
    return;
  gtk_widget_hide (data->popup_line);

  if (filesel->fileop_ren_file)
    {
      gtk_widget_destroy (filesel->fileop_ren_file);
      filesel->fileop_ren_file = NULL;
    }

  if (filesel->fileop_del_file)
    {
      gtk_widget_destroy (filesel->fileop_del_file);
      filesel->fileop_del_file = NULL;
    }

  if (filesel->fileop_c_dir)
    {
      gtk_widget_destroy (filesel->fileop_c_dir);
      filesel->fileop_c_dir = NULL;
      gtk_widget_destroy (data->popup_create);
      data->popup_create = NULL;
    }
}

void
gtk_file_selection_set_filename (GtkFileSelection *filesel,
                                 const gchar      *filename)
{
  char  buf[MAXPATHLEN];
  const char *name, *last_slash;

  g_return_if_fail (filesel != NULL);
  g_return_if_fail (GTK_IS_FILE_SELECTION (filesel));
  g_return_if_fail (filename != NULL);

  last_slash = strrchr (filename, '/');

  if (!last_slash)
    {
      buf[0] = 0;
      name = filename;
    }
  else
    {
      gint len = MIN (MAXPATHLEN - 1, last_slash - filename + 1);

      strncpy (buf, filename, len);
      buf[len] = 0;

      name = last_slash + 1;
    }

  gtk_file_selection_populate (filesel, buf, FALSE);

  if (filesel->selection_entry)
    gtk_entry_set_text (GTK_ENTRY (filesel->selection_entry), name);
}

gchar*
gtk_file_selection_get_filename (GtkFileSelection *filesel)
{
  static char nothing[2] = "";
  char *text;
  char *filename;

  g_return_val_if_fail (filesel != NULL, nothing);
  g_return_val_if_fail (GTK_IS_FILE_SELECTION (filesel), nothing);

  text = gtk_entry_get_text (GTK_ENTRY (filesel->selection_entry));
  if (text)
    {
      filename = cmpl_completion_fullname (text, filesel->cmpl_state);
      return filename;
    }

  return nothing;
}

void
gtk_file_selection_complete (GtkFileSelection *filesel,
                             const gchar      *pattern)
{
  g_return_if_fail (filesel != NULL);
  g_return_if_fail (GTK_IS_FILE_SELECTION (filesel));
  g_return_if_fail (pattern != NULL);

  if (filesel->selection_entry)
    gtk_entry_set_text (GTK_ENTRY (filesel->selection_entry), pattern);
  gtk_file_selection_populate (filesel, (gchar*) pattern, TRUE);
}

/*
 * The box of the buttons is stood where it belongs as it now is: beside
 * the fields if it is a column, under them at the right if a row.
 */
static void
file_sel_place_buttons (GtkFileSelection *fs)
{
  FileSelData *data = FILE_SEL_DATA (fs);
  GtkWidget *slot;
  gboolean row;

  if (!data || !data->button_box)
    return;

  row = file_sel_buttons_count (GTK_BOX (data->button_box)) > 2;
  slot = row ? data->row_slot : data->foot;
  if (data->button_box->parent == slot)
    return;

  gtk_widget_reparent (data->button_box, slot);
  gtk_box_set_child_packing (GTK_BOX (slot), data->button_box, FALSE, FALSE, 0,
                             row ? GTK_PACK_END : GTK_PACK_START);
  if (row)
    gtk_widget_show (data->row_slot);
  else
    gtk_widget_hide (data->row_slot);
}

static gint
file_sel_place_idle (gpointer fs)
{
  FileSelData *data;

  GDK_THREADS_ENTER ();

  data = FILE_SEL_DATA (fs);
  if (data)
    {
      data->place_idle = 0;
      file_sel_place_buttons (fs);
    }

  GDK_THREADS_LEAVE ();

  return FALSE;
}

/* The box has been asked its size, and may stand where the other
 * arrangement belongs: a program put a button in it, or took one out,
 * with the selector already showing. It cannot be moved while sizes
 * are being asked; it is as soon as nothing else is doing.
 */
static void
file_sel_buttons_misplaced (GtkWidget *box)
{
  GtkWidget *fs = gtk_widget_get_toplevel (box);
  FileSelData *data;

  if (!fs || !GTK_IS_FILE_SELECTION (fs))
    return;
  data = FILE_SEL_DATA (fs);
  if (!data || data->place_idle || data->button_box != box)
    return;

  if (box->parent != (file_sel_buttons_count (GTK_BOX (box)) > 2 ? data->row_slot : data->foot))
    data->place_idle = gtk_idle_add (file_sel_place_idle, fs);
}

/* A program has put its buttons in by the time it shows the selector.
 */
static void
gtk_file_selection_show (GtkWidget *widget)
{
  file_sel_place_buttons (GTK_FILE_SELECTION (widget));

  if (GTK_WIDGET_CLASS (parent_class)->show)
    (* GTK_WIDGET_CLASS (parent_class)->show) (widget);
}

static void
gtk_file_selection_destroy (GtkObject *object)
{
  GtkFileSelection *filesel;
  GList *list;
  HistoryCallbackArg *callback_arg;

  g_return_if_fail (object != NULL);
  g_return_if_fail (GTK_IS_FILE_SELECTION (object));

  filesel = GTK_FILE_SELECTION (object);

  if (filesel->fileop_dialog)
          gtk_widget_destroy (filesel->fileop_dialog);

  if (filesel->history_list)
    {
      list = filesel->history_list;
      while (list)
        {
          callback_arg = list->data;
          g_free (callback_arg->directory);
          g_free (callback_arg);
          list = list->next;
        }
      g_list_free (filesel->history_list);
      filesel->history_list = NULL;
    }

  cmpl_free_state (filesel->cmpl_state);
  filesel->cmpl_state = NULL;

  if (filesel->dir_list)
    {
      gtk_widget_destroy (filesel->dir_list);
      gtk_object_unref (GTK_OBJECT (filesel->dir_list));
      filesel->dir_list = NULL;
    }
  if (FILE_SEL_DATA (filesel))
    {
      FileSelData *data = FILE_SEL_DATA (filesel);
      GList *l;

      if (data->place_idle)
        gtk_idle_remove (data->place_idle);
      data->place_idle = 0;
      gtk_object_remove_data (GTK_OBJECT (filesel), file_sel_key);
      gtk_widget_destroy (data->popup);
      gtk_object_unref (GTK_OBJECT (data->popup));
      gtk_widget_destroy (data->places_popup);
      gtk_object_unref (GTK_OBJECT (data->places_popup));
      for (l = data->filters; l; l = l->next)
        {
          FileFilter *filter = l->data;

          g_free (filter->name);
          g_strfreev (filter->patterns);
          g_free (filter);
        }
      g_list_free (data->filters);
      file_sel_free_strings (&data->places);
      file_sel_free_strings (&data->back);
      file_sel_free_strings (&data->forward);
      g_free (data->current);
      g_free (data->place_target);
      g_free (data);
    }

  if (GTK_OBJECT_CLASS (parent_class)->destroy)
    (* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}

/* Begin file operations callbacks */

static void
gtk_file_selection_fileop_error (GtkFileSelection *fs, gchar *error_message)
{
  GtkWidget *label;
  GtkWidget *vbox;
  GtkWidget *button;
  GtkWidget *dialog;

  g_return_if_fail (error_message != NULL);

  /* main dialog */
  dialog = gtk_dialog_new ();
  /*
  gtk_signal_connect (GTK_OBJECT (dialog), "destroy",
                      (GtkSignalFunc) gtk_file_selection_fileop_destroy,
                      (gpointer) fs);
  */
  gtk_window_set_title (GTK_WINDOW (dialog), _("Error"));
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_MOUSE);

  /* If file dialog is grabbed, make this dialog modal too */
  /* When error dialog is closed, file dialog will be grabbed again */
  if (GTK_WINDOW(fs)->modal)
      gtk_window_set_modal (GTK_WINDOW(dialog), TRUE);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (fs));

  vbox = gtk_vbox_new(FALSE, 0);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), vbox,
                     FALSE, FALSE, 0);
  gtk_widget_show(vbox);

  label = gtk_label_new(error_message);
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
  gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 5);
  gtk_widget_show(label);

  /* yes, we free it */
  g_free (error_message);

  /* close button */
  button = gtk_button_new_with_label (_("Close"));
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
                             (GtkSignalFunc) gtk_widget_destroy,
                             (gpointer) dialog);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area),
                     button, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
  gtk_widget_grab_default(button);
  gtk_widget_show (button);

  gtk_widget_show (dialog);
}

static void
gtk_file_selection_fileop_destroy (GtkWidget *widget, gpointer data)
{
  GtkFileSelection *fs = data;

  g_return_if_fail (fs != NULL);
  g_return_if_fail (GTK_IS_FILE_SELECTION (fs));

  fs->fileop_dialog = NULL;
}


static void
gtk_file_selection_create_dir_confirmed (GtkWidget *widget, gpointer data)
{
  GtkFileSelection *fs = data;
  gchar *dirname;
  gchar *path;
  gchar *full_path;
  gchar *buf;
  CompletionState *cmpl_state;

  g_return_if_fail (fs != NULL);
  g_return_if_fail (GTK_IS_FILE_SELECTION (fs));

  dirname = gtk_entry_get_text (GTK_ENTRY (fs->fileop_entry));
  cmpl_state = (CompletionState*) fs->cmpl_state;
  path = cmpl_reference_position (cmpl_state);

  full_path = g_strconcat (path, "/", dirname, NULL);
  if ( (mkdir (full_path, 0755) < 0) )
    {
      buf = g_strconcat ("Error creating directory \"", dirname, "\":  ",
                         g_strerror(errno), NULL);
      gtk_file_selection_fileop_error (fs, buf);
    }
  g_free (full_path);

  gtk_widget_destroy (fs->fileop_dialog);
  gtk_file_selection_populate (fs, "", FALSE);
}

static void
gtk_file_selection_create_dir (GtkWidget *widget, gpointer data)
{
  GtkFileSelection *fs = data;
  GtkWidget *label;
  GtkWidget *dialog;
  GtkWidget *vbox;
  GtkWidget *button;

  g_return_if_fail (fs != NULL);
  g_return_if_fail (GTK_IS_FILE_SELECTION (fs));

  if (fs->fileop_dialog)
          return;

  /* main dialog */
  fs->fileop_dialog = dialog = gtk_dialog_new ();
  gtk_signal_connect (GTK_OBJECT (dialog), "destroy",
                      (GtkSignalFunc) gtk_file_selection_fileop_destroy,
                      (gpointer) fs);
  gtk_window_set_title (GTK_WINDOW (dialog), _("Create Directory"));
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_MOUSE);

  /* If file dialog is grabbed, grab option dialog */
  /* When option dialog is closed, file dialog will be grabbed again */
  if (GTK_WINDOW(fs)->modal)
      gtk_window_set_modal (GTK_WINDOW(dialog), TRUE);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (fs));

  vbox = gtk_vbox_new(FALSE, 0);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), vbox,
                     FALSE, FALSE, 0);
  gtk_widget_show(vbox);

  label = gtk_label_new(_("Directory name:"));
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
  gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 5);
  gtk_widget_show(label);

  /*  The directory entry widget  */
  fs->fileop_entry = gtk_entry_new ();
  gtk_box_pack_start (GTK_BOX (vbox), fs->fileop_entry,
                      TRUE, TRUE, 5);
  GTK_WIDGET_SET_FLAGS(fs->fileop_entry, GTK_CAN_DEFAULT);
  gtk_signal_connect (GTK_OBJECT (fs->fileop_entry), "activate",
                      (GtkSignalFunc) gtk_file_selection_create_dir_confirmed,
                      (gpointer) fs);
  gtk_widget_show (fs->fileop_entry);
  gtk_widget_grab_focus (fs->fileop_entry);

  /* buttons */
  button = gtk_button_new_with_label (_("Create"));
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
                      (GtkSignalFunc) gtk_file_selection_create_dir_confirmed,
                      (gpointer) fs);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area),
                     button, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
  gtk_widget_show(button);

  button = gtk_button_new_with_label (_("Cancel"));
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
                             (GtkSignalFunc) gtk_widget_destroy,
                             (gpointer) dialog);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area),
                     button, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
  gtk_widget_grab_default(button);
  gtk_widget_show (button);

  gtk_widget_show (dialog);
}

static void
gtk_file_selection_delete_file_confirmed (GtkWidget *widget, gpointer data)
{
  GtkFileSelection *fs = data;
  CompletionState *cmpl_state;
  gchar *path;
  gchar *full_path;
  gchar *buf;

  g_return_if_fail (fs != NULL);
  g_return_if_fail (GTK_IS_FILE_SELECTION (fs));

  cmpl_state = (CompletionState*) fs->cmpl_state;
  path = cmpl_reference_position (cmpl_state);

  full_path = g_strconcat (path, "/", fs->fileop_file, NULL);
  if ( (unlink (full_path) < 0) )
    {
      buf = g_strconcat ("Error deleting file \"", fs->fileop_file, "\":  ",
                         g_strerror(errno), NULL);
      gtk_file_selection_fileop_error (fs, buf);
    }
  g_free (full_path);

  gtk_widget_destroy (fs->fileop_dialog);
  gtk_file_selection_populate (fs, "", FALSE);
}

static void
gtk_file_selection_delete_file (GtkWidget *widget, gpointer data)
{
  GtkFileSelection *fs = data;
  GtkWidget *label;
  GtkWidget *vbox;
  GtkWidget *button;
  GtkWidget *dialog;
  gchar *filename;
  gchar *buf;

  g_return_if_fail (fs != NULL);
  g_return_if_fail (GTK_IS_FILE_SELECTION (fs));

  if (fs->fileop_dialog)
          return;

  filename = gtk_entry_get_text (GTK_ENTRY (fs->selection_entry));
  if (strlen(filename) < 1)
          return;

  fs->fileop_file = filename;

  /* main dialog */
  fs->fileop_dialog = dialog = gtk_dialog_new ();
  gtk_signal_connect (GTK_OBJECT (dialog), "destroy",
                      (GtkSignalFunc) gtk_file_selection_fileop_destroy,
                      (gpointer) fs);
  gtk_window_set_title (GTK_WINDOW (dialog), _("Delete File"));
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_MOUSE);

  /* If file dialog is grabbed, grab option dialog */
  /* When option dialog is closed, file dialog will be grabbed again */
  if (GTK_WINDOW(fs)->modal)
      gtk_window_set_modal (GTK_WINDOW(dialog), TRUE);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (fs));

  vbox = gtk_vbox_new(FALSE, 0);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), vbox,
                     FALSE, FALSE, 0);
  gtk_widget_show(vbox);

  buf = g_strconcat ("Really delete file \"", filename, "\" ?", NULL);
  label = gtk_label_new(buf);
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
  gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 5);
  gtk_widget_show(label);
  g_free(buf);

  /* buttons */
  button = gtk_button_new_with_label (_("Delete"));
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
                      (GtkSignalFunc) gtk_file_selection_delete_file_confirmed,
                      (gpointer) fs);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area),
                     button, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
  gtk_widget_show(button);

  button = gtk_button_new_with_label (_("Cancel"));
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
                             (GtkSignalFunc) gtk_widget_destroy,
                             (gpointer) dialog);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area),
                     button, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
  gtk_widget_grab_default(button);
  gtk_widget_show (button);

  gtk_widget_show (dialog);

}

static void
gtk_file_selection_rename_file_confirmed (GtkWidget *widget, gpointer data)
{
  GtkFileSelection *fs = data;
  gchar *buf;
  gchar *file;
  gchar *path;
  gchar *new_filename;
  gchar *old_filename;
  CompletionState *cmpl_state;

  g_return_if_fail (fs != NULL);
  g_return_if_fail (GTK_IS_FILE_SELECTION (fs));

  file = gtk_entry_get_text (GTK_ENTRY (fs->fileop_entry));
  cmpl_state = (CompletionState*) fs->cmpl_state;
  path = cmpl_reference_position (cmpl_state);

  new_filename = g_strconcat (path, "/", file, NULL);
  old_filename = g_strconcat (path, "/", fs->fileop_file, NULL);

  if ( (rename (old_filename, new_filename)) < 0)
    {
      buf = g_strconcat ("Error renaming file \"", file, "\":  ",
                         g_strerror(errno), NULL);
      gtk_file_selection_fileop_error (fs, buf);
    }
  g_free (new_filename);
  g_free (old_filename);

  gtk_widget_destroy (fs->fileop_dialog);
  gtk_file_selection_populate (fs, "", FALSE);
}

static void
gtk_file_selection_rename_file (GtkWidget *widget, gpointer data)
{
  GtkFileSelection *fs = data;
  GtkWidget *label;
  GtkWidget *dialog;
  GtkWidget *vbox;
  GtkWidget *button;
  gchar *buf;

  g_return_if_fail (fs != NULL);
  g_return_if_fail (GTK_IS_FILE_SELECTION (fs));

  if (fs->fileop_dialog)
          return;

  fs->fileop_file = gtk_entry_get_text (GTK_ENTRY (fs->selection_entry));
  if (strlen(fs->fileop_file) < 1)
          return;

  /* main dialog */
  fs->fileop_dialog = dialog = gtk_dialog_new ();
  gtk_signal_connect (GTK_OBJECT (dialog), "destroy",
                      (GtkSignalFunc) gtk_file_selection_fileop_destroy,
                      (gpointer) fs);
  gtk_window_set_title (GTK_WINDOW (dialog), _("Rename File"));
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_MOUSE);

  /* If file dialog is grabbed, grab option dialog */
  /* When option dialog  closed, file dialog will be grabbed again */
  if (GTK_WINDOW(fs)->modal)
    gtk_window_set_modal (GTK_WINDOW(dialog), TRUE);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (fs));

  vbox = gtk_vbox_new(FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER(vbox), 8);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), vbox,
                     FALSE, FALSE, 0);
  gtk_widget_show(vbox);

  buf = g_strconcat ("Rename file \"", fs->fileop_file, "\" to:", NULL);
  label = gtk_label_new(buf);
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
  gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 5);
  gtk_widget_show(label);
  g_free(buf);

  /* New filename entry */
  fs->fileop_entry = gtk_entry_new ();
  gtk_box_pack_start (GTK_BOX (vbox), fs->fileop_entry,
                      TRUE, TRUE, 5);
  GTK_WIDGET_SET_FLAGS(fs->fileop_entry, GTK_CAN_DEFAULT);
  gtk_widget_show (fs->fileop_entry);

  gtk_entry_set_text (GTK_ENTRY (fs->fileop_entry), fs->fileop_file);
  gtk_editable_select_region (GTK_EDITABLE (fs->fileop_entry),
                              0, strlen (fs->fileop_file));
  gtk_signal_connect (GTK_OBJECT (fs->fileop_entry), "activate",
                      (GtkSignalFunc) gtk_file_selection_rename_file_confirmed,
                      (gpointer) fs);
  gtk_widget_grab_focus (fs->fileop_entry);

  /* buttons */
  button = gtk_button_new_with_label (_("Rename"));
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
                      (GtkSignalFunc) gtk_file_selection_rename_file_confirmed,
                      (gpointer) fs);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area),
                     button, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
  gtk_widget_show(button);

  button = gtk_button_new_with_label (_("Cancel"));
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
                             (GtkSignalFunc) gtk_widget_destroy,
                             (gpointer) dialog);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area),
                     button, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
  gtk_widget_grab_default(button);
  gtk_widget_show (button);

  gtk_widget_show (dialog);
}


static gint
gtk_file_selection_key_press (GtkWidget   *widget,
                              GdkEventKey *event,
                              gpointer     user_data)
{
  GtkFileSelection *fs;
  char *text;

  g_return_val_if_fail (widget != NULL, FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  if (event->keyval == GDK_Tab)
    {
      fs = GTK_FILE_SELECTION (user_data);
      text = gtk_entry_get_text (GTK_ENTRY (fs->selection_entry));

      text = g_strdup (text);

      gtk_file_selection_populate (fs, text, TRUE);

      g_free (text);

      gtk_signal_emit_stop_by_name (GTK_OBJECT (widget), "key_press_event");

      return TRUE;
    }

  return FALSE;
}


static void
gtk_file_selection_history_callback (GtkWidget *widget, gpointer data)
{
  GtkFileSelection *fs = data;
  HistoryCallbackArg *callback_arg;
  GList *list;

  g_return_if_fail (fs != NULL);
  g_return_if_fail (GTK_IS_FILE_SELECTION (fs));

  list = fs->history_list;

  while (list) {
    callback_arg = list->data;

    if (callback_arg->menu_item == widget)
      {
        gtk_file_selection_populate (fs, callback_arg->directory, FALSE);
        break;
      }

    list = list->next;
  }
}

static void
gtk_file_selection_update_history_menu (GtkFileSelection *fs,
                                        gchar *current_directory)
{
  HistoryCallbackArg *callback_arg;
  GtkWidget *menu_item;
  GList *list;
  gchar *current_dir;
  gint dir_len;
  gint i;

  g_return_if_fail (fs != NULL);
  g_return_if_fail (GTK_IS_FILE_SELECTION (fs));
  g_return_if_fail (current_directory != NULL);

  list = fs->history_list;

  if (fs->history_menu)
    {
      while (list) {
        callback_arg = list->data;
        g_free (callback_arg->directory);
        g_free (callback_arg);
        list = list->next;
      }
      g_list_free (fs->history_list);
      fs->history_list = NULL;

      gtk_widget_destroy (fs->history_menu);
    }

  fs->history_menu = gtk_menu_new();

  current_dir = g_strdup (current_directory);

  dir_len = strlen (current_dir);

  for (i = dir_len; i >= 0; i--)
    {
      /* the i == dir_len is to catch the full path for the first
       * entry. */
      if ( (current_dir[i] == '/') || (i == dir_len))
        {
          /* another small hack to catch the full path */
          if (i != dir_len)
                  current_dir[i + 1] = '\0';
          menu_item = gtk_menu_item_new_with_label (current_dir);

          callback_arg = g_new (HistoryCallbackArg, 1);
          callback_arg->menu_item = menu_item;

          /* since the autocompletion gets confused if you don't
           * supply a trailing '/' on a dir entry, set the full
           * (current) path to "" which just refreshes the filesel */
          if (dir_len == i) {
            callback_arg->directory = g_strdup ("");
          } else {
            callback_arg->directory = g_strdup (current_dir);
          }

          fs->history_list = g_list_append (fs->history_list, callback_arg);

          gtk_signal_connect (GTK_OBJECT (menu_item), "activate",
                              (GtkSignalFunc) gtk_file_selection_history_callback,
                              (gpointer) fs);
          gtk_menu_append (GTK_MENU (fs->history_menu), menu_item);
          gtk_widget_show (menu_item);
        }
    }

  gtk_option_menu_set_menu (GTK_OPTION_MENU (fs->history_pulldown),
                            fs->history_menu);
  g_free (current_dir);
}

static void
gtk_file_selection_file_button (GtkWidget *widget,
                               gint row,
                               gint column,
                               GdkEventButton *bevent,
                               gpointer user_data)
{
  GtkFileSelection *fs = NULL;
  gchar *filename, *temp = NULL;

  g_return_if_fail (GTK_IS_CLIST (widget));

  fs = user_data;
  g_return_if_fail (fs != NULL);
  g_return_if_fail (GTK_IS_FILE_SELECTION (fs));

  gtk_clist_get_text (GTK_CLIST (fs->file_list), row, 0, &temp);
  filename = g_strdup (temp);

  if (filename)
    {
      FileRow *file = gtk_clist_get_row_data (GTK_CLIST (fs->file_list), row);

      if (bevent)
        switch (bevent->type)
          {
          case GDK_2BUTTON_PRESS:
            /* Into a directory, and OK for a file. */
            if (file && file->is_dir)
              gtk_file_selection_populate (fs, filename, FALSE);
            else
              gtk_button_clicked (GTK_BUTTON (fs->ok_button));
            break;

          default:
            gtk_entry_set_text (GTK_ENTRY (fs->selection_entry), filename);
            break;
          }
      else
        gtk_entry_set_text (GTK_ENTRY (fs->selection_entry), filename);

      g_free (filename);
    }
}

static void
gtk_file_selection_dir_button (GtkWidget *widget,
                               gint row,
                               gint column,
                               GdkEventButton *bevent,
                               gpointer user_data)
{
  GtkFileSelection *fs = NULL;
  gchar *filename, *temp = NULL;

  g_return_if_fail (GTK_IS_CLIST (widget));

  fs = GTK_FILE_SELECTION (user_data);
  g_return_if_fail (fs != NULL);
  g_return_if_fail (GTK_IS_FILE_SELECTION (fs));

  gtk_clist_get_text (GTK_CLIST (fs->dir_list), row, 0, &temp);
  filename = g_strdup (temp);

  if (filename)
    {
      if (bevent)
        switch (bevent->type)
          {
          case GDK_2BUTTON_PRESS:
            gtk_file_selection_populate (fs, filename, FALSE);
            break;

          default:
            gtk_entry_set_text (GTK_ENTRY (fs->selection_entry), filename);
            break;
          }
      else
        gtk_entry_set_text (GTK_ENTRY (fs->selection_entry), filename);

      g_free (filename);
    }
}

static void
gtk_file_selection_populate (GtkFileSelection *fs,
                             gchar            *rel_path,
                             gint              try_complete)
{
  CompletionState *cmpl_state;
  PossibleCompletion* poss;
  gchar* filename;
  gchar* rem_path = rel_path;
  gchar* sel_text;
  gchar* text[3];
  gint did_recurse = FALSE;
  gint selection_index = -1;
  gint dir_list_width;
  FileSelData *data;

  g_return_if_fail (fs != NULL);
  g_return_if_fail (GTK_IS_FILE_SELECTION (fs));

  data = FILE_SEL_DATA (fs);
  cmpl_show_hidden = data && data->show_hidden;
  cmpl_state = (CompletionState*) fs->cmpl_state;
  poss = cmpl_completion_matches (rel_path, &rem_path, cmpl_state);

  if (!cmpl_state_okay (cmpl_state))
    {
      /* Something went wrong. */
      gtk_file_selection_abort (fs);
      return;
    }

  g_assert (cmpl_state->reference_dir);

  gtk_clist_freeze (GTK_CLIST (fs->dir_list));
  gtk_clist_clear (GTK_CLIST (fs->dir_list));
  gtk_clist_freeze (GTK_CLIST (fs->file_list));
  gtk_clist_clear (GTK_CLIST (fs->file_list));

  /* Set the dir_list to include ./ and ../ */
  text[1] = NULL;
  text[0] = "./";
  gtk_clist_append (GTK_CLIST (fs->dir_list), text);

  text[0] = "../";
  gtk_clist_append (GTK_CLIST (fs->dir_list), text);

  /*reset the max widths of the lists*/
  dir_list_width = gdk_string_width(fs->dir_list->style->font,"../");
  gtk_clist_set_column_width(GTK_CLIST(fs->dir_list),0,dir_list_width);

  while (poss)
    {
      if (cmpl_is_a_completion (poss))
        {
          gboolean is_dir = cmpl_is_directory (poss);

          filename = cmpl_this_completion (poss);

          if (is_dir && (strcmp (filename, "./") == 0 || strcmp (filename, "../") == 0))
            ;
          else if (is_dir || file_sel_filter_matches (data ? data->filter : NULL, filename))
            {
              gchar size[32], date[32], *path;
              FileRow *file = g_new0 (FileRow, 1);
              struct stat st;
              gint row;

              if (is_dir)
                {
                  /* In the list of old, as well. */
                  int width = gdk_string_width(fs->dir_list->style->font,
                                               filename);

                  text[0] = filename;
                  text[1] = NULL;
                  gtk_clist_append (GTK_CLIST (fs->dir_list), text);
                  if(width > dir_list_width)
                    {
                      dir_list_width = width;
                      gtk_clist_set_column_width(GTK_CLIST(fs->dir_list),0,
                                                 width);
                    }
                }

              /* How big it is and when it was last written. */
              size[0] = date[0] = 0;
              file->is_dir = is_dir;
              path = g_strconcat (cmpl_reference_position (cmpl_state), "/", filename, NULL);
              if (stat (path, &st) == 0)
                {
                  struct tm *when = localtime (&st.st_mtime);

                  file->size = is_dir ? 0 : st.st_size;
                  file->mtime = st.st_mtime;
                  if (!is_dir)
                    file_sel_size_text (file->size, size, sizeof size);
                  if (when)
                    strftime (date, sizeof date, "%Y-%m-%d %H:%M", when);
                }
              g_free (path);

              text[0] = filename;
              text[1] = size;
              text[2] = date;
              row = gtk_clist_append (GTK_CLIST (fs->file_list), text);
              gtk_clist_set_row_data_full (GTK_CLIST (fs->file_list), row, file, g_free);
            }
        }

      poss = cmpl_next_completion (cmpl_state);
    }

  gtk_clist_sort (GTK_CLIST (fs->file_list));
  gtk_clist_thaw (GTK_CLIST (fs->dir_list));
  gtk_clist_thaw (GTK_CLIST (fs->file_list));

  /* File lists are set. */

  g_assert (cmpl_state->reference_dir);

  if (try_complete)
    {

      /* User is trying to complete filenames, so advance the user's input
       * string to the updated_text, which is the common leading substring
       * of all possible completions, and if its a directory attempt
       * attempt completions in it. */

      if (cmpl_updated_text (cmpl_state)[0])
        {

          if (cmpl_updated_dir (cmpl_state))
            {
              gchar* dir_name = g_strdup (cmpl_updated_text (cmpl_state));

              did_recurse = TRUE;

              gtk_file_selection_populate (fs, dir_name, TRUE);

              g_free (dir_name);
            }
          else
            {
              if (fs->selection_entry)
                      gtk_entry_set_text (GTK_ENTRY (fs->selection_entry),
                                          cmpl_updated_text (cmpl_state));
            }
        }
      else
        {
          selection_index = cmpl_last_valid_char (cmpl_state) -
                            (strlen (rel_path) - strlen (rem_path));
          if (fs->selection_entry)
            gtk_entry_set_text (GTK_ENTRY (fs->selection_entry), rem_path);
        }
    }
  else
    {
      if (fs->selection_entry)
        gtk_entry_set_text (GTK_ENTRY (fs->selection_entry), "");
    }

  if (!did_recurse)
    {
      if (fs->selection_entry)
        gtk_entry_set_position (GTK_ENTRY (fs->selection_entry), selection_index);

      if (fs->selection_entry)
        {
          /* Where it is has its place at the top now. The label has
           * it still, for a program that reads it, and is not shown
           * unless there is something wrong to say. */
          sel_text = g_strconcat (_("Selection: "),
                                  cmpl_reference_position (cmpl_state),
                                  NULL);

          gtk_label_set_text (GTK_LABEL (fs->selection_text), sel_text);
          gtk_widget_hide (fs->selection_text);
          g_free (sel_text);
        }

      file_sel_moved (fs, cmpl_reference_position (cmpl_state));

      if (fs->history_pulldown)
        {
          gtk_file_selection_update_history_menu (fs, cmpl_reference_position (cmpl_state));
        }

    }
}

static void
gtk_file_selection_abort (GtkFileSelection *fs)
{
  gchar err_buf[256];

  sprintf (err_buf, _("Directory unreadable: %s"), cmpl_strerror (cmpl_errno));

  /*  BEEP gdk_beep();  */

  if (fs->selection_entry)
    {
      gtk_label_set_text (GTK_LABEL (fs->selection_text), err_buf);
      gtk_widget_show (fs->selection_text);
    }
}

/**********************************************************************/
/*                        External Interface                          */
/**********************************************************************/

/* The four completion state selectors
 */
static gchar*
cmpl_updated_text (CompletionState* cmpl_state)
{
  return cmpl_state->updated_text;
}

static gint
cmpl_updated_dir (CompletionState* cmpl_state)
{
  return cmpl_state->re_complete;
}

static gchar*
cmpl_reference_position (CompletionState* cmpl_state)
{
  return cmpl_state->reference_dir->fullname;
}

static gint
cmpl_last_valid_char (CompletionState* cmpl_state)
{
  return cmpl_state->last_valid_char;
}

static gchar*
cmpl_completion_fullname (gchar* text, CompletionState* cmpl_state)
{
  static char nothing[2] = "";

  if (!cmpl_state_okay (cmpl_state))
    {
      return nothing;
    }
  else if (text[0] == '/')
    {
      strcpy (cmpl_state->updated_text, text);
    }
  else if (text[0] == '~')
    {
      CompletionDir* dir;
      char* slash;

      dir = open_user_dir (text, cmpl_state);

      if (!dir)
        {
          /* spencer says just return ~something, so
           * for now just do it. */
          strcpy (cmpl_state->updated_text, text);
        }
      else
        {

          strcpy (cmpl_state->updated_text, dir->fullname);

          slash = strchr (text, '/');

          if (slash)
            strcat (cmpl_state->updated_text, slash);
        }
    }
  else
    {
      strcpy (cmpl_state->updated_text, cmpl_state->reference_dir->fullname);
      if (strcmp (cmpl_state->reference_dir->fullname, "/") != 0)
        strcat (cmpl_state->updated_text, "/");
      strcat (cmpl_state->updated_text, text);
    }

  return cmpl_state->updated_text;
}

/* The three completion selectors
 */
static gchar*
cmpl_this_completion (PossibleCompletion* pc)
{
  return pc->text;
}

static gint
cmpl_is_directory (PossibleCompletion* pc)
{
  return pc->is_directory;
}

static gint
cmpl_is_a_completion (PossibleCompletion* pc)
{
  return pc->is_a_completion;
}

/**********************************************************************/
/*                       Construction, deletion                       */
/**********************************************************************/

static CompletionState*
cmpl_init_state (void)
{
  gchar getcwd_buf[2*MAXPATHLEN];
  CompletionState *new_state;

  new_state = g_new (CompletionState, 1);

  if (!getcwd (getcwd_buf, MAXPATHLEN))
    {
      /* Oh joy, we can't get the current directory. Um..., we should have
       * a root directory, right? Right? (Probably not portable to non-Unix)
       */
      strcpy (getcwd_buf, "/");
    }

tryagain:

  new_state->reference_dir = NULL;
  new_state->completion_dir = NULL;
  new_state->active_completion_dir = NULL;
  new_state->directory_storage = NULL;
  new_state->directory_sent_storage = NULL;
  new_state->last_valid_char = 0;
  new_state->updated_text = g_new (gchar, MAXPATHLEN);
  new_state->updated_text_alloc = MAXPATHLEN;
  new_state->the_completion.text = g_new (gchar, MAXPATHLEN);
  new_state->the_completion.text_alloc = MAXPATHLEN;
  new_state->user_dir_name_buffer = NULL;
  new_state->user_directories = NULL;

  new_state->reference_dir =  open_dir (getcwd_buf, new_state);

  if (!new_state->reference_dir)
    {
      /* Directories changing from underneath us, grumble */
      strcpy (getcwd_buf, "/");
      goto tryagain;
    }

  return new_state;
}

static void
cmpl_free_dir_list(GList* dp0)
{
  GList *dp = dp0;

  while (dp) {
    free_dir (dp->data);
    dp = dp->next;
  }

  g_list_free(dp0);
}

static void
cmpl_free_dir_sent_list(GList* dp0)
{
  GList *dp = dp0;

  while (dp) {
    free_dir_sent (dp->data);
    dp = dp->next;
  }

  g_list_free(dp0);
}

static void
cmpl_free_state (CompletionState* cmpl_state)
{
  cmpl_free_dir_list (cmpl_state->directory_storage);
  cmpl_free_dir_sent_list (cmpl_state->directory_sent_storage);

  if (cmpl_state->user_dir_name_buffer)
    g_free (cmpl_state->user_dir_name_buffer);
  if (cmpl_state->user_directories)
    g_free (cmpl_state->user_directories);
  if (cmpl_state->the_completion.text)
    g_free (cmpl_state->the_completion.text);
  if (cmpl_state->updated_text)
    g_free (cmpl_state->updated_text);

  g_free (cmpl_state);
}

static void
free_dir(CompletionDir* dir)
{
  g_free(dir->fullname);
  g_free(dir);
}

static void
free_dir_sent(CompletionDirSent* sent)
{
  g_free(sent->name_buffer);
  g_free(sent->entries);
  g_free(sent);
}

static void
prune_memory_usage(CompletionState *cmpl_state)
{
  GList* cdsl = cmpl_state->directory_sent_storage;
  GList* cdl = cmpl_state->directory_storage;
  GList* cdl0 = cdl;
  gint len = 0;

  for(; cdsl && len < CMPL_DIRECTORY_CACHE_SIZE; len += 1)
    cdsl = cdsl->next;

  if (cdsl) {
    cmpl_free_dir_sent_list(cdsl->next);
    cdsl->next = NULL;
  }

  cmpl_state->directory_storage = NULL;
  while (cdl) {
    if (cdl->data == cmpl_state->reference_dir)
      cmpl_state->directory_storage = g_list_prepend(NULL, cdl->data);
    else
      free_dir (cdl->data);
    cdl = cdl->next;
  }

  g_list_free(cdl0);
}

/**********************************************************************/
/*                        The main entrances.                         */
/**********************************************************************/

static PossibleCompletion*
cmpl_completion_matches (gchar* text_to_complete,
                         gchar** remaining_text,
                         CompletionState* cmpl_state)
{
  gchar* first_slash;
  PossibleCompletion *poss;

  prune_memory_usage(cmpl_state);

  g_assert (text_to_complete != NULL);

  cmpl_state->user_completion_index = -1;
  cmpl_state->last_completion_text = text_to_complete;
  cmpl_state->the_completion.text[0] = 0;
  cmpl_state->last_valid_char = 0;
  cmpl_state->updated_text_len = -1;
  cmpl_state->updated_text[0] = 0;
  cmpl_state->re_complete = FALSE;

  first_slash = strchr (text_to_complete, '/');

  if (text_to_complete[0] == '~' && !first_slash)
    {
      /* Text starts with ~ and there is no slash, show all the
       * home directory completions.
       */
      poss = attempt_homedir_completion (text_to_complete, cmpl_state);

      update_cmpl(poss, cmpl_state);

      return poss;
    }

  cmpl_state->reference_dir =
    open_ref_dir (text_to_complete, remaining_text, cmpl_state);

  if(!cmpl_state->reference_dir)
    return NULL;

  cmpl_state->completion_dir =
    find_completion_dir (*remaining_text, remaining_text, cmpl_state);

  cmpl_state->last_valid_char = *remaining_text - text_to_complete;

  if(!cmpl_state->completion_dir)
    return NULL;

  cmpl_state->completion_dir->cmpl_index = -1;
  cmpl_state->completion_dir->cmpl_parent = NULL;
  cmpl_state->completion_dir->cmpl_text = *remaining_text;

  cmpl_state->active_completion_dir = cmpl_state->completion_dir;

  cmpl_state->reference_dir = cmpl_state->completion_dir;

  poss = attempt_file_completion(cmpl_state);

  update_cmpl(poss, cmpl_state);

  return poss;
}

static PossibleCompletion*
cmpl_next_completion (CompletionState* cmpl_state)
{
  PossibleCompletion* poss = NULL;

  cmpl_state->the_completion.text[0] = 0;

  if(cmpl_state->user_completion_index >= 0)
    poss = attempt_homedir_completion(cmpl_state->last_completion_text, cmpl_state);
  else
    poss = attempt_file_completion(cmpl_state);

  update_cmpl(poss, cmpl_state);

  return poss;
}

/**********************************************************************/
/*                       Directory Operations                         */
/**********************************************************************/

/* Open the directory where completion will begin from, if possible. */
static CompletionDir*
open_ref_dir(gchar* text_to_complete,
             gchar** remaining_text,
             CompletionState* cmpl_state)
{
  gchar* first_slash;
  CompletionDir *new_dir;

  first_slash = strchr(text_to_complete, '/');

  if (text_to_complete[0] == '~')
    {
      new_dir = open_user_dir(text_to_complete, cmpl_state);

      if(new_dir)
        {
          if(first_slash)
            *remaining_text = first_slash + 1;
          else
            *remaining_text = text_to_complete + strlen(text_to_complete);
        }
      else
        {
          return NULL;
        }
    }
  else if (text_to_complete[0] == '/' || !cmpl_state->reference_dir)
    {
      gchar *tmp = g_strdup(text_to_complete);
      gchar *p;

      p = tmp;
      while (*p && *p != '*' && *p != '?')
        p++;

      *p = '\0';
      p = strrchr(tmp, '/');
      if (p)
        {
          if (p == tmp)
            p++;

          *p = '\0';

          new_dir = open_dir(tmp, cmpl_state);

          if(new_dir)
            *remaining_text = text_to_complete +
              ((p == tmp + 1) ? (p - tmp) : (p + 1 - tmp));
        }
      else
        {
          /* If no possible candidates, use the cwd */
          gchar *curdir = g_get_current_dir ();

          new_dir = open_dir(curdir, cmpl_state);

          if (new_dir)
            *remaining_text = text_to_complete;

          g_free (curdir);
        }

      g_free (tmp);
    }
  else
    {
      *remaining_text = text_to_complete;

      new_dir = open_dir(cmpl_state->reference_dir->fullname, cmpl_state);
    }

  if(new_dir)
    {
      new_dir->cmpl_index = -1;
      new_dir->cmpl_parent = NULL;
    }

  return new_dir;
}

/* open a directory by user name */
static CompletionDir*
open_user_dir(gchar* text_to_complete,
              CompletionState *cmpl_state)
{
  gchar *first_slash;
  gint cmp_len;

  g_assert(text_to_complete && text_to_complete[0] == '~');

  first_slash = strchr(text_to_complete, '/');

  if (first_slash)
    cmp_len = first_slash - text_to_complete - 1;
  else
    cmp_len = strlen(text_to_complete + 1);

  if(!cmp_len)
    {
      /* ~/ */
      gchar *homedir = g_get_home_dir ();

      if (homedir)
        return open_dir(homedir, cmpl_state);
      else
        return NULL;
    }
  else
    {
      /* ~user/ */
      char* copy = g_new(char, cmp_len + 1);
      struct passwd *pwd;
      strncpy(copy, text_to_complete + 1, cmp_len);
      copy[cmp_len] = 0;
      pwd = getpwnam(copy);
      g_free(copy);
      if (!pwd)
        {
          cmpl_errno = errno;
          return NULL;
        }

      return open_dir(pwd->pw_dir, cmpl_state);
    }
}

/* open a directory relative the the current relative directory */
static CompletionDir*
open_relative_dir(gchar* dir_name,
                  CompletionDir* dir,
                  CompletionState *cmpl_state)
{
  gchar path_buf[2*MAXPATHLEN];

  if(dir->fullname_len + strlen(dir_name) + 2 >= MAXPATHLEN)
    {
      cmpl_errno = CMPL_ERRNO_TOO_LONG;
      return NULL;
    }

  strcpy(path_buf, dir->fullname);

  if(dir->fullname_len > 1)
    {
      path_buf[dir->fullname_len] = '/';
      strcpy(path_buf + dir->fullname_len + 1, dir_name);
    }
  else
    {
      strcpy(path_buf + dir->fullname_len, dir_name);
    }

  return open_dir(path_buf, cmpl_state);
}

/* after the cache lookup fails, really open a new directory */
static CompletionDirSent*
open_new_dir(gchar* dir_name, struct stat* sbuf, gboolean stat_subdirs)
{
  CompletionDirSent* sent;
  DIR* directory;
  gchar *buffer_ptr;
  struct dirent *dirent_ptr;
  gint buffer_size = 0;
  gint entry_count = 0;
  gint i;
  struct stat ent_sbuf;
  char path_buf[MAXPATHLEN*2];
  gint path_buf_len;

  sent = g_new(CompletionDirSent, 1);
  sent->mtime = sbuf->st_mtime;
  sent->inode = sbuf->st_ino;
  sent->device = sbuf->st_dev;

  path_buf_len = strlen(dir_name);

  if (path_buf_len > MAXPATHLEN)
    {
      cmpl_errno = CMPL_ERRNO_TOO_LONG;
      return NULL;
    }

  strcpy(path_buf, dir_name);

  directory = opendir(dir_name);

  if(!directory)
    {
      cmpl_errno = errno;
      return NULL;
    }

  while((dirent_ptr = readdir(directory)) != NULL)
    {
      int entry_len = strlen(dirent_ptr->d_name);
      buffer_size += entry_len + 1;
      entry_count += 1;

      if(path_buf_len + entry_len + 2 >= MAXPATHLEN)
        {
          cmpl_errno = CMPL_ERRNO_TOO_LONG;
          closedir(directory);
          return NULL;
        }
    }

  sent->name_buffer = g_new(gchar, buffer_size);
  sent->entries = g_new(CompletionDirEntry, entry_count);
  sent->entry_count = entry_count;

  buffer_ptr = sent->name_buffer;

  rewinddir(directory);

  for(i = 0; i < entry_count; i += 1)
    {
      dirent_ptr = readdir(directory);

      if(!dirent_ptr)
        {
          cmpl_errno = errno;
          closedir(directory);
          return NULL;
        }

      strcpy(buffer_ptr, dirent_ptr->d_name);
      sent->entries[i].entry_name = buffer_ptr;
      buffer_ptr += strlen(dirent_ptr->d_name);
      *buffer_ptr = 0;
      buffer_ptr += 1;

      path_buf[path_buf_len] = '/';
      strcpy(path_buf + path_buf_len + 1, dirent_ptr->d_name);

      if (stat_subdirs)
        {
          if(stat(path_buf, &ent_sbuf) >= 0 && S_ISDIR(ent_sbuf.st_mode))
            sent->entries[i].is_dir = 1;
          else
            /* stat may fail, and we don't mind, since it could be a
             * dangling symlink. */
            sent->entries[i].is_dir = 0;
        }
      else
        sent->entries[i].is_dir = 1;
    }

  qsort(sent->entries, sent->entry_count, sizeof(CompletionDirEntry), compare_cmpl_dir);

  closedir(directory);

  return sent;
}

static gboolean
check_dir(gchar *dir_name, struct stat *result, gboolean *stat_subdirs)
{
  /* A list of directories that we know only contain other directories.
   * Trying to stat every file in these directories would be very
   * expensive.
   */

  static struct {
    gchar *name;
    gboolean present;
    struct stat statbuf;
  } no_stat_dirs[] = {
    { "/afs", FALSE, { 0 } },
    { "/net", FALSE, { 0 } }
  };

  static const gint n_no_stat_dirs = sizeof(no_stat_dirs) / sizeof(no_stat_dirs[0]);
  static gboolean initialized = FALSE;

  gint i;

  if (!initialized)
    {
      initialized = TRUE;
      for (i = 0; i < n_no_stat_dirs; i++)
        {
          if (stat (no_stat_dirs[i].name, &no_stat_dirs[i].statbuf) == 0)
            no_stat_dirs[i].present = TRUE;
        }
    }

  if(stat(dir_name, result) < 0)
    {
      cmpl_errno = errno;
      return FALSE;
    }

  *stat_subdirs = TRUE;
  for (i=0; i<n_no_stat_dirs; i++)
    {
      if (no_stat_dirs[i].present &&
          (no_stat_dirs[i].statbuf.st_dev == result->st_dev) &&
          (no_stat_dirs[i].statbuf.st_ino == result->st_ino))
        {
          *stat_subdirs = FALSE;
          break;
        }
    }

  return TRUE;
}

/* open a directory by absolute pathname */
static CompletionDir*
open_dir(gchar* dir_name, CompletionState* cmpl_state)
{
  struct stat sbuf;
  gboolean stat_subdirs;
  CompletionDirSent *sent;
  GList* cdsl;

  if (!check_dir (dir_name, &sbuf, &stat_subdirs))
    return NULL;

  cdsl = cmpl_state->directory_sent_storage;

  while (cdsl)
    {
      sent = cdsl->data;

      if(sent->inode == sbuf.st_ino &&
         sent->mtime == sbuf.st_mtime &&
         sent->device == sbuf.st_dev)
        return attach_dir(sent, dir_name, cmpl_state);

      cdsl = cdsl->next;
    }

  sent = open_new_dir(dir_name, &sbuf, stat_subdirs);

  if (sent) {
    cmpl_state->directory_sent_storage =
      g_list_prepend(cmpl_state->directory_sent_storage, sent);

    return attach_dir(sent, dir_name, cmpl_state);
  }

  return NULL;
}

static CompletionDir*
attach_dir(CompletionDirSent* sent, gchar* dir_name, CompletionState *cmpl_state)
{
  CompletionDir* new_dir;

  new_dir = g_new(CompletionDir, 1);

  cmpl_state->directory_storage =
    g_list_prepend(cmpl_state->directory_storage, new_dir);

  new_dir->sent = sent;
  new_dir->fullname = g_strdup(dir_name);
  new_dir->fullname_len = strlen(dir_name);

  return new_dir;
}

static gint
correct_dir_fullname(CompletionDir* cmpl_dir)
{
  gint length = strlen(cmpl_dir->fullname);
  struct stat sbuf;

  if (strcmp(cmpl_dir->fullname + length - 2, "/.") == 0)
    {
      if (length == 2)
        {
          strcpy(cmpl_dir->fullname, "/");
          cmpl_dir->fullname_len = 1;
          return TRUE;
        } else {
          cmpl_dir->fullname[length - 2] = 0;
        }
    }
  else if (strcmp(cmpl_dir->fullname + length - 3, "/./") == 0)
    cmpl_dir->fullname[length - 2] = 0;
  else if (strcmp(cmpl_dir->fullname + length - 3, "/..") == 0)
    {
      if(length == 3)
        {
          strcpy(cmpl_dir->fullname, "/");
          cmpl_dir->fullname_len = 1;
          return TRUE;
        }

      if(stat(cmpl_dir->fullname, &sbuf) < 0)
        {
          cmpl_errno = errno;
          return FALSE;
        }

      cmpl_dir->fullname[length - 2] = 0;

      if(!correct_parent(cmpl_dir, &sbuf))
        return FALSE;
    }
  else if (strcmp(cmpl_dir->fullname + length - 4, "/../") == 0)
    {
      if(length == 4)
        {
          strcpy(cmpl_dir->fullname, "/");
          cmpl_dir->fullname_len = 1;
          return TRUE;
        }

      if(stat(cmpl_dir->fullname, &sbuf) < 0)
        {
          cmpl_errno = errno;
          return FALSE;
        }

      cmpl_dir->fullname[length - 3] = 0;

      if(!correct_parent(cmpl_dir, &sbuf))
        return FALSE;
    }

  cmpl_dir->fullname_len = strlen(cmpl_dir->fullname);

  return TRUE;
}

static gint
correct_parent(CompletionDir* cmpl_dir, struct stat *sbuf)
{
  struct stat parbuf;
  gchar *last_slash;
  gchar *new_name;
  gchar c = 0;

  last_slash = strrchr(cmpl_dir->fullname, '/');

  g_assert(last_slash);

  if(last_slash != cmpl_dir->fullname)
    { /* last_slash[0] = 0; */ }
  else
    {
      c = last_slash[1];
      last_slash[1] = 0;
    }

  if (stat(cmpl_dir->fullname, &parbuf) < 0)
    {
      cmpl_errno = errno;
      return FALSE;
    }

  if (parbuf.st_ino == sbuf->st_ino && parbuf.st_dev == sbuf->st_dev)
    /* it wasn't a link */
    return TRUE;

  if(c)
    last_slash[1] = c;
  /* else
    last_slash[0] = '/'; */

  /* it was a link, have to figure it out the hard way */

  new_name = find_parent_dir_fullname(cmpl_dir->fullname);

  if (!new_name)
    return FALSE;

  g_free(cmpl_dir->fullname);

  cmpl_dir->fullname = new_name;

  return TRUE;
}

static gchar*
find_parent_dir_fullname(gchar* dirname)
{
  gchar buffer[MAXPATHLEN];
  gchar buffer2[MAXPATHLEN];

  if(!getcwd(buffer, MAXPATHLEN))
    {
      cmpl_errno = errno;
      return NULL;
    }

  if(chdir(dirname) != 0 || chdir("..") != 0)
    {
      cmpl_errno = errno;
      return NULL;
    }

  if(!getcwd(buffer2, MAXPATHLEN))
    {
      cmpl_errno = errno;
      if (chdir (buffer) != 0)
        cmpl_errno = errno;

      return NULL;
    }

  if(chdir(buffer) != 0)
    {
      cmpl_errno = errno;
      return NULL;
    }

  return g_strdup(buffer2);
}

/**********************************************************************/
/*                        Completion Operations                       */
/**********************************************************************/

static PossibleCompletion*
attempt_homedir_completion(gchar* text_to_complete,
                           CompletionState *cmpl_state)
{
  gint index;

  if (!cmpl_state->user_dir_name_buffer &&
      !get_pwdb(cmpl_state))
    return NULL;

  cmpl_state->user_completion_index += 1;

  while(cmpl_state->user_completion_index < cmpl_state->user_directories_len)
    {
      index = first_diff_index(text_to_complete + 1,
                               cmpl_state->user_directories
                               [cmpl_state->user_completion_index].login);

      switch(index)
        {
        case PATTERN_MATCH:
          break;
        default:
          if(cmpl_state->last_valid_char < (index + 1))
            cmpl_state->last_valid_char = index + 1;
          cmpl_state->user_completion_index += 1;
          continue;
        }

      cmpl_state->the_completion.is_a_completion = 1;
      cmpl_state->the_completion.is_directory = 1;

      append_completion_text("~", cmpl_state);

      append_completion_text(cmpl_state->
                              user_directories[cmpl_state->user_completion_index].login,
                             cmpl_state);

      return append_completion_text("/", cmpl_state);
    }

  if(text_to_complete[1] ||
     cmpl_state->user_completion_index > cmpl_state->user_directories_len)
    {
      cmpl_state->user_completion_index = -1;
      return NULL;
    }
  else
    {
      cmpl_state->user_completion_index += 1;
      cmpl_state->the_completion.is_a_completion = 1;
      cmpl_state->the_completion.is_directory = 1;

      return append_completion_text("~/", cmpl_state);
    }
}

/* returns the index (>= 0) of the first differing character,
 * PATTERN_MATCH if the completion matches */
static gint
first_diff_index(gchar* pat, gchar* text)
{
  gint diff = 0;

  while(*pat && *text && *text == *pat)
    {
      pat += 1;
      text += 1;
      diff += 1;
    }

  if(*pat)
    return diff;

  return PATTERN_MATCH;
}

static PossibleCompletion*
append_completion_text(gchar* text, CompletionState* cmpl_state)
{
  gint len, i = 1;

  if(!cmpl_state->the_completion.text)
    return NULL;

  len = strlen(text) + strlen(cmpl_state->the_completion.text) + 1;

  if(cmpl_state->the_completion.text_alloc > len)
    {
      strcat(cmpl_state->the_completion.text, text);
      return &cmpl_state->the_completion;
    }

  while(i < len) { i <<= 1; }

  cmpl_state->the_completion.text_alloc = i;

  cmpl_state->the_completion.text = (gchar*)g_realloc(cmpl_state->the_completion.text, i);

  if(!cmpl_state->the_completion.text)
    return NULL;
  else
    {
      strcat(cmpl_state->the_completion.text, text);
      return &cmpl_state->the_completion;
    }
}

static CompletionDir*
find_completion_dir(gchar* text_to_complete,
                    gchar** remaining_text,
                    CompletionState* cmpl_state)
{
  gchar* first_slash = strchr(text_to_complete, '/');
  CompletionDir* dir = cmpl_state->reference_dir;
  CompletionDir* next;
  *remaining_text = text_to_complete;

  while(first_slash)
    {
      gint len = first_slash - *remaining_text;
      gint found = 0;
      gchar *found_name = NULL;         /* Quiet gcc */
      gint i;
      gchar* pat_buf = g_new (gchar, len + 1);

      strncpy(pat_buf, *remaining_text, len);
      pat_buf[len] = 0;

      for(i = 0; i < dir->sent->entry_count; i += 1)
        {
          if(dir->sent->entries[i].is_dir &&
             fnmatch(pat_buf, dir->sent->entries[i].entry_name,
                     FNMATCH_FLAGS)!= FNM_NOMATCH)
            {
              if(found)
                {
                  g_free (pat_buf);
                  return dir;
                }
              else
                {
                  found = 1;
                  found_name = dir->sent->entries[i].entry_name;
                }
            }
        }

      if (!found)
        {
          /* Perhaps we are trying to open an automount directory */
          found_name = pat_buf;
        }

      next = open_relative_dir(found_name, dir, cmpl_state);

      if(!next)
        {
          g_free (pat_buf);
          return NULL;
        }

      next->cmpl_parent = dir;

      dir = next;

      if(!correct_dir_fullname(dir))
        {
          g_free(pat_buf);
          return NULL;
        }

      *remaining_text = first_slash + 1;
      first_slash = strchr(*remaining_text, '/');

      g_free (pat_buf);
    }

  return dir;
}

static void
update_cmpl(PossibleCompletion* poss, CompletionState* cmpl_state)
{
  gint cmpl_len;

  if(!poss || !cmpl_is_a_completion(poss))
    return;

  cmpl_len = strlen(cmpl_this_completion(poss));

  if(cmpl_state->updated_text_alloc < cmpl_len + 1)
    {
      cmpl_state->updated_text =
        (gchar*)g_realloc(cmpl_state->updated_text,
                          cmpl_state->updated_text_alloc);
      cmpl_state->updated_text_alloc = 2*cmpl_len;
    }

  if(cmpl_state->updated_text_len < 0)
    {
      strcpy(cmpl_state->updated_text, cmpl_this_completion(poss));
      cmpl_state->updated_text_len = cmpl_len;
      cmpl_state->re_complete = cmpl_is_directory(poss);
    }
  else if(cmpl_state->updated_text_len == 0)
    {
      cmpl_state->re_complete = FALSE;
    }
  else
    {
      gint first_diff =
        first_diff_index(cmpl_state->updated_text,
                         cmpl_this_completion(poss));

      cmpl_state->re_complete = FALSE;

      if(first_diff == PATTERN_MATCH)
        return;

      if(first_diff > cmpl_state->updated_text_len)
        strcpy(cmpl_state->updated_text, cmpl_this_completion(poss));

      cmpl_state->updated_text_len = first_diff;
      cmpl_state->updated_text[first_diff] = 0;
    }
}

static PossibleCompletion*
attempt_file_completion(CompletionState *cmpl_state)
{
  gchar *pat_buf, *first_slash;
  CompletionDir *dir = cmpl_state->active_completion_dir;

  dir->cmpl_index += 1;

  if(dir->cmpl_index == dir->sent->entry_count)
    {
      if(dir->cmpl_parent == NULL)
        {
          cmpl_state->active_completion_dir = NULL;

          return NULL;
        }
      else
        {
          cmpl_state->active_completion_dir = dir->cmpl_parent;

          return attempt_file_completion(cmpl_state);
        }
    }

  g_assert(dir->cmpl_text);

  first_slash = strchr(dir->cmpl_text, '/');

  if(first_slash)
    {
      gint len = first_slash - dir->cmpl_text;

      pat_buf = g_new (gchar, len + 1);
      strncpy(pat_buf, dir->cmpl_text, len);
      pat_buf[len] = 0;
    }
  else
    {
      gint len = strlen(dir->cmpl_text);

      pat_buf = g_new (gchar, len + 2);
      strcpy(pat_buf, dir->cmpl_text);
      strcpy(pat_buf + len, "*");
    }

  if(first_slash)
    {
      if(dir->sent->entries[dir->cmpl_index].is_dir)
        {
          if(fnmatch(pat_buf, dir->sent->entries[dir->cmpl_index].entry_name,
                     FNMATCH_FLAGS) != FNM_NOMATCH)
            {
              CompletionDir* new_dir;

              new_dir = open_relative_dir(dir->sent->entries[dir->cmpl_index].entry_name,
                                          dir, cmpl_state);

              if(!new_dir)
                {
                  g_free (pat_buf);
                  return NULL;
                }

              new_dir->cmpl_parent = dir;

              new_dir->cmpl_index = -1;
              new_dir->cmpl_text = first_slash + 1;

              cmpl_state->active_completion_dir = new_dir;

              g_free (pat_buf);
              return attempt_file_completion(cmpl_state);
            }
          else
            {
              g_free (pat_buf);
              return attempt_file_completion(cmpl_state);
            }
        }
      else
        {
          g_free (pat_buf);
          return attempt_file_completion(cmpl_state);
        }
    }
  else
    {
      if(dir->cmpl_parent != NULL)
        {
          append_completion_text(dir->fullname +
                                 strlen(cmpl_state->completion_dir->fullname) + 1,
                                 cmpl_state);
          append_completion_text("/", cmpl_state);
        }

      append_completion_text(dir->sent->entries[dir->cmpl_index].entry_name, cmpl_state);

      cmpl_state->the_completion.is_a_completion =
        (fnmatch(pat_buf, dir->sent->entries[dir->cmpl_index].entry_name,
                 FNMATCH_FLAGS) != FNM_NOMATCH);

      cmpl_state->the_completion.is_directory = dir->sent->entries[dir->cmpl_index].is_dir;
      if(dir->sent->entries[dir->cmpl_index].is_dir)
        append_completion_text("/", cmpl_state);

      g_free (pat_buf);
      return &cmpl_state->the_completion;
    }
}


static gint
get_pwdb(CompletionState* cmpl_state)
{
  struct passwd *pwd_ptr;
  gchar* buf_ptr;
  gint len = 0, i, count = 0;

  if(cmpl_state->user_dir_name_buffer)
    return TRUE;
  setpwent ();

  while ((pwd_ptr = getpwent()) != NULL)
    {
      len += strlen(pwd_ptr->pw_name);
      len += strlen(pwd_ptr->pw_dir);
      len += 2;
      count += 1;
    }

  setpwent ();

  cmpl_state->user_dir_name_buffer = g_new(gchar, len);
  cmpl_state->user_directories = g_new(CompletionUserDir, count);
  cmpl_state->user_directories_len = count;

  buf_ptr = cmpl_state->user_dir_name_buffer;

  for(i = 0; i < count; i += 1)
    {
      pwd_ptr = getpwent();
      if(!pwd_ptr)
        {
          cmpl_errno = errno;
          goto error;
        }

      strcpy(buf_ptr, pwd_ptr->pw_name);
      cmpl_state->user_directories[i].login = buf_ptr;
      buf_ptr += strlen(buf_ptr);
      buf_ptr += 1;
      strcpy(buf_ptr, pwd_ptr->pw_dir);
      cmpl_state->user_directories[i].homedir = buf_ptr;
      buf_ptr += strlen(buf_ptr);
      buf_ptr += 1;
    }

  qsort(cmpl_state->user_directories,
        cmpl_state->user_directories_len,
        sizeof(CompletionUserDir),
        compare_user_dir);

  endpwent();

  return TRUE;

error:

  if(cmpl_state->user_dir_name_buffer)
    g_free(cmpl_state->user_dir_name_buffer);
  if(cmpl_state->user_directories)
    g_free(cmpl_state->user_directories);

  cmpl_state->user_dir_name_buffer = NULL;
  cmpl_state->user_directories = NULL;

  return FALSE;
}

static gint
compare_user_dir(const void* a, const void* b)
{
  return strcmp((((CompletionUserDir*)a))->login,
                (((CompletionUserDir*)b))->login);
}

static gint
compare_cmpl_dir(const void* a, const void* b)
{
  return strcmp((((CompletionDirEntry*)a))->entry_name,
                (((CompletionDirEntry*)b))->entry_name);
}

static gint
cmpl_state_okay(CompletionState* cmpl_state)
{
  return  cmpl_state && cmpl_state->reference_dir;
}

static gchar*
cmpl_strerror(gint err)
{
  if(err == CMPL_ERRNO_TOO_LONG)
    return "Name too long";
  else
    return g_strerror (err);
}
