#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.65])
AC_INIT([FULL-PACKAGE-NAME], [VERSION], [BUG-REPORT-ADDRESS])
AM_INIT_AUTOMAKE([foreign])
LT_INIT
AM_SILENT_RULES([yes])
AC_CONFIG_SRCDIR([hashpipe_status.h])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

# Set CFLAGS to nothing if it is not set by the user.  This prevents AC_PROG_CC
# from setting the (supposedly reserved-for-the-user!) variable CFLAGS in
# Makefile, which prevents AM_CFLAGS in Makefile.am from setting an
# optimization level.  For more details, see
# http://lists.gnu.org/archive/html/autoconf/2006-04/msg00007.html
AS_VAR_SET_IF(CFLAGS,[],[CFLAGS=])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB([dl], [dlopen])
AC_CHECK_LIB([m], [floor])
AC_CHECK_LIB([pthread], [pthread_create])
build_hpibv="will NOT"
AC_CHECK_LIB([ibverbs], [ibv_get_device_list],[
              AC_DEFINE([HAVE_LIBIBVERBS],[1],[Define this if ibverbs library is found.])
              LIBS="-libverbs $LIBS"
              build_hpibv="WILL"])
AM_CONDITIONAL([BUILD_HPIBV], [test x$build_hpibv = xWILL])

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h limits.h netdb.h netinet/in.h stdint.h stdlib.h string.h sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_MODE_T
AC_TYPE_SIZE_T
AC_TYPE_UINT64_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([floor gettimeofday memset socket strchr strerror strrchr strtol strtoul strtoull])

AC_CHECK_PROGS([DOXYGEN], [doxygen])
if test -z "$DOXYGEN"; then
   AC_MSG_WARN([Doxygen not found - continue without Doxygen support])
fi
AM_CONDITIONAL([HAVE_DOXYGEN], [test -n "$DOXYGEN"])
AM_COND_IF([HAVE_DOXYGEN], [AC_CONFIG_FILES([doxygen1])])
AM_COND_IF([HAVE_DOXYGEN], [AC_CONFIG_FILES([doxygen3])])
AM_COND_IF([HAVE_DOXYGEN], [AC_CONFIG_FILES([doxygen7])])

# Report results
build_hpibv="will NOT"
if test "${ac_cv_lib_ibverbs_ibv_get_device_list}" = "yes"
then
  build_hpibv="WILL"
fi

AC_MSG_NOTICE([[

The hashpipe_ibverbs library ${build_hpibv} be built.
]])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
