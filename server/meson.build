thread_dep = dependency('threads')

server_deps = [ openssl_dep, thread_dep ]

inc_dirs = [ configinc, '../common' ]

if get_option('systemd')
  server_deps += dependency('libsystemd')
  cdata.set('USE_SYSTEMD', true)
  service_dir = get_option('sysconfdir') / 'verda-sxtelo' / 'services'

  install_data('verda-sxtelo.socket',
               install_dir : service_dir)

  configure_file(input : 'verda-sxtelo.service.in',
                 output : 'verda-sxtelo.service',
                 configuration : {'bindir' : vs_bindir},
                 install_dir : service_dir)
endif

server_src = [
        'vsx-base64.c',
        '../common/vsx-buffer.c',
        'vsx-config.c',
        'vsx-connection.c',
        'vsx-conversation.c',
        'vsx-conversation-set.c',
        'vsx-error.c',
        'vsx-file-error.c',
        'vsx-generate-id.c',
        'vsx-key-value.c',
        '../common/vsx-list.c',
        'vsx-log.c',
        'vsx-main.c',
        'vsx-main-context.c',
        '../common/vsx-netaddress.c',
        'vsx-normalize-name.c',
        'vsx-object.c',
        'vsx-person.c',
        'vsx-person-set.c',
        'vsx-player.c',
        '../common/vsx-proto.c',
        'vsx-server.c',
        'vsx-slab.c',
        'vsx-slice.c',
        '../common/vsx-socket.c',
        'vsx-ssl-error.c',
        'vsx-tile-data.c',
        '../common/vsx-utf8.c',
        '../common/vsx-util.c',
        'vsx-ws-parser.c',
]

executable('verda-sxtelo', server_src,
           dependencies: server_deps,
           install: true,
           include_directories: inc_dirs)

test_ws_parser_src = [
        'vsx-error.c',
        '../common/vsx-util.c',
        'vsx-ws-parser.c',
        'test-ws-parser.c',
]
test_ws_parser = executable('test-ws-parser',
                            test_ws_parser_src,
                            dependencies: server_deps,
                            include_directories: inc_dirs)
test('ws-parser', test_ws_parser)

test_connection_src = [
        'vsx-base64.c',
        '../common/vsx-buffer.c',
        'vsx-connection.c',
        'vsx-conversation.c',
        'vsx-conversation-set.c',
        'vsx-error.c',
        'vsx-file-error.c',
        'vsx-generate-id.c',
        '../common/vsx-list.c',
        'vsx-log.c',
        'vsx-main-context.c',
        'vsx-object.c',
        '../common/vsx-netaddress.c',
        'vsx-normalize-name.c',
        'vsx-person.c',
        'vsx-person-set.c',
        'vsx-player.c',
        '../common/vsx-proto.c',
        'vsx-slab.c',
        'vsx-slice.c',
        'vsx-tile-data.c',
        '../common/vsx-utf8.c',
        '../common/vsx-util.c',
        'vsx-ws-parser.c',
        'test-connection.c',
]
test_connection = executable('test-connection',
                             test_connection_src,
                             dependencies: server_deps,
                             include_directories: inc_dirs)
test('connection', test_connection)
