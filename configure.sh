#! /bin/sh

ac_help='--enable-uuencode	Install uuencode/uudecode binaries'
LOCAL_AC_OPTIONS='case "$1" in
                  --enable-uuencode) ENABLE_UUE=T; shift ;;
		  *) ac_error=T;;
		  esac'

. ./configure.inc

AC_INIT mimecode

case "$AC_CC $AC_CFLAGS" in
*-Wall*|*-pedantic*)    ;;
*)			AC_DEFINE 'while(x)' 'while( (x) != 0 )'
			AC_DEFINE 'if(x)' 'if( (x) != 0 )' ;;
esac

if [ "$ENABLE_UUE" ]; then
    AC_SUB MKUUE ''
else
    AC_SUB MKUUE '#'
fi


AC_PROG_CC || exit 1
unset _MK_LIBRARIAN

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

if [ "$need_local_getopt" ]; then
    TLOG " (not found)"
    AC_SUB OPTIONS basis/options.c
    AC_CFLAGS="$AC_CFLAGS -I${AC_SRCDIR}"
else
    TLOG " (found)"
    AC_SUB OPTIONS ''
fi

AC_CHECK_HEADERS errno.h
test "$OS_FREEBSD" || AC_CHECK_HEADERS malloc.h

AC_CHECK_HEADERS libgen.h	# FreeBSD; contains prototype for basename()

AC_CHECK_FIELD utsname domainname sys/utsname.h

AC_CHECK_FUNCS setbuffer
AC_CHECK_FUNCS basename

VERSION=`cat VERSION`
AC_SUB VERSION ${VERSION:-1.0}

AC_OUTPUT Makefile
