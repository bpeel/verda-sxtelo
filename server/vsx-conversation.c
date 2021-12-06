/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2011, 2013  Neil Roberts
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>

#include <string.h>
#include <assert.h>

#include "vsx-conversation.h"
#include "vsx-main-context.h"
#include "vsx-log.h"
#include "vsx-proto.h"
#include "vsx-utf8.h"

#define VSX_CONVERSATION_CENTER_X (600 / 2 - VSX_TILE_SIZE / 2)
#define VSX_CONVERSATION_CENTER_Y (360 / 2 - VSX_TILE_SIZE / 2)

static uint16_t next_id = 0;

static void
vsx_conversation_free (void *object)
{
  VsxConversation *self = object;
  int i;

  vsx_log ("Game %i destroyed", self->id);

  int n_messages = vsx_conversation_get_n_messages (self);

  for (i = 0; i < n_messages; i++)
    {
      const VsxConversationMessage *message =
        vsx_conversation_get_message (self, i);
      vsx_free (message->text);
    }

  for (i = 0; i < self->n_players; i++)
    vsx_player_free (self->players[i]);

  vsx_buffer_destroy (&self->messages);

  vsx_free (self);
}

static const VsxObjectClass
vsx_conversation_class =
  {
    .free = vsx_conversation_free,
  };

static void
vsx_conversation_changed (VsxConversation *conversation,
                          VsxConversationChangedType type)
{
  VsxConversationChangedData data;

  data.conversation = conversation;
  data.type = type;

  vsx_signal_emit (&conversation->changed_signal, &data);
}

static void
vsx_conversation_player_changed (VsxConversation *conversation,
                                 VsxPlayer *player)
{
  VsxConversationChangedData data;

  data.conversation = conversation;
  data.type = VSX_CONVERSATION_PLAYER_CHANGED;
  data.num = player->num;

  vsx_signal_emit (&conversation->changed_signal, &data);
}

static void
vsx_conversation_tile_changed (VsxConversation *conversation,
                               VsxTile *tile)
{
  VsxConversationChangedData data;

  data.conversation = conversation;
  data.type = VSX_CONVERSATION_TILE_CHANGED;
  data.num = tile - conversation->tiles;

  vsx_signal_emit (&conversation->changed_signal, &data);
}

void
vsx_conversation_start (VsxConversation *conversation)
{
  if (conversation->state == VSX_CONVERSATION_AWAITING_START)
    {
      vsx_log (conversation->n_connected_players == 1 ?
               "Game %i started with %i player" :
               "Game %i started with %i players",
               conversation->id,
               conversation->n_connected_players);
      conversation->state = VSX_CONVERSATION_IN_PROGRESS;
      vsx_conversation_changed (conversation,
                                VSX_CONVERSATION_STATE_CHANGED);
    }
}

void
vsx_conversation_add_message (VsxConversation *conversation,
                              unsigned int player_num,
                              const char *buffer,
                              unsigned int length)
{
  /* Ignore attempts to add messages for a player that has left */
  if (!vsx_player_is_connected (conversation->players[player_num]))
    return;

  int n_messages = vsx_conversation_get_n_messages (conversation);
  vsx_buffer_set_length (&conversation->messages,
                         ++n_messages * sizeof (VsxConversationMessage));
  VsxConversationMessage *message =
    vsx_conversation_get_message (conversation, n_messages - 1);

  message->player_num = player_num;

  unsigned int raw_length = length;

  if (raw_length > VSX_PROTO_MAX_MESSAGE_LENGTH)
    {
      raw_length = VSX_PROTO_MAX_MESSAGE_LENGTH;
      /* If we’ve clipped before a continuation byte then also clip
       * the rest of the UTF-8 sequence so that it will remain valid
       * UTF-8. */
      while ((buffer[raw_length] & 0xc0) == 0x80)
        raw_length--;
    }

  message->text = vsx_strndup (buffer, raw_length);

  vsx_conversation_changed (conversation,
                            VSX_CONVERSATION_MESSAGE_ADDED);
}

static void
vsx_conversation_set_flags (VsxConversation *conversation,
                            VsxPlayer *player,
                            VsxPlayerFlags flags)
{
  if (player->flags != flags)
    {
      player->flags = flags;
      vsx_conversation_player_changed (conversation, player);
    }
}

static void
vsx_conversation_set_flag (VsxConversation *conversation,
                           VsxPlayer *player,
                           VsxPlayerFlags flag,
                           bool value)
{
  VsxPlayerFlags flags;

  if (value)
    flags = player->flags | flag;
  else
    flags = player->flags & ~flag;

  vsx_conversation_set_flags (conversation, player, flags);
}

void
vsx_conversation_set_typing (VsxConversation *conversation,
                             unsigned int player_num,
                             bool typing)
{
  VsxPlayer *player = conversation->players[player_num];

  /* Ignore attempts to set typing state for a player that has left */
  if (!vsx_player_is_connected (player))
    return;

  vsx_conversation_set_flag (conversation,
                             player,
                             VSX_PLAYER_TYPING,
                             typing);
}

