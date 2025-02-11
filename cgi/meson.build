crc_table_h = custom_target(
        'crc-table.h',
        output: 'crc-table.h',
        input: 'make-crc-table.py',
        command: [python, '@INPUT0@', '@OUTPUT@'],
)

invite_cgi_src = [
        'vsx-generate-qr.c',
        'invite-cgi.c',
        '../common/vsx-id-url.c',
        '../common/vsx-qr.c',
        '../common/vsx-util.c',
        crc_table_h,
]

deps = []

if get_option('fastcgi')
  if not cc.check_header('fcgi_stdio.h')
    error('fcgi_stdio.h is needed with the fastcgi option')
  endif
  deps += cc.find_library('fcgi')
  cdata.set('HAVE_FASTCGI', true)
endif

executable('invite-cgi', invite_cgi_src,
           install: true,
           install_dir: get_option('datadir') / 'web/cgi-bin',
           dependencies: deps,
           include_directories: [ configinc, '../common' ])
