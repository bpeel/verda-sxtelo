AC_INIT([gemelo], 0.1)

AM_CONFIG_HEADER([config.h])

AM_INIT_AUTOMAKE([1.9])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PROG_CC

PKG_CHECK_MODULES(GLIB, [glib-2.0 >= 2.16 gobject-2.0 >= 2.16 gio-2.0 >= 2.22 gthread-2.0])

have_epoll=yes
AC_CHECK_HEADER(sys/epoll.h, , have_epoll=no)
AC_CHECK_FUNC(epoll_create1, , have_epoll=no)
AC_CHECK_FUNC(epoll_ctl, , have_epoll=no)
AS_IF([test "x$have_epoll" = "xno"],
      AC_MSG_ERROR([epoll support is required to build gemelo.
This should be provided by glibc since version 2.3.2]))

have_timerfd=yes
AC_CHECK_HEADER(sys/timerfd.h, , have_timerfd=no)
AC_CHECK_FUNC(timerfd_create, , have_timerfd=no)

AS_IF([test "x$have_timerfd" = "xno"],
      AC_MSG_ERROR([timerfd support is required to build gemelo.
This should be provided by glibc since version 2.8]))

have_signalfd=yes
AC_CHECK_HEADER(sys/signalfd.h, , have_signalfd=no)
AC_CHECK_FUNC(signalfd, , have_signalfd=no)

AS_IF([test "x$have_signalfd" = "xno"],
      AC_MSG_ERROR([signalfd support is required to build gemelo.
This should be provided by glibc since version 2.8]))

AC_PATH_PROG([GLIB_MKENUMS], [glib-mkenums])
AC_PATH_PROG([GLIB_GENMARSHAL], [glib-genmarshal])

AC_CONFIG_FILES([
        Makefile
        src/Makefile
        web/Makefile
])

AC_OUTPUT
