/* GDK - The GIMP Drawing Kit
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
 * Framebuffers onto the X server. This is the whole of what X11 does
 * for drawing now: a window's pixels, once GDK has drawn them, are put
 * into the X window with XPutImage, converted on the way to whatever
 * the screen's visual wants. A pixmap gets an X pixmap only when X
 * itself must have one -- a shape mask, an icon, a cursor -- and that
 * copy is refreshed from the framebuffer whenever it has changed since.
 */

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "gdk.h"
#include "gdkprivate-x11.h"
#include "gdkbackend-x11.h"
#include "gdkframebuffer.h"

static gboolean window_set_xvisual (GdkWindowPrivate *private, VisualID xvisualid);

static Visual   *x_visual = NULL;
static Colormap  x_colormap = None;
static gint      x_depth = 0;
static gint      x_bits_per_pixel = 0;
static gboolean  x_direct = FALSE;   /* the visual takes 0x00RRGGBB as is */
static gint      x_shift[3], x_prec[3];
static GC        x_gc = None;        /* for drawables of the screen's depth */
static GC        x_gc_bitmap = None; /* for depth 1 pixmaps */

/* The number of low bits set in a mask, and where they start. */
static void
mask_decompose (gulong mask, gint *shift, gint *prec)
{
  *shift = 0;
  *prec = 0;
  if (mask == 0)
    return;
  while (!(mask & 1))
    {
      mask >>= 1;
      (*shift)++;
    }
  while (mask & 1)
    {
      mask >>= 1;
      (*prec)++;
    }
}

/*
 * The visual every window is made with: the default one if it is true
 * colour, else the deepest true colour visual the screen has.
 */
void
_gdk_x11_publish_init (void)
{
  XVisualInfo template, *info;
  XPixmapFormatValues *formats;
  gint n, i, best = -1;

  template.screen = gdk_screen;
  info = XGetVisualInfo (gdk_display, VisualScreenMask, &template, &n);
  for (i = 0; i < n; i++)
    {
      if (info[i].class != TrueColor && info[i].class != DirectColor)
        continue;
      if (info[i].visual == DefaultVisual (gdk_display, gdk_screen) &&
          info[i].depth >= 16)
        {
          best = i;
          break;
        }
      if (best < 0 || info[i].depth > info[best].depth ||
          (info[i].depth == info[best].depth && info[i].class == TrueColor))
        best = i;
    }
  if (best < 0)
    g_error ("GDK needs a true colour visual, and the display has none");

  x_visual = info[best].visual;
  x_depth = info[best].depth;
  mask_decompose (info[best].red_mask, &x_shift[0], &x_prec[0]);
  mask_decompose (info[best].green_mask, &x_shift[1], &x_prec[1]);
  mask_decompose (info[best].blue_mask, &x_shift[2], &x_prec[2]);
  x_direct = info[best].red_mask == 0xff0000 &&
    info[best].green_mask == 0x00ff00 && info[best].blue_mask == 0x0000ff;

  if (x_visual == DefaultVisual (gdk_display, gdk_screen))
    x_colormap = DefaultColormap (gdk_display, gdk_screen);
  else
    x_colormap = XCreateColormap (gdk_display, gdk_root_window, x_visual,
                                  AllocNone);
  XFree (info);

  x_bits_per_pixel = x_depth;
  formats = XListPixmapFormats (gdk_display, &n);
  for (i = 0; i < n; i++)
    if (formats[i].depth == x_depth)
      x_bits_per_pixel = formats[i].bits_per_pixel;
  XFree (formats);
  if (x_bits_per_pixel != 16 && x_bits_per_pixel != 32)
    g_error ("GDK cannot draw to a screen with %d bits a pixel", x_bits_per_pixel);
}

GdkWindowImpl *
_gdk_x11_impl_new (Window xwindow)
{
  GdkWindowImpl *impl = g_new0 (GdkWindowImpl, 1);

  impl->xwindow = xwindow;
  impl->xdisplay = gdk_display;
  impl->xpixmap = None;

  return impl;
}

Visual *
_gdk_x11_visual (void)
{
  return x_visual;
}

Colormap
_gdk_x11_colormap (void)
{
  return x_colormap;
}

gint
_gdk_x11_depth (void)
{
  return x_depth;
}

