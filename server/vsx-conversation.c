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

#include "vsx-conversation.h"
#include "vsx-main-context.h"
#include "vsx-log.h"

#define VSX_CONVERSATION_CENTER_X (600 / 2 - VSX_TILE_SIZE / 2)
#define VSX_CONVERSATION_CENTER_Y (360 / 2 - VSX_TILE_SIZE / 2)

static void
vsx_conversation_free (void *object)
{
  VsxConversation *self = object;
  int i;

  for (i = 0; i < self->messages->len; i++)
    {
      VsxConversationMessage *message = &g_array_index (self->messages,
                                                        VsxConversationMessage,
                                                        i);
      g_free (message->text);
    }

  for (i = 0; i < self->n_players; i++)
    vsx_player_free (self->players[i]);

  g_array_free (self->messages, TRUE);

  vsx_object_get_class ()->free (object);
}

static const VsxObjectClass *
vsx_conversation_get_class (void)
{
  static VsxObjectClass klass;

  if (klass.free == NULL)
    {
      klass = *vsx_object_get_class ();
      klass.instance_size = sizeof (VsxConversation);
      klass.free = vsx_conversation_free;
    }

  return &klass;
}

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
  VsxConversationMessage *message;
  GString *message_str;

  /* Ignore attempts to add messages for a player that has left */
  if (!vsx_player_is_connected (conversation->players[player_num]))
    return;

  g_array_set_size (conversation->messages,
                    conversation->messages->len + 1);
  message = &g_array_index (conversation->messages,
                            VsxConversationMessage,
                            conversation->messages->len - 1);

  message_str = g_string_sized_new (length + 32);

  g_string_append_printf (message_str,
                          "[\"message\", {\"person\": %u, "
                          "\"text\": \"",
                          player_num);
  while (length-- > 0)
    {
      /* Replace any control characters or spaces with a space */
      if ((guint8) *buffer <= ' ')
        g_string_append_c (message_str, ' ');
      /* Quote special characters */
      else if (*buffer == '"' || *buffer == '\\')
        {
          g_string_append_c (message_str, '\\');
          g_string_append_c (message_str, *buffer);
        }
      else
        g_string_append_c (message_str, *buffer);

      buffer++;
    }
  g_string_append (message_str, "\"}]\r\n");

  message->length = message_str->len;
  message->text = g_string_free (message_str, FALSE);

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
                           gboolean value)
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
                             gboolean typing)
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
  /* Find the next player that is connected */
  unsigned int next_turn_player = old_player;

  while (TRUE)
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
                                     FALSE);
          vsx_conversation_set_flag (conversation,
                                     conversation->players[next_turn_player],
                                     VSX_PLAYER_NEXT_TURN,
                                     TRUE);
          break;
        }
    }
}

void
vsx_conversation_player_left (VsxConversation *conversation,
                              unsigned int player_num)
{
  VsxPlayer *player = conversation->players[player_num];
  gboolean had_next_turn;

  had_next_turn = vsx_player_has_next_turn (player);

  /* Set the flags before moving the turn so that it will generate
   * only one callback */
  vsx_conversation_set_flags (conversation, player, 0);

  if (had_next_turn)
    set_next_player (conversation, player_num);
}

VsxPlayer *
vsx_conversation_add_player (VsxConversation *conversation,
                             const char *player_name)
{
  VsxPlayer *player;

  g_assert_cmpint (conversation->n_players, <, VSX_CONVERSATION_MAX_PLAYERS);

  player = vsx_player_new (player_name, conversation->n_players);
  conversation->players[conversation->n_players++] = player;

  vsx_conversation_player_changed (conversation, player);

  /* If we've reached the maximum number of players then we'll
   * immediately start the game so that no more players will join */
  if (conversation->n_players >= VSX_CONVERSATION_MAX_PLAYERS)
    vsx_conversation_start (conversation);

  return player;
}

VsxConversation *
vsx_conversation_new (void)
{
  VsxConversation *self = vsx_object_allocate (vsx_conversation_get_class ());
  int i;

  vsx_object_init (self);

  vsx_signal_init (&self->changed_signal);

  self->messages = g_array_new (FALSE, FALSE, sizeof (VsxConversationMessage));

  self->state = VSX_CONVERSATION_AWAITING_START;

  /* Initialise the tile data with the defaults */
  memcpy (self->tiles, vsx_tile_data, sizeof (self->tiles));
  /* Shuffle the tiles. The positions are irrelevant */
  for (i = 0; i < VSX_TILE_DATA_N_TILES; i++)
    {
      int swap_pos = g_random_int_range (0, VSX_TILE_DATA_N_TILES);
      VsxTile temp;

      temp = self->tiles[swap_pos];
      self->tiles[swap_pos] = self->tiles[i];
      self->tiles[i] = temp;
    }

  return self;
}

static gboolean
try_location (VsxConversation *conversation,
              int x,
              int y)
{
  int i;

  /* Check if this position would overlap any existing tiles */
  for (i = 0; i < conversation->n_tiles; i++)
    {
      const VsxTile *tile = conversation->tiles + i;

      if (x + VSX_TILE_SIZE > tile->x &&
          x < tile->x + VSX_TILE_SIZE &&
          y + VSX_TILE_SIZE > tile->y &&
          y < tile->y + VSX_TILE_SIZE)
        return FALSE;
    }

  return TRUE;
}

static void
find_free_location (VsxConversation *conversation,
                    gint16 *x_out,
                    gint16 *y_out)
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

static gboolean
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
  gboolean is_first_turn =
    conversation->state == VSX_CONVERSATION_AWAITING_START;
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
  if (conversation->n_tiles >= VSX_TILE_DATA_N_TILES)
    return;

  tile = conversation->tiles + conversation->n_tiles;

  find_free_location (conversation, &tile->x, &tile->y);

  conversation->n_tiles++;

  /* Once the first tile is flipped the game is considered to be
   * started so no more players can join */
  vsx_conversation_start (conversation);
  vsx_conversation_tile_changed (conversation, tile);

  /* As a special case, if there is only one player and it is the
   * first turn then set_next_player won't work because it will leave
   * the player flags as they are when there is only one player */
  if (is_first_turn && conversation->n_players == 1)
    vsx_conversation_set_flag (conversation,
                               player,
                               VSX_PLAYER_NEXT_TURN,
                               TRUE);
  else
    set_next_player (conversation, player_num);
}

void
vsx_conversation_move_tile (VsxConversation *conversation,
                            int tile_num,
                            int x,
                            int y)
{
  VsxTile *tile = conversation->tiles + tile_num;

  if (tile->x != x || tile->y != y)
    {
      tile->x = x;
      tile->y = y;
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
