/* GtkGLArea drawn without pause, the way a game or an emulator draws:
 * one frame after another from the main loop, with the menus and the
 * dialogs of the program getting their turn between them.
 *
 * What is drawn stands for a machine that makes 60.0988 pictures a
 * second, as an NTSC NES does. It can be timed two ways, and the View
 * menu changes from one to the other:
 *
 *   by the screen: each swap waits for the screen's next refresh, and
 *   the machine makes one picture for each, whatever the screen's rate
 *   (an emulator then fits its sound to the rate it gets, which
 *   gdk_window_get_refresh_rate tells it);
 *
 *   by the clock: the swap waits for nothing, and the next picture is
 *   drawn when it is due by the machine's own rate.
 *
 * The line under the picture says how the frames came and how long the
 * longest of them kept the main loop: that is the longest a menu can
 * have been kept waiting.
 *
 * Give it a number of seconds to have it quit by itself and say the
 * same on standard output; "clock" and "half" (every other refresh)
 * before the number set it going that way.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include <gtkgl/gtkglarea.h>
#include <GL/gl.h>

#define MACHINE_HZ 60.0988

static GtkWidget *window, *area, *label;
static gboolean by_clock = FALSE;
static gint interval = 1;
static guint frame_idle = 0, frame_timer = 0;
static guint n_frames = 0;
static double due = 0;                  /* when the next is, by the clock */

/* How the frames came: since the label last said, and since the start. */
static guint count = 0, all_count = 0;
static double last = 0, start = 0;
static double held_max = 0, all_held_max = 0;   /* seconds in one frame */

static double now( void )
{
  struct timespec ts;

  clock_gettime (CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* A bar that crosses the picture once a second of the machine's time:
 * a frame shown twice or not at all is a jerk in it. */
static void draw( guint frame, gint width, gint height )
{
  GLfloat x = fmod (frame / MACHINE_HZ, 1.0) * 2.4 - 1.2;
  gint i;

  glViewport (0, 0, width, height);
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();

  glClearColor (0.10, 0.11, 0.14, 1.0);
  glClear (GL_COLOR_BUFFER_BIT);

  glBegin (GL_QUADS);
  for (i = 0; i < 8; i++)
    {
      /* The colour bars of a test card along the bottom. */
      glColor3f ((i & 4) ? 0.1 : 0.8, (i & 2) ? 0.1 : 0.8, (i & 1) ? 0.1 : 0.8);
      glVertex2f (-1.0 + i * 0.25, -1.0);
      glVertex2f (-0.75 + i * 0.25, -1.0);
      glVertex2f (-0.75 + i * 0.25, -0.7);
      glVertex2f (-1.0 + i * 0.25, -0.7);
    }
  glColor3f (0.95, 0.80, 0.25);
  glVertex2f (x - 0.08, -0.7);
  glVertex2f (x + 0.08, -0.7);
  glVertex2f (x + 0.08, 1.0);
  glVertex2f (x - 0.08, 1.0);
  glEnd ();
}

static gint frame( gpointer data );

/* The next frame: as soon as the main loop has nothing else to do when
 * the swap sets the pace, when it is due when the clock does. */
static void schedule( void )
{
  if (frame_idle)
    gtk_idle_remove (frame_idle);
  if (frame_timer)
    gtk_timeout_remove (frame_timer);
  frame_idle = frame_timer = 0;

  if (by_clock)
    {
      double wait = due - now ();

      if (wait < -0.1)
        {
          /* Too far behind to catch up: start counting again. */
          due = now ();
          wait = 0;
        }
      frame_timer = gtk_timeout_add (wait > 0 ? (guint32) (wait * 1000) : 0,
                                     frame, NULL);
    }
  else
    frame_idle = gtk_idle_add (frame, NULL);
}

static gint frame( gpointer data )
{
  double before = now (), after;
  gint width, height;

  /* A timeout is as fine as a thousandth of a second: the rest of the
   * wait, if any, is spent here. */
  if (by_clock)
    while (now () < due)
      ;

  if (GTK_WIDGET_REALIZED (area) &&
      gtk_gl_area_make_current (GTK_GL_AREA (area)))
    {
      /* NEW: the size to draw at is asked for, each frame. */
      gtk_gl_area_get_drawable_size (GTK_GL_AREA (area), &width, &height);
      draw (n_frames++, width, height);
      /* With an interval, this is where the frame waits for the
       * screen: for a refresh of it and no longer. */
      gtk_gl_area_swap_buffers (GTK_GL_AREA (area));
    }

  after = now ();
  if (after - before > held_max)
    held_max = after - before;
  if (after - before > all_held_max)
    all_held_max = after - before;
  count += 1;
  all_count += 1;

  if (by_clock)
    {
      /* A timeout is for once: the next frame asks for its own. */
      due += 1.0 / MACHINE_HZ;
      frame_timer = 0;
      schedule ();
      return FALSE;
    }
  return TRUE;
}

/* NEW: how many refreshes of the screen a swap waits for. */
static void set_pace( void )
{
  if (GTK_WIDGET_REALIZED (area))
    gdk_gl_set_swap_interval (area->window, by_clock ? 0 : interval);
  due = now ();
  schedule ();
}

static void realize( GtkWidget *widget,
                     gpointer   data )
{
  set_pace ();
}

static void clock_toggled( GtkWidget *widget,
                           gpointer   data )
{
  by_clock = GTK_CHECK_MENU_ITEM (widget)->active;
  set_pace ();
}

static gint say( gpointer data )
{
  /* NEW: the rate of the screen the window is on, in thousandths of a hertz. */
  gint rate = GTK_WIDGET_REALIZED (area)
    ? gdk_window_get_refresh_rate (area->window) : 0;
  double t = now ();
  gchar text[200];

  g_snprintf (text, sizeof text,
              "screen %.3f Hz; %.1f frames a second, the longest kept the loop %.1f ms",
              rate / 1000.0, last > 0 ? count / (t - last) : 0.0, held_max * 1000);
  gtk_label_set_text (GTK_LABEL (label), text);
  count = 0;
  held_max = 0;
  last = t;
  return TRUE;
}

static void about( GtkWidget *widget,
                   gpointer   data )
{
  GtkWidget *dialog = gtk_dialog_new ();
  GtkWidget *text, *button;

  gtk_window_set_title (GTK_WINDOW (dialog), "About");
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (window));
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

  text = gtk_label_new ("The picture goes on being drawn\n"
                        "while this is here to be read.");
  gtk_misc_set_padding (GTK_MISC (text), 20, 20);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), text, TRUE, TRUE, 0);
  button = gtk_button_new_with_label ("OK");
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
                             GTK_SIGNAL_FUNC (gtk_widget_destroy),
                             GTK_OBJECT (dialog));
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->action_area), button,
                      TRUE, TRUE, 0);
  gtk_widget_show_all (dialog);
}

