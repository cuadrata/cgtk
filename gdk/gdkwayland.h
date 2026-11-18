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
 * The Wayland backend as programs see it: the display, and the surface
 * behind a GdkWindow, for programs that talk to the compositor
 * themselves. It is to Wayland what gdkx.h is to X.
 *
 * GDK may be running another backend: then gdk_wayland_display() is
 * NULL and every accessor here gives nothing. A program that talks to
 * the compositor checks GDK_IS_WAYLAND() first, or does its Wayland
 * work under GDK_WINDOWING_WAYLAND and only when that is true.
 */

#ifndef __GDK_WAYLAND_H__
#define __GDK_WAYLAND_H__

#include <wayland-client.h>
#include <gdk/gdk.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct wl_display *gdk_wayland_display (void);

#define GDK_IS_WAYLAND()   (gdk_wayland_display () != NULL)

/*
 * The surface behind a window: its own if it is native, else that of
 * the native window it is a part of. Every commit of a surface is
 * GDK's to make, with one exception: an external window's
 * (gdk_window_set_external), which is its owner's to attach buffers
 * to and to commit, and GDK's only to place.
 */
struct wl_surface *gdk_wayland_window_surface (GdkWindow *window);

/*
 * What an external window's owner sets as its surface's buffer scale:
 * its buffers are the window's size times gdk_window_get_scale_factor,
 * of which this is the part the compositor is told of. The two are
 * the same unless GDK_SCALE forces a scale the outputs do not have.
 */
gint gdk_wayland_window_buffer_scale (GdkWindow *window);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GDK_WAYLAND_H__ */
