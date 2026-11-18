/* GDK - The GIMP Drawing Kit
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

/*
 * Client messages to every CGTK program of the user, on any backend.
 *
 * gdk_event_send_clientmessage_toall reaches every X client's windows
 * through the X server, but a Wayland client cannot reach other clients
 * at all, so "the theme has changed" (_GTK_READ_RCFILES) went nowhere.
 * Every program therefore also binds a datagram socket, named for its
 * process, in $XDG_RUNTIME_DIR/cgtk, and a broadcast is one datagram to
 * each socket there. A socket nobody reads any more is one a program
 * left behind when it died, and the sender removes it.
 *
 * A message carries its type by name, since atoms are numbers that only
 * mean something in one program on Wayland, and the backend and display
 * it was sent from: an X11 program on the same X display has the message
 * from the X server already, and takes no second one from the socket.
 * Without $XDG_RUNTIME_DIR, or where the socket cannot be made, there is
 * no broadcast and nothing else changes.
 */

#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "gdk.h"
#include "gdkprivate.h"
#include "gdkbackend.h"

#define BROADCAST_MAGIC "CGTKMSG1"

typedef struct
{
  char    magic[8];             /* BROADCAST_MAGIC */
  char    backend[8];           /* "x11" or "wayland" */
  char    display[64];          /* the sender's display */
  char    type[64];             /* the message type's atom name */
  guint32 format;               /* 8, 16 or 32, as in GdkEventClient */
  union {
    char    b[20];
    gint16  s[10];
    gint64  l[5];
  } data;
} BroadcastMessage;

static gchar *broadcast_dir = NULL;
static gchar *broadcast_path = NULL;
static gint broadcast_fd = -1;
static gint broadcast_tag = 0;

static const gchar *
broadcast_backend_name (void)
{
#ifdef GDK_WINDOWING_X11
  if (_gdk_backend == &_gdk_x11_backend)
    return "x11";
#endif
  return "wayland";
}

static const gchar *
broadcast_display_name (void)
{
  const gchar *name = gdk_get_display ();

  if (name && *name)
    return name;
  name = getenv (strcmp (broadcast_backend_name (), "x11") == 0
                 ? "DISPLAY" : "WAYLAND_DISPLAY");
  return name ? name : "";
}

static gboolean
broadcast_address (const gchar *path, struct sockaddr_un *addr)
{
  if (strlen (path) >= sizeof (addr->sun_path))
    return FALSE;
  memset (addr, 0, sizeof (*addr));
  addr->sun_family = AF_UNIX;
  strcpy (addr->sun_path, path);
  return TRUE;
}

/* Hand a message on to every toplevel, as the X server would. */
static void
broadcast_deliver (const BroadcastMessage *msg)
{
  GdkEvent event;
  GList *tmp_list;
  gint i;

  memset (&event, 0, sizeof (event));
  event.client.type = GDK_CLIENT_EVENT;
  event.client.send_event = TRUE;
  event.client.message_type = gdk_atom_intern (msg->type, FALSE);
  event.client.data_format = msg->format;
  switch (msg->format)
    {
    case 8:
      memcpy (event.client.data.b, msg->data.b, sizeof (event.client.data.b));
      break;
    case 16:
      for (i = 0; i < 10; i++)
        event.client.data.s[i] = msg->data.s[i];
      break;
    default:
      for (i = 0; i < 5; i++)
        event.client.data.l[i] = msg->data.l[i];
      break;
    }

  for (tmp_list = gdk_root_parent.children; tmp_list; tmp_list = tmp_list->next)
    {
      GdkWindowPrivate *private = tmp_list->data;

      if (private->destroyed || private->window_type == GDK_WINDOW_FOREIGN)
        continue;
      event.client.window = (GdkWindow *) private;
      gdk_event_put (&event);
    }
}

static void
broadcast_read (gpointer data, gint source, GdkInputCondition condition)
{
  BroadcastMessage msg;
  ssize_t n;

  while ((n = recv (source, &msg, sizeof (msg), MSG_DONTWAIT)) >= 0 || errno == EINTR)
    {
      if (n != sizeof (msg) || memcmp (msg.magic, BROADCAST_MAGIC, 8) != 0)
        continue;
      msg.backend[sizeof (msg.backend) - 1] = '\0';
      msg.display[sizeof (msg.display) - 1] = '\0';
      msg.type[sizeof (msg.type) - 1] = '\0';
      if (!msg.type[0] || (msg.format != 8 && msg.format != 16 && msg.format != 32))
        continue;

      /* The X server brought this one already. */
      if (strcmp (msg.backend, "x11") == 0 &&
          strcmp (broadcast_backend_name (), "x11") == 0 &&
          strcmp (msg.display, broadcast_display_name ()) == 0)
        continue;

      broadcast_deliver (&msg);
    }
}

