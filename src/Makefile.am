AM_CPPFLAGS = -I"@XGPU_INCDIR@"
AM_CFLAGS = -ggdb -fPIC -O3 -Wall -Werror -fno-strict-aliasing -mavx
AM_LDFLAGS = -L"@XGPU_LIBDIR@" -Wl,-rpath,"@XGPU_LIBDIR@"
ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS     =
noinst_PROGRAMS  =
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

paper_databuf = paper_databuf.h \
	        paper_databuf.c

paper_threads = paper_fake_net_thread.c       \
		paper_gpu_cpu_output_thread.c \
		paper_gpu_output_thread.c     \
		paper_gpu_thread.c            \
		paper_fluff_thread.c          \
		paper_net_thread.c

paper_utils = paper_fluff.c

bin_PROGRAMS += check_hashpipe_databuf
check_hashpipe_databuf_SOURCES = check_hashpipe_databuf.c
check_hashpipe_databuf_LDADD = libhashpipe.la libhashpipestatus.la

bin_PROGRAMS += check_hashpipe_status
check_hashpipe_status_SOURCES = check_hashpipe_status.c
check_hashpipe_status_LDADD = libhashpipestatus.la

bin_PROGRAMS += clean_hashpipe_shmem
clean_hashpipe_shmem_SOURCES = clean_hashpipe_shmem.c
clean_hashpipe_shmem_LDADD = libhashpipe.la libhashpipestatus.la

bin_PROGRAMS += dump_hashpipe_databuf
dump_hashpipe_databuf_SOURCES = dump_hashpipe_databuf.c
dump_hashpipe_databuf_LDADD = libhashpipe.la

bin_PROGRAMS += hashpipe
hashpipe_SOURCES = $(hashpipe_exec)
hashpipe_LDADD = libhashpipe.la libhashpipestatus.la

lib_LTLIBRARIES += libhashpipestatus.la
libhashpipestatus_la_SOURCES = $(hashpipe_status)
libhashpipestatus_la_LIBADD = -lm

lib_LTLIBRARIES += libhashpipe.la
libhashpipe_la_SOURCES = $(hashpipe_base)
libhashpipe_la_LIBADD = libhashpipestatus.la

include_HEADERS = fitshead.h \
		  hashpipe_databuf.h \
		  hashpipe_error.h \
		  hashpipe_status.h \
		  hashpipe_udp.h

noinst_PROGRAMS += paper_fluff_test
paper_fluff_test_SOURCES = paper_fluff_test.c paper_fluff.c
paper_fluff_test_LDADD = -lrt

lib_LTLIBRARIES += paper_gpu.la
paper_gpu_la_SOURCES = $(paper_databuf) $(paper_threads)
paper_gpu_la_LDFLAGS = -avoid-version -module -shared -export-dynamic

# Installed scripts
dist_bin_SCRIPTS = paper_init.sh \
		   xeng_init.sh

# vi: set ts=8 noet :
