AC_INIT([verda-sxtelo], 0.1)

AM_CONFIG_HEADER([config.h])
AC_GNU_SOURCE

AM_INIT_AUTOMAKE([1.9])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PROG_CC

dnl gio 2.22 is needed for the socket API
dnl glib 2.28 is needed for g_get_monotonic_time
PKG_CHECK_MODULES(GLIB, [glib-2.0 >= 2.32 gobject-2.0 >= 2.32 gio-2.0 >= 2.32 gthread-2.0 >= 2.32])

AC_ARG_ENABLE([server],
              [AC_HELP_STRING([--enable-server=@<:@no/yes@:>@],
                              [Enable building the server @<:@default=yes@:>@])],
              [],
              [enable_server=yes])

AC_ARG_ENABLE([systemd],
              [AC_HELP_STRING([--enable-systemd=@<:@no/yes@:>@],
                              [Enable socket activation support via systemd @<:@default=yes@:>@])],
              [],
              [enable_systemd=yes])

AS_IF([test "x$enable_server" = "xyes"],
      [have_epoll=yes
       AC_CHECK_HEADER(sys/epoll.h, , have_epoll=no)
       AC_CHECK_FUNC(epoll_create, , have_epoll=no)
       AC_CHECK_FUNC(epoll_ctl, , have_epoll=no)
       AS_IF([test "x$have_epoll" = "xno"],
             AC_MSG_ERROR([epoll support is required to build the verda sxtelo server.
This should be provided by glibc since version 2.3.2]))],
      [enable_systemd=no])

AS_IF([test "x$enable_systemd" = "xyes"],
      [PKG_CHECK_MODULES(LIBSYSTEMD_DAEMON, [libsystemd-daemon])
       AC_DEFINE(USE_SYSTEMD, 1, [Enable socket activation via systemd])])
AM_CONDITIONAL(USE_SYSTEMD, [test "x$enable_systemd" = "xyes"])

AM_CONDITIONAL(BUILD_SERVER, [test "x$enable_server" = "xyes"])

AC_ARG_ENABLE([client],
              [AC_HELP_STRING([--enable-client=@<:@no/yes@:>@],
                              [Enable building the client @<:@default=yes@:>@])],
              [],
              [enable_client=yes])

AS_IF([test "x$enable_client" = "xyes"],
      [PKG_CHECK_MODULES(JSON_GLIB, [json-glib-1.0])
       PKG_CHECK_MODULES(SOUP, [libsoup-2.4])
       AC_CHECK_HEADER([readline/readline.h], ,
                       AC_MSG_ERROR([readline required to build the client]))
       AC_CHECK_LIB([readline], readline, ,
                    AC_MSG_ERROR([readline required to build the client]))
       AC_CHECK_HEADER([term.h], ,
                       AC_MSG_ERROR([term.h required to build the client]))
       AC_CHECK_LIB([termcap], tgetent, ,
                    AC_MSG_ERROR([libtermcap required to build the client]))])

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
