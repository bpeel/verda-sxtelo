server_deps = glib_deps + [ openssl_dep ]

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
        'vsx-arguments.c',
        'vsx-chunked-iconv.c',
        'vsx-config.c',
        'vsx-conversation.c',
        'vsx-conversation-set.c',
        'vsx-http-parser.c',
        'vsx-keep-alive-handler.c',
        'vsx-key-value.c',
        'vsx-leave-handler.c',
        'vsx-list.c',
        'vsx-log.c',
        'vsx-main.c',
        'vsx-main-context.c',
        'vsx-move-tile-handler.c',
        'vsx-new-person-handler.c',
        'vsx-object.c',
        'vsx-parse-content-type.c',
        'vsx-person.c',
        'vsx-person-set.c',
        'vsx-player.c',
        'vsx-request-handler.c',
        'vsx-response.c',
        'vsx-send-message-handler.c',
        'vsx-set-n-tiles-handler.c',
        'vsx-server.c',
        'vsx-simple-handler.c',
        'vsx-shout-handler.c',
        'vsx-ssl-error.c',
        'vsx-start-typing-handler.c',
        'vsx-stop-typing-handler.c',
        'vsx-string-response.c',
        'vsx-tile-data.c',
        'vsx-turn-handler.c',
        'vsx-watch-person-handler.c',
        'vsx-watch-person-response.c',
]

executable('verda-sxtelo', server_src,
           dependencies: server_deps,
           install: true,
           include_directories: configinc)
