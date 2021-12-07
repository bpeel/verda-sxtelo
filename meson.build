project('verda-sxtelo', 'c',
        version: '0.1')

cc = meson.get_compiler('c')

foreach a : ['-Wall', '-Wuninitialized', '-Wempty-body', '-Wformat',
      '-Wformat-security', '-Winit-self', '-Wundef',
      '-Wpointer-arith', '-Wmissing-declarations']
  if cc.has_argument(a)
    add_project_arguments(a, language : 'c')
  endif
endforeach

add_project_arguments('-DHAVE_CONFIG_H', language : 'c')

configinc = include_directories('.')

cdata = configuration_data()

if build_machine.endian() == 'little'
   cdata.set('HAVE_LITTLE_ENDIAN', true)
elif build_machine.endian() == 'big'
   cdata.set('HAVE_BIG_ENDIAN', true)
endif

cdata.set_quoted('PACKAGE_VERSION', meson.project_version())

# Paths
vs_bindir = join_paths(get_option('prefix'), get_option('bindir'))

openssl_dep = dependency('openssl')

if get_option('server')
  subdir('server')
endif

if get_option('client')
  subdir('client')
endif

subdir('web')
subdir('common')

configure_file(output : 'config.h', configuration : cdata)
