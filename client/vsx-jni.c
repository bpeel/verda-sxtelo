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
#include <stdarg.h>

#include "vsx-gl.h"
#include "vsx-util.h"
#include "vsx-asset-android.h"
#include "vsx-game-painter.h"
#include "vsx-main-thread.h"
#include "vsx-thread-jni.h"
#include "vsx-game-state.h"
#include "vsx-connection.h"
#include "vsx-worker.h"
#include "vsx-id-url.h"

#define TAG "Grabagram"

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#define VSX_JNI_RENDERER_PREFIX(x)                              \
        Java_uk_co_busydoingnothing_anagrams_GameView_00024GameRenderer_ ## x

#define GET_DATA(native_data) ((struct data *) (native_data))

struct data {
        JavaVM *jvm;

        struct vsx_main_thread *main_thread;
        struct vsx_connection *connection;
        struct vsx_worker *worker;
        struct vsx_asset_manager *asset_manager;

        struct vsx_game_state *game_state;
        struct vsx_listener modified_listener;

        bool has_conversation_id;
        uint64_t conversation_id;

        bool is_first_run;

        char game_language_code[8];

        /* Instance state that is queued to be set on the
         * vsx_game_state when it is created. It will be freed after
         * being used. This shouldn’t be used for reading the game
         * state, only setting it.
         */
        char *instance_state;

        /* Weak pointer to the surface view so that we can queue redraws */
        jobject surface;
        jclass surface_class;
        jmethodID request_render_method_id;
        bool redraw_queued;

        jmethodID queue_flush_idle_method_id;

        jmethodID share_link_method_id;

        jmethodID open_link_method_id;

        jmethodID set_name_properties_method_id;

        jmethodID request_name_method_id;

        int name_y, name_width, name_height;

        /* Graphics data that needs to be recreated when the context changes */

        struct vsx_gl *gl;

        void *gl_lib;

        int fb_width, fb_height;
        int dpi;

        struct vsx_game_painter *game_painter;

        struct vsx_shell_interface shell;
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

        if (data->gl) {
                vsx_gl_free(data->gl);
                data->gl = NULL;
        }

        if (data->gl_lib) {
                dlclose(data->gl_lib);
                data->gl_lib = NULL;
        }
}

static void
call_void_surface_method(struct data *data,
                         jmethodID method,
                         ...)
{
        JNIEnv *env;

        (*data->jvm)->GetEnv(data->jvm, (void **) &env, JNI_VERSION_1_6);

        jobject surface = (*env)->NewLocalRef(env, data->surface);

        if (surface == NULL)
                return;

        va_list ap;

        va_start(ap, method);

        (*env)->CallVoidMethodV(env, surface, method, ap);

        va_end(ap);

        (*env)->DeleteLocalRef(env, surface);
}

