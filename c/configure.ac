dnl Process this file with autoconf to produce a configure script.
dnl Created by Anjuta application wizard.

AC_INIT(usbnarf, 0.1)

AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AC_CONFIG_HEADERS([config.h])
AM_MAINTAINER_MODE

AC_ISC_POSIX
AC_PROG_CC
AM_PROG_CC_STDC
AC_HEADER_STDC



AC_CHECK_PROGS(gengetopt, gengetopt, [false])
GENGETOPT=$gengetopt
AC_SUBST(GENGETOPT)





PKG_CHECK_MODULES(USBNARF, [libusb-1.0])




AC_OUTPUT([
Makefile
src/Makefile

])