void
_gdk_broadcast_init (void)
{
  const gchar *runtime_dir = getenv ("XDG_RUNTIME_DIR");
  struct sockaddr_un addr;
  gchar name[32];

  if (broadcast_fd >= 0 || !runtime_dir || runtime_dir[0] != '/')
    return;

  broadcast_dir = g_strconcat (runtime_dir, "/cgtk", NULL);
  if (mkdir (broadcast_dir, 0700) != 0 && errno != EEXIST)
    goto fail;

  sprintf (name, "/%lu", (gulong) getpid ());
  broadcast_path = g_strconcat (broadcast_dir, name, NULL);
  if (!broadcast_address (broadcast_path, &addr))
    goto fail;

  broadcast_fd = socket (AF_UNIX, SOCK_DGRAM, 0);
  if (broadcast_fd < 0)
    goto fail;
  fcntl (broadcast_fd, F_SETFD, FD_CLOEXEC);
  fcntl (broadcast_fd, F_SETFL, fcntl (broadcast_fd, F_GETFL) | O_NONBLOCK);

  /* A socket by this name is one a program with this number left. */
  unlink (broadcast_path);
  if (bind (broadcast_fd, (struct sockaddr *) &addr, sizeof (addr)) != 0)
    goto fail;

  broadcast_tag = gdk_input_add (broadcast_fd, GDK_INPUT_READ, broadcast_read, NULL);
  return;

 fail:
  GDK_NOTE (MISC, g_message ("no client-message socket in %s: %s",
                             broadcast_dir ? broadcast_dir : "(none)",
                             g_strerror (errno)));
  if (broadcast_fd >= 0)
    close (broadcast_fd);
  broadcast_fd = -1;
  g_free (broadcast_path);
  broadcast_path = NULL;
  g_free (broadcast_dir);
  broadcast_dir = NULL;
}

void
_gdk_broadcast_exit (void)
{
  if (broadcast_fd < 0)
    return;

  gdk_input_remove (broadcast_tag);
  close (broadcast_fd);
  broadcast_fd = -1;
  unlink (broadcast_path);
  g_free (broadcast_path);
  broadcast_path = NULL;
  g_free (broadcast_dir);
  broadcast_dir = NULL;
}

void
_gdk_broadcast_send (GdkEventClient *event)
{
  BroadcastMessage msg;
  struct sockaddr_un addr;
  struct dirent *entry;
  gchar *type;
  DIR *dir;
  gint fd, i;

  if (!broadcast_dir)
    return;

  type = gdk_atom_name (event->message_type);
  if (!type || strlen (type) >= sizeof (msg.type))
    {
      g_free (type);
      return;
    }

  memset (&msg, 0, sizeof (msg));
  memcpy (msg.magic, BROADCAST_MAGIC, 8);
  g_snprintf (msg.backend, sizeof (msg.backend), "%s", broadcast_backend_name ());
  g_snprintf (msg.display, sizeof (msg.display), "%s", broadcast_display_name ());
  strcpy (msg.type, type);
  g_free (type);
  msg.format = event->data_format;
  switch (msg.format)
    {
    case 8:
      memcpy (msg.data.b, event->data.b, sizeof (msg.data.b));
      break;
    case 16:
      for (i = 0; i < 10; i++)
        msg.data.s[i] = event->data.s[i];
      break;
    default:
      msg.format = 32;
      for (i = 0; i < 5; i++)
        msg.data.l[i] = event->data.l[i];
      break;
    }

  dir = opendir (broadcast_dir);
  if (!dir)
    return;
  fd = socket (AF_UNIX, SOCK_DGRAM, 0);
  if (fd < 0)
    {
      closedir (dir);
      return;
    }
  fcntl (fd, F_SETFD, FD_CLOEXEC);

  while ((entry = readdir (dir)))
    {
      gchar *path;

      if (entry->d_name[0] == '.' ||
          strspn (entry->d_name, "0123456789") != strlen (entry->d_name))
        continue;
      path = g_strconcat (broadcast_dir, "/", entry->d_name, NULL);
      if (strcmp (path, broadcast_path) != 0 && broadcast_address (path, &addr))
        {
          if (sendto (fd, &msg, sizeof (msg), MSG_DONTWAIT,
                      (struct sockaddr *) &addr, sizeof (addr)) < 0 &&
              (errno == ECONNREFUSED || errno == ENOENT))
            unlink (path);
        }
      g_free (path);
    }

  close (fd);
  closedir (dir);

  /* An X11 program's own windows have it from the X server; anyone
   * else's, from here, as other programs do. */
  if (strcmp (msg.backend, "x11") != 0)
    broadcast_deliver (&msg);
}
