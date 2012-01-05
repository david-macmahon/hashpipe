#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.65])
AC_INIT([FULL-PACKAGE-NAME], [VERSION], [BUG-REPORT-ADDRESS])
AM_INIT_AUTOMAKE([foreign])
AM_SILENT_RULES([yes])
AC_CONFIG_SRCDIR([guppi_time.h])
AC_CONFIG_HEADERS([config.h])

AC_ARG_WITH([cuda],
            AC_HELP_STRING([--with-cuda=DIR],
                           [Location of CUDA files (/usr/local/cuda)]),
            [CUDADIR="$withval"],
            [CUDADIR=/usr/local/cuda])

AC_ARG_ENABLE([vegas],
            AC_HELP_STRING([--enable-vegas],
                           [Enable compiltion of VEGAS programs (no)]),
            [COMPILE_VEGAS="yes"
             AC_SUBST(gnumake_endif,endif)],
            [COMPILE_VEGAS="no"])

AM_CONDITIONAL(COMPILE_VEGAS, [test "$COMPILE_VEGAS" = "yes"])

# Set CFLAGS to nothing if it is not set by the user.  This prevents AC_PROG_CC
# from setting the (supposedly reserved-for-the-user!) variable CFLAGS in
# Makefile, which prevents AM_CFLAGS in Makefile.am from setting an
# optimization level.  For more details, see
# http://lists.gnu.org/archive/html/autoconf/2006-04/msg00007.html
AS_VAR_SET_IF(CFLAGS,[],[CFLAGS=])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB([cfitsio], [ffclos])

AS_IF([test "$COMPILE_VEGAS" = "yes"],[
# Try CUDADIR/lib first
AC_MSG_NOTICE([checking for libcufft in ${CUDADIR}/lib])
orig_LDFLAGS="${LDFLAGS}"
LDFLAGS="${orig_LDFLAGS} -L${CUDADIR}/lib"
AC_CHECK_LIB([cufft], [cufftDestroy],[],
             # Not found there, check CUDADIR/lib64
             AC_MSG_NOTICE([checking for libcufft in ${CUDADIR}/lib64])
             AS_UNSET(ac_cv_lib_cufft_cufftDestroy)
             LDFLAGS="${orig_LDFLAGS} -L${CUDADIR}/lib64"
             AC_CHECK_LIB([cufft], [cufftDestroy],[],
                          AC_MSG_ERROR([VEGAS needs libcufft])))
],[]) # end AS_IF

## FIXME: Replace `main' with a function in `-lm':
#AC_CHECK_LIB([m], [main])

AC_CHECK_LIB([pthread], [pthread_create])

## FIXME: Replace `main' with a function in `-lrt':
#AC_CHECK_LIB([rt], [main])

AC_CHECK_LIB([xgpu], [xgpuInit])

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h limits.h netdb.h netinet/in.h stdint.h stdlib.h string.h sys/socket.h sys/time.h unistd.h xgpu.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_INT64_T
AC_TYPE_INT8_T
AC_TYPE_MODE_T
AC_TYPE_SIZE_T
AC_HEADER_STDBOOL
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([floor getcwd gettimeofday memmove memset rint socket sqrt strchr strrchr strstr strtoul])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
