test_utf8_src = [
        'vsx-utf8.c',
        'test-utf8.c',
]
test_utf8 = executable('test-utf8', test_utf8_src,
                       include_directories: configinc)
test('utf8', test_utf8)

test_hash_table_src = [
        'vsx-hash-table.c',
        'vsx-list.c',
        'vsx-util.c',
        'test-hash-table.c',
]

test_hash_table = executable('test-hash-table',
                             test_hash_table_src,
                             include_directories: inc_dirs)
test('hash-table', test_hash_table)
