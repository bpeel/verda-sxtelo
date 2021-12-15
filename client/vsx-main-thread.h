/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2021  Neil Roberts
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

#ifndef VSX_MAIN_THREAD_H
#define VSX_MAIN_THREAD_H

struct vsx_main_thread_token;

typedef void
(* vsx_main_thread_idle_func)(void *data);

typedef void
(* vsx_main_thread_wakeup_func)(void *data);

struct vsx_main_thread_token *
vsx_main_thread_queue_idle(vsx_main_thread_idle_func func,
                           void *user_data);

/* This should only be called from the main thread */
void
vsx_main_thread_cancel_idle(struct vsx_main_thread_token *token);

void
vsx_main_thread_set_wakeup_func(vsx_main_thread_wakeup_func func,
                                void *user_data);

void
vsx_main_thread_flush_idle_events(void);

void
vsx_main_thread_clean_up(void);

#endif /* VSX_MAIN_THREAD_H */
