/* GdkGL - OpenGL for GDK
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
 * GtkGLArea's gdkgl, on EGL.
 *
 * EGL is what X11 and Wayland have in common: one display made from
 * GDK's, a config for each thing an attribute list can ask for, and a
 * window surface for each GdkWindow GL draws in. Only the making of
 * that surface, and how a swap is paced, differ between the two.
 *
 * A window GL draws in is an external window (gdk_window_set_external):
 * a window of the window system's own, which GDK places and sizes and
 * otherwise leaves alone. On X11 it is an X window, whose size is the
 * surface's without anything being done. On Wayland it is a
 * subsurface, and the surface's size is that of a wl_egl_window made
 * for it, which is kept at the window's size here.
 *
 * The old interface names what it wants by GdkVisual, and GDK has only
 * one of those, so a visual from gdk_gl_choose_visual is a copy of it
 * that stands for what was asked: the config, and the context to make.
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <gdk/gdk.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

/* EGL's headers bring in Xlib's unless told not to, and Xlib is not
 * there to bring in on a system that is only Wayland. */
#ifndef EGL_NO_X11
#define EGL_NO_X11
#endif
#ifndef MESA_EGL_NO_X11_HEADERS
#define MESA_EGL_NO_X11_HEADERS
#endif
#include <EGL/egl.h>
#include <EGL/eglext.h>

#ifdef GDK_WINDOWING_WAYLAND
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <gdk/gdkwayland.h>
#include <wayland-egl.h>
#endif

#include "gdkgl.h"

/* What EGL 1.5 and the extensions name, for headers that do not. */
#ifndef EGL_PLATFORM_X11_KHR
#define EGL_PLATFORM_X11_KHR 0x31D5
#endif
#ifndef EGL_PLATFORM_WAYLAND_KHR
#define EGL_PLATFORM_WAYLAND_KHR 0x31D8
#endif
#ifndef EGL_PRESENT_OPAQUE_EXT
#define EGL_PRESENT_OPAQUE_EXT 0x31DF
#endif
#ifndef EGL_CONTEXT_MAJOR_VERSION
#define EGL_CONTEXT_MAJOR_VERSION 0x3098
#endif
#ifndef EGL_CONTEXT_MINOR_VERSION
#define EGL_CONTEXT_MINOR_VERSION 0x30FB
#endif
#ifndef EGL_CONTEXT_FLAGS_KHR
#define EGL_CONTEXT_FLAGS_KHR 0x30FC
#endif
#ifndef EGL_CONTEXT_OPENGL_PROFILE_MASK
#define EGL_CONTEXT_OPENGL_PROFILE_MASK 0x30FD
#endif
#ifndef EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT
#define EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT 0x00000001
#endif
#ifndef EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT
#define EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT 0x00000002
#endif
#ifndef EGL_CONTEXT_OPENGL_DEBUG
#define EGL_CONTEXT_OPENGL_DEBUG 0x31B0
#endif
#ifndef EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE
#define EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE 0x31B1
#endif
#ifndef EGL_OPENGL_ES3_BIT
#define EGL_OPENGL_ES3_BIT 0x00000040
#endif

/* How long a swap waits for a frame it was promised, in milliseconds,
 * before it takes the window to be one nobody can see. */
#define FRAME_WAIT 50
/* How long a screen is taken to show a frame when nothing says: a
 * sixtieth of a second, in nanoseconds. */
#define FRAME_PERIOD 16666667

typedef struct _GdkGLFormat GdkGLFormat;
typedef struct _GdkGLWindow GdkGLWindow;
typedef struct _GdkGLRequest GdkGLRequest;

/* What an attribute list asked for, once read. */
struct _GdkGLRequest
{
  gint red, green, blue, alpha;
  gint depth, stencil;
  gint sample_buffers, samples;
  gint major, minor;        /* of the context; 0.0 for the one GL always gave */
  gint profile, flags;
};

/*
 * A visual from gdk_gl_choose_visual. The GdkVisual is first, so that
 * the visual's address is the format's; formats are never freed, as
 * visuals never were.
 */
struct _GdkGLFormat
{
  GdkVisual visual;
  EGLConfig config;
  EGLenum api;              /* EGL_OPENGL_API or EGL_OPENGL_ES_API */
  gint major, minor;
  gint profile, flags;
};

struct _GdkGLContext
{
  guint ref_count;
  GdkGLFormat *format;
  EGLContext context;
};

/* What GL has of a window it draws in. */
struct _GdkGLWindow
{
  GdkWindow *window;
  GdkGLFormat *format;      /* what the surface was made for */
  EGLSurface surface;
  GdkGLContext *context;    /* the last one current here; not held */
  gint width, height;       /* the surface's, in device pixels */
  gint scale;
  gint swap_interval;
  guint interval_set : 1;   /* and the surface has been told */
#ifdef GDK_WINDOWING_WAYLAND
  struct wl_egl_window *egl_window;
  struct wl_surface *wrapper;     /* the surface, for a queue of our own */
  struct wl_callback *frame;      /* the frame the surface was last promised */
  struct timespec frame_asked;    /* and when it was asked for */
  gint frames;                    /* how many have come since the last swap */
  gint64 pace_next;               /* when the clock lets the next swap go */
  guint swapped : 1;              /* the surface has been given a buffer */
  guint starved : 1;              /* frames come late: the clock paces instead */
#endif
};

static EGLDisplay egl_display = EGL_NO_DISPLAY;
static gboolean egl_tried = FALSE;
static gboolean egl_1_5 = FALSE;
static gboolean egl_create_context = FALSE;
static gboolean egl_present_opaque = FALSE;

