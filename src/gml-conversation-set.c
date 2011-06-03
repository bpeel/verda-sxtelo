/*
 * Gemelo - A server for chatting with strangers in a foreign language
 * Copyright (C) 2011  Neil Roberts
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

#include <glib-object.h>

#include "gml-conversation-set.h"

G_DEFINE_TYPE (GmlConversationSet, gml_conversation_set, G_TYPE_OBJECT);

typedef struct
{
  char *room_name;
  GmlConversationSet *set;
  GmlConversation *conversation;
} GmlConversationSetHashData;

static void
free_hash_data (GmlConversationSetHashData *data)
{
  g_free (data->room_name);
  g_slice_free (GmlConversationSetHashData, data);
}

static void
conversation_weak_ref_cb (gpointer user_data,
                          GObject *where_the_object_was)
{
  GmlConversationSetHashData *data = user_data;

  /* This will also destroy the hash data */
  g_hash_table_remove (data->set->hash_table, data->room_name);
}

static void
gml_conversation_set_finalize (GObject *object)
{
  GmlConversationSet *self = (GmlConversationSet *) object;

  /* By the time this happens all of the people should be destroyed so
     there should be no conversations. Therefore we don't have to
     bother removing all of the weak references */
  g_warn_if_fail (g_hash_table_size (self->hash_table) == 0);

  g_hash_table_destroy (self->hash_table);

  G_OBJECT_CLASS (gml_conversation_set_parent_class)->finalize (object);
}

static void
gml_conversation_set_class_init (GmlConversationSetClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gml_conversation_set_finalize;
}

static void
gml_conversation_set_init (GmlConversationSet *self)
{
  /* The hash table doesn't have a destroy function for the key
     because its owned by the hash data */
  self->hash_table =
    g_hash_table_new_full (g_str_hash,
                           g_str_equal,
                           NULL, /* key_destroy */
                           /* value_destroy */
                           (GDestroyNotify) free_hash_data);
}

GmlConversationSet *
gml_conversation_set_new (void)
{
  GmlConversationSet *self = g_object_new (GML_TYPE_CONVERSATION_SET, NULL);

  return self;
}

GmlConversation *
gml_conversation_set_get_conversation (GmlConversationSet *set,
                                       const char *room_name)
{
  GmlConversationSetHashData *data;
  GmlConversation *conversation;

  if ((data = g_hash_table_lookup (set->hash_table, room_name)) == NULL)
    {
      /* If there's no conversation with that name then we'll create it */
      conversation = gml_conversation_new ();

      data = g_slice_new (GmlConversationSetHashData);

      data->room_name = g_strdup (room_name);
      data->set = set;
      data->conversation = conversation;

      /* Take a weak reference on the conversation so we can remove it
         from the pending conversation list if the first person
         disappears before another person joins */
      g_object_weak_ref (G_OBJECT (conversation),
                         conversation_weak_ref_cb,
                         data);

      g_hash_table_insert (set->hash_table,
                           data->room_name,
                           data);
    }
  else
    {
      conversation = g_object_ref (data->conversation);

      g_object_weak_unref (G_OBJECT (conversation),
                           conversation_weak_ref_cb,
                           data);

      /* This should also free the data */
      g_hash_table_remove (set->hash_table, room_name);
    }

  return conversation;
}
