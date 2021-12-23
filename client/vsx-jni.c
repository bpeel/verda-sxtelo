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

#include <jni.h>
#include <dlfcn.h>
#include <string.h>
#include <android/log.h>

#include "vsx-gl.h"
#include "vsx-util.h"
#include "vsx-asset-android.h"
#include "vsx-game-painter.h"
#include "vsx-main-thread.h"
#include "vsx-thread-jni.h"
#include "vsx-game-state.h"
#include "vsx-connection.h"
#include "vsx-worker.h"

#define TAG "Anagrams"

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#define VSX_JNI_RENDERER_PREFIX(x)                              \
        Java_uk_co_busydoingnothing_anagrams_GameView_00024GameRenderer_ ## x

#define GET_DATA(native_data) ((struct data *) (native_data))

struct data {
        JavaVM *jvm;

        struct vsx_connection *connection;
        struct vsx_worker *worker;
        struct vsx_asset_manager *asset_manager;

        struct vsx_game_state *game_state;
        struct vsx_listener modified_listener;

        /* Weak pointer to the surface view so that we can queue redraws */
        jobject surface;
        jclass surface_class;
        jmethodID request_render_method_id;
        bool redraw_queued;

        jmethodID queue_flush_idle_method_id;

        /* Graphics data that needs to be recreated when the context changes */
        void *gl_lib;

        int fb_width, fb_height;
        int dpi;

        struct vsx_game_painter *game_painter;
        struct vsx_listener redraw_needed_listener;
};

static void *
get_proc_address_func(const char *procname,
                      void *user_data)
{
        struct data *data = user_data;

        return dlsym(data->gl_lib, procname);
}

static void
destroy_graphics(struct data *data)
{
        if (data->game_painter) {
                vsx_game_painter_free(data->game_painter);
                data->game_painter = NULL;
        }

        if (data->gl_lib) {
                dlclose(data->gl_lib);
                data->gl_lib = NULL;
        }
}

static void
call_void_surface_method(struct data *data,
                         jmethodID method)
{
        JNIEnv *env;

        (*data->jvm)->GetEnv(data->jvm, (void **) &env, JNI_VERSION_1_6);

        jobject surface = (*env)->NewLocalRef(env, data->surface);

        if (surface == NULL)
                return;

        (*env)->CallVoidMethod(env, surface, method);
}

static void
queue_redraw(struct data *data)
{
        if (data->redraw_queued)
                return;

        /* According to the docs this can be called from any thread so
         * it should be safe to call it directly from the renderer
         * thread.
         */
        call_void_surface_method(data, data->request_render_method_id);

        data->redraw_queued = true;
}

static void
redraw_needed_cb(struct vsx_listener *listener,
                 void *signal_data)
{
        struct data *data = vsx_container_of(listener,
                                             struct data,
                                             redraw_needed_listener);

        queue_redraw(data);
}

static void
wakeup_cb(void *user_data)
{
        struct data *data = user_data;

        call_void_surface_method(data, data->queue_flush_idle_method_id);
}

static void
game_state_modified_cb(struct vsx_listener *listener,
                       void *user_data)
{
        struct data *data =
                vsx_container_of(listener, struct data, modified_listener);

        queue_redraw(data);
}

static void
init_game_state(struct data *data)
{
        data->game_state = vsx_game_state_new(data->worker,
                                              data->connection);

        struct vsx_signal *modified_signal =
                vsx_game_state_get_modified_signal(data->game_state);

        data->modified_listener.notify = game_state_modified_cb;
        vsx_signal_add(modified_signal, &data->modified_listener);
}

JNIEXPORT jlong JNICALL
VSX_JNI_RENDERER_PREFIX(createNativeData)(JNIEnv *env,
                                          jobject this,
                                          jobject surface,
                                          jobject asset_manager_jni,
                                          jint dpi)
{
        struct data *data = vsx_calloc(sizeof *data);

        (*env)->GetJavaVM(env, &data->jvm);
        data->surface = (*env)->NewWeakGlobalRef(env, surface);
        data->surface_class = (*env)->GetObjectClass(env, surface);

        data->request_render_method_id =
                (*env)->GetMethodID(env,
                                    data->surface_class,
                                    "requestRender",
                                    "()V");

        data->queue_flush_idle_method_id =
                (*env)->GetMethodID(env,
                                    data->surface_class,
                                    "queueFlushIdleEvents",
                                    "()V");

        vsx_thread_set_jvm(data->jvm);

        data->asset_manager = vsx_asset_manager_new(env, asset_manager_jni);

        data->dpi = dpi;

        vsx_main_thread_set_wakeup_func(wakeup_cb, data);


        data->connection = vsx_connection_new("eo:test", /* room */
                                              "test" /* player_name */);

        struct vsx_error *error = NULL;

        data->worker = vsx_worker_new(data->connection, &error);

        if (data->worker == NULL) {
                LOGE("vsx_worker_new failed: %s", error->message);
                vsx_error_free(error);
        } else {
                vsx_worker_queue_address_resolve(data->worker,
                                                 "gemelo.org",
                                                 5144);

                init_game_state(data);

                vsx_worker_lock(data->worker);

                vsx_connection_set_running(data->connection, true);

                vsx_worker_unlock(data->worker);
        }

        return (jlong) data;
}

