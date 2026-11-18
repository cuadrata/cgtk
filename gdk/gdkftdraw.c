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
 * Text onto a framebuffer. The glyphs come out of gdkftfont.c as
 * coverage; the rasteriser blends them into a colour buffer with the
 * GC's foreground, or, where the GC's function or fill style means a
 * pixel cannot be a mixture, draws each pixel the glyph covers by more
 * than half, which is what an X core font would have drawn. A bitmap
 * gets glyphs rasterised for one bit a pixel in the first place.
 */

#include "config.h"
#include "gdk.h"
#include "gdkprivate.h"
#include "gdkframebuffer.h"
#include "gdkftfont.h"


void
_gdk_ft_draw_text (GdkPaint      *paint,
                   GdkFont       *font,
                   gint           x,
                   gint           y,
                   const guint32 *text,
                   gint           text_length)
{
  GdkFTSet *metrics, *set;
  GdkFTImage image;
  gboolean mono;

  g_return_if_fail (paint != NULL);
  g_return_if_fail (font != NULL);

  if (text_length <= 0)
    return;

  metrics = ((GdkFontPrivate *) font)->ft;
  if (!metrics)
    return;
  set = _gdk_font_ft_set_at_scale (font, paint->scale);

  mono = paint->fb->depth == 1 || paint->function != GDK_COPY ||
    paint->fill != GDK_SOLID;

  if (!_gdk_ft_set_render (set, metrics, paint->scale, text, text_length,
                           mono, &image))
    return;

  /* The image is in device pixels, placed by the pen. */
  _gdk_paint_coverage (paint, image.pixels, image.stride,
                       image.width, image.height, x, y, image.x, image.y);
  _gdk_ft_image_free (&image);
}

/*
 * The resolution sizes in points are read at: what the desktop set, as
 * the backend reads it, or 96.
 */
gdouble
_gdk_ft_display_dpi (void)
{
  static gdouble dpi;

  if (dpi == 0)
    {
      dpi = _gdk_backend_dpi ();
      if (dpi < 24 || dpi > 1000)
        dpi = 96;
    }

  return dpi;
}