static GSList *gl_formats = NULL;
static GSList *gl_windows = NULL;
static GdkGLFormat *gl_default_format = NULL;
static GdkGLContext *current_context = NULL;
static GdkGLWindow *current_window = NULL;

static const gchar gl_window_key[] = "gdk-gl-window";

#ifdef GDK_WINDOWING_WAYLAND
static struct wl_event_queue *frame_queue = NULL;
#endif

static void gl_window_destroy_surface (GdkGLWindow *glwin);

/* ---------------------------------------------------------------------
 * The display
 */

static gboolean
has_extension (const char *extensions, const char *name)
{
  gsize len = strlen (name);
  const char *p = extensions;

  while (p && (p = strstr (p, name)) != NULL)
    {
      if ((p == extensions || p[-1] == ' ') && (p[len] == ' ' || p[len] == '\0'))
        return TRUE;
      p += len;
    }
  return FALSE;
}

/*
 * At exit, or when the library is unloaded: EGL lets go of GDK's
 * display while GDK still has it. This runs before GDK's own exit,
 * having been registered after it.
 */
static void
gdk_gl_exit (void)
{
  if (egl_display == EGL_NO_DISPLAY)
    return;
  eglMakeCurrent (egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglTerminate (egl_display);
  eglReleaseThread ();
  egl_display = EGL_NO_DISPLAY;
  current_context = NULL;
  current_window = NULL;
}

static gboolean
gdk_gl_init (void)
{
  const char *extensions;
  void *native = NULL;
  EGLenum platform = 0;
  EGLDisplay display = EGL_NO_DISPLAY;
  EGLint major, minor;

  if (egl_tried)
    return egl_display != EGL_NO_DISPLAY;

#ifdef GDK_WINDOWING_WAYLAND
  if (!native && GDK_IS_WAYLAND ())
    {
      native = gdk_wayland_display ();
      platform = EGL_PLATFORM_WAYLAND_KHR;
    }
#endif
#ifdef GDK_WINDOWING_X11
  if (!native && GDK_IS_X11 ())
    {
      native = GDK_DISPLAY ();
      platform = EGL_PLATFORM_X11_KHR;
    }
#endif
  /* No display yet: asked again when GDK has one. */
  if (!native)
    return FALSE;
  egl_tried = TRUE;

  /* Told which window system the display is of, where EGL can be:
   * left to guess, it has been known to guess wrong. */
  extensions = eglQueryString (EGL_NO_DISPLAY, EGL_EXTENSIONS);
  if (extensions && has_extension (extensions, "EGL_EXT_platform_base"))
    {
      PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC) eglGetProcAddress ("eglGetPlatformDisplayEXT");

      if (get_platform_display)
        display = get_platform_display (platform, native, NULL);
    }
  if (display == EGL_NO_DISPLAY)
    display = eglGetDisplay ((EGLNativeDisplayType) native);
  if (display == EGL_NO_DISPLAY || !eglInitialize (display, &major, &minor))
    return FALSE;

  egl_display = display;
  egl_1_5 = major > 1 || (major == 1 && minor >= 5);
  extensions = eglQueryString (display, EGL_EXTENSIONS);
  egl_create_context = egl_1_5 ||
    (extensions && has_extension (extensions, "EGL_KHR_create_context"));
  egl_present_opaque =
    extensions && has_extension (extensions, "EGL_EXT_present_opaque");

  atexit (gdk_gl_exit);

  return TRUE;
}

gint
gdk_gl_query (void)
{
  return gdk_gl_init ();
}

gchar *
gdk_gl_get_info (void)
{
  const char *vendor = NULL, *version = NULL, *extensions = NULL;

  if (gdk_gl_init ())
    {
      vendor = eglQueryString (egl_display, EGL_VENDOR);
      version = eglQueryString (egl_display, EGL_VERSION);
      extensions = eglQueryString (egl_display, EGL_EXTENSIONS);
    }
  return g_strdup_printf ("VENDOR     : %s\n"
                          "VERSION    : %s\n"
                          "EXTENSIONS : %s\n",
                          vendor ? vendor : "", version ? version : "",
                          extensions ? extensions : "");
}

/* ---------------------------------------------------------------------
 * Attribute lists, and the formats that answer them
 */

