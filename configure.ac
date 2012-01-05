AC_INIT([gemelo], 0.1)

AM_CONFIG_HEADER([config.h])

AM_INIT_AUTOMAKE([1.9])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PROG_CC

dnl gio 2.22 is needed for the socket API
dnl glib 2.28 is needed for g_get_monotonic_time
PKG_CHECK_MODULES(GLIB, [glib-2.0 >= 2.28 gobject-2.0 >= 2.16 gio-2.0 >= 2.22 gthread-2.0])

AC_ARG_ENABLE([server],
              [AC_HELP_STRING([--enable-server=@<:@no/yes@:>@],
                              [Enable building the server @<:@default=yes@:>@])],
              [],
              [enable_server=yes])

AS_IF([test "x$enable_server" = "xyes"],
      [have_epoll=yes
       AC_CHECK_HEADER(sys/epoll.h, , have_epoll=no)
       AC_CHECK_FUNC(epoll_create, , have_epoll=no)
       AC_CHECK_FUNC(epoll_ctl, , have_epoll=no)
       AS_IF([test "x$have_epoll" = "xno"],
             AC_MSG_ERROR([epoll support is required to build the gemelo server.
This should be provided by glibc since version 2.3.2]))])

AM_CONDITIONAL(BUILD_SERVER, [test "x$enable_server" = "xyes"])

AC_ARG_ENABLE([client],
              [AC_HELP_STRING([--enable-client=@<:@no/yes@:>@],
                              [Enable building the client @<:@default=yes@:>@])],
              [],
              [enable_client=yes])

AS_IF([test "x$enable_client" = "xyes"],
      [PKG_CHECK_MODULES(JSON_GLIB, [json-glib-1.0])
       PKG_CHECK_MODULES(SOUP, [libsoup-2.4])])

AM_CONDITIONAL(BUILD_CLIENT, [test "x$enable_client" = "xyes"])

AC_PATH_PROG([GLIB_MKENUMS], [glib-mkenums])
AC_PATH_PROG([GLIB_GENMARSHAL], [glib-genmarshal])

AC_CONFIG_FILES([
        Makefile
        server/Makefile
        client/Makefile
        web/Makefile
        doc/Makefile
])

AC_OUTPUT
