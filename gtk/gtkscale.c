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

#include <math.h>
#include "gtkcontainer.h"
#include "gtkscale.h"


#define SCALE_CLASS(w)  GTK_SCALE_CLASS (GTK_OBJECT (w)->klass)

enum {
  ARG_0,
  ARG_DIGITS,
  ARG_DRAW_VALUE,
  ARG_VALUE_POS
};


static void gtk_scale_class_init      (GtkScaleClass *klass);
static void gtk_scale_init            (GtkScale      *scale);
static void gtk_scale_set_arg         (GtkObject     *object,
                                       GtkArg        *arg,
                                       guint          arg_id);
static void gtk_scale_get_arg         (GtkObject     *object,
                                       GtkArg        *arg,
                                       guint          arg_id);
static void gtk_scale_map             (GtkWidget     *widget);
static void gtk_scale_unmap           (GtkWidget     *widget);
static void gtk_scale_draw_focus      (GtkWidget     *widget);
static void gtk_scale_style_set       (GtkWidget     *widget,
                                       GtkStyle      *previous_style);

static void gtk_scale_draw_background (GtkRange      *range);


static GtkRangeClass *parent_class = NULL;


GtkType
gtk_scale_get_type (void)
{
  static GtkType scale_type = 0;

  if (!scale_type)
    {
      static const GtkTypeInfo scale_info =
      {
        "GtkScale",
        sizeof (GtkScale),
        sizeof (GtkScaleClass),
        (GtkClassInitFunc) gtk_scale_class_init,
        (GtkObjectInitFunc) gtk_scale_init,
        /* reserved_1 */ NULL,
        /* reserved_2 */ NULL,
        (GtkClassInitFunc) NULL,
      };

      scale_type = gtk_type_unique (GTK_TYPE_RANGE, &scale_info);
    }

  return scale_type;
}

static void
gtk_scale_class_init (GtkScaleClass *class)
{
  GtkObjectClass *object_class;
  GtkWidgetClass *widget_class;
  GtkRangeClass *range_class;

  object_class = (GtkObjectClass*) class;
  range_class = (GtkRangeClass*) class;
  widget_class = (GtkWidgetClass*) class;

  parent_class = gtk_type_class (GTK_TYPE_RANGE);

  gtk_object_add_arg_type ("GtkScale::digits",
                           GTK_TYPE_INT,
                           GTK_ARG_READWRITE,
                           ARG_DIGITS);
  gtk_object_add_arg_type ("GtkScale::draw_value",
                           GTK_TYPE_BOOL,
                           GTK_ARG_READWRITE,
                           ARG_DRAW_VALUE);
  gtk_object_add_arg_type ("GtkScale::value_pos",
                           GTK_TYPE_POSITION_TYPE,
                           GTK_ARG_READWRITE,
                           ARG_VALUE_POS);

  object_class->set_arg = gtk_scale_set_arg;
  object_class->get_arg = gtk_scale_get_arg;

  widget_class->map = gtk_scale_map;
  widget_class->unmap = gtk_scale_unmap;
  widget_class->draw_focus = gtk_scale_draw_focus;
  widget_class->style_set = gtk_scale_style_set;

  range_class->draw_background = gtk_scale_draw_background;

  class->slider_length = 31;
  class->value_spacing = 2;
  class->draw_value = NULL;
}

/* How long the slider is: the class's, unless the style says otherwise */
gint
_gtk_scale_get_slider_length (GtkScale *scale)
{
  GtkWidget *widget = GTK_WIDGET (scale);

  return gtk_style_get_prop_experimental (widget->style,
                                          "GtkScale::slider_length",
                                          GTK_SCALE_CLASS (GTK_OBJECT (scale)->klass)->slider_length);
}

/* A style that draws the trough's two sides of the slider differently,
 * as GTK+ 2's trough-side-details asks, needs the trough drawn again
 * when the slider moves. The slider is drawn again too: its window
 * shows the trough under it, which is not the same everywhere -- it
 * ends at either end -- and moving the window would carry along what
 * was under the slider where it was. */
void
_gtk_scale_slider_moved (GtkScale *scale)
{
  GtkWidget *widget = GTK_WIDGET (scale);

  if (!GTK_WIDGET_DRAWABLE (widget))
    return;

  if (gtk_style_get_prop_experimental (widget->style,
                                       "GtkScale::trough_side_details",
                                       FALSE))
    gtk_range_draw_trough (GTK_RANGE (scale));
  gtk_range_draw_slider (GTK_RANGE (scale));
}