/* An attribute list read; FALSE if it asks for what there is not. */
static gboolean
request_from_attrlist (const int *attrlist, GdkGLRequest *request)
{
  gboolean rgba = FALSE, doublebuffer = FALSE, context = FALSE;
  const int *p;

  memset (request, 0, sizeof (GdkGLRequest));
  if (!attrlist)
    return FALSE;

  for (p = attrlist; *p != GDK_GL_NONE; p++)
    {
      switch (*p)
        {
        case GDK_GL_USE_GL:
          break;
        case GDK_GL_RGBA:
          rgba = TRUE;
          break;
        case GDK_GL_DOUBLEBUFFER:
          doublebuffer = TRUE;
          break;
        case GDK_GL_STEREO:
          return FALSE;

        case GDK_GL_RED_SIZE:
          request->red = *++p;
          break;
        case GDK_GL_GREEN_SIZE:
          request->green = *++p;
          break;
        case GDK_GL_BLUE_SIZE:
          request->blue = *++p;
          break;
        case GDK_GL_ALPHA_SIZE:
          request->alpha = *++p;
          break;
        case GDK_GL_DEPTH_SIZE:
          request->depth = *++p;
          break;
        case GDK_GL_STENCIL_SIZE:
          request->stencil = *++p;
          break;
        case GDK_GL_SAMPLE_BUFFERS:
          request->sample_buffers = *++p;
          break;
        case GDK_GL_SAMPLES:
          request->samples = *++p;
          break;

          /* Nothing but none of these is to be had. */
        case GDK_GL_LEVEL:
        case GDK_GL_AUX_BUFFERS:
        case GDK_GL_ACCUM_RED_SIZE:
        case GDK_GL_ACCUM_GREEN_SIZE:
        case GDK_GL_ACCUM_BLUE_SIZE:
        case GDK_GL_ACCUM_ALPHA_SIZE:
          if (*++p != 0)
            return FALSE;
          break;

          /* GLX read these and went by something else. */
        case GDK_GL_BUFFER_SIZE:
        case GDK_GL_X_VISUAL_TYPE_EXT:
        case GDK_GL_TRANSPARENT_TYPE_EXT:
        case GDK_GL_TRANSPARENT_INDEX_VALUE_EXT:
        case GDK_GL_TRANSPARENT_RED_VALUE_EXT:
        case GDK_GL_TRANSPARENT_GREEN_VALUE_EXT:
        case GDK_GL_TRANSPARENT_BLUE_VALUE_EXT:
        case GDK_GL_TRANSPARENT_ALPHA_VALUE_EXT:
          p++;
          break;

        case GDK_GL_CONTEXT_MAJOR_VERSION:
          request->major = *++p;
          context = TRUE;
          break;
        case GDK_GL_CONTEXT_MINOR_VERSION:
          request->minor = *++p;
          context = TRUE;
          break;
        case GDK_GL_CONTEXT_FLAGS:
          request->flags = *++p;
          context = TRUE;
          break;
        case GDK_GL_CONTEXT_PROFILE_MASK:
          request->profile = *++p;
          context = TRUE;
          break;

        default:
          g_warning ("gdk_gl_choose_visual: attribute %d is not one there is", *p);
          return FALSE;
        }
    }

  /*
   * Colour index is long gone, and nothing GL has now would show a
   * program that does not swap what it drew: a program that asks for
   * either is told there is no such visual, as it always could be.
   */
  if (!rgba && !context)
    return FALSE;
  if (!doublebuffer)
    return FALSE;
  if (request->red < 0 || request->green < 0 || request->blue < 0 ||
      request->alpha < 0 || request->depth < 0 || request->stencil < 0 ||
      request->sample_buffers < 0 || request->samples < 0 ||
      request->major < 0 || request->minor < 0)
    return FALSE;
  /* A context of a version has that version's number. */
  if (context && request->major == 0)
    request->major = (request->profile & GDK_GL_CONTEXT_ES_PROFILE_BIT) ? 2 : 1;

  return TRUE;
}

/*
 * How far a size is from what is wanted, less being better: nothing
 * asked for wants as little as there is; something asked for wants
 * the usual size if that is enough, else the least that is.
 */
static gint
size_distance (gint have, gint want, gint usual)
{
  if (want == 0)
    return have;
  if (want <= usual)
    return have == usual ? 0 : have > usual ? have - usual : 64 + usual - have;
  return have - want;
}

static gint
config_get (EGLConfig config, EGLint attribute)
{
  EGLint value = 0;

  eglGetConfigAttrib (egl_display, config, attribute, &value);
  return value;
}

/*
 * The config for a request: of those that have at least what was
 * asked, the one a window can be made for without making it again --
 * on X11, one for the visual GDK's windows have -- and then the one
 * glXChooseVisual would have liked best.
 */
static gboolean
config_for_request (const GdkGLRequest *request, EGLenum api, EGLConfig *config)
{
  EGLint attribs[32], count = 0, i, n = 0;
  EGLConfig *configs;
  gint best = -1, best_score[7] = { 0 };
  gint visualid = 0;

#ifdef GDK_WINDOWING_X11
  if (GDK_IS_X11 ())
    visualid = (gint) GDK_VISUAL_XVISUAL (gdk_visual_get_system ())->visualid;
#endif

  attribs[n++] = EGL_SURFACE_TYPE;
  attribs[n++] = EGL_WINDOW_BIT;
  attribs[n++] = EGL_COLOR_BUFFER_TYPE;
  attribs[n++] = EGL_RGB_BUFFER;
  attribs[n++] = EGL_RENDERABLE_TYPE;
  if (api == EGL_OPENGL_API)
    attribs[n++] = EGL_OPENGL_BIT;
  else
    attribs[n++] = request->major >= 3 ? EGL_OPENGL_ES3_BIT :
      request->major == 1 ? EGL_OPENGL_ES_BIT : EGL_OPENGL_ES2_BIT;
  attribs[n++] = EGL_RED_SIZE;
  attribs[n++] = request->red;
  attribs[n++] = EGL_GREEN_SIZE;
  attribs[n++] = request->green;
  attribs[n++] = EGL_BLUE_SIZE;
  attribs[n++] = request->blue;
  attribs[n++] = EGL_ALPHA_SIZE;
  attribs[n++] = request->alpha;
  attribs[n++] = EGL_DEPTH_SIZE;
  attribs[n++] = request->depth;
  attribs[n++] = EGL_STENCIL_SIZE;
  attribs[n++] = request->stencil;
  attribs[n++] = EGL_SAMPLE_BUFFERS;
  attribs[n++] = request->sample_buffers;
  attribs[n++] = EGL_SAMPLES;
  attribs[n++] = request->samples;
  attribs[n++] = EGL_NONE;

  if (!eglChooseConfig (egl_display, attribs, NULL, 0, &count) || count < 1)
    return FALSE;
  configs = g_new (EGLConfig, count);
  if (!eglChooseConfig (egl_display, attribs, configs, count, &count))
    count = 0;

  for (i = 0; i < count; i++)
    {
      gint score[7], k;

      score[0] = config_get (configs[i], EGL_CONFIG_CAVEAT) != EGL_NONE;
      score[1] = visualid != 0 &&
        config_get (configs[i], EGL_NATIVE_VISUAL_ID) != visualid;
      score[2] = size_distance (config_get (configs[i], EGL_ALPHA_SIZE), request->alpha, 8);
      score[3] =
        size_distance (config_get (configs[i], EGL_RED_SIZE), MAX (request->red, 1), 8) +
        size_distance (config_get (configs[i], EGL_GREEN_SIZE), MAX (request->green, 1), 8) +
        size_distance (config_get (configs[i], EGL_BLUE_SIZE), MAX (request->blue, 1), 8);
      score[4] = size_distance (config_get (configs[i], EGL_DEPTH_SIZE), request->depth, 24);
      score[5] = config_get (configs[i], EGL_STENCIL_SIZE) - request->stencil;
      score[6] = config_get (configs[i], EGL_SAMPLES) - request->samples;

      for (k = 0; k < 7 && best >= 0; k++)
        if (score[k] != best_score[k])
          break;
      if (best < 0 || (k < 7 && score[k] < best_score[k]))
        {
          best = i;
          memcpy (best_score, score, sizeof score);
        }
    }

  if (best >= 0)
    *config = configs[best];
  g_free (configs);

  return best >= 0;
}

