inc_dirs = [ configinc, '../common' ]

readline_dep = declare_dependency(link_args: '-lreadline -ltermcap')

readline_dep = cc.find_library('readline', required : true)
cc.check_header('readline/readline.h',
                dependencies: readline_dep,
                required: true,
                prefix: '#include <stdio.h>')
termcap_dep = cc.find_library('termcap', required : true)
cc.check_header('term.h',
                dependencies: readline_dep,
                required: true)
 
client_deps = [ readline_dep, termcap_dep ]

client_headers = [
        'vsx-connection.h',
        'vsx-player.h',
        'vsx-tile.h',
]

client_src = [
        '../common/vsx-buffer.c',
        'vsx-connection.c',
        '../common/vsx-error.c',
        '../common/vsx-file-error.c',
        '../common/vsx-list.c',
        'vsx-main.c',
        'vsx-monotonic.c',
        '../common/vsx-netaddress.c',
        'vsx-player.c',
        '../common/vsx-proto.c',
        '../common/vsx-slab.c',
        '../common/vsx-socket.c',
        'vsx-tile.c',
        '../common/vsx-utf8.c',
        '../common/vsx-util.c',
]

executable('verda-sxtelo-client', client_src,
           dependencies: client_deps,
           install: true,
           include_directories: inc_dirs)
