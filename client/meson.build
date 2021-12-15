inc_dirs = [ configinc, '../common' ]

connection_src = [
        '../common/vsx-bitmask.c',
        '../common/vsx-buffer.c',
        'vsx-connection.c',
        '../common/vsx-error.c',
        '../common/vsx-file-error.c',
        '../common/vsx-list.c',
        '../common/vsx-netaddress.c',
        'vsx-player.c',
        '../common/vsx-proto.c',
        '../common/vsx-slab.c',
        '../common/vsx-socket.c',
        'vsx-tile.c',
        '../common/vsx-utf8.c',
        '../common/vsx-util.c',
]

sdl_dep = dependency('sdl2')

client_deps = [ thread_dep, sdl_dep ]

client_headers = [
        'vsx-connection.h',
        'vsx-player.h',
        'vsx-tile.h',
]

client_src = [
        'vsx-array-object.c',
        'vsx-asset-linux.c',
        'vsx-game-painter.c',
        'vsx-game-state.c',
        'vsx-gl.c',
        'vsx-main.c',
        'vsx-main-thread.c',
        'vsx-map-buffer.c',
        'vsx-monotonic.c',
        'vsx-quad-buffer.c',
        'vsx-shader-data.c',
        'vsx-tile-painter.c',
        'vsx-worker.c',
] + connection_src

executable('verda-sxtelo-client', client_src,
           dependencies: client_deps,
           install: true,
           include_directories: inc_dirs)

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
        'vsx-worker.c',
] + connection_src

test_worker = executable('test-worker',
                         test_worker_src,
                         dependencies: [thread_dep],
                         include_directories: inc_dirs)
test('worker', test_worker)
