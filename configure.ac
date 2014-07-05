AC_PREREQ([2.69])
AC_INIT([jackwsmeter], [1])
AC_CONFIG_SRCDIR([jackwsmeter.c])
AM_INIT_AUTOMAKE([1.11 -Wall foreign no-define tar-ustar no-dist-gzip dist-xz])
AM_SILENT_RULES([yes])
AC_PROG_CC

dnl check for libraries
AC_CHECK_LIBM
AC_SUBST(LIBM)

PKG_CHECK_MODULES(WEBSOCKETS, libwebsockets)
AC_CHECK_LIB(websockets, lws_daemonize, [true], [AC_DEFINE(LWS_NO_DAEMONIZE)])
AC_CHECK_LIB(websockets, libwebsocket_get_internal_extensions, [true], [AC_DEFINE(LWS_NO_EXTENSIONS)])

PKG_CHECK_MODULES(JACK, jack >= 0.100.0)
AC_SUBST(JACK_CFLAGS)
AC_SUBST(JACK_LIBS)

AC_CONFIG_FILES(Makefile)
AC_OUTPUT
