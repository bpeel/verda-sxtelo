//
//  ViewController.m
//  Grabagram
//
//  Created by demo on 02/03/2022.
//

#import "ViewController.h"

#import <GLKit/GLKit.h>

#include <dlfcn.h>
#include <stdarg.h>

#include "vsx-util.h"
#include "vsx-asset-ios.h"
#include "vsx-main-thread.h"
#include "vsx-signal.h"
#include "vsx-game-state.h"
#include "vsx-connection.h"
#include "vsx-worker.h"
#include "vsx-gl.h"
#include "vsx-shell-interface.h"
#include "vsx-game-painter.h"

@interface ViewController ()

- (void) deinit;

@end

struct callback_wrapper {
        struct vsx_shell_interface shell;
        struct vsx_listener modified_listener;
        void *controller;
};

static ViewController *
controller_for_shell(struct vsx_shell_interface *shell)
{
        struct callback_wrapper *wrapper = vsx_container_of(shell,
                                                            struct callback_wrapper,
                                                            shell);
        
        return (__bridge ViewController *) wrapper->controller;
}

#define GL_LIB_NAME "/System/Library/Frameworks/OpenGLES.framework/OpenGLES"

@implementation ViewController {
        bool initialized;

        struct vsx_main_thread *main_thread;
        struct vsx_asset_manager *asset_manager;
        
        dispatch_queue_main_t main_queue;
        dispatch_block_t wakeup_dispatch;
        
        struct vsx_connection *connection;
        struct vsx_worker *worker;
        struct vsx_game_state *game_state;
        
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
        
        bool redraw_queued;
        
        /* Graphics data that needs to be recreated when the context changes */
        
        struct vsx_gl *gl;
        
        void *gl_lib;
        
        int fb_width, fb_height;
        int dpi;
        
        struct vsx_game_painter *game_painter;
        struct vsx_listener redraw_needed_listener;
        
        struct callback_wrapper callback_wrapper;
}

- (void) destroyGraphics {
        if (game_painter) {
                vsx_game_painter_free(game_painter);
                game_painter = NULL;
        }
        
        if (gl) {
                vsx_gl_free(gl);
                gl = NULL;
        }
        
        if (gl_lib) {
                dlclose(gl_lib);
                gl_lib = NULL;
        }
}

- (void) freeInstanceState {
        vsx_free(instance_state);
        instance_state = NULL;
}

static void
queue_redraw_cb(struct vsx_shell_interface *shell)
{
        ViewController *self = controller_for_shell(shell);

        if (self->redraw_queued)
                return;

        self->redraw_queued = true;
}

static void
log_error_cb(struct vsx_shell_interface *shell,
             const char *format,
             ...)
{
        va_list ap;
        
        va_start(ap, format);
        
        NSString *format_str = [[NSString alloc] initWithUTF8String:format];
        
        NSLogv(format_str, ap);
        
        va_end(ap);
}

static void
share_link_cb(struct vsx_shell_interface *shell,
              const char *link,
              int link_x, int link_y,
              int link_width, int link_height)
{
}

static void
set_name_position_cb(struct vsx_shell_interface *shell,
                     int y_pos,
                     int max_width)
{
}

static int
get_name_height_cb(struct vsx_shell_interface *shell)
{
        return 0;
}

static void
request_name_cb(struct vsx_shell_interface *shell)
{
}

static void
wakeup_cb(void *user_data)
{
        ViewController *self = (__bridge ViewController *) user_data;

        dispatch_async(self->main_queue, self->wakeup_dispatch);
}

static void *
get_proc_address_func(const char *procname,
                      void *user_data)
{
        ViewController *self = (__bridge ViewController *) user_data;
        
        return dlsym(self->gl_lib, procname);
}

- (void) updateNameProperties {
}

- (void) setJoinGame {
        vsx_game_state_reset_for_conversation_id(game_state,
                                                 conversation_id);
}

static void
modified_cb(struct vsx_listener *listener,
            void *user_data)
{
        struct callback_wrapper *wrapper = vsx_container_of(listener,
                                                            struct callback_wrapper,
                                                            modified_listener);
        ViewController *self = (__bridge ViewController *) wrapper->controller;
        const struct vsx_game_state_modified_event *event = user_data;

        switch (event->type) {
        case VSX_GAME_STATE_MODIFIED_TYPE_DIALOG:
                [self updateNameProperties];
                break;
        default:
                break;
        }
}

- (void) loadInstanceState {
        if (instance_state == NULL)
                return;

        vsx_game_state_load_instance_state(game_state,
                                           instance_state);

        [self freeInstanceState];
}

