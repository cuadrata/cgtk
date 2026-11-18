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
 * OpenGL in a GdkWindow. The interface is the one Janne Lof's
 * GtkGLArea gave GTK+ 1.2, so that programs written for it build
 * unchanged; what is behind it is EGL, the same on X11 and on Wayland.
 *
 * What a program draws with -- libGL, libOpenGL, libGLESv2 -- is its
 * own to include and link: nothing here does either.
 *
 * Differences a program written for the old library may meet:
 *
 *   The framebuffer is in the device's pixels: on a display scaled by
 *   two it is twice the window's width and height. A viewport is set
 *   from gdk_gl_get_drawable_size, not from the widget's allocation,
 *   and set again on every configure event: one comes when the window
 *   goes to a display of another scale, as when its size changes.
 *
 *   Only windows are drawn in, and only with two buffers: there are no
 *   GL pixmaps, no single buffered, stereo or colour index visuals, and
 *   no accumulation or auxiliary buffers. gdk_gl_choose_visual gives
 *   NULL when asked for any of them.
 *
 *   Text is the program's to draw: there is no gdk_gl_use_gdk_font.
 *
 *   These calls belong to the thread GTK+ runs in.
 */

#ifndef __GDK_GL_H__
#define __GDK_GL_H__

#include <glib.h>
#include <gdk/gdk.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * What an attribute list is made of. The values are GLX's, as they
 * always were, so a list written with GLX's names is read the same.
 * GDK_GL_USE_GL, GDK_GL_RGBA, GDK_GL_DOUBLEBUFFER and GDK_GL_STEREO
 * stand alone; every other attribute is followed by its value. The
 * list ends with GDK_GL_NONE.
 */
enum _GDK_GL_CONFIGS {
  GDK_GL_NONE                   = 0,
  GDK_GL_USE_GL                 = 1,
  GDK_GL_BUFFER_SIZE            = 2,
  GDK_GL_LEVEL                  = 3,
  GDK_GL_RGBA                   = 4,
  GDK_GL_DOUBLEBUFFER           = 5,
  GDK_GL_STEREO                 = 6,
  GDK_GL_AUX_BUFFERS            = 7,
  GDK_GL_RED_SIZE               = 8,
  GDK_GL_GREEN_SIZE             = 9,
  GDK_GL_BLUE_SIZE              = 10,
  GDK_GL_ALPHA_SIZE             = 11,
  GDK_GL_DEPTH_SIZE             = 12,
  GDK_GL_STENCIL_SIZE           = 13,
  GDK_GL_ACCUM_RED_SIZE         = 14,
  GDK_GL_ACCUM_GREEN_SIZE       = 15,
  GDK_GL_ACCUM_BLUE_SIZE        = 16,
  GDK_GL_ACCUM_ALPHA_SIZE       = 17,

  /* GLX_EXT_visual_info extension: read, and ignored */
  GDK_GL_X_VISUAL_TYPE_EXT              = 0x22,
  GDK_GL_TRANSPARENT_TYPE_EXT           = 0x23,
  GDK_GL_TRANSPARENT_INDEX_VALUE_EXT    = 0x24,
  GDK_GL_TRANSPARENT_RED_VALUE_EXT      = 0x25,
  GDK_GL_TRANSPARENT_GREEN_VALUE_EXT    = 0x26,
  GDK_GL_TRANSPARENT_BLUE_VALUE_EXT     = 0x27,
  GDK_GL_TRANSPARENT_ALPHA_VALUE_EXT    = 0x28,

  /* Multisampling: GLX_SAMPLE_BUFFERS and GLX_SAMPLES */
  GDK_GL_SAMPLE_BUFFERS         = 100000,
  GDK_GL_SAMPLES                = 100001,

  /*
   * The context to make, for a program that wants another than the
   * one GL always gave: GLX_ARB_create_context's attributes. With
   * none of these the context is what an old program expects, every
   * version of GL the system has with nothing taken out. With a
   * version of 3.2 or later the profile is the core one unless the
   * mask says otherwise; GDK_GL_CONTEXT_ES_PROFILE_BIT asks for
   * OpenGL ES of that version. Any of them implies GDK_GL_RGBA.
   */
  GDK_GL_CONTEXT_MAJOR_VERSION  = 0x2091,
  GDK_GL_CONTEXT_MINOR_VERSION  = 0x2092,
  GDK_GL_CONTEXT_FLAGS          = 0x2094,
  GDK_GL_CONTEXT_PROFILE_MASK   = 0x9126
};

