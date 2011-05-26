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

#include <glib.h>
#include <unistd.h>

#include "gml-main-context.h"

static int carry_on = TRUE;

static void
timer_cb (GmlMainContextSource *source,
          void *user_data)
{
  g_print ("timer %p\n", user_data);
}

static void
poll_in_cb (GmlMainContextSource *source,
            int fd,
            GmlMainContextPollFlags flags,
            void *user_data)
{
  g_print ("%i %p 0x%x\n", fd, user_data, flags);
  carry_on = FALSE;
}

int
main (int argc, char **argv)
{
  GError *error = NULL;
  GmlMainContext *mc = gml_main_context_new (&error);
  GmlMainContextSource *poll_source, *timer_source;

  if (mc == NULL)
    {
      g_print ("%s\n", error->message);
      return 1;
    }

  poll_source = gml_main_context_add_poll (mc,
                                           STDIN_FILENO,
                                           GML_MAIN_CONTEXT_POLL_IN,
                                           poll_in_cb,
                                           (void *) 0xdeadbeef);
  timer_source = gml_main_context_add_timer (mc,
                                             timer_cb,
                                             (void *) 0xdeadbeef);
  gml_main_context_set_timer (mc, timer_source, 1750);

  while (carry_on)
    gml_main_context_poll (mc, -1);

  gml_main_context_remove_source (mc, poll_source);
  gml_main_context_remove_source (mc, timer_source);

  gml_main_context_free (mc);

  return 0;
}
