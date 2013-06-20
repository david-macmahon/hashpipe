AM_CPPFLAGS = -I"@XGPU_INCDIR@"
AM_CFLAGS = -ggdb -fPIC -O3 -Wall -Werror -fno-strict-aliasing -mavx
AM_LDFLAGS = -L"@XGPU_LIBDIR@" -Wl,-rpath,"@XGPU_LIBDIR@"
ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS  =
noinst_PROGRAMS  =

guppi_base = guppi_databuf.c \
	     fitshead.h      \
	     guppi_ipckey.h  \
	     hashpipe_status.h  \
	     guppi_udp.h     \
	     guppi_threads.h     \
	     guppi_thread_args.h \
	     guppi_thread_main.h \
	     guppi_databuf.h \
	     guppi_error.h

guppi_net = $(guppi_base) \
	    guppi_udp.c

guppi_utils = guppi_thread_args.c

paper_net = $(guppi_net) \
	     paper_databuf.c \
	     paper_databuf.h

paper_threads = $(paper_net)                  \
		paper_fake_net_thread.c       \
		null_output_thread.c          \
		paper_gpu_cpu_output_thread.c \
		paper_gpu_output_thread.c     \
		paper_gpu_thread.c            \
		paper_fluff_thread.c          \
		paper_net_thread.c            \
		guppi_thread_main.h

paper_utils = $(guppi_utils) \
	      paper_fluff.c  \
	      paper_thread.c \
	      paper_thread.h

bin_PROGRAMS += check_guppi_databuf
check_guppi_databuf_SOURCES = check_guppi_databuf.c $(guppi_base)
check_guppi_databuf_LDADD = libhashpipestatus.la -lm

bin_PROGRAMS += check_hashpipe_status
check_hashpipe_status_SOURCES = check_hashpipe_status.c $(guppi_base)
check_hashpipe_status_LDADD = libhashpipestatus.la -lm

bin_PROGRAMS += clean_guppi_shmem
clean_guppi_shmem_SOURCES = clean_guppi_shmem.c $(guppi_base)
clean_guppi_shmem_LDADD = libhashpipestatus.la -lm

bin_PROGRAMS += dump_databuf
dump_databuf_SOURCES = dump_databuf.c $(guppi_base)
dump_databuf_LDADD = libhashpipestatus.la -lm

bin_PROGRAMS += paper_xgpu
paper_xgpu_SOURCES = paper_xgpu.c $(paper_threads) $(paper_utils)
paper_xgpu_LDADD = libhashpipestatus.la -lxgpu -lm -lrt

noinst_PROGRAMS += paper_fluff_test
paper_fluff_test_SOURCES = paper_fluff_test.c paper_fluff.c
paper_fluff_test_LDADD = -lrt

lib_LTLIBRARIES = libhashpipestatus.la
libhashpipestatus_la_SOURCES = hashpipe_status.c \
			       hget.c \
			       hput.c \
			       guppi_error.c \
			       guppi_ipckey.c
libhashpipestatus_la_LIBADD = -lm

include_HEADERS = fitshead.h \
		  hashpipe_status.h

# Installed scripts
dist_bin_SCRIPTS = paper_init.sh \
		   xeng_init.sh

# Staging target to do a local install into $(abs_builddir)/staging/{bin,include,lib}
.PHONY: staging
staging:
	make install DESTDIR=$(abs_builddir)/staging prefix=

clean-local:
	rm -rf $(abs_builddir)/staging

# vi: set ts=8 noet :
