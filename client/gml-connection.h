/*
 * Gemelo - A server for chatting with strangers in a foreign language
 * Copyright (C) 2012  Neil Roberts
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __GML_CONNECTION_H__
#define __GML_CONNECTION_H__

#include <glib-object.h>
#include <libsoup/soup.h>

G_BEGIN_DECLS

#define GML_TYPE_CONNECTION                                             \
  (gml_connection_get_type())
#define GML_CONNECTION(obj)                                             \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                                   \
                               GML_TYPE_CONNECTION,                     \
                               GmlConnection))
#define GML_CONNECTION_CLASS(klass)                                     \
  (G_TYPE_CHECK_CLASS_CAST ((klass),                                    \
                            GML_TYPE_CONNECTION,                        \
                            GmlConnectionClass))
#define GML_IS_CONNECTION(obj)                                          \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                                   \
                               GML_TYPE_CONNECTION))
#define GML_IS_CONNECTION_CLASS(klass)                                  \
  (G_TYPE_CHECK_CLASS_TYPE ((klass),                                    \
                            GML_TYPE_CONNECTION))
#define GML_CONNECTION_GET_CLASS(obj)                                   \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                                    \
                              GML_CONNECTION,                           \
                              GmlConnectionClass))

#define GML_CONNECTION_ERROR gml_connection_error_quark ()

typedef struct _GmlConnection GmlConnection;
typedef struct _GmlConnectionClass GmlConnectionClass;
typedef struct _GmlConnectionPrivate GmlConnectionPrivate;

typedef enum
{
  GML_CONNECTION_PERSON_YOU,
  GML_CONNECTION_PERSON_STRANGER
} GmlConnectionPerson;

typedef enum
{
  GML_CONNECTION_STATE_AWAITING_PARTNER,
  GML_CONNECTION_STATE_IN_PROGRESS,
  GML_CONNECTION_STATE_DONE
} GmlConnectionState;

struct _GmlConnectionClass
{
  GObjectClass parent_class;

  /* Emitted whenever the connection encounters an error. These could be
     either an I/O error from the underlying socket, an HTTP error or
     an error trying to parse the JSON.  Usually the connection will try
     to recover from the error by reconnected, but you can prevent
     this in the signal handler by calling
     gml_connection_set_running().*/
  void (* got_error) (GmlConnection *connection,
                      GError *error);

  void (* message) (GmlConnection *connection,
                    GmlConnectionPerson person,
                    const char *message);
};

struct _GmlConnection
{
  GObject parent;

  GmlConnectionPrivate *priv;
};

typedef enum
{
  GML_CONNECTION_ERROR_BAD_DATA,
  GML_CONNECTION_ERROR_CONNECTION_CLOSED
} GmlConnectionError;

GType
gml_connection_get_type (void) G_GNUC_CONST;

GmlConnection *
gml_connection_new (const char *server_base_url,
                    const char *room);

void
gml_connection_set_running (GmlConnection *connection,
                            gboolean running);

gboolean
gml_connection_get_running (GmlConnection *connection);

gboolean
gml_connection_get_stranger_typing (GmlConnection *connection);

GmlConnectionState
gml_connection_get_state (GmlConnection *connection);

GQuark
gml_connection_error_quark (void);

G_END_DECLS

#endif /* __GML_CONNECTION_H__ */