static void
queue_redraw_cb(struct vsx_shell_interface *shell)
{
        struct data *data = vsx_container_of(shell, struct data, shell);

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
log_error_cb(struct vsx_shell_interface *shell,
             const char *format,
             ...)
{
        va_list ap;

        va_start(ap, format);

        __android_log_vprint(ANDROID_LOG_ERROR,
                             TAG,
                             format,
                             ap);

        va_end(ap);
}

static char *
get_app_version_cb(struct vsx_shell_interface *shell)
{
        return vsx_strdup(APP_VERSION);
}

static void
wakeup_cb(void *user_data)
{
        struct data *data = user_data;

        call_void_surface_method(data, data->queue_flush_idle_method_id);
}

static void
call_handle_link_method(struct data *data,
                        const char *link,
                        jmethodID method_id)
{
        JNIEnv *env;

        (*data->jvm)->GetEnv(data->jvm, (void **) &env, JNI_VERSION_1_6);

        jstring link_string = (*env)->NewStringUTF(env, link);

        call_void_surface_method(data, method_id, link_string);

        (*env)->DeleteLocalRef(env, link_string);
}

static void
share_link_cb(struct vsx_shell_interface *shell,
              const char *link,
              int link_x, int link_y,
              int link_width, int link_height)
{
        struct data *data = vsx_container_of(shell, struct data, shell);

        call_handle_link_method(data, link, data->share_link_method_id);
}

static void
open_link_cb(struct vsx_shell_interface *shell,
              const char *link,
              int link_x, int link_y,
              int link_width, int link_height)
{
        struct data *data = vsx_container_of(shell, struct data, shell);

        call_handle_link_method(data, link, data->open_link_method_id);
}

static void
update_name_properties(struct data *data)
{
        enum vsx_dialog dialog = vsx_game_state_get_dialog(data->game_state);

        call_void_surface_method(data,
                                 data->set_name_properties_method_id,
                                 (int) (dialog == VSX_DIALOG_NAME),
                                 data->name_y,
                                 data->name_width);
}

static void
set_name_position_cb(struct vsx_shell_interface *shell,
                     int y_pos,
                     int max_width)
{
        struct data *data = vsx_container_of(shell, struct data, shell);

        if (y_pos == data->name_y && max_width == data->name_width)
                return;

        data->name_y = y_pos;
        data->name_width = max_width;

        update_name_properties(data);
}

static int
get_name_height_cb(struct vsx_shell_interface *shell)
{
        struct data *data = vsx_container_of(shell, struct data, shell);

        return data->name_height;
}

static void
request_name_cb(struct vsx_shell_interface *shell)
{
        struct data *data = vsx_container_of(shell, struct data, shell);

        call_void_surface_method(data, data->request_name_method_id);
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
        data->share_link_method_id =
                (*env)->GetMethodID(env,
                                    data->surface_class,
                                    "shareLink",
                                    "(Ljava/lang/String;)V");

        data->open_link_method_id =
                (*env)->GetMethodID(env,
                                    data->surface_class,
                                    "openLink",
                                    "(Ljava/lang/String;)V");

        data->set_name_properties_method_id =
                (*env)->GetMethodID(env,
                                    data->surface_class,
                                    "setNameProperties",
                                    "(ZII)V");

        data->request_name_method_id =
                (*env)->GetMethodID(env,
                                    data->surface_class,
                                    "requestName",
                                    "()V");

        vsx_signal_init(&data->shell.name_size_signal);

        data->shell.queue_redraw_cb = queue_redraw_cb;
        data->shell.log_error_cb = log_error_cb;
        data->shell.get_app_version_cb = get_app_version_cb;
        data->shell.share_link_cb = share_link_cb;
        data->shell.open_link_cb = open_link_cb;
        data->shell.set_name_position_cb = set_name_position_cb;
        data->shell.get_name_height_cb = get_name_height_cb;
        data->shell.request_name_cb = request_name_cb;

        vsx_thread_set_jvm(data->jvm);

        data->asset_manager = vsx_asset_manager_new(env, asset_manager_jni);

        data->dpi = dpi;

        data->main_thread = vsx_main_thread_new(wakeup_cb, data);

        return (jlong) data;
}

static void
free_instance_state(struct data *data)
{
        vsx_free(data->instance_state);
        data->instance_state = NULL;
}

static void
load_instance_state(struct data *data)
{
        if (data->instance_state == NULL)
                return;

        vsx_game_state_load_instance_state(data->game_state,
                                           data->instance_state);

        free_instance_state(data);
}

static void
modified_cb(struct vsx_listener *listener,
            void *user_data)
{
        struct data *data = vsx_container_of(listener,
                                             struct data,
                                             modified_listener);
        const struct vsx_game_state_modified_event *event = user_data;

        switch (event->type) {
        case VSX_GAME_STATE_MODIFIED_TYPE_DIALOG:
                update_name_properties(data);
                break;
        default:
                break;
        }
}

static void
set_join_game(struct data *data)
{
        vsx_game_state_reset_for_conversation_id(data->game_state,
                                                 data->conversation_id);
}

static bool
ensure_game_state(struct data *data)
{
        if (data->connection == NULL) {
                data->connection = vsx_connection_new();

                vsx_connection_set_default_language(data->connection,
                                                    data->game_language_code);
        }

        if (data->worker == NULL) {
                struct vsx_error *error = NULL;

                data->worker = vsx_worker_new(data->connection, &error);

                if (data->worker == NULL) {
                        LOGE("vsx_worker_new failed: %s", error->message);
                        vsx_error_free(error);
                        return false;
                } else {
                        vsx_worker_queue_address_resolve(data->worker,
                                                         "gemelo.org",
                                                         5144);
                }
        }

        if (data->game_state == NULL) {
                data->game_state = vsx_game_state_new(data->main_thread,
                                                      data->worker,
                                                      data->connection,
                                                      data->game_language_code);

                if (data->is_first_run) {
                        vsx_game_state_set_dialog(data->game_state,
                                                  VSX_DIALOG_GUIDE);
                }


                struct vsx_signal *signal =
                        vsx_game_state_get_modified_signal(data->game_state);
                data->modified_listener.notify = modified_cb;
                vsx_signal_add(signal, &data->modified_listener);

                load_instance_state(data);

                if (data->has_conversation_id)
                        set_join_game(data);

                vsx_worker_lock(data->worker);

                vsx_connection_set_running(data->connection, true);

                vsx_worker_unlock(data->worker);

                update_name_properties(data);
        }

        return true;
}

JNIEXPORT jboolean JNICALL
VSX_JNI_RENDERER_PREFIX(initContext)(JNIEnv *env,
                                     jobject this,
                                     jlong native_data)
{
        struct data *data = GET_DATA(native_data);

        destroy_graphics(data);

        if (!ensure_game_state(data))
                return JNI_FALSE;

        data->gl_lib = dlopen("libGLESv2.so", 0);

        if (data->gl_lib == NULL)
                return JNI_FALSE;

        struct vsx_error *error = NULL;

        data->gl = vsx_gl_new(get_proc_address_func, data);

        data->game_painter = vsx_game_painter_new(data->gl,
                                                  data->main_thread,
                                                  data->game_state,
                                                  data->asset_manager,
                                                  data->dpi,
                                                  &data->shell,
                                                  &error);

        if (data->game_painter == NULL) {
                LOGE("vsx_game_painter_new failed: %s", error->message);
                vsx_error_free(error);
                return JNI_FALSE;
        }

        vsx_game_painter_set_fb_size(data->game_painter,
                                     data->fb_width,
                                     data->fb_height);

        return JNI_TRUE;
}

JNIEXPORT void JNICALL
VSX_JNI_RENDERER_PREFIX(setInstanceState)(JNIEnv *env,
                                          jobject this,
                                          jlong native_data,
                                          jstring state_string)
{
        struct data *data = GET_DATA(native_data);

        free_instance_state(data);

        const char *state =
                (*env)->GetStringUTFChars(env,
                                          state_string,
                                          NULL /* isCopy */);

        data->instance_state = vsx_strdup(state);

        (*env)->ReleaseStringUTFChars(env, state_string, state);
}

JNIEXPORT jstring JNICALL
VSX_JNI_RENDERER_PREFIX(getInstanceState)(JNIEnv *env,
                                          jobject this,
                                          jlong native_data)
{
        struct data *data = GET_DATA(native_data);

        if (data->game_state == NULL)
                return NULL;

        char *str = vsx_game_state_save_instance_state(data->game_state);

        jstring state_string = (*env)->NewStringUTF(env, str);

        vsx_free(str);

        return state_string;
}

JNIEXPORT void JNICALL
VSX_JNI_RENDERER_PREFIX(setInviteUrl)(JNIEnv *env,
                                      jobject this,
                                      jlong native_data,
                                      jstring url_string)
{
        struct data *data = GET_DATA(native_data);

        const char *url =
                (*env)->GetStringUTFChars(env, url_string, NULL /* isCopy */);

        uint64_t id;
        bool ret = vsx_id_url_decode(url, &id);

        (*env)->ReleaseStringUTFChars(env, url_string, url);

        if (ret) {
                data->has_conversation_id = true;
                data->conversation_id = id;

                if (data->game_state)
                        set_join_game(data);
        }
}

JNIEXPORT void JNICALL
VSX_JNI_RENDERER_PREFIX(setGameLanguageCode)(JNIEnv *env,
                                             jobject this,
                                             jlong native_data,
                                             jstring language_code_str)
{
        struct data *data = GET_DATA(native_data);

        const char *language_code =
                (*env)->GetStringUTFChars(env,
                                          language_code_str,
                                          NULL /* isCopy */);

        strncpy(data->game_language_code,
                language_code,
                VSX_N_ELEMENTS(data->game_language_code));

        data->game_language_code[VSX_N_ELEMENTS(data->game_language_code) - 1] =
                '\0';

        (*env)->ReleaseStringUTFChars(env, language_code_str, language_code);
}

JNIEXPORT void JNICALL
VSX_JNI_RENDERER_PREFIX(setFirstRun)(JNIEnv *env,
                                     jobject this,
                                     jlong native_data)
{
        struct data *data = GET_DATA(native_data);

        data->is_first_run = true;
}

JNIEXPORT void JNICALL
VSX_JNI_RENDERER_PREFIX(setNameHeight)(JNIEnv *env,
                                       jobject this,
                                       jlong native_data,
                                       jint height)
{
        struct data *data = GET_DATA(native_data);

        if (height == data->name_height)
                return;

        data->name_height = height;

        vsx_signal_emit(&data->shell.name_size_signal, NULL);
}

JNIEXPORT void JNICALL
VSX_JNI_RENDERER_PREFIX(setPlayerName)(JNIEnv *env,
                                       jobject this,
                                       jlong native_data,
                                       jstring name_str)
{
        struct data *data = GET_DATA(native_data);

        if (data->game_state == NULL)
                return;

        const char *name =
                (*env)->GetStringUTFChars(env,
                                          name_str,
                                          NULL /* isCopy */);

        vsx_game_state_set_player_name(data->game_state, name);

        (*env)->ReleaseStringUTFChars(env, name_str, name);

        vsx_game_state_set_dialog(data->game_state,
                                  VSX_DIALOG_INVITE_LINK);
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

        data->redraw_queued = false;

        vsx_game_painter_paint(data->game_painter);
}

JNIEXPORT void JNICALL
VSX_JNI_RENDERER_PREFIX(flushIdleEvents)(JNIEnv *env,
                                         jobject this,
                                         jlong native_data)
{
        struct data *data = GET_DATA(native_data);

        vsx_main_thread_flush_idle_events(data->main_thread);
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

        free_instance_state(data);

        if (data->game_state) {
                vsx_list_remove(&data->modified_listener.link);
                vsx_game_state_free(data->game_state);
        }
        if (data->worker)
                vsx_worker_free(data->worker);
        if (data->connection)
                vsx_connection_free(data->connection);

        vsx_asset_manager_free(env, data->asset_manager);

        (*env)->DeleteWeakGlobalRef(env, data->surface);

        vsx_main_thread_free(data->main_thread);

        vsx_free(data);
}