static GC
gc_for_depth (Drawable drawable, gint depth)
{
  if (depth == 1)
    {
      if (x_gc_bitmap == None)
        {
          XGCValues values;

          values.foreground = 1;
          values.background = 0;
          values.graphics_exposures = False;
          x_gc_bitmap = XCreateGC (gdk_display, drawable,
                                   GCForeground | GCBackground | GCGraphicsExposures,
                                   &values);
        }
      return x_gc_bitmap;
    }
  if (x_gc == None)
    {
      XGCValues values;

      values.graphics_exposures = False;
      x_gc = XCreateGC (gdk_display, drawable, GCGraphicsExposures, &values);
    }
  return x_gc;
}

/*
 * A rectangle of a framebuffer put onto an X drawable at the same
 * place. Colour goes through an image in the screen's format; a bitmap
 * through an XYBitmap the server expands with the GC's 1 and 0.
 */
static void
put_rect (GdkFramebuffer *fb, Drawable drawable, const GdkRectangle *rect,
          gint xdest, gint ydest)
{
  XImage *ximage;
  gchar *data;
  gint x, y;

  if (fb->depth == 1)
    {
      gint stride = (rect->width + 7) / 8;

      data = g_malloc0 ((gsize) stride * rect->height);
      for (y = 0; y < rect->height; y++)
        {
          const guint32 *in = fb->pixels + (gsize) (rect->y + y) * fb->stride + rect->x;
          guchar *out = (guchar *) data + y * stride;

          for (x = 0; x < rect->width; x++)
            if (in[x] & 1)
              out[x >> 3] |= 1 << (x & 7);
        }
      ximage = XCreateImage (gdk_display, x_visual, 1, XYBitmap, 0, data,
                             rect->width, rect->height, 8, stride);
      ximage->byte_order = LSBFirst;
      ximage->bitmap_bit_order = LSBFirst;
    }
  else
    {
      gint bytes = x_bits_per_pixel / 8;
      gint stride = rect->width * bytes;

      data = g_malloc ((gsize) stride * rect->height);
      for (y = 0; y < rect->height; y++)
        {
          const guint32 *in = fb->pixels + (gsize) (rect->y + y) * fb->stride + rect->x;
          gchar *out = data + y * stride;

          if (x_direct && bytes == 4)
            memcpy (out, in, stride);
          else
            for (x = 0; x < rect->width; x++)
              {
                guint32 p = in[x];
                gulong v = ((gulong) ((p >> 16 & 0xff) >> (8 - x_prec[0])) << x_shift[0]) |
                  ((gulong) ((p >> 8 & 0xff) >> (8 - x_prec[1])) << x_shift[1]) |
                  ((gulong) ((p & 0xff) >> (8 - x_prec[2])) << x_shift[2]);

                if (bytes == 4)
                  ((guint32 *) out)[x] = v;
                else
                  ((guint16 *) out)[x] = v;
              }
        }
      ximage = XCreateImage (gdk_display, x_visual, x_depth, ZPixmap, 0, data,
                             rect->width, rect->height, 32, stride);
      ximage->byte_order = (G_BYTE_ORDER == G_LITTLE_ENDIAN) ? LSBFirst : MSBFirst;
    }

  XPutImage (gdk_display, drawable, gc_for_depth (drawable, fb->depth), ximage,
             0, 0, xdest, ydest, rect->width, rect->height);
  XDestroyImage (ximage);    /* frees data too */
}

/*
 * The framebuffer holds the whole native window, children and all.
 * Most children have no X window; a native child has, and its own
 * framebuffer with it, which is published into it separately, so the
 * server clips the parent's image by the child's window, and a window
 * of another program's inside it (a GtkSocket's plug) is never painted
 * over.
 *
 * On the screen, the device pixels of the framebuffer become X pixels
 * one for one: a scaled window is that much bigger to X.
 */
static void
publish_tree (GdkWindowPrivate *private, GdkFramebuffer *fb,
              const GdkRectangle *rect, gint x, gint y)
{
  GList *l;

  if (private->impl->xwindow == None)
    return;

  for (l = private->children; l; l = l->next)
    {
      GdkWindowPrivate *child = l->data;
      GdkRectangle crect, part;

      /* A native child has a framebuffer of its own to publish. */
      if (!child->mapped || child->destroyed || child->depth == 0 || child->native)
        continue;
      crect.x = x + child->x * fb->scale;
      crect.y = y + child->y * fb->scale;
      crect.width = child->width * fb->scale;
      crect.height = child->height * fb->scale;
      if (gdk_rectangle_intersect (&crect, (GdkRectangle *) rect, &part))
        publish_tree (child, fb, &part, crect.x, crect.y);
    }

  put_rect (fb, private->impl->xwindow, rect, rect->x - x, rect->y - y);
}

