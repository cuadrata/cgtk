/* GTK - The GIMP Toolkit
 * SVG files, drawn into pixels by the toolkit's own NanoSVG
 *
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

/* What draws. The parser and the rasteriser are NanoSVG's, taken into
 * the toolkit and its own from then on (nanosvg.h, nanosvgrast.h; they
 * say what was changed, and carry their author's licence). They are
 * compiled here and nowhere else, and nothing of them shows outside
 * this file: a program sees a GdkSvg and the calls below.
 *
 * It knows paths, the basic shapes, strokes with their joins, caps and
 * dashes, fills, linear and radial gradients, and style sheets of
 * plain class selectors. It does not yet know text, <use>, clipping,
 * masks, filters or pictures within the picture, and a group's opacity
 * is given to each shape of it and not to the group as a whole.
 */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gdk.h"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

/* As for a PNG file: no more pixels than this are drawn. */
#define MAX_SIDE   32767
#define MAX_PIXELS (64 * 1024 * 1024)

struct _GdkSvg
{
  NSVGimage *image;
};

GQuark
gdk_svg_error_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("gdk-svg-error-quark");

  return quark;
}

/* The picture the text is, which is parsed where it stands and
 * changed by it. Lengths are in pixels at 96 to the inch.
 */
static GdkSvg*
svg_parse (gchar   *text,
           GError **error)
{
  NSVGimage *image = nsvgParse (text, "px", 96.0f);
  GdkSvg *svg;

  if (!image || image->width <= 0 || image->height <= 0)
    {
      if (image)
        nsvgDelete (image);
      g_set_error (error, GDK_SVG_ERROR, GDK_SVG_ERROR_FAILED,
                   "Not an SVG picture, or one without a size");
      return NULL;
    }
  svg = g_new (GdkSvg, 1);
  svg->image = image;

  return svg;
}

/* A picture of SVG text in memory, length bytes of it or, if length
 * is negative, up to its zero. NULL with the error set if it is not
 * SVG or says no size.
 */
GdkSvg*
gdk_svg_load_from_data (const gchar  *data,
                        gssize        length,
                        GError      **error)
{
  GdkSvg *svg;
  gchar *text;

  g_return_val_if_fail (data != NULL, NULL);

  text = length < 0 ? g_strdup (data) : g_strndup (data, length);
  svg = svg_parse (text, error);
  g_free (text);

  return svg;
}

/* The same of a file. One that cannot be read is a G_FILE_ERROR.
 */
GdkSvg*
gdk_svg_load (const gchar  *filename,
              GError      **error)
{
  GdkSvg *svg;
  gchar *text;

  g_return_val_if_fail (filename != NULL, NULL);

  if (!g_file_get_contents (filename, &text, NULL, error))
    return NULL;
  svg = svg_parse (text, error);
  g_free (text);

  return svg;
}

void
gdk_svg_free (GdkSvg *svg)
{
  if (!svg)
    return;
  nsvgDelete (svg->image);
  g_free (svg);
}

/* The size the picture says it is, in its own units, which are
 * pixels at 96 to the inch.
 */
void
gdk_svg_get_size (GdkSvg  *svg,
                  gdouble *width,
                  gdouble *height)
{
  g_return_if_fail (svg != NULL);

  if (width)
    *width = svg->image->width;
  if (height)
    *height = svg->image->height;
}

static guchar*
svg_draw (GdkSvg   *svg,
          gfloat    tx,
          gfloat    ty,
          gfloat    scale,
          gint      width,
          gint      height,
          gboolean  straight)
{
  NSVGrasterizer *rast;
  guchar *pixels;

  if (width > MAX_SIDE || height > MAX_SIDE || (gsize) width * height > MAX_PIXELS)
    return NULL;
  rast = nsvgCreateRasterizer ();
  if (!rast)
    return NULL;
  pixels = g_malloc0 ((gsize) width * height * 4);
  nsvgRasterize (rast, svg->image, tx, ty, scale, pixels, width, height, width * 4);
  nsvgDeleteRasterizer (rast);
  if (straight)
    nsvg__unpremultiplyAlpha (pixels, width, height, width * 4);

  return pixels;
}

/* The whole picture drawn into width by height pixels, as
 * gdk_draw_rgba_image_scaled() takes them: red, green, blue and
 * alpha, a byte each, the colours premultiplied, in rows of width * 4
 * bytes from the top; to be freed with g_free(). It is scaled alike
 * both ways, by as much as keeps the whole of it in, and stands at
 * the top left. NULL if more pixels are asked for than 32767 either
 * way or 64 million in all.
 */
guchar*
gdk_svg_render (GdkSvg *svg,
                gint    width,
                gint    height)
{
  gfloat sx, sy;

  g_return_val_if_fail (svg != NULL, NULL);
  g_return_val_if_fail (width > 0 && height > 0, NULL);

  sx = (gfloat) width / svg->image->width;
  sy = (gfloat) height / svg->image->height;

  return svg_draw (svg, 0, 0, MIN (sx, sy), width, height, FALSE);
}

/* A part of the picture: width by height pixels of it drawn scale
 * times its own size, from (x, y) in its own units.
 */
guchar*
gdk_svg_render_area (GdkSvg  *svg,
                     gdouble  x,
                     gdouble  y,
                     gdouble  scale,
                     gint     width,
                     gint     height)
{
  g_return_val_if_fail (svg != NULL, NULL);
  g_return_val_if_fail (width > 0 && height > 0 && scale > 0, NULL);

  return svg_draw (svg, (gfloat) (-x * scale), (gfloat) (-y * scale), (gfloat) scale, width, height, FALSE);
}

/* The same two with straight alpha: the colours of each pixel are
 * what they are, not multiplied by its alpha, for a program that
 * lays its pictures over one another itself and reckons that way, or
 * hands them to something that does. They are not for
 * gdk_draw_rgba_image_scaled(). The colours are divided out of the
 * premultiplied ones, there being no others while a picture is
 * drawn, and a pixel that is wholly clear is given the mean colour
 * of its neighbours that are not, so that a picture scaled afterwards
 * has no dark edge: NanoSVG's own way, to the byte.
 */
guchar*
gdk_svg_render_straight (GdkSvg *svg,
                         gint    width,
                         gint    height)
{
  gfloat sx, sy;

  g_return_val_if_fail (svg != NULL, NULL);
  g_return_val_if_fail (width > 0 && height > 0, NULL);

  sx = (gfloat) width / svg->image->width;
  sy = (gfloat) height / svg->image->height;

  return svg_draw (svg, 0, 0, MIN (sx, sy), width, height, TRUE);
}

guchar*
gdk_svg_render_area_straight (GdkSvg  *svg,
                              gdouble  x,
                              gdouble  y,
                              gdouble  scale,
                              gint     width,
                              gint     height)
{
  g_return_val_if_fail (svg != NULL, NULL);
  g_return_val_if_fail (width > 0 && height > 0 && scale > 0, NULL);

  return svg_draw (svg, (gfloat) (-x * scale), (gfloat) (-y * scale), (gfloat) scale, width, height, TRUE);
}