static GdkGLFormat *
format_for_request (const GdkGLRequest *request)
{
  GdkGLFormat *format;
  EGLConfig config;
  EGLenum api;
  GSList *l;

  if (!gdk_gl_init ())
    return NULL;

  api = (request->profile & GDK_GL_CONTEXT_ES_PROFILE_BIT)
    ? EGL_OPENGL_ES_API : EGL_OPENGL_API;
  /* A context of a version, a profile or with flags takes an EGL that
   * can be asked for one; OpenGL ES has always been asked by version. */
  if (api == EGL_OPENGL_API && request->major > 0 && !egl_create_context)
    return NULL;
  if (!eglBindAPI (api) || !config_for_request (request, api, &config))
    return NULL;

  for (l = gl_formats; l; l = l->next)
    {
      format = l->data;
      if (format->config == config && format->api == api &&
          format->major == request->major && format->minor == request->minor &&
          format->profile == request->profile && format->flags == request->flags)
        return format;
    }

  format = g_new0 (GdkGLFormat, 1);
  format->visual = *gdk_visual_get_system ();
  format->config = config;
  format->api = api;
  format->major = request->major;
  format->minor = request->minor;
  format->profile = request->profile;
  format->flags = request->flags;
  gl_formats = g_slist_prepend (gl_formats, format);

  return format;
}

/*
 * The format a visual stands for. A visual that came from somewhere
 * else -- GDK's own, as an old program might pass -- stands for what a
 * GLX visual usually had: colour, a depth buffer and a stencil.
 */
static GdkGLFormat *
format_for_visual (GdkVisual *visual)
{
  GdkGLRequest request;

  if (visual && g_slist_find (gl_formats, visual))
    return (GdkGLFormat *) visual;

  if (!gl_default_format)
    {
      memset (&request, 0, sizeof request);
      request.red = request.green = request.blue = 1;
      request.depth = 1;
      request.stencil = 1;
      gl_default_format = format_for_request (&request);
      if (!gl_default_format)
        {
          request.stencil = 0;
          gl_default_format = format_for_request (&request);
        }
    }
  return gl_default_format;
}

GdkVisual *
gdk_gl_choose_visual (int *attrlist)
{
  GdkGLRequest request;
  GdkGLFormat *format;

  g_return_val_if_fail (attrlist != NULL, NULL);

  if (!request_from_attrlist (attrlist, &request))
    return NULL;
  format = format_for_request (&request);

  return format ? &format->visual : NULL;
}

int
gdk_gl_get_config (GdkVisual *visual, int attrib)
{
  GdkGLFormat *format;

  g_return_val_if_fail (visual != NULL, -1);

  if (!gdk_gl_init () || (format = format_for_visual (visual)) == NULL)
    return -1;

  switch (attrib)
    {
    case GDK_GL_USE_GL:
    case GDK_GL_RGBA:
    case GDK_GL_DOUBLEBUFFER:
      return TRUE;
    case GDK_GL_STEREO:
      return FALSE;
    case GDK_GL_LEVEL:
    case GDK_GL_AUX_BUFFERS:
    case GDK_GL_ACCUM_RED_SIZE:
    case GDK_GL_ACCUM_GREEN_SIZE:
    case GDK_GL_ACCUM_BLUE_SIZE:
    case GDK_GL_ACCUM_ALPHA_SIZE:
      return 0;
    case GDK_GL_BUFFER_SIZE:
      return config_get (format->config, EGL_BUFFER_SIZE);
    case GDK_GL_RED_SIZE:
      return config_get (format->config, EGL_RED_SIZE);
    case GDK_GL_GREEN_SIZE:
      return config_get (format->config, EGL_GREEN_SIZE);
    case GDK_GL_BLUE_SIZE:
      return config_get (format->config, EGL_BLUE_SIZE);
    case GDK_GL_ALPHA_SIZE:
      return config_get (format->config, EGL_ALPHA_SIZE);
    case GDK_GL_DEPTH_SIZE:
      return config_get (format->config, EGL_DEPTH_SIZE);
    case GDK_GL_STENCIL_SIZE:
      return config_get (format->config, EGL_STENCIL_SIZE);
    case GDK_GL_SAMPLE_BUFFERS:
      return config_get (format->config, EGL_SAMPLE_BUFFERS);
    case GDK_GL_SAMPLES:
      return config_get (format->config, EGL_SAMPLES);
    case GDK_GL_CONTEXT_MAJOR_VERSION:
      return format->major;
    case GDK_GL_CONTEXT_MINOR_VERSION:
      return format->minor;
    case GDK_GL_CONTEXT_FLAGS:
      return format->flags;
    case GDK_GL_CONTEXT_PROFILE_MASK:
      return format->profile;
    default:
      return -1;
    }
}

