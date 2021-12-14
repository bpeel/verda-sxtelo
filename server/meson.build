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

server_common = [
        '../common/vsx-buffer.c',
        'vsx-conversation.c',
        'vsx-conversation-set.c',
        '../common/vsx-error.c',
        '../common/vsx-file-error.c',
        'vsx-generate-id.c',
        'vsx-hash-table.c',
        '../common/vsx-list.c',
        'vsx-log.c',
        'vsx-main-context.c',
        'vsx-object.c',
        '../common/vsx-netaddress.c',
        'vsx-player.c',
        '../common/vsx-slab.c',
        'vsx-slice.c',
        'vsx-tile-data.c',
        '../common/vsx-utf8.c',
        '../common/vsx-util.c',
]

server_src = [
        'vsx-base64.c',
        '../common/vsx-bitmask.c',
        'vsx-config.c',
        'vsx-connection.c',
        'vsx-key-value.c',
        'vsx-main.c',
        'vsx-normalize-name.c',
        'vsx-person.c',
        'vsx-person-set.c',
        '../common/vsx-proto.c',
        'vsx-server.c',
        '../common/vsx-socket.c',
        'vsx-ssl-error.c',
        'vsx-ws-parser.c',
] + server_common

executable('verda-sxtelo', server_src,
           dependencies: server_deps,
           install: true,
           include_directories: inc_dirs)

test_ws_parser_src = [
        '../common/vsx-error.c',
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
        '../common/vsx-bitmask.c',
        'vsx-connection.c',
        'vsx-normalize-name.c',
        'vsx-person.c',
        'vsx-person-set.c',
        '../common/vsx-proto.c',
        'vsx-ws-parser.c',
        'test-connection.c',
] + server_common

test_connection = executable('test-connection',
                             test_connection_src,
                             dependencies: server_deps,
                             include_directories: inc_dirs)
test('connection', test_connection)

test_hash_table_src = [
        'vsx-hash-table.c',
        '../common/vsx-list.c',
        '../common/vsx-util.c',
        'test-hash-table.c',
]

test_hash_table = executable('test-hash-table',
                             test_hash_table_src,
                             include_directories: inc_dirs)
test('hash-table', test_hash_table)

test_conversation_set_src = [
        'test-conversation-set.c',
] + server_common

test_conversation_set = executable('test-conversation-set',
                                   test_conversation_set_src,
                                   dependencies: server_deps,
                                   include_directories: inc_dirs)
test('conversation-set', test_conversation_set)