static void
set_next_player (VsxConversation *conversation,
                 unsigned int old_player)
{
  if (conversation->n_tiles_in_play >= conversation->total_n_tiles)
    {
      /* If there are no tiles left then make it nobody’s turn */
      vsx_conversation_set_flag (conversation,
                                 conversation->players[old_player],
                                 VSX_PLAYER_NEXT_TURN,
                                 false);
      return;
    }

  /* Find the next player that is connected */
  unsigned int next_turn_player = old_player;

  while (true)
    {
      next_turn_player = (next_turn_player + 1) % conversation->n_players;

      /* If we make it back to the same player then he or she is the
       * only one left connected so we'll just leave them with the
       * next turn flag */
      if (next_turn_player == old_player)
        break;

      /* If we find a connected player then transfer the flag to them */
      if (vsx_player_is_connected (conversation->players[next_turn_player]))
        {
          vsx_conversation_set_flag (conversation,
                                     conversation->players[old_player],
                                     VSX_PLAYER_NEXT_TURN,
                                     false);
          vsx_conversation_set_flag (conversation,
                                     conversation->players[next_turn_player],
                                     VSX_PLAYER_NEXT_TURN,
                                     true);
          break;
        }
    }
}

void
vsx_conversation_player_left (VsxConversation *conversation,
                              unsigned int player_num)
{
  VsxPlayer *player = conversation->players[player_num];
  bool had_next_turn;

  if (!vsx_player_is_connected (player))
    return;

  vsx_log ("Player “%s” left game %i",
           player->name,
           conversation->id);

  had_next_turn = vsx_player_has_next_turn (player);

  /* Set the flags before moving the turn so that it will generate
   * only one callback */
  vsx_conversation_set_flags (conversation, player, 0);

  if (had_next_turn)
    set_next_player (conversation, player_num);

  conversation->n_connected_players--;
}

VsxPlayer *
vsx_conversation_add_player (VsxConversation *conversation,
                             const char *player_name)
{
  VsxPlayer *player;

  assert (conversation->n_players < VSX_CONVERSATION_MAX_PLAYERS);

  player = vsx_player_new (player_name, conversation->n_players);
  conversation->players[conversation->n_players] = player;

  conversation->n_players++;
  conversation->n_connected_players++;

  vsx_conversation_player_changed (conversation, player);

  /* If we've reached the maximum number of players then we'll
   * immediately start the game so that no more players will join */
  if (conversation->n_players >= VSX_CONVERSATION_MAX_PLAYERS)
    vsx_conversation_start (conversation);

  return player;
}

static const VsxTileData *
get_tile_data_for_room_name (const char *room_name)
{
  const char *colon = strchr (room_name, ':');
  int i;

  /* The language code can be specified by prefixing the room name
   * separated by a colon. If we didn’t find one then just use the
   * first tile set. */
  if (colon == NULL)
    return vsx_tile_data;

  /* Look for some tile data for the corresponding room */
  for (i = 0; i < VSX_TILE_DATA_N_ROOMS; i++)
    {
      const VsxTileData *tile_data = vsx_tile_data + i;
      if (strlen (tile_data->language_code) == colon - room_name &&
          !memcmp (tile_data->language_code, room_name, colon - room_name))
        return tile_data;
    }

  /* No language found, just use the first one */
  return vsx_tile_data;
}

static void
shuffle_tiles (VsxConversation *self)
{
  int i;

  for (i = VSX_TILE_DATA_N_TILES - 1; i > 0; i--)
    {
      int swap_pos = g_random_int_range (0, i + 1);
      VsxTile temp;

      temp = self->tiles[swap_pos];
      self->tiles[swap_pos] = self->tiles[i];
      self->tiles[i] = temp;
    }
}

VsxConversation *
vsx_conversation_new (const char *room_name)
{
  VsxConversation *self = vsx_calloc (sizeof *self);
  const VsxTileData *tile_data = get_tile_data_for_room_name (room_name);
  const char *t;
  int i;

  vsx_object_init (self, &vsx_conversation_class);

  self->id = next_id++;
  self->n_tiles_in_play = 0;
  self->total_n_tiles = VSX_TILE_DATA_N_TILES;

  vsx_signal_init (&self->changed_signal);

  vsx_buffer_init (&self->messages);

  self->state = VSX_CONVERSATION_AWAITING_START;

  /* Initialise the tile data with the letters */
  t = tile_data->letters;
  for (i = 0; i < VSX_TILE_DATA_N_TILES; i++)
    {
      const char *t_next = vsx_utf8_next (t);

      assert (*t);

      memcpy (self->tiles[i].letter, t, t_next - t);
      self->tiles[i].letter[t_next - t] = '\0';
      self->tiles[i].x = 0;
      self->tiles[i].y = 0;
      self->tiles[i].last_player = -1;

      t = t_next;
    }

  assert (*t == 0);

  /* Shuffle the tiles */
  shuffle_tiles (self);

  return self;
}

