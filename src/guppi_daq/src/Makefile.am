AM_CPPFLAGS = -I"@XGPU_INCDIR@"
AM_CFLAGS = -ggdb -fPIC -O3 -Wall -Werror -fno-strict-aliasing
AM_LDFLAGS = -L"@XGPU_LIBDIR@" -Wl,-rpath,"@XGPU_LIBDIR@"

bin_PROGRAMS  =
noinst_PROGRAMS  =

guppi_base = guppi_databuf.c \
	     guppi_error.c   \
	     guppi_ipckey.c  \
	     guppi_status.c  \
	     hget.c          \
	     hput.c          \
	     fitshead.h      \
	     guppi_ipckey.h  \
	     guppi_status.h  \
	     guppi_params.h  \
	     guppi_udp.h     \
	     guppi_defines.h \
	     guppi_threads.h     \
	     guppi_thread_args.h \
	     guppi_thread_main.h \
	     sdfits.h        \
	     guppi_databuf.h \
	     guppi_error.h

guppi_net = $(guppi_base) \
	    guppi_udp.c

guppi_threads = $(guppi_net)            \
		guppi_accum_thread.c    \
		guppi_fake_net_thread.c \
		guppi_null_thread.c     \
		guppi_sdfits_thread.c   \
		spead_heap.h            \
		guppi_time.h

guppi_utils = guppi_params.c      \
	      guppi_thread_args.c \
	      guppi_time.c        \
	      write_sdfits.c

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

bin_PROGRAMS += check_guppi_status
check_guppi_status_SOURCES = check_guppi_status.c $(guppi_base)

bin_PROGRAMS += clean_guppi_shmem
clean_guppi_shmem_SOURCES = clean_guppi_shmem.c $(guppi_base)

bin_PROGRAMS += dump_databuf
dump_databuf_SOURCES = dump_databuf.c $(guppi_base)

bin_PROGRAMS += paper_xgpu
paper_xgpu_SOURCES = paper_xgpu.c $(paper_threads) $(paper_utils)
paper_xgpu_LDADD = -lxgpu

noinst_PROGRAMS += paper_fluff_test
paper_fluff_test_SOURCES = paper_fluff_test.c paper_fluff.c
paper_fluff_test_LDADD = -lrt

if COMPILE_VEGAS
vegas_threads = $(guppi_threads) \
		vegas_net_thread.c

bin_PROGRAMS += test_udp_recv
test_udp_recv_SOURCES = test_udp_recv.c $(guppi_net)

bin_PROGRAMS += vegas_hpc_hbw
vegas_hpc_hbw_SOURCES = vegas_hpc_hbw.c $(guppi_utils) $(vegas_threads)

bin_PROGRAMS += vegas_hpc_lbw
vegas_hpc_lbw_SOURCES = vegas_hpc_lbw.c    \
			pfb_gpu.cu         \
			pfb_gpu_kernels.cu \
			vegas_pfb_thread.c \
			$(guppi_utils) $(vegas_threads)

ifdef PAYLOAD
	AM_CFLAGS += -DPAYLOAD_SIZE=$(PAYLOAD)
@gnumake_endif@

ifdef RAW_DISK
	AM_CFLAGS += -DRAW_DISK
@gnumake_endif@

ifdef NULL_DISK
	AM_CFLAGS += -DNULL_DISK
@gnumake_endif@

ifdef EXT_DISK
	AM_CFLAGS += -DEXT_DISK
@gnumake_endif@

ifdef PYSLALIB
	AM_CFLAGS += -I$(PYSLALIB)
@gnumake_endif@

ifdef GUPPI_DIR
	AM_CFLAGS += -I$(GUPPI_DIR)/src
@gnumake_endif@

NVCCFLAGS = -arch=sm_20 -I$(CUDA)/include

# Rule to build object file from CUDA .cu file
.cu.o:
	nvcc -c $(NVCCFLAGS) $< -o $@
endif

# vi: set ts=8 noet :
