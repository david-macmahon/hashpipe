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

paper_fluff = paper_fluff.h \
	      paper_fluff.c

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
paper_fluff_test_SOURCES = paper_fluff_test.c $(paper_fluff)
# The paper_fluff_test_CFLAGS is an autotools workaround.
# http://www.gnu.org/software/automake/manual/html_node/Objects-created-both-with-libtool-and-without.html
paper_fluff_test_CFLAGS = $(AM_CFLAGS)
paper_fluff_test_LDADD = -lrt

lib_LTLIBRARIES += paper_gpu.la
paper_gpu_la_SOURCES  = $(paper_databuf) $(paper_threads) $(paper_fluff)
paper_gpu_la_LIBADD   = -lrt -lxgpu
paper_gpu_la_LDFLAGS  = -avoid-version -module -shared -export-dynamic
paper_gpu_la_LDFLAGS += -L"@XGPU_LIBDIR@" -Wl,-rpath,"@XGPU_LIBDIR@"

# Installed scripts
dist_bin_SCRIPTS = paper_init.sh \
		   xeng_init.sh

# vi: set ts=8 noet :