/* ---------------------------------------------------------------------
 * Contexts
 */

static GdkGLContext *
context_new (GdkGLFormat *format, GdkGLContext *share)
{
  GdkGLContext *context;
  EGLContext egl_context;
  EGLint attribs[16], n = 0;
  gint flags;

  if (!format || !eglBindAPI (format->api))
    return NULL;

  if (format->api == EGL_OPENGL_ES_API)
    {
      attribs[n++] = EGL_CONTEXT_MAJOR_VERSION;   /* the client version */
      attribs[n++] = format->major;
      if (format->minor > 0 && egl_create_context)
        {
          attribs[n++] = EGL_CONTEXT_MINOR_VERSION;
          attribs[n++] = format->minor;
        }
    }
  else if (format->major > 0)
    {
      gint profile = format->profile;

      attribs[n++] = EGL_CONTEXT_MAJOR_VERSION;
      attribs[n++] = format->major;
      attribs[n++] = EGL_CONTEXT_MINOR_VERSION;
      attribs[n++] = format->minor;
      /* Profiles came with 3.2, and the core one is what that gives
       * unless asked for the other. */
      if (format->major > 3 || (format->major == 3 && format->minor >= 2))
        {
          attribs[n++] = EGL_CONTEXT_OPENGL_PROFILE_MASK;
          attribs[n++] = (profile & GDK_GL_CONTEXT_COMPATIBILITY_PROFILE_BIT) &&
            !(profile & GDK_GL_CONTEXT_CORE_PROFILE_BIT)
            ? EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT
            : EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT;
        }
    }
  /* OpenGL ES has no contexts that are forward compatible: all are. */
  flags = format->flags & (GDK_GL_CONTEXT_DEBUG_BIT |
                           (format->api == EGL_OPENGL_API
                            ? GDK_GL_CONTEXT_FORWARD_COMPATIBLE_BIT : 0));
  if (flags && egl_create_context)
    {
      if (egl_1_5)
        {
          if (flags & GDK_GL_CONTEXT_DEBUG_BIT)
            {
              attribs[n++] = EGL_CONTEXT_OPENGL_DEBUG;
              attribs[n++] = EGL_TRUE;
            }
          if (flags & GDK_GL_CONTEXT_FORWARD_COMPATIBLE_BIT)
            {
              attribs[n++] = EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE;
              attribs[n++] = EGL_TRUE;
            }
        }
      else
        {
          /* The extension's flags are GLX's, bit for bit. */
          attribs[n++] = EGL_CONTEXT_FLAGS_KHR;
          attribs[n++] = flags;
        }
    }
  attribs[n++] = EGL_NONE;

  egl_context = eglCreateContext (egl_display, format->config,
                                  share ? share->context : EGL_NO_CONTEXT,
                                  attribs);
  if (egl_context == EGL_NO_CONTEXT)
    return NULL;

  context = g_new0 (GdkGLContext, 1);
  context->ref_count = 1;
  context->format = format;
  context->context = egl_context;

  return context;
}

GdkGLContext *
gdk_gl_context_new (GdkVisual *visual)
{
  return gdk_gl_context_share_new (visual, NULL, FALSE);
}

GdkGLContext *
gdk_gl_context_share_new (GdkVisual    *visual,
                          GdkGLContext *sharelist,
                          gint          direct)
{
  g_return_val_if_fail (visual != NULL, NULL);

  if (!gdk_gl_init ())
    return NULL;
  return context_new (format_for_visual (visual), sharelist);
}

GdkGLContext *
gdk_gl_context_attrlist_share_new (int          *attrlist,
                                   GdkGLContext *sharelist,
                                   gint          direct)
{
  GdkVisual *visual;

  g_return_val_if_fail (attrlist != NULL, NULL);

  visual = gdk_gl_choose_visual (attrlist);
  if (!visual)
    return NULL;
  return context_new ((GdkGLFormat *) visual, sharelist);
}

GdkGLContext *
gdk_gl_context_ref (GdkGLContext *context)
{
  g_return_val_if_fail (context != NULL, NULL);

  context->ref_count += 1;
  return context;
}

void
gdk_gl_context_unref (GdkGLContext *context)
{
  GSList *l;

  g_return_if_fail (context != NULL);
  g_return_if_fail (context->ref_count > 0);

  context->ref_count -= 1;
  if (context->ref_count > 0)
    return;

  if (current_context == context)
    gdk_gl_release_current ();
  for (l = gl_windows; l; l = l->next)
    if (((GdkGLWindow *) l->data)->context == context)
      ((GdkGLWindow *) l->data)->context = NULL;
  if (egl_display != EGL_NO_DISPLAY)
    eglDestroyContext (egl_display, context->context);
  g_free (context);
}

/* ---------------------------------------------------------------------
 * Windows
 */

static void
window_device_size (GdkWindow *window, gint *width, gint *height, gint *scale)
{
  gint w, h, s = gdk_window_get_scale_factor (window);

  gdk_window_get_size (window, &w, &h);
  *width = MAX (w * s, 1);
  *height = MAX (h * s, 1);
  *scale = s;
}