void
_gdk_x11_publish (GdkWindow *window, const GdkRectangle *rect)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;

  if (!rect)
    {
      /* That was all: the server has it now, not when Xlib next
       * has reason to send what it holds. */
      XFlush (gdk_display);
      return;
    }
  if (!private->impl || private->impl->xwindow == None || !private->fb)
    return;

  /*
   * A window that was something else's to draw, in a visual of its
   * own, and is GDK's again: its X window is made again with the
   * visual GDK's pixels are for.
   */
  if (private->impl->xcolormap != None)
    window_set_xvisual (private, XVisualIDFromVisual (x_visual));

  publish_tree (private, private->fb, rect, 0, 0);
}

Pixmap
_gdk_x11_bitmap_xid_at_scale (GdkBitmap *bitmap, GdkPixmap **temp)
{
  GdkFramebuffer *fb;
  GdkPixmap *scaled;
  GdkWindowPrivate *private;
  GdkPaint paint;

  *temp = NULL;
  fb = _gdk_drawable_fb (bitmap);
  if (!fb)
    return None;
  if (fb->scale == _gdk_x11_scale)
    return _gdk_x11_pixmap_xid (bitmap);

  /* The same bitmap drawn at the display's scale. */
  scaled = gdk_pixmap_new (NULL, fb->width / fb->scale,
                           fb->height / fb->scale, 1);
  private = (GdkWindowPrivate *) scaled;
  _gdk_framebuffer_free (private->fb);
  private->scale = _gdk_x11_scale;
  private->fb = _gdk_framebuffer_new (private->width, private->height, 1,
                                      _gdk_x11_scale);
  if (_gdk_paint_init (&paint, private->fb, NULL, 0, 0, NULL))
    {
      _gdk_paint_copy (&paint, fb, 0, 0, 0, 0, private->width, private->height);
      _gdk_paint_finish (&paint);
    }
  *temp = scaled;
  return _gdk_x11_pixmap_xid (scaled);
}

/* The X pixmap of a pixmap, brought up to date with the framebuffer. */
Pixmap
_gdk_x11_pixmap_xid (GdkPixmap *pixmap)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) pixmap;
  GdkFramebuffer *fb;
  GdkRectangle whole;

  g_return_val_if_fail (pixmap != NULL, None);

  if (private->window_type != GDK_WINDOW_PIXMAP)
    return private->impl ? private->impl->xwindow : None;

  fb = _gdk_drawable_fb (pixmap);
  if (!fb)
    return None;

  if (!private->impl)
    {
      private->impl = _gdk_x11_impl_new (None);
      private->impl->xpixmap = XCreatePixmap (gdk_display, gdk_root_window,
                                              fb->width, fb->height,
                                              fb->depth == 1 ? 1 : x_depth);
      private->impl->xwindow = private->impl->xpixmap;
      if (!private->damage)
        private->damage = gdk_region_new ();
      whole.x = whole.y = 0;
      whole.width = fb->width;
      whole.height = fb->height;
      _gdk_region_add_rect (private->damage, &whole);
    }

  if (private->damage && !gdk_region_empty (private->damage))
    {
      whole.x = whole.y = 0;
      whole.width = fb->width;
      whole.height = fb->height;
      put_rect (fb, private->impl->xpixmap, &whole, 0, 0);
      _gdk_region_clear (private->damage);
    }

  return private->impl->xpixmap;
}

void
_gdk_x11_pixmap_free (GdkWindowPrivate *private)
{
  if (!private->impl)
    return;
  if (private->impl->xpixmap != None)
    XFreePixmap (gdk_display, private->impl->xpixmap);
  g_free (private->impl);
  private->impl = NULL;
}

