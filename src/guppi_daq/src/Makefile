# Makefile to install GUPPI C code
# Commented lines are lines that were modified by Simon Scott to allow installation on otto.

# Default location for CUDA (can override on command line or environment)
CUDA ?= /usr/local/cuda

OPT_FLAGS =

ifdef PAYLOAD
	OPT_FLAGS += -DPAYLOAD_SIZE=$(PAYLOAD)
endif

ifdef RAW_DISK
	OPT_FLAGS += -DRAW_DISK
endif

ifdef NULL_DISK
	OPT_FLAGS += -DNULL_DISK
endif

ifdef EXT_DISK
	OPT_FLAGS += -DEXT_DISK
endif

CFLAGS = -g -fPIC -O3 -Wall -Werror -D_GNU_SOURCE $(OPT_FLAGS) -fno-strict-aliasing

ifdef PYSLALIB
	CFLAGS += -I$(PYSLALIB)
endif

ifdef GUPPI_DIR
	CFLAGS += -I$(GUPPI_DIR)/src
endif

NVCCFLAGS = -arch=sm_20 -I$(CUDA)/include

LIBS =
ifdef PYSLALIB
	LIBS += -L$(PYSLALIB)
endif
ifdef VEGAS_LIB
	LIBS += -L$(VEGAS_LIB)
endif
LIBS += -lcfitsio -lm -lpthread -lxgpu
CUDA_LIBS = -L$(CUDA)/lib64 -lcufft -lcuda -lcudart -lrt -lm -lpthread

PROGS = check_guppi_databuf check_guppi_status clean_guppi_shmem test_udp_recv
OBJS  = guppi_status.o guppi_databuf.o paper_databuf.o guppi_udp.o guppi_error.o \
	guppi_params.o guppi_time.o guppi_thread_args.o paper_thread.o \
	write_sdfits.o misc_utils.o guppi_ipckey.o \
	hget.o hput.o
THREAD_PROGS = test_net_thread vegas_hpc_hbw paper_xgpu
THREAD_OBJS  = vegas_net_thread.o vegas_rawdisk_thread.o \
	        guppi_sdfits_thread.o guppi_accum_thread.o \
	        guppi_null_thread.o guppi_fake_net_thread.o \
	        paper_net_thread.o paper_fake_net_thread.o \
	        paper_gpu_thread.o null_output_thread.o

CUDA_OBJS = pfb_gpu.o pfb_gpu_kernels.o vegas_pfb_thread.o

all: $(PROGS) $(THREAD_PROGS) vegas_hpc_lbw
clean:
	rm -f $(PROGS) $(THREAD_PROGS) vegas_hpc_lbw *~ *.o sdfits.tgz test_psrfits_0*.fits *.ptx tags
INSTALL_DIR = ../bin
install: $(PROGS) $(THREAD_PROGS) vegas_hpc_lbw
	mkdir -p $(INSTALL_DIR) && \
	cp -f $(PROGS) $(THREAD_PROGS) vegas_hpc_lbw $(INSTALL_DIR)
sdfits.tgz: sdfits.h read_sdfits.c \
    guppi_SDFITS_template.txt \
	tar cvzf $@ $^
find_dropped_blocks: find_dropped_blocks.o 
#	$(CC) $(CFLAGS) $< -o $@ -L$(OPT64)/lib -lcfitsio -lm
	$(CC) $(CFLAGS) $< -o $@ -lcfitsio -lm

tags:
	ctags -R .

%.o : %.cu
	nvcc -c $(NVCCFLAGS) $< -o $@

vegas_hpc_lbw: vegas_hpc_lbw.c $(THREAD_OBJS) $(OBJS) $(CUDA_OBJS)
	$(CC) $(CFLAGS) $(CUDA_CFLAGS) $< -o $@ $(THREAD_OBJS) \
		$(CUDA_OBJS) $(OBJS) $(LIBS) $(CUDA_LIBS)

.SECONDEXPANSION:
$(PROGS): $$@.c $(OBJS)
	$(CC) $(CFLAGS) $< -o $@ $(OBJS) $(LIBS) $(THREAD_LIBS)
$(THREAD_PROGS): $$@.c $(THREAD_OBJS) $(OBJS)
	$(CC) $(CFLAGS) $< -o $@ $(THREAD_OBJS) $(OBJS) $(LIBS)
