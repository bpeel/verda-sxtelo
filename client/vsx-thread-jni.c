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

#include "config.h"

#include "vsx-thread-jni.h"

#include "vsx-util.h"

static JavaVM *jvm;

struct thread_create_data {
        void *(*start_routine) (void *);
        void *arg;
};

static void *
thread_create_cb(void *user_data)
{
        struct thread_create_data *data = user_data;
        void *(*start_routine) (void *) = data->start_routine;
        void *arg = data->arg;

        vsx_free(data);

        JNIEnv *env;

        (*jvm)->AttachCurrentThread(jvm, &env, NULL);

        return start_routine(arg);
}

void
vsx_thread_set_jvm(JavaVM *jvm_arg)
{
        jvm = jvm_arg;
}

int
vsx_thread_create(pthread_t *thread,
                  const pthread_attr_t *attr,
                  void *(*start_routine) (void *),
                  void *arg)
{
        struct thread_create_data *data = vsx_alloc(sizeof *data);

        data->start_routine = start_routine;
        data->arg = arg;

        int ret = pthread_create(thread, attr, thread_create_cb, data);

        if (ret != 0)
                vsx_free(data);

        return ret;
}