/* What the screen shows in a window GDK has no framebuffer for. */
void
_gdk_x11_get_image (GdkWindow *window, gint x, gint y, GdkImage *image)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  XImage *ximage;
  gint xx, yy;

  if (!private->impl)
    return;

  /* X's pixels, every scale-th of them, as a framebuffer is read. */
  _gdk_x11_error_trap_push ();
  ximage = XGetImage (private->impl->xdisplay, private->impl->xwindow,
                      GDK_X11_TO_X (x), GDK_X11_TO_X (y),
                      GDK_X11_TO_X (image->width), GDK_X11_TO_X (image->height),
                      AllPlanes, ZPixmap);
  if (_gdk_x11_error_trap_pop () || !ximage)
    return;

  for (yy = 0; yy < image->height; yy++)
    for (xx = 0; xx < image->width; xx++)
      {
        gulong v = XGetPixel (ximage, GDK_X11_TO_X (xx), GDK_X11_TO_X (yy));
        guint32 r = (v >> x_shift[0]) & ((1 << x_prec[0]) - 1);
        guint32 g = (v >> x_shift[1]) & ((1 << x_prec[1]) - 1);
        guint32 b = (v >> x_shift[2]) & ((1 << x_prec[2]) - 1);

        r = x_prec[0] ? r * 255 / ((1 << x_prec[0]) - 1) : 0;
        g = x_prec[1] ? g * 255 / ((1 << x_prec[1]) - 1) : 0;
        b = x_prec[2] ? b * 255 / ((1 << x_prec[2]) - 1) : 0;
        gdk_image_put_pixel (image, xx, yy, r << 16 | g << 8 | b);
      }
  XDestroyImage (ximage);
}


/* ---------------------------------------------------------------------
 * gdkx.h: for programs that talk to X themselves.
 */

/*
 * The X window behind a window. A child that is not native has none
 * of its own, and its native ancestor's is what X knows it as -- fine
 * for properties, selections and hints, which were the toplevel's
 * anyway. A child that must be an X window of its own, for GL or for
 * another program's window, asks with GDK_WA_NATIVE or
 * _gdk_x11_window_ensure_native.
 */
Window
gdk_x11_window_xid (GdkWindow *window)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;

  g_return_val_if_fail (window != NULL, None);

  if (!gdk_display)
    return None;            /* another backend is running */
  if (private->window_type == GDK_WINDOW_PIXMAP)
    return _gdk_x11_pixmap_xid (window);
  private = _gdk_window_native (private, NULL, NULL);
  return private->impl ? private->impl->xwindow : None;
}

Pixmap
gdk_x11_pixmap_xid (GdkPixmap *pixmap)
{
  if (!gdk_display)
    return None;
  return _gdk_x11_pixmap_xid (pixmap);
}

VisualID
gdk_x11_window_xvisualid (GdkWindow *window)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;
  XWindowAttributes attrs;

  g_return_val_if_fail (window != NULL, 0);

  if (!gdk_display || private->destroyed ||
      private->window_type == GDK_WINDOW_PIXMAP)
    return 0;
  private = _gdk_window_native (private, NULL, NULL);
  /* Only a window given a visual of its own has anything but GDK's. */
  if (!private->impl || private->impl->xcolormap == None)
    return XVisualIDFromVisual (x_visual);
  if (!XGetWindowAttributes (gdk_display, private->impl->xwindow, &attrs))
    return 0;
  return XVisualIDFromVisual (attrs.visual);
}

static Bool
event_is_for_window (Display *display, XEvent *xevent, XPointer arg)
{
  return xevent->xany.window == *(Window *) arg;
}

/*
 * The X window of an external window made again with another visual.
 * Nothing of GDK's is drawn in such a window, so the visual is only
 * the business of whoever draws there; all GDK does is make the window
 * the same in every other way, and know it by its new id.
 */
