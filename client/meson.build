inc_dirs = [ configinc, '../common' ]

m_dep = cc.find_library('m', required : false)
freetype = dependency('freetype2')

connection_src = [
        '../common/vsx-bitmask.c',
        '../common/vsx-buffer.c',
        'vsx-connection.c',
        '../common/vsx-error.c',
        '../common/vsx-file-error.c',
        '../common/vsx-list.c',
        '../common/vsx-netaddress.c',
        '../common/vsx-proto.c',
        '../common/vsx-slab.c',
        '../common/vsx-socket.c',
        '../common/vsx-utf8.c',
        '../common/vsx-util.c',
]

client_common_src = [
        'vsx-array-object.c',
        'vsx-board.c',
        'vsx-board-painter.c',
        'vsx-bsp.c',
        'vsx-button-painter.c',
        'vsx-dialog.c',
        'vsx-dialog-painter.c',
        'vsx-error-painter.c',
        'vsx-fireworks-painter.c',
        'vsx-font.c',
        'vsx-game-painter.c',
        'vsx-game-state.c',
        'vsx-gl.c',
        'vsx-glyph-hash.c',
        '../common/vsx-hash-table.c',
        'vsx-id-url.c',
        'vsx-image.c',
        'vsx-image-loader.c',
        'vsx-instance-state.c',
        'vsx-invite-painter.c',
        'vsx-language-painter.c',
        'vsx-layout.c',
        'vsx-main-thread.c',
        'vsx-map-buffer.c',
        'vsx-menu-painter.c',
        'vsx-mipmap.c',
        'vsx-monotonic.c',
        'vsx-name-painter.c',
        'vsx-note-painter.c',
        'vsx-paint-state.c',
        'vsx-qr.c',
        'vsx-quad-tool.c',
        'vsx-shader-data.c',
        'vsx-shadow-painter.c',
        'vsx-text.c',
        'vsx-tile-painter.c',
        'vsx-tile-texture.c',
        'vsx-tile-texture-letters.c',
        'vsx-tile-tool.c',
        'vsx-worker.c',
] + connection_src

if get_option('client')
        sdl_dep = dependency('sdl2')

        client_deps = [ thread_dep, sdl_dep, m_dep, freetype ]

        client_src = [
                'vsx-asset-linux.c',
                'vsx-main.c',
                'vsx-thread-linux.c',
        ] + client_common_src

        executable('verda-sxtelo-client', client_src,
                   dependencies: client_deps,
                   install: true,
                   include_directories: inc_dirs)


        test_image_loader_src = [
                'vsx-image-loader.c',
                'vsx-asset-linux.c',
                'vsx-main-thread.c',
                'vsx-monotonic.c',
                'vsx-thread-linux.c',
                '../common/vsx-util.c',
                'vsx-image.c',
                '../common/vsx-error.c',
                'test-image-loader.c',
        ]
        test_image_loader = executable('test-image-loader',
                                       test_image_loader_src,
                                       dependencies: [sdl_dep, thread_dep],
                                       include_directories: inc_dirs)
        test('image_loader', test_image_loader)
endif

if get_option('jni')
        jni_src = [
                'vsx-jni.c',
                'vsx-asset-android.c',
                'vsx-thread-jni.c',
        ] + client_common_src

        android_dep = cc.find_library('android', required : true)
        log_dep = cc.find_library('log', required : true)

        jni_lib = library('anagrams', jni_src,
                          dependencies: [android_dep, log_dep, m_dep, freetype],
                          include_directories: inc_dirs,
                          install: true)
endif

test_client_connection_src = [
        'test-client-connection.c',
] + connection_src

test_client_connection = executable('test-client-connection',
                                    test_client_connection_src,
                                    include_directories: inc_dirs)
test('client-connection', test_client_connection)

test_worker_src = [
        'test-worker.c',
        'vsx-monotonic.c',
        'vsx-thread-linux.c',
        'vsx-worker.c',
] + connection_src

test_worker = executable('test-worker',
                         test_worker_src,
                         dependencies: [thread_dep],
                         include_directories: inc_dirs)
test('worker', test_worker)

test_game_state_src = [
        'test-game-state.c',
        'vsx-dialog.c',
        'vsx-game-state.c',
        'vsx-instance-state.c',
        'vsx-main-thread.c',
        'vsx-monotonic.c',
        'vsx-text.c',
        'vsx-thread-linux.c',
        'vsx-worker.c',
] + connection_src

test_game_state = executable('test-game-state',
                             test_game_state_src,
                             dependencies: [thread_dep],
                             include_directories: inc_dirs)
test('game-state', test_game_state)

test_id_url_src = [
        'vsx-id-url.c',
        '../common/vsx-util.c',
        'test-id-url.c',
]

test_id_url = executable('test-id-url',
                         test_id_url_src,
                         include_directories: inc_dirs)
test('id-url', test_id_url)

generate_qr_src = [
        'vsx-qr.c',
        '../common/vsx-util.c',
        'generate-qr.c',
]

generate_qr = executable('generate-qr',
                         generate_qr_src,
                         include_directories: inc_dirs)

test('test-qr',
     files('test-qr.py'),
     depends: generate_qr,
     args: [ generate_qr.full_path() ])

test_instance_state_src = [
        '../common/vsx-buffer.c',
        'vsx-dialog.c',
        'vsx-instance-state.c',
        '../common/vsx-util.c',
        'test-instance-state.c',
]
test_instance_state = executable('test-instance-state',
                                 test_instance_state_src,
                                 include_directories: inc_dirs)
test('test-instance-state', test_instance_state)

test_main_thread_src = [
        'vsx-main-thread.c',
        'vsx-monotonic.c',
        'vsx-thread-linux.c',
        '../common/vsx-util.c',
        'test-main-thread.c',
]
test_main_thread = executable('test-main-thread',
                              test_main_thread_src,
                              dependencies: [thread_dep],
                              include_directories: inc_dirs)
test('main-thread', test_main_thread)

test_glyph_hash_src = [
        'test-glyph-hash.c',
        'vsx-glyph-hash.c',
        '../common/vsx-buffer.c',
        '../common/vsx-hash-table.c',
        '../common/vsx-slab.c',
        '../common/vsx-util.c',
]
test_glyph_hash = executable('test-glyph-hash', test_glyph_hash_src,
                             include_directories: inc_dirs)
test('glyph-hash', test_glyph_hash)

test_bsp_src = [
        'test-bsp.c',
        'vsx-bsp.c',
        '../common/vsx-buffer.c',
        '../common/vsx-util.c',
]
test_bsp = executable('test-bsp', test_bsp_src,
                      include_directories: inc_dirs)
test('bsp', test_bsp)

test_tile_texture_src = [
        'test-tile-texture.c',
        'vsx-tile-texture.c',
        'vsx-tile-texture.c',
        'vsx-tile-texture-letters.c',
        '../common/vsx-utf8.c',
]
test_tile_texture = executable('test-tile-texture', test_tile_texture_src,
                               include_directories: inc_dirs)
test('tile-texture', test_tile_texture)
