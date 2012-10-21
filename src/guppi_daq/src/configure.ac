#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.65])
AC_INIT([FULL-PACKAGE-NAME], [VERSION], [BUG-REPORT-ADDRESS])
AM_INIT_AUTOMAKE([foreign])
LT_INIT
AM_SILENT_RULES([yes])
AC_CONFIG_SRCDIR([guppi_time.h])
AC_CONFIG_HEADERS([config.h])

AC_ARG_WITH([cuda],
            AC_HELP_STRING([--with-cuda=DIR],
                           [Location of CUDA files (/usr/local/cuda)]),
            [CUDADIR="$withval"],
            [CUDADIR=/usr/local/cuda])

AC_ARG_WITH([xgpu],
            AC_HELP_STRING([--with-xgpu=DIR],
                           [Location of xGPU headers/libs (/usr/local)]),
            [XGPUDIR="$withval"],
            [XGPUDIR=/usr/local])

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

# Save original LDFLAGS
orig_LDFLAGS="${LDFLAGS}"

AC_CHECK_LIB([cfitsio], [ffclos])

AS_IF([test "$COMPILE_VEGAS" = "yes"],[
# Try CUDADIR/lib first
AC_MSG_NOTICE([checking for libcufft in ${CUDADIR}/lib])
LDFLAGS="${LDFLAGS} -L${CUDADIR}/lib"
AC_CHECK_LIB([cufft], [cufftDestroy],
             # Found
             AC_SUBST(CUDALIBDIR,${CUDADIR}/lib),
             # Not found there, check CUDADIR/lib64
             AC_MSG_NOTICE([checking for libcufft in ${CUDADIR}/lib64])
             AS_UNSET(ac_cv_lib_cufft_cufftDestroy)
             LDFLAGS="${orig_LDFLAGS} -L${CUDADIR}/lib64"
             AC_CHECK_LIB([cufft], [cufftDestroy],
                          # Found
                          AC_SUBST(CUDALIBDIR,${CUDADIR}/lib64),
                          # Not found there, error
                          AC_MSG_ERROR([VEGAS needs libcufft])))
LDFLAGS="${orig_LDFLAGS}"
],[]) # end AS_IF

LDFLAGS="${orig_LDFLAGS} -L${XGPUDIR}/lib"
AC_CHECK_LIB([xgpu], [xgpuInit],
             # Found
             AC_SUBST(XGPU_LIBDIR,${XGPUDIR}/lib),
             # Not found there, check XGPUDIR
             AS_UNSET(ac_cv_lib_xgpu_xgpuInit)
             LDFLAGS="${orig_LDFLAGS} -L${XGPUDIR}"
             AC_CHECK_LIB([xgpu], [xgpuInit],
                          # Found
                          AC_SUBST(XGPU_LIBDIR,${XGPUDIR}),
                          # Not found there, error
                          AC_MSG_ERROR([xGPU library not found])))
LDFLAGS="${orig_LDFLAGS}"

AC_CHECK_FILE([${XGPUDIR}/include/xgpu.h],
              # Found
              AC_SUBST(XGPU_INCDIR,${XGPUDIR}/include),
              # Not found there, check XGPUDIR
              AC_CHECK_FILE([${XGPUDIR}/xgpu.h],
                            # Found
                            AC_SUBST(XGPU_INCDIR,${XGPUDIR}),
                            # Not found there, error
                            AC_MSG_ERROR([xgpu.h header file not found])))


AC_CHECK_FILE([${XGPUDIR}/bin/xgpuinfo],
              # Found
              AC_SUBST(XGPU_BINDIR,${XGPUDIR}/bin),
              # Not found there, check XGPUDIR
              AC_CHECK_FILE([${XGPUDIR}/xgpuinfo],
                            # Found
                            AC_SUBST(XGPU_BINDIR,${XGPUDIR}),
                            # Not found there, error
                            AC_MSG_ERROR([xgpuinfo program not found])))

AC_DEFINE_UNQUOTED([XGPU_NSTATION],
                   [`${XGPU_BINDIR}/xgpuinfo | sed -n '/Number of stations: /{s/.*: //;p}'`],
                   [Number of stations == Ninputs/2])

AC_DEFINE_UNQUOTED([XGPU_NFREQUENCY],
                   [`${XGPU_BINDIR}/xgpuinfo | sed -n '/Number of frequencies: /{s/.*: //;p}'`],
                   [Number of frequency channels per xGPU instance])

AC_DEFINE_UNQUOTED([XGPU_NTIME],
                   [`${XGPU_BINDIR}/xgpuinfo | sed -n '/time samples per GPU integration: /{s/.*: //;p}'`],
                   [Number of time samples (i.e. spectra) per xGPU integration])

## FIXME: Replace `main' with a function in `-lm':
#AC_CHECK_LIB([m], [main])

AC_CHECK_LIB([pthread], [pthread_create])

## FIXME: Replace `main' with a function in `-lrt':
#AC_CHECK_LIB([rt], [main])

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h limits.h netdb.h netinet/in.h stdint.h stdlib.h string.h sys/socket.h sys/time.h unistd.h])

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