static gboolean
window_set_xvisual (GdkWindowPrivate *private, VisualID xvisualid)
{
  GdkWindow *window = (GdkWindow *) private;
  GdkWindowImpl *impl;
  XVisualInfo template, *info;
  XSetWindowAttributes xattributes;
  XWindowAttributes old;
  XWindowChanges xchanges;
  Window xparent, root, *xchildren = NULL, xwindow, xold;
  Colormap xcolormap = None;
  XEvent xevent;
  unsigned int n_xchildren = 0, i;
  gint n;

  if (gdk_x11_window_xvisualid (window) == xvisualid)
    return TRUE;

  impl = private->impl;
  template.visualid = xvisualid;
  template.screen = gdk_screen;
  info = XGetVisualInfo (gdk_display, VisualIDMask | VisualScreenMask, &template, &n);
  if (!info)
    return FALSE;
  if (!XGetWindowAttributes (gdk_display, impl->xwindow, &old) ||
      !XQueryTree (gdk_display, impl->xwindow, &root, &xparent,
                   &xchildren, &n_xchildren))
    {
      XFree (info);
      return FALSE;
    }

  /* A window whose visual is not its parent's needs a colormap of its
   * own, and a border pixel, to be made at all. */
  if (info->visual == x_visual)
    xattributes.colormap = x_colormap;
  else
    xattributes.colormap = xcolormap =
      XCreateColormap (gdk_display, gdk_root_window, info->visual, AllocNone);
  xattributes.background_pixmap = None;
  xattributes.border_pixel = 0;
  xattributes.event_mask = old.your_event_mask;
  xattributes.cursor = impl->cursor ? impl->cursor->xcursor : None;
  xwindow = XCreateWindow (gdk_display, xparent, old.x, old.y,
                           old.width, old.height, 0, info->depth, InputOutput,
                           info->visual,
                           CWColormap | CWBackPixmap | CWBorderPixel |
                           CWEventMask | CWCursor, &xattributes);
  XFree (info);

  /* Where the old one is among the windows beside it; what was
   * inside the old one goes into the new; then the old goes. */
  xchanges.sibling = impl->xwindow;
  xchanges.stack_mode = Above;
  XConfigureWindow (gdk_display, xwindow, CWSibling | CWStackMode, &xchanges);
  for (i = 0; i < n_xchildren; i++)
    {
      XWindowAttributes child;

      if (XGetWindowAttributes (gdk_display, xchildren[i], &child))
        XReparentWindow (gdk_display, xchildren[i], xwindow, child.x, child.y);
    }
  if (xchildren)
    XFree (xchildren);
  if (old.map_state != IsUnmapped)
    XMapWindow (gdk_display, xwindow);

  /* Whatever X had still to say about the old one is for nobody. */
  xold = impl->xwindow;
  gdk_xid_table_remove (xold);
  XDestroyWindow (gdk_display, xold);
  XSync (gdk_display, False);
  while (XCheckIfEvent (gdk_display, &xevent, event_is_for_window, (XPointer) &xold))
    ;
  if (impl->xcolormap != None)
    XFreeColormap (gdk_display, impl->xcolormap);
  impl->xwindow = xwindow;
  impl->xcolormap = xcolormap;
  impl->xcursor_shown = xattributes.cursor;
  gdk_xid_table_insert (&impl->xwindow, window);

  return TRUE;
}

gboolean
gdk_x11_window_set_xvisual (GdkWindow *window, VisualID xvisualid)
{
  GdkWindowPrivate *private = (GdkWindowPrivate *) window;

  g_return_val_if_fail (window != NULL, FALSE);

  if (!gdk_display || private->destroyed || !private->external ||
      private->window_type != GDK_WINDOW_CHILD)
    return FALSE;
  return window_set_xvisual (private, xvisualid);
}

Visual *
gdk_x11_visual_xvisual (GdkVisual *visual)
{
  return x_visual;
}

Colormap
gdk_x11_colormap_xcolormap (GdkColormap *colormap)
{
  return x_colormap;
}

GdkVisual *
gdkx_visual_get (VisualID xvisualid)
{
  if (xvisualid == XVisualIDFromVisual (x_visual))
    return gdk_visual_get_system ();
  return NULL;
}

GdkColormap *
gdkx_colormap_get (Colormap xcolormap)
{
  if (xcolormap == x_colormap)
    return gdk_colormap_get_system ();
  return NULL;
}