static void quit( GtkWidget *widget,
                  gpointer   data )
{
  /* No more frames: the picture may be gone already. */
  if (frame_idle)
    gtk_idle_remove (frame_idle);
  if (frame_timer)
    gtk_timeout_remove (frame_timer);
  frame_idle = frame_timer = 0;
  gtk_main_quit ();
}

static gint finish( gpointer data )
{
  double t = now ();

  printf ("screen %.3f Hz, %u frames, %.2f a second, the longest kept the loop %.1f ms\n",
          gdk_window_get_refresh_rate (area->window) / 1000.0, all_count,
          all_count / (t - start), all_held_max * 1000);
  quit (NULL, NULL);
  return FALSE;
}

static GtkWidget *menu_item( GtkWidget     *menu,
                             GtkWidget     *item,
                             GtkSignalFunc  func )
{
  gtk_menu_append (GTK_MENU (menu), item);
  if (func)
    gtk_signal_connect (GTK_OBJECT (item), "activate", func, NULL);
  return item;
}

static GtkWidget *menu_new( GtkWidget   *bar,
                            const gchar *name )
{
  GtkWidget *menu = gtk_menu_new ();
  GtkWidget *item = gtk_menu_item_new_with_label (name);

  gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), menu);
  gtk_menu_bar_append (GTK_MENU_BAR (bar), item);
  return menu;
}

int main( int   argc,
          char *argv[] )
{
  /* What is asked of GL: as it was with GLX, a list ending in GDK_GL_NONE. */
  int attributes[] = { GDK_GL_RGBA, GDK_GL_DOUBLEBUFFER, GDK_GL_NONE };
  GtkWidget *vbox, *bar, *menu, *item;
  gint seconds = 0, i;

  gtk_init (&argc, &argv);

  for (i = 1; i < argc; i++)
    {
      if (strcmp (argv[i], "clock") == 0)
        by_clock = TRUE;
      else if (strcmp (argv[i], "half") == 0)
        interval = 2;
      else
        seconds = atoi (argv[i]);
    }

  if (!gdk_gl_query ())
    {
      g_print ("No OpenGL here.\n");
      return 1;
    }

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (window), "GtkGLArea from the main loop");
  gtk_signal_connect (GTK_OBJECT (window), "destroy",
                      GTK_SIGNAL_FUNC (quit), NULL);

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (window), vbox);

  bar = gtk_menu_bar_new ();
  gtk_box_pack_start (GTK_BOX (vbox), bar, FALSE, FALSE, 0);
  menu = menu_new (bar, "File");
  menu_item (menu, gtk_menu_item_new_with_label ("Quit"),
             GTK_SIGNAL_FUNC (quit));
  menu = menu_new (bar, "View");
  item = menu_item (menu, gtk_check_menu_item_new_with_label ("Keep time by the clock"),
                    NULL);
  gtk_check_menu_item_set_show_toggle (GTK_CHECK_MENU_ITEM (item), TRUE);
  gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), by_clock);
  gtk_signal_connect (GTK_OBJECT (item), "toggled",
                      GTK_SIGNAL_FUNC (clock_toggled), NULL);
  menu = menu_new (bar, "Help");
  menu_item (menu, gtk_menu_item_new_with_label ("About..."),
             GTK_SIGNAL_FUNC (about));

  area = gtk_gl_area_new (attributes);
  if (!area)
    {
      g_print ("No OpenGL of the kind asked for.\n");
      return 1;
    }
  gtk_widget_set_usize (area, 480, 360);
  gtk_signal_connect_after (GTK_OBJECT (area), "realize",
                            GTK_SIGNAL_FUNC (realize), NULL);
  gtk_box_pack_start (GTK_BOX (vbox), area, TRUE, TRUE, 0);

  label = gtk_label_new ("");
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_misc_set_padding (GTK_MISC (label), 6, 4);
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

  gtk_widget_show_all (window);

  start = last = now ();
  gtk_timeout_add (1000, say, NULL);
  if (seconds > 0)
    gtk_timeout_add (seconds * 1000, finish, NULL);

  gtk_main ();

  return 0;
}
