/* GtkGLArea with a context of today's GL: version 3.3, the core
 * profile, where everything is drawn with shaders and buffers and the
 * GL of glBegin is gone. The attribute list asks for it; the rest is
 * a GtkGLArea like any other.
 */

#include <math.h>
#include <stdio.h>
#include <gtk/gtk.h>
#include <gtkgl/gtkglarea.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

static const char vertex_source[] =
  "#version 330 core\n"
  "layout (location = 0) in vec2 position;\n"
  "layout (location = 1) in vec3 colour;\n"
  "uniform float angle;\n"
  "uniform float aspect;\n"
  "out vec3 shade;\n"
  "void main ()\n"
  "{\n"
  "  float c = cos (angle), s = sin (angle);\n"
  "  vec2 p = vec2 (position.x * c - position.y * s,\n"
  "                 position.x * s + position.y * c);\n"
  "  gl_Position = vec4 (p.x / aspect, p.y, 0.0, 1.0);\n"
  "  shade = colour;\n"
  "}\n";

static const char fragment_source[] =
  "#version 330 core\n"
  "in vec3 shade;\n"
  "out vec4 pixel;\n"
  "void main ()\n"
  "{\n"
  "  pixel = vec4 (shade, 1.0);\n"
  "}\n";

/* A corner's place, and its colour. */
static const GLfloat triangle[] = {
   0.00,  0.80,   0.95, 0.30, 0.30,
  -0.70, -0.40,   0.30, 0.85, 0.40,
   0.70, -0.40,   0.30, 0.50, 0.95
};

static GLuint program, vertex_array, vertex_buffer;
static GLint angle_uniform, aspect_uniform;
static GLfloat angle = 0.0, aspect = 1.0;

static GLuint compile( GLenum      type,
                       const char *source )
{
  GLuint shader = glCreateShader (type);
  GLint ok;

  glShaderSource (shader, 1, &source, NULL);
  glCompileShader (shader);
  glGetShaderiv (shader, GL_COMPILE_STATUS, &ok);
  if (!ok)
    {
      char log[512];

      glGetShaderInfoLog (shader, sizeof log, NULL, log);
      g_print ("shader: %s\n", log);
    }

  return shader;
}

static void realize( GtkWidget *widget,
                     gpointer   data )
{
  GLuint vertex, fragment;
  gchar *text;

  if (!gtk_gl_area_make_current (GTK_GL_AREA (widget)))
    return;

  vertex = compile (GL_VERTEX_SHADER, vertex_source);
  fragment = compile (GL_FRAGMENT_SHADER, fragment_source);
  program = glCreateProgram ();
  glAttachShader (program, vertex);
  glAttachShader (program, fragment);
  glLinkProgram (program);
  glDeleteShader (vertex);
  glDeleteShader (fragment);
  angle_uniform = glGetUniformLocation (program, "angle");
  aspect_uniform = glGetUniformLocation (program, "aspect");

  glGenVertexArrays (1, &vertex_array);
  glBindVertexArray (vertex_array);
  glGenBuffers (1, &vertex_buffer);
  glBindBuffer (GL_ARRAY_BUFFER, vertex_buffer);
  glBufferData (GL_ARRAY_BUFFER, sizeof triangle, triangle, GL_STATIC_DRAW);
  glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat),
                         (void *) 0);
  glEnableVertexAttribArray (0);
  glVertexAttribPointer (1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat),
                         (void *) (2 * sizeof (GLfloat)));
  glEnableVertexAttribArray (1);

  text = g_strdup_printf ("OpenGL %s", (const char *) glGetString (GL_VERSION));
  gtk_label_set_text (GTK_LABEL (data), text);
  g_free (text);
}

static gint configure_event( GtkWidget         *widget,
                             GdkEventConfigure *event )
{
  gint width, height;

  if (!gtk_gl_area_make_current (GTK_GL_AREA (widget)))
    return TRUE;

  /* In the pixels GL draws, not the widget's: see glarea.c. */
  gtk_gl_area_get_drawable_size (GTK_GL_AREA (widget), &width, &height);
  glViewport (0, 0, width, height);
  aspect = (GLfloat) width / height;

  return TRUE;
}

static gint expose_event( GtkWidget      *widget,
                          GdkEventExpose *event )
{
  if (event->count > 0)
    return TRUE;

  if (gtk_gl_area_make_current (GTK_GL_AREA (widget)))
    {
      glClearColor (0.12, 0.13, 0.16, 1.0);
      glClear (GL_COLOR_BUFFER_BIT);

      glUseProgram (program);
      glUniform1f (angle_uniform, angle);
      glUniform1f (aspect_uniform, aspect);
      glBindVertexArray (vertex_array);
      glDrawArrays (GL_TRIANGLES, 0, 3);

      gtk_gl_area_swap_buffers (GTK_GL_AREA (widget));
    }

  return TRUE;
}

static gint spin( gpointer data )
{
  angle += 0.02;
  gtk_widget_queue_draw (GTK_WIDGET (data));

  return TRUE;
}

int main( int   argc,
          char *argv[] )
{
  GtkWidget *window, *vbox, *glarea, *label;

  /* The context to make comes first; what it draws in, after. */
  int attrlist[] = {
    GDK_GL_CONTEXT_MAJOR_VERSION, 3,
    GDK_GL_CONTEXT_MINOR_VERSION, 3,
    GDK_GL_CONTEXT_PROFILE_MASK, GDK_GL_CONTEXT_CORE_PROFILE_BIT,
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
  gtk_window_set_title (GTK_WINDOW (window), "GtkGLArea, core profile");
  gtk_signal_connect (GTK_OBJECT (window), "destroy",
                      GTK_SIGNAL_FUNC (gtk_main_quit), NULL);

  vbox = gtk_vbox_new (FALSE, 4);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 4);
  gtk_container_add (GTK_CONTAINER (window), vbox);

  label = gtk_label_new ("");

  glarea = gtk_gl_area_new (attrlist);
  if (glarea == NULL)
    {
      g_print ("No OpenGL 3.3 core profile here\n");
      return 0;
    }
  gtk_signal_connect (GTK_OBJECT (glarea), "realize",
                      GTK_SIGNAL_FUNC (realize), label);
  gtk_signal_connect (GTK_OBJECT (glarea), "configure_event",
                      GTK_SIGNAL_FUNC (configure_event), NULL);
  gtk_signal_connect (GTK_OBJECT (glarea), "expose_event",
                      GTK_SIGNAL_FUNC (expose_event), NULL);
  gtk_widget_set_usize (glarea, 320, 240);
  gtk_box_pack_start (GTK_BOX (vbox), glarea, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

  gtk_timeout_add (20, spin, glarea);

  gtk_widget_show_all (window);

  gtk_main ();

  return 0;
}