#ifdef GDK_WINDOWING_WAYLAND

/* Milliseconds since `since', by a clock that only goes forward. */
static glong
elapsed_ms (const struct timespec *since)
{
  struct timespec now;

  if (clock_gettime (CLOCK_MONOTONIC, &now) != 0)
    return FRAME_WAIT;
  return (glong) (now.tv_sec - since->tv_sec) * 1000 +
    (now.tv_nsec - since->tv_nsec) / 1000000;
}

/* Nanoseconds, by the same clock. */
static gint64
clock_ns (void)
{
  struct timespec now;

  if (clock_gettime (CLOCK_MONOTONIC, &now) != 0)
    return 0;
  return (gint64) now.tv_sec * 1000000000 + now.tv_nsec;
}

/*
 * A frame that came in good time is waited for again; one that was
 * long in coming, as the first is when a window comes back into view,
 * or as all are from a compositor that is hard pressed, leaves the
 * next unwaited for.
 */
static void
frame_done (void *data, struct wl_callback *callback, uint32_t time)
{
  GdkGLWindow *glwin = data;

  wl_callback_destroy (callback);
  glwin->frame = NULL;
  glwin->frames += 1;
  glwin->starved = elapsed_ms (&glwin->frame_asked) > FRAME_WAIT;
}

static const struct wl_callback_listener frame_listener = {
  frame_done
};

/* A frame asked for: it comes after the surface's next commit. */
static void
frame_request (GdkGLWindow *glwin)
{
  glwin->frame = wl_surface_frame (glwin->wrapper);
  wl_callback_add_listener (glwin->frame, &frame_listener, glwin);
  if (clock_gettime (CLOCK_MONOTONIC, &glwin->frame_asked) != 0)
    memset (&glwin->frame_asked, 0, sizeof glwin->frame_asked);
}

/*
 * The frame the last swap was promised, waited for, but not for ever:
 * a compositor promises no frame to a window nobody can see, and the
 * program has other things to do than wait for one. Only the frame's
 * own queue is dispatched here; what else comes in while waiting stays
 * queued for GDK to find.
 */
static void
frame_wait (GdkGLWindow *glwin)
{
  struct wl_display *display = gdk_wayland_display ();
  struct timespec start;

  if (clock_gettime (CLOCK_MONOTONIC, &start) != 0)
    return;

  while (glwin->frame)
    {
      struct pollfd pfd;
      glong remaining;
      gint ready, error;

      if (wl_display_prepare_read_queue (display, frame_queue) != 0)
        {
          if (wl_display_dispatch_queue_pending (display, frame_queue) < 0)
            break;
          continue;
        }
      wl_display_flush (display);

      remaining = FRAME_WAIT - elapsed_ms (&start);
      pfd.fd = wl_display_get_fd (display);
      pfd.events = POLLIN;
      pfd.revents = 0;
      ready = remaining > 0 ? poll (&pfd, 1, (int) MIN (remaining, FRAME_WAIT)) : 0;
      error = errno;
      if (ready > 0 && (pfd.revents & POLLIN))
        {
          if (wl_display_read_events (display) < 0 ||
              wl_display_dispatch_queue_pending (display, frame_queue) < 0)
            break;
          continue;
        }
      wl_display_cancel_read (display);
      if (ready < 0 && error == EINTR)
        continue;
      break;
    }
}

/*
 * A window that is shown no frames -- hidden, on another workspace,
 * covered whole -- has its swaps let go by the clock instead, one for
 * every step of it. A program that draws as fast as its swaps come
 * back then draws no faster for not being seen, and the thread it
 * draws in, which is the one its menus are in, is never held for
 * longer than a step.
 */
static void
frame_clock (GdkGLWindow *glwin, gint64 step)
{
  gint64 now = clock_ns ();

  if (glwin->pace_next < now - step || glwin->pace_next > now + step)
    glwin->pace_next = now;     /* the first, or the first in a while */
  else if (glwin->pace_next > now)
    {
      struct timespec until;

      until.tv_sec = glwin->pace_next / 1000000000;
      until.tv_nsec = glwin->pace_next % 1000000000;
      while (clock_nanosleep (CLOCK_MONOTONIC, TIMER_ABSTIME, &until, NULL) == EINTR)
        ;
    }
  glwin->pace_next += step;
}

/*
 * Before a swap: a frame is waited for for each refresh of the screen
 * the swap is to wait for, and one is asked for to come after it. The
 * first is promised with the swap before; any more are asked for with
 * a commit that shows nothing new.
 */
static void
frame_pace (GdkGLWindow *glwin)
{
  struct wl_display *display = gdk_wayland_display ();

  wl_display_dispatch_queue_pending (display, frame_queue);
  if (glwin->swap_interval <= 0)
    return;

  /* What the program has hidden is known to be shown no frames. */
  if (!gdk_window_is_viewable (glwin->window))
    glwin->starved = TRUE;

  while (glwin->swapped && !glwin->starved && glwin->frames < glwin->swap_interval)
    {
      if (!glwin->frame)
        {
          frame_request (glwin);
          wl_surface_commit (glwin->wrapper);
        }
      frame_wait (glwin);
      if (glwin->frame)
        glwin->starved = TRUE;
    }
  if (glwin->starved)
    {
      gint rate = gdk_window_get_refresh_rate (glwin->window);
      gint64 period = rate > 0 ? (gint64) 1000000000 * 1000 / rate : FRAME_PERIOD;

      frame_clock (glwin, period * glwin->swap_interval);
    }
  if (!glwin->frame)
    frame_request (glwin);
}

#endif /* GDK_WINDOWING_WAYLAND */