static void
gtk_scale_set_arg (GtkObject      *object,
                   GtkArg         *arg,
                   guint           arg_id)
{
  GtkScale *scale;

  scale = GTK_SCALE (object);

  switch (arg_id)
    {
    case ARG_DIGITS:
      gtk_scale_set_digits (scale, GTK_VALUE_INT (*arg));
      break;
    case ARG_DRAW_VALUE:
      gtk_scale_set_draw_value (scale, GTK_VALUE_BOOL (*arg));
      break;
    case ARG_VALUE_POS:
      gtk_scale_set_value_pos (scale, GTK_VALUE_ENUM (*arg));
      break;
    default:
      break;
    }
}

static void
gtk_scale_get_arg (GtkObject      *object,
                   GtkArg         *arg,
                   guint           arg_id)
{
  GtkScale *scale;

  scale = GTK_SCALE (object);

  switch (arg_id)
    {
    case ARG_DIGITS:
      GTK_VALUE_INT (*arg) = GTK_RANGE (scale)->digits;
      break;
    case ARG_DRAW_VALUE:
      GTK_VALUE_BOOL (*arg) = scale->draw_value;
      break;
    case ARG_VALUE_POS:
      GTK_VALUE_ENUM (*arg) = scale->value_pos;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
    }
}

static void
gtk_scale_init (GtkScale *scale)
{
  GTK_WIDGET_SET_FLAGS (scale, GTK_CAN_FOCUS);
  GTK_WIDGET_SET_FLAGS (scale, GTK_NO_WINDOW);
  GTK_RANGE (scale)->digits = 1;
  scale->draw_value = TRUE;
  scale->value_pos = GTK_POS_TOP;
}

static void
gtk_scale_map (GtkWidget *widget)
{
  GtkRange *range;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_SCALE (widget));

  GTK_WIDGET_SET_FLAGS (widget, GTK_MAPPED);
  range = GTK_RANGE (widget);

  gdk_window_show (range->trough);
}

static void
gtk_scale_unmap (GtkWidget *widget)
{
  GtkRange *range;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_SCALE (widget));

  GTK_WIDGET_UNSET_FLAGS (widget, GTK_MAPPED);
  range = GTK_RANGE (widget);

  if (GTK_WIDGET_NO_WINDOW (widget))
     gtk_widget_queue_clear (widget);

  gdk_window_hide (range->trough);
}

static void
gtk_scale_style_set (GtkWidget *widget,
                     GtkStyle  *previous_style)
{
  if (GTK_WIDGET_CLASS (parent_class)->style_set)
    (* GTK_WIDGET_CLASS (parent_class)->style_set) (widget, previous_style);
  if (GTK_WIDGET_REALIZED (widget))
    _gtk_scale_set_backgrounds (GTK_SCALE (widget));
}

/* A scale's trough and slider are windows of their own, where GTK+ 2's
 * scale has none: what a theme leaves of them shows what is behind the
 * scale, as it would in GTK+ 2, not colours of GTK+ 1's choosing */
void
_gtk_scale_set_backgrounds (GtkScale *scale)
{
  GtkRange *range = GTK_RANGE (scale);

  if (range->trough)
    gdk_window_set_back_pixmap (range->trough, NULL, TRUE);
  if (range->slider)
    gdk_window_set_back_pixmap (range->slider, NULL, TRUE);
}

/* The slider's window hides the trough under it, and the focus round
 * the trough: draw them on the slider first, where they would show
 * round the slider's corners */
void
_gtk_scale_draw_under_slider (GtkScale *scale)
{
  GtkWidget *widget = GTK_WIDGET (scale);
  GtkRange *range = GTK_RANGE (scale);
  gint sx, sy, width, height;

  if (!range->slider || !range->trough)
    return;

  gdk_window_get_position (range->slider, &sx, &sy);
  gdk_window_get_size (range->trough, &width, &height);

  gdk_window_clear (range->slider);
  gtk_paint_box (widget->style, range->slider,
                 GTK_STATE_ACTIVE, GTK_SHADOW_IN,
                 NULL, widget, "trough",
                 -sx, -sy, width, height);
  if (GTK_WIDGET_HAS_FOCUS (widget))
    gtk_paint_focus (widget->style, range->slider,
                     NULL, widget, "trough",
                     -sx, -sy, width - 1, height - 1);
}

