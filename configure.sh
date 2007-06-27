#! /bin/sh

. ./configure.inc

AC_INIT mimecode

AC_PROG_CC || exit 1

TLOGN "Checking for system x_getopt"
need_local_getopt=T
if AC_QUIET AC_CHECK_HEADERS basis/options.h; then
    if AC_QUIET AC_CHECK_FUNCS x_getopt; then
	unset need_local_getopt
    elif LIBS="$AC_LIBS -lbasis" AC_QUIET AC_CHECK_FUNCS x_getopt; then
	AC_LIBS="$AC_LIBS -lbasis"
	unset need_local_getopt
    fi
fi

echo "need_local_getopt is $need_local_getopt"

if [ "$need_local_getopt" ]; then
    TLOG " (not found)"
    AC_SUB OPTIONS basis/options.c
    AC_CFLAGS="$AC_CFLAGS -I${AC_SRCDIR}"
    echo "AC_CFLAGS = $AC_CFLAGS"
else
    TLOG " (found)"
    AC_SUB OPTIONS ''
fi

AC_CHECK_HEADERS errno.h
test "$OS_FREEBSD" || AC_CHECK_HEADERS malloc.h

AC_CHECK_FIELD utsname domainname sys/utsname.h

VERSION=`cat VERSION`
AC_SUB VERSION ${VERSION:-1.0}

AC_OUTPUT Makefile