static void
gl_window_destroy_surface (GdkGLWindow *glwin)
{
  if (current_window == glwin)
    gdk_gl_release_current ();
  if (glwin->surface != EGL_NO_SURFACE && egl_display != EGL_NO_DISPLAY)
    eglDestroySurface (egl_display, glwin->surface);
  glwin->surface = EGL_NO_SURFACE;
  glwin->interval_set = FALSE;

#ifdef GDK_WINDOWING_WAYLAND
  if (glwin->frame)
    wl_callback_destroy (glwin->frame);
  glwin->frame = NULL;
  glwin->frames = 0;
  glwin->swapped = FALSE;
  glwin->starved = FALSE;
  if (glwin->wrapper)
    wl_proxy_wrapper_destroy (glwin->wrapper);
  glwin->wrapper = NULL;
  if (glwin->egl_window)
    wl_egl_window_destroy (glwin->egl_window);
  glwin->egl_window = NULL;
#endif
}

static gboolean
gl_window_create_surface (GdkGLWindow *glwin)
{
  EGLint attribs[3], n = 0;
  EGLNativeWindowType native = 0;
  gboolean have_native = FALSE;

  window_device_size (glwin->window, &glwin->width, &glwin->height, &glwin->scale);

  /*
   * A window is not to be seen through, whatever the alpha GL leaves
   * in it: a program that wanted alpha to draw with did not want a
   * hole in its window, and never got one. Where EGL cannot be told
   * so, a compositor shows the hole; a config with alpha is chosen
   * only for a program that asked for alpha.
   */
  if (egl_present_opaque && config_get (glwin->format->config, EGL_ALPHA_SIZE) > 0)
    {
      attribs[n++] = EGL_PRESENT_OPAQUE_EXT;
      attribs[n++] = EGL_TRUE;
    }
  attribs[n++] = EGL_NONE;

#ifdef GDK_WINDOWING_X11
  if (GDK_IS_X11 ())
    {
      gint visualid = config_get (glwin->format->config, EGL_NATIVE_VISUAL_ID);

      /* Nearly always the visual the window has already. */
      if (visualid != 0 &&
          !gdk_x11_window_set_xvisual (glwin->window, (VisualID) visualid))
        return FALSE;
      native = (EGLNativeWindowType) (gulong) GDK_WINDOW_XWINDOW (glwin->window);
      have_native = TRUE;
    }
#endif
#ifdef GDK_WINDOWING_WAYLAND
  if (GDK_IS_WAYLAND ())
    {
      struct wl_surface *surface = gdk_wayland_window_surface (glwin->window);

      if (!surface)
        return FALSE;
      if (!frame_queue)
        frame_queue = wl_display_create_queue (gdk_wayland_display ());
      glwin->wrapper = wl_proxy_create_wrapper (surface);
      wl_proxy_set_queue ((struct wl_proxy *) glwin->wrapper, frame_queue);
      glwin->egl_window = wl_egl_window_create (surface, glwin->width, glwin->height);
      if (!glwin->egl_window)
        {
          gl_window_destroy_surface (glwin);
          return FALSE;
        }
      /* Said with the first buffer, which is of this scale's size. */
      if (wl_proxy_get_version ((struct wl_proxy *) surface) >= 3)
        wl_surface_set_buffer_scale (surface,
                                     gdk_wayland_window_buffer_scale (glwin->window));
      native = (EGLNativeWindowType) glwin->egl_window;
      have_native = TRUE;
    }
#endif
  if (!have_native)
    return FALSE;

  glwin->surface = eglCreateWindowSurface (egl_display, glwin->format->config,
                                           native, attribs);
  if (glwin->surface == EGL_NO_SURFACE)
    {
      gl_window_destroy_surface (glwin);
      return FALSE;
    }
  return TRUE;
}

/* GDK is about to take the window system's window away. */
static void
gl_window_release (gpointer data)
{
  GdkGLWindow *glwin = data;

  gl_window_destroy_surface (glwin);
  g_dataset_remove_data (glwin->window, gl_window_key);
  gl_windows = g_slist_remove (gl_windows, glwin);
  g_free (glwin);
}

static GdkGLWindow *
gl_window_lookup (GdkDrawable *drawable)
{
  return g_dataset_get_data (drawable, gl_window_key);
}

/* What GL has of the window, made if it had nothing yet. */
static GdkGLWindow *
gl_window_get (GdkDrawable *drawable)
{
  GdkGLWindow *glwin = gl_window_lookup (drawable);

  if (glwin)
    return glwin;
  if (gdk_window_get_type (drawable) != GDK_WINDOW_CHILD)
    {
      g_warning ("GL draws in child windows only: give it a GtkGLArea, "
                 "or a drawing area's window");
      return NULL;
    }

  glwin = g_new0 (GdkGLWindow, 1);
  glwin->window = drawable;
  glwin->surface = EGL_NO_SURFACE;
  glwin->swap_interval = 1;
  gdk_window_set_external (drawable, gl_window_release, glwin);
  if (!gdk_window_is_external (drawable))
    {
      g_free (glwin);
      return NULL;
    }
  g_dataset_set_data (drawable, gl_window_key, glwin);
  gl_windows = g_slist_prepend (gl_windows, glwin);

  return glwin;
}

static gboolean
gl_bind (GdkGLWindow *glwin, GdkGLContext *context)
{
  if (!eglBindAPI (context->format->api) ||
      !eglMakeCurrent (egl_display, glwin->surface, glwin->surface, context->context))
    return FALSE;

  current_window = glwin;
  current_context = context;
  glwin->context = context;
  if (!glwin->interval_set)
    {
      gint interval = glwin->swap_interval;

#ifdef GDK_WINDOWING_WAYLAND
      /* EGL would wait for a frame without end; frame_pace waits here. */
      if (GDK_IS_WAYLAND ())
        interval = 0;
#endif
      eglSwapInterval (egl_display, interval);
      glwin->interval_set = TRUE;
    }
  return TRUE;
}

