# serial 1 hashpipe.m4
AC_DEFUN([AX_CHECK_HASHPIPE],
[AC_PREREQ([2.65])dnl
AC_ARG_WITH([hashpipe],
            AC_HELP_STRING([--with-hashpipe=DIR],
                           [Location of HASHPIPE files (/usr/local)]),
            [HASHPIPEDIR="$withval"],
            [HASHPIPEDIR=/usr/local])

orig_LDFLAGS="${LDFLAGS}"
LDFLAGS="${orig_LDFLAGS} -L${HASHPIPEDIR}/lib"
AC_CHECK_LIB([hashpipe], [hashpipe_databuf_create],
             # Found
             AC_SUBST(HASHPIPE_LIBDIR,${HASHPIPEDIR}/lib),
             # Not found there, check HASHPIPEDIR
             AS_UNSET(ac_cv_lib_hashpipe_hashpipe_databuf_create)
             LDFLAGS="${orig_LDFLAGS} -L${HASHPIPEDIR}"
             AC_CHECK_LIB([hashpipe], [hashpipe_databuf_create],
                          # Found
                          AC_SUBST(HASHPIPE_LIBDIR,${HASHPIPEDIR}),
                          # Not found there, error
                          AC_MSG_ERROR([HASHPIPE library not found])))
LDFLAGS="${orig_LDFLAGS}"

AC_CHECK_FILE([${HASHPIPEDIR}/include/hashpipe.h],
              # Found
              AC_SUBST(HASHPIPE_INCDIR,${HASHPIPEDIR}/include),
              # Not found there, check HASHPIPEDIR
              AC_CHECK_FILE([${HASHPIPEDIR}/hashpipe.h],
                            # Found
                            AC_SUBST(HASHPIPE_INCDIR,${HASHPIPEDIR}),
                            # Not found there, error
                            AC_MSG_ERROR([hashpipe.h header file not found])))

])
