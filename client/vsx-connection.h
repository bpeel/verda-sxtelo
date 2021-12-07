/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2012, 2013  Neil Roberts
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

#ifndef VSX_CONNECTION_H
#define VSX_CONNECTION_H

#include <glib-object.h>
#include <gio/gio.h>

#include "vsx-player.h"
#include "vsx-tile.h"
#include "vsx-signal.h"

#define VSX_TYPE_CONNECTION                                             \
  (vsx_connection_get_type())
#define VSX_CONNECTION(obj)                                             \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                                   \
                               VSX_TYPE_CONNECTION,                     \
                               VsxConnection))
#define VSX_CONNECTION_CLASS(klass)                                     \
  (G_TYPE_CHECK_CLASS_CAST ((klass),                                    \
                            VSX_TYPE_CONNECTION,                        \
                            VsxConnectionClass))
#define VSX_IS_CONNECTION(obj)                                          \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                                   \
                               VSX_TYPE_CONNECTION))
#define VSX_IS_CONNECTION_CLASS(klass)                                  \
  (G_TYPE_CHECK_CLASS_TYPE ((klass),                                    \
                            VSX_TYPE_CONNECTION))
#define VSX_CONNECTION_GET_CLASS(obj)                                   \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                                    \
                              VSX_CONNECTION,                           \
                              VsxConnectionClass))

#define VSX_CONNECTION_ERROR vsx_connection_error_quark ()

typedef struct _VsxConnection VsxConnection;
typedef struct _VsxConnectionClass VsxConnectionClass;
typedef struct _VsxConnectionPrivate VsxConnectionPrivate;

typedef enum
{
  VSX_CONNECTION_STATE_AWAITING_HEADER,
  VSX_CONNECTION_STATE_IN_PROGRESS,
  VSX_CONNECTION_STATE_DONE
} VsxConnectionState;

struct _VsxConnectionClass
{
  GObjectClass parent_class;
};

struct _VsxConnection
{
  GObject parent;

  VsxConnectionPrivate *priv;
};

typedef enum
{
  /* Emitted whenever the connection encounters an error. These could
     be either an I/O error from the underlying socket or a protocol
     error. Usually the connection will try to recover from the error
     by reconnecting, but you can prevent this in the signal handler
     by calling vsx_connection_set_running().*/
  VSX_CONNECTION_EVENT_TYPE_ERROR,
  VSX_CONNECTION_EVENT_TYPE_MESSAGE,
  /* Emitted whenever the details of a player have changed or a new
   * player has been created */
  VSX_CONNECTION_EVENT_TYPE_PLAYER_CHANGED,
  VSX_CONNECTION_EVENT_TYPE_PLAYER_SHOUTED,
  VSX_CONNECTION_EVENT_TYPE_TILE_CHANGED,
  VSX_CONNECTION_EVENT_TYPE_RUNNING_STATE_CHANGED,
} VsxConnectionEventType;

typedef struct
{
  VsxConnectionEventType type;

  union
  {
    struct
    {
      GError *error;
    } error;

    struct
    {
      const VsxPlayer *player;
      const char *message;
    } message;

    struct
    {
      const VsxPlayer *player;
    } player_changed;

    struct
    {
      const VsxPlayer *player;
    } player_shouted;

    struct
    {
      bool new_tile;
      const VsxTile *tile;
    } tile_changed;

    struct
    {
      bool running;
    } running_state_changed;
  };
} VsxConnectionEvent;

typedef enum
{
  VSX_CONNECTION_ERROR_BAD_DATA,
  VSX_CONNECTION_ERROR_CONNECTION_CLOSED
} VsxConnectionError;

GType
vsx_connection_get_type (void) G_GNUC_CONST;

VsxConnection *
vsx_connection_new (GSocketAddress *address,
                    const char *room,
                    const char *player_name);

void
vsx_connection_set_running (VsxConnection *connection,
                            bool running);

bool
vsx_connection_get_running (VsxConnection *connection);

bool
vsx_connection_get_typing (VsxConnection *connection);

void
vsx_connection_set_typing (VsxConnection *connection,
                           bool typing);

void
vsx_connection_shout (VsxConnection *connection);

void
vsx_connection_turn (VsxConnection *connection);

void
vsx_connection_move_tile (VsxConnection *connection,
                          int tile_num,
                          int x,
                          int y);

VsxConnectionState
vsx_connection_get_state (VsxConnection *connection);

void
vsx_connection_send_message (VsxConnection *connection,
                             const char *message);

void
vsx_connection_leave (VsxConnection *connection);

const VsxPlayer *
vsx_connection_get_player (VsxConnection *connection,
                           int player_num);

typedef void
(* VsxConnectionForeachPlayerCallback) (const VsxPlayer *player,
                                        void *user_data);

void
vsx_connection_foreach_player (VsxConnection *connection,
                               VsxConnectionForeachPlayerCallback callback,
                               void *user_data);

const VsxPlayer *
vsx_connection_get_self (VsxConnection *connection);

const VsxTile *
vsx_connection_get_tile (VsxConnection *connection,
                         int tile_num);

typedef void
(* VsxConnectionForeachTileCallback) (const VsxTile *tile,
                                      void *user_data);

void
vsx_connection_foreach_tile (VsxConnection *connection,
                             VsxConnectionForeachTileCallback callback,
                             void *user_data);

VsxSignal *
vsx_connection_get_event_signal (VsxConnection *connection);

GQuark
vsx_connection_error_quark (void);

#endif /* VSX_CONNECTION_H */
