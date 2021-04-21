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

cdata.set_quoted('PACKAGE_VERSION', meson.project_version())

# Paths
vs_bindir = join_paths(get_option('prefix'), get_option('bindir'))

glib_parts = ['glib-2.0', 'gobject-2.0', 'gio-2.0', 'gthread-2.0']
glib_deps = []

foreach part : glib_parts
  # gio 2.22 is needed for the socket API
  # glib 2.28 is needed for g_get_monotonic_time
  glib_deps += dependency(part, version: '>=2.32')
endforeach

openssl_dep = dependency('openssl')

if get_option('server')
  subdir('server')
endif

if get_option('client')
  subdir('client')
endif

subdir('web')

configure_file(output : 'config.h', configuration : cdata)