/* The slider has a window over the trough, and a theme may show the
 * focus under its corners */
static void
gtk_scale_draw_focus (GtkWidget *widget)
{
  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_SCALE (widget));

  if (GTK_WIDGET_CLASS (parent_class)->draw_focus)
    (* GTK_WIDGET_CLASS (parent_class)->draw_focus) (widget);
  if (GTK_WIDGET_DRAWABLE (widget))
    gtk_range_draw_slider (GTK_RANGE (widget));
}

void
gtk_scale_set_digits (GtkScale *scale,
                      gint      digits)
{
  g_return_if_fail (scale != NULL);
  g_return_if_fail (GTK_IS_SCALE (scale));

  digits = CLAMP (digits, -1, 16);

  if (GTK_RANGE (scale)->digits != digits)
    {
      GTK_RANGE (scale)->digits = digits;

      gtk_widget_queue_resize (GTK_WIDGET (scale));
    }
}

void
gtk_scale_set_draw_value (GtkScale *scale,
                          gboolean  draw_value)
{
  g_return_if_fail (scale != NULL);
  g_return_if_fail (GTK_IS_SCALE (scale));

  draw_value = draw_value != FALSE;

  if (scale->draw_value != draw_value)
    {
      scale->draw_value = draw_value;

      gtk_widget_queue_resize (GTK_WIDGET (scale));
    }
}

void
gtk_scale_set_value_pos (GtkScale        *scale,
                         GtkPositionType  pos)
{
  g_return_if_fail (scale != NULL);
  g_return_if_fail (GTK_IS_SCALE (scale));

  if (scale->value_pos != pos)
    {
      scale->value_pos = pos;

      if (GTK_WIDGET_VISIBLE (scale) && GTK_WIDGET_MAPPED (scale))
        gtk_widget_queue_resize (GTK_WIDGET (scale));
    }
}

gint
gtk_scale_get_value_width (GtkScale *scale)
{
  GtkRange *range;
  gchar buffer[128];
  gfloat value;
  gint temp;
  gint return_val;
  gint digits;
  gint i, j;

  g_return_val_if_fail (scale != NULL, 0);
  g_return_val_if_fail (GTK_IS_SCALE (scale), 0);

  return_val = 0;
  if (scale->draw_value)
    {
      range = GTK_RANGE (scale);

      value = ABS (range->adjustment->lower);
      if (value == 0) value = 1;
      digits = log10 (value) + 1;
      if (digits > 13)
        digits = 13;

      i = 0;
      if (range->adjustment->lower < 0)
        buffer[i++] = '-';
      for (j = 0; j < digits; j++)
        buffer[i++] = '0';
      if (GTK_RANGE (scale)->digits)
        buffer[i++] = '.';
      for (j = 0; j < GTK_RANGE (scale)->digits; j++)
        buffer[i++] = '0';
      buffer[i] = '\0';

      return_val = gdk_string_measure (GTK_WIDGET (scale)->style->font, buffer);

      value = ABS (range->adjustment->upper);
      if (value == 0) value = 1;
      digits = log10 (value) + 1;
      if (digits > 13)
        digits = 13;

      i = 0;
      if (range->adjustment->upper < 0)
        buffer[i++] = '-';
      for (j = 0; j < digits; j++)
        buffer[i++] = '0';
      if (GTK_RANGE (scale)->digits)
        buffer[i++] = '.';
      for (j = 0; j < GTK_RANGE (scale)->digits; j++)
        buffer[i++] = '0';
      buffer[i] = '\0';

      temp = gdk_string_measure (GTK_WIDGET (scale)->style->font, buffer);
      return_val = MAX (return_val, temp);
    }

  return return_val;
}

void
gtk_scale_draw_value (GtkScale *scale)
{
  g_return_if_fail (scale != NULL);
  g_return_if_fail (GTK_IS_SCALE (scale));

  if (SCALE_CLASS (scale)->draw_value)
    (* SCALE_CLASS (scale)->draw_value) (scale);
}


static void
gtk_scale_draw_background (GtkRange *range)
{
  g_return_if_fail (range != NULL);
  g_return_if_fail (GTK_IS_SCALE (range));

  gtk_scale_draw_value (GTK_SCALE (range));
}