- (bool) ensureGameState {
        if (connection == NULL) {
                connection = vsx_connection_new();

                vsx_connection_set_default_language(connection,
                                                    game_language_code);
        }

        if (worker == NULL) {
                struct vsx_error *error = NULL;

                worker = vsx_worker_new(connection, &error);

                if (worker == NULL) {
                        fprintf(stderr,
                                "vsx_worker_new failed: %s",
                                error->message);
                        vsx_error_free(error);
                        return false;
                } else {
                        vsx_worker_queue_address_resolve(worker,
                                                         "gemelo.org",
                                                         5144);
                }
        }

        if (game_state == NULL) {
                game_state = vsx_game_state_new(main_thread,
                                                      worker,
                                                      connection,
                                                      game_language_code);

                if (is_first_run) {
                        vsx_game_state_set_dialog(game_state,
                                                  VSX_DIALOG_GUIDE);
                }

                if (has_conversation_id)
                        [self setJoinGame];

                struct vsx_signal *signal =
                        vsx_game_state_get_modified_signal(game_state);
                callback_wrapper.modified_listener.notify = modified_cb;
                vsx_signal_add(signal, &callback_wrapper.modified_listener);

                [self loadInstanceState];
                
                vsx_worker_lock(worker);

                vsx_connection_set_running(connection, true);

                vsx_worker_unlock(worker);

                [self updateNameProperties];
        }

        return true;
}

- (bool) ensureGraphics {
        if (game_painter)
                return true;

        if (![self ensureGameState])
                return false;

        if (gl_lib == NULL) {
                gl_lib = dlopen(GL_LIB_NAME, 0);
                
                if (gl_lib == NULL)
                        return false;
        }

        if (gl == NULL)
                gl = vsx_gl_new(get_proc_address_func, (__bridge void *) self);

        struct vsx_error *error = NULL;

        game_painter = vsx_game_painter_new(gl,
                                            main_thread,
                                            game_state,
                                            asset_manager,
                                            dpi,
                                            &callback_wrapper.shell,
                                            &error);
        
        if (game_painter == NULL) {
                log_error_cb(&callback_wrapper.shell,
                             "vsx_game_painter_new failed: %s",
                             error->message);
                vsx_error_free(error);
                return false;
        }

        vsx_game_painter_set_fb_size(game_painter,
                                     fb_width,
                                     fb_height);

        return true;
}

- (void) doInit {
        if (initialized)
                return;

        struct vsx_shell_interface *shell = &callback_wrapper.shell;

        vsx_signal_init(&shell->name_size_signal);

        shell->queue_redraw_cb = queue_redraw_cb;
        shell->log_error_cb = log_error_cb;
        shell->share_link_cb = share_link_cb;
        shell->set_name_position_cb = set_name_position_cb;
        shell->get_name_height_cb = get_name_height_cb;
        shell->request_name_cb = request_name_cb;

        callback_wrapper.controller = (__bridge void *) self;
        
        main_queue = dispatch_get_main_queue();
        
        ViewController * __weak __block closure = self;

        wakeup_dispatch = [^{
                ViewController *controller = closure;

                if (controller != nil)
                        vsx_main_thread_flush_idle_events(controller->main_thread);
        } copy];
        
        main_thread = vsx_main_thread_new(wakeup_cb, (__bridge void *) self);
        asset_manager = vsx_asset_manager_new();
        
        initialized = true;
}

- (void)deinit {
        [self destroyGraphics];
    
        [self freeInstanceState];

        if (game_state) {
                vsx_list_remove(&callback_wrapper.modified_listener.link);
                vsx_game_state_free(game_state);
        }

        if (worker)
                vsx_worker_free(worker);

        if (connection)
                vsx_connection_free(connection);
        
        if (main_thread)
                vsx_main_thread_free(main_thread);
        
        if (asset_manager)
                vsx_asset_manager_free(asset_manager);
}

- (void) glkView:(GLKView *)view drawInRect:(CGRect)rect {
        if (![self ensureGraphics])
                return;
        
        redraw_queued = false;
        
        int width = (int) view.drawableWidth;
        int height = (int) view.drawableHeight;
        
        if (width != fb_width || height != fb_height) {
                fb_width = width;
                fb_height = height;
                vsx_game_painter_set_fb_size(game_painter, width, height);
        }
        
        vsx_game_painter_paint(game_painter);
}

- (void)viewDidLoad {
        [super viewDidLoad];
        
        [self doInit];
        
        GLKView *glView = (GLKView *) self.view;
        
        glView.context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
}

@end
