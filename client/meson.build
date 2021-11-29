gnome = import('gnome')

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
 
client_deps = glib_deps + [ readline_dep, termcap_dep ]

client_headers = [
        'vsx-connection.h',
        'vsx-player.h',
        'vsx-tile.h',
]

client_src = [
        'vsx-connection.c',
        'vsx-main.c',
        'vsx-player.c',
        'vsx-tile.c',
]

enums = gnome.mkenums_simple('vsx-enum-types', sources : client_headers)
marshals = gnome.genmarshal('vsx-marshal',
                            sources : ['vsx-marshal.list'],
                            prefix : 'vsx_marshal')

executable('verda-sxtelo-client', client_src, enums, marshals,
           dependencies: client_deps,
           install: true,
           include_directories: configinc)