/*
 * The surface brought to the window's size. On X11 it follows the X
 * window by itself. On Wayland it is as big as it is told to be; and
 * when it is the scale that changed, it is made again, since a buffer
 * drawn for the old scale must not be shown as one of the new.
 */
static void
gl_window_sync (GdkGLWindow *glwin)
{
  gint width, height, scale;

  if (glwin->surface == EGL_NO_SURFACE)
    return;
  window_device_size (glwin->window, &width, &height, &scale);
  if (width == glwin->width && height == glwin->height && scale == glwin->scale)
    return;

#ifdef GDK_WINDOWING_WAYLAND
  if (GDK_IS_WAYLAND ())
    {
      if (scale != glwin->scale)
        {
          GdkGLContext *context = current_window == glwin ? current_context : NULL;

          gl_window_destroy_surface (glwin);
          if (gl_window_create_surface (glwin) && context)
            gl_bind (glwin, context);
          return;
        }
      wl_egl_window_resize (glwin->egl_window, width, height, 0, 0);
    }
#endif
  glwin->width = width;
  glwin->height = height;
  glwin->scale = scale;
}

gint
gdk_gl_make_current (GdkDrawable  *drawable,
                     GdkGLContext *context)
{
  GdkGLWindow *glwin;

  g_return_val_if_fail (drawable != NULL, FALSE);
  g_return_val_if_fail (context != NULL, FALSE);

  if (egl_display == EGL_NO_DISPLAY)
    return FALSE;
  glwin = gl_window_get (drawable);
  if (!glwin)
    return FALSE;

  /* A surface is for one config: the context's. */
  if (glwin->surface != EGL_NO_SURFACE &&
      glwin->format->config != context->format->config)
    gl_window_destroy_surface (glwin);
  if (glwin->surface == EGL_NO_SURFACE)
    {
      glwin->format = context->format;
      if (!gl_window_create_surface (glwin))
        return FALSE;
    }
  else
    gl_window_sync (glwin);
  if (glwin->surface == EGL_NO_SURFACE)
    return FALSE;

  if (current_window == glwin && current_context == context)
    return TRUE;
  return gl_bind (glwin, context);
}

void
gdk_gl_release_current (void)
{
  if (egl_display != EGL_NO_DISPLAY && current_context)
    {
      eglBindAPI (current_context->format->api);
      eglMakeCurrent (egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
  current_context = NULL;
  current_window = NULL;
}

void
gdk_gl_swap_buffers (GdkDrawable *drawable)
{
  GdkGLWindow *glwin, *was_window = NULL;
  GdkGLContext *was_context = NULL;
  gboolean borrowed = FALSE;

  g_return_if_fail (drawable != NULL);

  glwin = gl_window_lookup (drawable);
  if (!glwin || glwin->surface == EGL_NO_SURFACE || egl_display == EGL_NO_DISPLAY)
    return;

  /* GLX swapped any window; EGL swaps the one that is current. */
  if (current_window != glwin)
    {
      if (!glwin->context)
        return;
      was_window = current_window;
      was_context = current_context;
      if (!gl_bind (glwin, glwin->context))
        return;
      borrowed = TRUE;
    }

#ifdef GDK_WINDOWING_WAYLAND
  if (GDK_IS_WAYLAND ())
    {
      frame_pace (glwin);
      glwin->frames = 0;
      glwin->swapped = TRUE;
    }
#endif
  eglSwapBuffers (egl_display, glwin->surface);
  /* Nothing is drawn for the next frame yet: the time to change size. */
  gl_window_sync (glwin);

  if (borrowed)
    {
      if (was_window && was_context)
        gl_bind (was_window, was_context);
      else
        gdk_gl_release_current ();
    }
}

void
gdk_gl_get_drawable_size (GdkDrawable *drawable,
                          gint        *width,
                          gint        *height)
{
  GdkGLWindow *glwin;
  gint w, h, scale;

  g_return_if_fail (drawable != NULL);

  /* What the program is about to set its viewport to is what GL
   * draws in from here on, not from the next swap. */
  glwin = gl_window_lookup (drawable);
  if (glwin)
    gl_window_sync (glwin);
  window_device_size (drawable, &w, &h, &scale);
  if (width)
    *width = w;
  if (height)
    *height = h;
}

void
gdk_gl_set_swap_interval (GdkDrawable *drawable,
                          gint         interval)
{
  GdkGLWindow *glwin;

  g_return_if_fail (drawable != NULL);

  glwin = gl_window_get (drawable);
  if (!glwin)
    return;
  glwin->swap_interval = MAX (interval, 0);
  glwin->interval_set = FALSE;
  if (current_window == glwin && current_context)
    gl_bind (glwin, current_context);
}

GdkGLProc
gdk_gl_get_proc_address (const gchar *name)
{
  g_return_val_if_fail (name != NULL, NULL);

  if (!gdk_gl_init ())
    return NULL;
  return (GdkGLProc) eglGetProcAddress (name);
}

/* GDK draws in its own memory: nothing of its is ever in GL's way. */
void
gdk_gl_wait_gdk (void)
{
  gdk_flush ();
}

void
gdk_gl_wait_gl (void)
{
  if (egl_display != EGL_NO_DISPLAY && current_context)
    eglWaitClient ();
}
