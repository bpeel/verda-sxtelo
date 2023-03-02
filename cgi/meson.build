invite_cgi_src = [
        'invite-cgi.c',
        '../common/vsx-id-url.c',
        '../common/vsx-qr.c',
        '../common/vsx-util.c',
]

executable('invite-cgi', invite_cgi_src,
           install: true,
           install_dir: get_option('datadir') / 'web/cgi-bin',
           include_directories: [ configinc, '../common' ])
