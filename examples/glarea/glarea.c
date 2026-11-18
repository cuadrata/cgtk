/* GtkGLArea as it has always been used: an attribute list, a context of
 * whatever GL the system has, and drawing with glBegin and glEnd. A
 * program written for the GtkGLArea of GTK+ 1.2 looks like this one,
 * but for the one thing marked NEW below.
 *
 * Drag in the picture to turn the cube.
 */

#include <math.h>
#include <stdio.h>
#include <gtk/gtk.h>
#include <gtkgl/gtkglarea.h>
#include <GL/gl.h>

static GLfloat angle_x = 25.0, angle_y = -35.0;
static gint last_x, last_y;
static guint spin_timer = 0;

static void face( GLfloat r, GLfloat g, GLfloat b,
                  GLfloat nx, GLfloat ny, GLfloat nz )
{
  /* The four corners of the side of a cube that faces along n. */
  GLfloat ux = ny, uy = nz, uz = nx;
  GLfloat vx = ny * uz - nz * uy, vy = nz * ux - nx * uz, vz = nx * uy - ny * ux;

  glColor3f (r, g, b);
  glVertex3f (nx - ux - vx, ny - uy - vy, nz - uz - vz);
  glVertex3f (nx + ux - vx, ny + uy - vy, nz + uz - vz);
  glVertex3f (nx + ux + vx, ny + uy + vy, nz + uz + vz);
  glVertex3f (nx - ux + vx, ny - uy + vy, nz - uz + vz);
}

static void draw( void )
{
  glClearColor (0.12, 0.13, 0.16, 1.0);
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();
  glTranslatef (0.0, 0.0, -6.0);
  glRotatef (angle_x, 1.0, 0.0, 0.0);
  glRotatef (angle_y, 0.0, 1.0, 0.0);

  glBegin (GL_QUADS);
  face (0.90, 0.25, 0.25,  1.0, 0.0, 0.0);
  face (0.55, 0.15, 0.15, -1.0, 0.0, 0.0);
  face (0.30, 0.80, 0.35,  0.0, 1.0, 0.0);
  face (0.15, 0.45, 0.20,  0.0,-1.0, 0.0);
  face (0.30, 0.50, 0.95,  0.0, 0.0, 1.0);
  face (0.15, 0.25, 0.55,  0.0, 0.0,-1.0);
  glEnd ();
}

/* The window has a size, or another one. */
static gint configure_event( GtkWidget         *widget,
                             GdkEventConfigure *event )
{
  gint width, height;
  GLdouble aspect;

  if (!gtk_gl_area_make_current (GTK_GL_AREA (widget)))
    return TRUE;

  /* NEW: the viewport is in the pixels GL draws, which on a scaled
   * display are more than the widget's allocation counts. */
  gtk_gl_area_get_drawable_size (GTK_GL_AREA (widget), &width, &height);
  glViewport (0, 0, width, height);

  aspect = (GLdouble) width / height;
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  glFrustum (-aspect, aspect, -1.0, 1.0, 2.5, 12.0);

  return TRUE;
}

static gint expose_event( GtkWidget      *widget,
                          GdkEventExpose *event )
{
  /* Only the last of a run of exposes is worth drawing for. */
  if (event->count > 0)
    return TRUE;

  if (gtk_gl_area_make_current (GTK_GL_AREA (widget)))
    {
      draw ();
      gtk_gl_area_swap_buffers (GTK_GL_AREA (widget));
    }

  return TRUE;
}

static void realize( GtkWidget *widget,
                     gpointer   data )
{
  if (gtk_gl_area_make_current (GTK_GL_AREA (widget)))
    {
      gchar *text;

      glEnable (GL_DEPTH_TEST);
      glDepthFunc (GL_LESS);

      text = g_strdup_printf ("OpenGL %s", (const char *) glGetString (GL_VERSION));
      gtk_label_set_text (GTK_LABEL (data), text);
      g_free (text);
    }
}

static gint button_press_event( GtkWidget      *widget,
                                GdkEventButton *event )
{
  last_x = event->x;
  last_y = event->y;

  return TRUE;
}

