AM_CFLAGS = -ggdb -fPIC -O3 -Wall -Werror
ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS     =
lib_LTLIBRARIES  =

hashpipe_status = hashpipe_error.h  \
                  hashpipe_error.c  \
		  hashpipe_ipckey.h \
		  hashpipe_ipckey.c \
                  hashpipe_status.h \
		  hashpipe_status.c \
		  fitshead.h        \
		  hget.c            \
		  hput.c

hashpipe_base = hashpipe.h             \
		hashpipe_databuf.h     \
	        hashpipe_databuf.c     \
	        hashpipe_thread.c      \
	        hashpipe_udp.h         \
	        hashpipe_udp.c

hashpipe_exec = hashpipe.c             \
	        hashpipe_thread_args.h \
	        hashpipe_thread_args.c \
		null_output_thread.c

bin_PROGRAMS += hashpipe_check_databuf
hashpipe_check_databuf_SOURCES = hashpipe_check_databuf.c
hashpipe_check_databuf_LDADD = libhashpipe.la libhashpipestatus.la

bin_PROGRAMS += hashpipe_check_status
hashpipe_check_status_SOURCES = hashpipe_check_status.c
hashpipe_check_status_LDADD = libhashpipestatus.la

bin_PROGRAMS += hashpipe_clean_shmem
hashpipe_clean_shmem_SOURCES = hashpipe_clean_shmem.c
hashpipe_clean_shmem_LDADD = libhashpipe.la libhashpipestatus.la

bin_PROGRAMS += hashpipe_dump_databuf
hashpipe_dump_databuf_SOURCES = hashpipe_dump_databuf.c
hashpipe_dump_databuf_LDADD = libhashpipe.la

bin_PROGRAMS += hashpipe
hashpipe_SOURCES = $(hashpipe_exec)
hashpipe_LDADD = -ldl libhashpipe.la libhashpipestatus.la
# Force -rpath to be set to libdir
hashpipe_LDFLAGS = -Wl,-rpath,"$(libdir)"

lib_LTLIBRARIES += libhashpipestatus.la
libhashpipestatus_la_SOURCES = $(hashpipe_status)
libhashpipestatus_la_LIBADD = -lm -lrt

lib_LTLIBRARIES += libhashpipe.la
libhashpipe_la_SOURCES = $(hashpipe_base)
libhashpipe_la_LIBADD = libhashpipestatus.la

include_HEADERS = fitshead.h \
		  hashpipe.h \
		  hashpipe_databuf.h \
		  hashpipe_error.h \
		  hashpipe_status.h \
		  hashpipe_udp.h

aclocaldir = $(datadir)/aclocal
aclocal_DATA = m4/hashpipe.m4

# vi: set ts=8 noet :