/* The bits of GDK_GL_CONTEXT_FLAGS */
enum _GDK_GL_CONTEXT_FLAG_BITS {
  GDK_GL_CONTEXT_DEBUG_BIT               = 1 << 0,
  GDK_GL_CONTEXT_FORWARD_COMPATIBLE_BIT  = 1 << 1
};

/* The bits of GDK_GL_CONTEXT_PROFILE_MASK */
enum _GDK_GL_CONTEXT_PROFILE_BITS {
  GDK_GL_CONTEXT_CORE_PROFILE_BIT          = 1 << 0,
  GDK_GL_CONTEXT_COMPATIBILITY_PROFILE_BIT = 1 << 1,
  GDK_GL_CONTEXT_ES_PROFILE_BIT            = 1 << 2
};

typedef struct _GdkGLContext GdkGLContext;

/* A function of GL's, by name: see gdk_gl_get_proc_address. */
typedef void (*GdkGLProc) (void);

/* Whether GL can be used at all. */
gint          gdk_gl_query            (void);
/* Who provides it, in a string the caller frees. */
gchar        *gdk_gl_get_info         (void);

/*
 * What an attribute list asks for, as a visual. The visual stands
 * for the whole list, the context it asks for included: it is what
 * gdk_gl_context_new takes. It is no visual to make windows with --
 * GDK has one of those -- though a program that does is not harmed.
 * NULL if the system has nothing to answer the list with.
 */
GdkVisual    *gdk_gl_choose_visual    (int          *attrlist);
int           gdk_gl_get_config       (GdkVisual    *visual,
                                       int           attrib);

GdkGLContext *gdk_gl_context_new      (GdkVisual    *visual);
GdkGLContext *gdk_gl_context_share_new (GdkVisual    *visual,
                                        GdkGLContext *sharelist,
                                        gint          direct);
GdkGLContext *gdk_gl_context_attrlist_share_new (int          *attrlist,
                                                 GdkGLContext *sharelist,
                                                 gint          direct);

GdkGLContext *gdk_gl_context_ref      (GdkGLContext *context);
void          gdk_gl_context_unref    (GdkGLContext *context);

/*
 * GL draws in the window with the context from here on. The window
 * is a child window: it becomes an external one (see
 * gdk_window_set_external), in which GDK draws nothing.
 */
gint          gdk_gl_make_current     (GdkDrawable  *drawable,
                                       GdkGLContext *context);
void          gdk_gl_swap_buffers     (GdkDrawable  *drawable);

void          gdk_gl_wait_gdk         (void);
void          gdk_gl_wait_gl          (void);

/*
 * What the old library did not have.
 *
 * gdk_gl_release_current: no context is current any more.
 *
 * gdk_gl_get_drawable_size: the size of the framebuffer GL draws the
 * window in, in the device's pixels. What is drawn from then on is
 * drawn at that size.
 *
 * gdk_gl_set_swap_interval: how many refreshes of the screen a swap
 * waits for, so that no more than one frame is shown for that many:
 * one unless set, none for 0. The wait is spent in gdk_gl_swap_buffers,
 * in the thread GTK+ runs in, and is never much longer than the frames
 * are apart. On Wayland a window that is shown no frames -- hidden,
 * on another workspace, covered whole -- would be waited for without
 * end, so it is not: its swaps are let go by the clock, at the rate of
 * its screen (gdk_window_get_refresh_rate). On X the wait is the
 * driver's.
 *
 * A program that draws without pause, as a game or an emulator does,
 * draws a frame and swaps from an idle function, and its swaps set its
 * pace; its menus and dialogs get their turn between one frame and the
 * next. examples/glarea/glloop.c is one. A program that keeps time
 * by a clock of its own has no use for the wait, and sets 0.
 *
 * gdk_gl_get_proc_address: a function of GL's by its name, for a
 * program that does not link the library it is in, or cannot know
 * whether the function is there. A context is current when it asks.
 */
void          gdk_gl_release_current   (void);
void          gdk_gl_get_drawable_size (GdkDrawable *drawable,
                                        gint        *width,
                                        gint        *height);
void          gdk_gl_set_swap_interval (GdkDrawable *drawable,
                                        gint         interval);
GdkGLProc     gdk_gl_get_proc_address  (const gchar *name);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GDK_GL_H__ */
