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

executable('invite-cgi', invite_cgi_src,
           install: true,
           install_dir: get_option('datadir') / 'web/cgi-bin',
           include_directories: [ configinc, '../common' ])
