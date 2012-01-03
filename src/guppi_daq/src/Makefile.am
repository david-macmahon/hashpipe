bin_PROGRAMS  =

guppi_base = guppi_databuf.c \
	     guppi_error.c   \
	     guppi_ipckey.c  \
	     guppi_status.c  \
	     hget.c          \
	     hput.c

guppi_net = $(guppi_base) \
	    guppi_udp.c

guppi_threads = $(guppi_net)            \
		guppi_accum_thread.c    \
		guppi_fake_net_thread.c \
		guppi_null_thread.c     \
		guppi_rawdisk_thread.c  \
		guppi_sdfits_thread.c

guppi_utils = guppi_params.c      \
	      guppi_thread_args.c \
	      guppi_time.c        \
	      write_sdfits.c

paper_net = $(guppi_net) \
	     paper_databuf.c

paper_threads = $(paper_net)                  \
		paper_fake_net_thread.c       \
		paper_gpu_cpu_output_thread.c \
		paper_gpu_thread.c            \
		paper_net_thread.c

paper_utils = $(guppi_utils) \
	      paper_thread.c

bin_PROGRAMS += check_guppi_databuf
check_guppi_databuf_SOURCES = check_guppi_databuf.c $(guppi_base)

bin_PROGRAMS += check_guppi_status
check_guppi_status_SOURCES = check_guppi_status.c $(guppi_base)

bin_PROGRAMS += clean_guppi_shmem
clean_guppi_shmem_SOURCES = clean_guppi_shmem.c $(guppi_base)

bin_PROGRAMS += paper_xgpu
paper_xgpu_SOURCES = paper_xgpu.c $(paper_threads) $(paper_utils)

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

# Rule to build object file from CUDA .cu file
.cu.o:
	nvcc -c $(NVCCFLAGS) $< -o $@
endif