void
vsx_conversation_set_n_tiles (VsxConversation *conversation,
                              unsigned int player_num,
                              int n_tiles)
{
  VsxPlayer *player = conversation->players[player_num];

  /* Ignore attempts from players that have left */
  if (!vsx_player_is_connected (player))
    return;

  /* Don't let the number of tiles chage once the game has started */
  if (conversation->state != VSX_CONVERSATION_AWAITING_START)
    return;

  n_tiles = CLAMP (n_tiles, 1, VSX_TILE_DATA_N_TILES);

  if (n_tiles != conversation->total_n_tiles)
    {
      conversation->total_n_tiles = n_tiles;
      vsx_conversation_changed (conversation,
                                VSX_CONVERSATION_N_TILES_CHANGED);
    }
}

static bool
try_location (VsxConversation *conversation,
              int x,
              int y)
{
  int i;

  /* Check if this position would overlap any existing tiles */
  for (i = 0; i < conversation->n_tiles_in_play; i++)
    {
      const VsxTile *tile = conversation->tiles + i;

      if (x + VSX_TILE_SIZE > tile->x &&
          x < tile->x + VSX_TILE_SIZE &&
          y + VSX_TILE_SIZE > tile->y &&
          y < tile->y + VSX_TILE_SIZE)
        return false;
    }

  return true;
}

static void
find_free_location (VsxConversation *conversation,
                    int16_t *x_out,
                    int16_t *y_out)
{
  int x, y;

  for (y = 0; ; y++)
    for (x = 0; x < 9; x++)
      {
        int sign_x, sign_y;

        for (sign_x = -1; sign_x <= 1; sign_x += 2)
          for (sign_y = -1; sign_y <= 1; sign_y += 2)
            {
              int try_x = (x * sign_x * (VSX_TILE_SIZE + VSX_TILE_GAP) +
                           VSX_CONVERSATION_CENTER_X);
              int try_y = (y * sign_y * (VSX_TILE_SIZE + VSX_TILE_GAP) +
                           VSX_CONVERSATION_CENTER_Y);

              if (try_location (conversation, try_x, try_y))
                {
                  *x_out = try_x;
                  *y_out = try_y;
                  return;
                }
            }
      }
}

static bool
is_shouting (VsxConversation *conversation)
{
  return (vsx_main_context_get_monotonic_clock (NULL) -
          conversation->last_shout_time <
          VSX_CONVERSATION_SHOUT_TIME);
}

void
vsx_conversation_turn (VsxConversation *conversation,
                       unsigned int player_num)
{
  VsxPlayer *player = conversation->players[player_num];
  bool is_first_turn =
    conversation->n_tiles_in_play == 0;
  VsxTile *tile;

  /* Ignore attempts to shout for a player that has left */
  if (!vsx_player_is_connected (player))
    return;

  /* Don't allow turns for players that don't have the next turn,
   * except for the first turn which is a free for all */
  if (!is_first_turn && !vsx_player_has_next_turn (player))
    return;

  /* Don't allow a turn to be taken while someone is shouting */
  if (is_shouting (conversation))
    return;

  /* Ignore turns if all of the tiles are already in */
  if (conversation->n_tiles_in_play >= conversation->total_n_tiles)
    return;

  tile = conversation->tiles + conversation->n_tiles_in_play;

  find_free_location (conversation, &tile->x, &tile->y);

  conversation->n_tiles_in_play++;

  /* Once the first tile is flipped the game is considered to be
   * started so no more players can join */
  vsx_conversation_start (conversation);
  vsx_conversation_tile_changed (conversation, tile);

  /* As a special case, if there is only one player and it is the
   * first turn then set_next_player won't work because it will leave
   * the player flags as they are when there is only one player */
  if (is_first_turn && conversation->n_connected_players == 1)
    vsx_conversation_set_flag (conversation,
                               player,
                               VSX_PLAYER_NEXT_TURN,
                               true);
  else
    set_next_player (conversation, player_num);
}

void
vsx_conversation_move_tile (VsxConversation *conversation,
                            unsigned int player_num,
                            int tile_num,
                            int x,
                            int y)
{
  VsxTile *tile = conversation->tiles + tile_num;

  if (tile->x != x || tile->y != y)
    {
      tile->x = x;
      tile->y = y;
      tile->last_player = player_num;
      vsx_conversation_tile_changed (conversation, tile);
    }
}

void
vsx_conversation_shout (VsxConversation *conversation,
                        unsigned int player_num)
{
  VsxPlayer *player = conversation->players[player_num];
  VsxConversationChangedData data;

  /* Ignore attempts to shout for a player that has left */
  if (!vsx_player_is_connected (player))
    return;

  /* Don't let shouts come too often */
  if (is_shouting (conversation))
    return;

  conversation->last_shout_time = vsx_main_context_get_monotonic_clock (NULL);

  data.conversation = conversation;
  data.type = VSX_CONVERSATION_SHOUTED;
  data.num = player_num;

  vsx_signal_emit (&conversation->changed_signal, &data);
}