JNIEXPORT jboolean JNICALL
VSX_JNI_RENDERER_PREFIX(initContext)(JNIEnv *env,
                                     jobject this,
                                     jlong native_data)
{
        struct data *data = GET_DATA(native_data);

        destroy_graphics(data);

        if (data->game_state == NULL)
                return JNI_FALSE;

        data->gl_lib = dlopen("libGLESv2.so", 0);

        if (data->gl_lib == NULL)
                return JNI_FALSE;

        struct vsx_error *error = NULL;

        vsx_gl_init(get_proc_address_func, data);

        data->game_painter = vsx_game_painter_new(data->game_state,
                                                  data->asset_manager,
                                                  data->dpi,
                                                  &error);

        if (data->game_painter == NULL) {
                LOGE("vsx_game_painter_new failed: %s", error->message);
                vsx_error_free(error);
                return JNI_FALSE;
        }

        struct vsx_signal *redraw_needed_signal =
                vsx_game_painter_get_redraw_needed_signal(data->game_painter);
        data->redraw_needed_listener.notify = redraw_needed_cb;
        vsx_signal_add(redraw_needed_signal, &data->redraw_needed_listener);

        vsx_game_painter_set_fb_size(data->game_painter,
                                     data->fb_width,
                                     data->fb_height);

        return JNI_TRUE;
}

JNIEXPORT void JNICALL
VSX_JNI_RENDERER_PREFIX(resize)(JNIEnv *env,
                                jobject this,
                                jlong native_data,
                                jint width, jint height)
{
        struct data *data = GET_DATA(native_data);

        data->fb_width = width;
        data->fb_height = height;

        if (data->game_painter) {
                vsx_game_painter_set_fb_size(data->game_painter,
                                             width,
                                             height);
        }
}

JNIEXPORT void JNICALL
VSX_JNI_RENDERER_PREFIX(redraw)(JNIEnv *env,
                                jobject this,
                                jlong native_data)
{
        struct data *data = GET_DATA(native_data);

        if (data->game_painter == NULL)
                return;

        vsx_game_state_update(data->game_state);
        vsx_game_painter_paint(data->game_painter);

        data->redraw_queued = false;
}

JNIEXPORT void JNICALL
VSX_JNI_RENDERER_PREFIX(flushIdleEvents)(JNIEnv *env,
                                         jobject this,
                                         jlong native_data)
{
        vsx_main_thread_flush_idle_events();
}

JNIEXPORT void JNICALL
VSX_JNI_RENDERER_PREFIX(handlePointerDown)(JNIEnv *env,
                                           jobject this,
                                           jlong native_data,
                                           jint pointer,
                                           jint x, int y)
{
        struct data *data = GET_DATA(native_data);

        if (data->game_painter) {
                vsx_game_painter_press_finger(data->game_painter,
                                              pointer,
                                              x, y);
        }
}

JNIEXPORT void JNICALL
VSX_JNI_RENDERER_PREFIX(handlePointerMotion)(JNIEnv *env,
                                             jobject this,
                                             jlong native_data,
                                             jint pointer,
                                             jint x, int y)
{
        struct data *data = GET_DATA(native_data);

        if (data->game_painter) {
                vsx_game_painter_move_finger(data->game_painter,
                                             pointer,
                                             x, y);
        }
}

JNIEXPORT void JNICALL
VSX_JNI_RENDERER_PREFIX(handlePointerUp)(JNIEnv *env,
                                         jobject this,
                                         jlong native_data,
                                         jint pointer)
{
        struct data *data = GET_DATA(native_data);

        if (data->game_painter)
                vsx_game_painter_release_finger(data->game_painter, pointer);
}

JNIEXPORT void JNICALL
VSX_JNI_RENDERER_PREFIX(handleGestureCancel)(JNIEnv *env,
                                             jobject this,
                                             jlong native_data)
{
        struct data *data = GET_DATA(native_data);

        if (data->game_painter)
                vsx_game_painter_cancel_gesture(data->game_painter);
}

JNIEXPORT void JNICALL
VSX_JNI_RENDERER_PREFIX(freeNativeData)(JNIEnv *env,
                                        jobject this,
                                        jlong native_data)
{
        struct data *data = GET_DATA(native_data);

        destroy_graphics(data);

        if (data->game_state)
                vsx_game_state_free(data->game_state);
        if (data->worker)
                vsx_worker_free(data->worker);
        if (data->connection)
                vsx_connection_free(data->connection);

        vsx_asset_manager_free(data->asset_manager);

        (*env)->DeleteWeakGlobalRef(env, data->surface);

        vsx_main_thread_set_wakeup_func(NULL, NULL);
        vsx_main_thread_clean_up();

        vsx_free(data);
}