/* An X pixmap of another program's: read once into a pixmap of ours. */
GdkPixmap *
gdk_pixmap_foreign_new (guint32 anid)
{
  Window root;
  int x, y;
  unsigned int width, height, border, depth;
  GdkPixmap *pixmap;
  GdkImage *image;
  GdkGC *gc;

  _gdk_x11_error_trap_push ();
  if (!XGetGeometry (gdk_display, anid, &root, &x, &y, &width, &height,
                     &border, &depth) || _gdk_x11_error_trap_pop ())
    return NULL;

  pixmap = gdk_pixmap_new (NULL, width, height, depth == 1 ? 1 : 24);
  if (depth == 1)
    return pixmap;

  image = gdk_image_new (GDK_IMAGE_NORMAL, gdk_visual_get_system (), width, height);
  if (!image)
    return pixmap;
  {
    GdkWindowPrivate fake;

    memset (&fake, 0, sizeof (fake));
    fake.impl = _gdk_x11_impl_new (anid);
    _gdk_x11_get_image ((GdkWindow *) &fake, 0, 0, image);
    g_free (fake.impl);
  }
  gc = gdk_gc_new (pixmap);
  gdk_draw_image (pixmap, gc, image, 0, 0, 0, 0, width, height);
  gdk_gc_unref (gc);
  gdk_image_destroy (image);

  return pixmap;
}

/*
 * An XImage over the GdkImage's own memory, for programs that read
 * image->data through it. A colour image is what X calls a 32 bit
 * ZPixmap; a bitmap is a byte a pixel, which X has no name for, so it
 * is described as depth 1 with 8 bits a pixel and only its data and
 * bytes_per_line mean anything.
 */
XImage *
gdk_x11_image_ximage (GdkImage *image)
{
  GdkImagePrivate *private = (GdkImagePrivate *) image;
  XImage *ximage;

  g_return_val_if_fail (image != NULL, NULL);

  if (!gdk_display)
    return NULL;
  if (private->impl)
    return private->impl;

  ximage = XCreateImage (gdk_display, x_visual,
                         image->depth == 1 ? 1 : 24, ZPixmap, 0,
                         image->mem, image->width, image->height,
                         image->bpp * 8, image->bpl);
  if (!ximage)
    return NULL;
  ximage->bits_per_pixel = image->bpp * 8;
  ximage->byte_order = (G_BYTE_ORDER == G_LITTLE_ENDIAN) ? LSBFirst : MSBFirst;
  ximage->red_mask = 0xff0000;
  ximage->green_mask = 0x00ff00;
  ximage->blue_mask = 0x0000ff;
  XInitImage (ximage);
  private->impl = ximage;

  return ximage;
}

void
_gdk_x11_image_free (GdkImagePrivate *private)
{
  XImage *ximage = private->impl;

  if (!ximage)
    return;
  ximage->data = NULL;      /* the GdkImage frees its memory itself */
  XDestroyImage (ximage);
  private->impl = NULL;
}

/*
 * An X GC with the GdkGC's values, for programs that draw through
 * Xlib with a GDK GC. Made once and brought up to date each time it
 * is asked for.
 */
GC
gdk_x11_gc_xgc (GdkGC *gc)
{
  /* GdkFunction counts in an order of its own; the styles of lines,
   * caps and joins and the subwindow mode count as X does. */
  static const int functions[] = {
    GXcopy, GXinvert, GXxor, GXclear, GXand, GXandReverse, GXandInverted,
    GXnoop, GXor, GXequiv, GXorReverse, GXcopyInverted, GXorInverted,
    GXnand, GXset
  };
  GdkGCPrivate *private = (GdkGCPrivate *) gc;
  GdkGCValues *v;
  XGCValues values;
  unsigned long mask;

  g_return_val_if_fail (gc != NULL, None);

  if (!gdk_display)
    return None;
  v = &private->values;
  values.foreground = v->foreground.pixel;
  values.background = v->background.pixel;
  values.function = (guint) v->function <= GDK_SET ? functions[v->function] : GXcopy;
  values.line_width = v->line_width;
  values.line_style = v->line_style;
  values.cap_style = v->cap_style;
  values.join_style = v->join_style;
  values.subwindow_mode = v->subwindow_mode;
  values.graphics_exposures = v->graphics_exposures;
  mask = GCForeground | GCBackground | GCFunction | GCLineWidth |
    GCLineStyle | GCCapStyle | GCJoinStyle | GCSubwindowMode |
    GCGraphicsExposures;

  if (!private->impl)
    private->impl = (gpointer) XCreateGC (gdk_display, gdk_root_window,
                                          mask, &values);
  else
    XChangeGC (gdk_display, (GC) private->impl, mask, &values);

  return (GC) private->impl;
}

void
_gdk_x11_gc_free (GdkGCPrivate *private)
{
  if (private->impl)
    XFreeGC (gdk_display, (GC) private->impl);
  private->impl = NULL;
}