static gint motion_notify_event( GtkWidget      *widget,
                                 GdkEventMotion *event )
{
  gint x, y;
  GdkModifierType state;

  if (event->is_hint)
    gdk_window_get_pointer (event->window, &x, &y, &state);
  else
    {
      x = event->x;
      y = event->y;
      state = event->state;
    }

  if (state & GDK_BUTTON1_MASK)
    {
      angle_y += x - last_x;
      angle_x += y - last_y;
      last_x = x;
      last_y = y;
      gtk_widget_queue_draw (widget);
    }

  return TRUE;
}

static gint spin( gpointer data )
{
  angle_y += 1.0;
  if (angle_y >= 360.0)
    angle_y -= 360.0;
  gtk_widget_queue_draw (GTK_WIDGET (data));

  return TRUE;
}

static void spin_toggled( GtkWidget *widget,
                          gpointer   data )
{
  if (GTK_TOGGLE_BUTTON (widget)->active && !spin_timer)
    spin_timer = gtk_timeout_add (20, spin, data);
  else if (!GTK_TOGGLE_BUTTON (widget)->active && spin_timer)
    {
      gtk_timeout_remove (spin_timer);
      spin_timer = 0;
    }
}

int main( int   argc,
          char *argv[] )
{
  GtkWidget *window, *vbox, *hbox, *glarea, *label, *button;

  /* What is wanted of GL: colour, a depth buffer, and two buffers to
   * swap. The list ends with GDK_GL_NONE. */
  int attrlist[] = {
    GDK_GL_RGBA,
    GDK_GL_RED_SIZE, 1,
    GDK_GL_GREEN_SIZE, 1,
    GDK_GL_BLUE_SIZE, 1,
    GDK_GL_DEPTH_SIZE, 1,
    GDK_GL_DOUBLEBUFFER,
    GDK_GL_NONE
  };

  gtk_init (&argc, &argv);

  if (gdk_gl_query () == FALSE)
    {
      g_print ("OpenGL not supported\n");
      return 0;
    }

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (window), "GtkGLArea");
  gtk_signal_connect (GTK_OBJECT (window), "destroy",
                      GTK_SIGNAL_FUNC (gtk_main_quit), NULL);

  vbox = gtk_vbox_new (FALSE, 4);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 4);
  gtk_container_add (GTK_CONTAINER (window), vbox);

  label = gtk_label_new ("");

  glarea = gtk_gl_area_new (attrlist);
  if (glarea == NULL)
    {
      g_print ("Can't create GtkGLArea widget\n");
      return 0;
    }
  gtk_widget_set_events (glarea, GDK_EXPOSURE_MASK
                         | GDK_BUTTON_PRESS_MASK
                         | GDK_BUTTON1_MOTION_MASK
                         | GDK_POINTER_MOTION_HINT_MASK);
  gtk_signal_connect (GTK_OBJECT (glarea), "realize",
                      GTK_SIGNAL_FUNC (realize), label);
  gtk_signal_connect (GTK_OBJECT (glarea), "configure_event",
                      GTK_SIGNAL_FUNC (configure_event), NULL);
  gtk_signal_connect (GTK_OBJECT (glarea), "expose_event",
                      GTK_SIGNAL_FUNC (expose_event), NULL);
  gtk_signal_connect (GTK_OBJECT (glarea), "button_press_event",
                      GTK_SIGNAL_FUNC (button_press_event), NULL);
  gtk_signal_connect (GTK_OBJECT (glarea), "motion_notify_event",
                      GTK_SIGNAL_FUNC (motion_notify_event), NULL);
  gtk_widget_set_usize (glarea, 320, 240);
  gtk_box_pack_start (GTK_BOX (vbox), glarea, TRUE, TRUE, 0);

  hbox = gtk_hbox_new (FALSE, 4);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  button = gtk_check_button_new_with_label ("Spin");
  gtk_signal_connect (GTK_OBJECT (button), "toggled",
                      GTK_SIGNAL_FUNC (spin_toggled), glarea);
  gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);

  button = gtk_button_new_with_label ("Quit");
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
                             GTK_SIGNAL_FUNC (gtk_widget_destroy),
                             GTK_OBJECT (window));
  gtk_box_pack_end (GTK_BOX (hbox), button, FALSE, FALSE, 0);

  gtk_widget_show_all (window);

  gtk_main ();

  return 0;
}
