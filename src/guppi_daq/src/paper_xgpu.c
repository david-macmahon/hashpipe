/*
 * paper_xgpu.c
 *
 * The main PAPER xGPU program
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <poll.h>
#include <getopt.h>
#include <errno.h>

#include <xgpu.h>

#include "guppi_error.h"
#include "guppi_status.h"
#include "guppi_databuf.h"
#include "paper_databuf.h"
#include "guppi_params.h"
#include "guppi_thread_main.h"
#include "guppi_defines.h"
#include "fitshead.h"

/* Thread declarations */
#ifdef FAKE_NET
void *paper_fake_net_thread(void *args);
#else
void *guppi_net_thread(void *args);
#endif
void *paper_gpu_thread(void *args);
//void *guppi_accum_thread(void *args);

#if 0
#if FITS_TYPE == PSRFITS
void *guppi_psrfits_thread(void *args);
#else
void *guppi_sdfits_thread(void *args);
#endif

#ifdef RAW_DISK
void *guppi_rawdisk_thread(void *args);
#endif

#ifdef NULL_DISK
void *guppi_null_thread(void *args);
#endif

#ifdef FAKE_NET
void *guppi_fake_net_thread(void *args);
#endif
#endif // 0


int main(int argc, char *argv[]) {

    /* thread args */
    struct guppi_thread_args net_args, gpu_args; //, accum_args, disk_args;
    guppi_thread_args_init(&net_args);
    guppi_thread_args_init(&gpu_args);
#if 0
    guppi_thread_args_init(&accum_args);
    guppi_thread_args_init(&disk_args);
#endif

    net_args.output_buffer = 1;
    gpu_args.input_buffer = net_args.output_buffer;
    gpu_args.output_buffer = 2;
#if 0
    accum_args.input_buffer = gpu_args.output_buffer;
    accum_args.output_buffer = 3;
    disk_args.input_buffer = accum_args.output_buffer;
#endif

    /* Init status shared mem */
    struct guppi_status stat;
    int rv = guppi_status_attach(&stat);
    if (rv!=GUPPI_OK) {
        fprintf(stderr, "Error connecting to guppi_status\n");
        exit(1);
    }

#if 0
    hputs(stat.buf, "BW_MODE", "low");
    hputs(stat.buf, "SWVER", SWVER);
#endif

    // Get xGPU sizing parameters
    XGPUInfo xgpu_info;
    xgpuInfo(&xgpu_info);

printf("trying attach of gpu buf\n");
    /* Init first shared data buffer */
    struct paper_input_databuf *gpu_input_dbuf=NULL;
    gpu_input_dbuf = paper_input_databuf_attach(gpu_args.input_buffer);
if(gpu_input_dbuf) printf("success %p\n", gpu_input_dbuf);

    /* If attach fails, first try to create the databuf */
    if (gpu_input_dbuf==NULL) { 
printf("trying create of gpu buf\n");
        gpu_input_dbuf = paper_input_databuf_create(4, xgpu_info.vecLength*sizeof(ComplexInput),
                            gpu_args.input_buffer, GPU_INPUT_BUF);
        /* If that also fails, exit */
        if (gpu_input_dbuf==NULL) {
            fprintf(stderr, "Error connecting to gpu_input_dbuf\n");
            exit(1);
        }
    } else {
        // Check size of existing shared memory
        if(gpu_input_dbuf->header.block_size < xgpu_info.vecLength*sizeof(ComplexInput)) {
            fprintf(stderr, "Connected to gpu_input_dbuf, but it has the wrong block_size\n");
            exit(1);
        }
    }

#if 0
printf("testing exit\n");
exit(0);
#endif

    paper_input_databuf_clear(gpu_input_dbuf);

    /* Init second shared data buffer */
    struct paper_output_databuf *gpu_output_dbuf=NULL;
    gpu_output_dbuf = paper_output_databuf_attach(gpu_args.output_buffer);

    /* If attach fails, first try to create the databuf */
    if (gpu_output_dbuf==NULL) {
        gpu_output_dbuf = paper_output_databuf_create(16, xgpu_info.matLength*sizeof(Complex),
                            gpu_args.output_buffer);

        /* If that also fails, exit */
        if (gpu_output_dbuf==NULL) {
            fprintf(stderr, "Error connecting to gpu_output_dbuf\n");
            exit(1);
        }
    } else {
        // Check size of existing shared memory
        if(gpu_output_dbuf->header.block_size < xgpu_info.matLength*sizeof(Complex)) {
            fprintf(stderr, "Connected to gpu_output_dbuf, but it has the wrong block_size\n");
            exit(1);
        }
    }

    paper_output_databuf_clear(gpu_output_dbuf);

#if 0
    /* Init third shared data buffer */
    struct guppi_databuf *disk_input_dbuf=NULL;
    disk_input_dbuf = guppi_databuf_attach(disk_args.input_buffer);

    /* If attach fails, first try to create the databuf */
    if (disk_input_dbuf==NULL) {
        // Get sizing parameters
        XGPUInfo xgpu_info;
        xgpuInfo(&xgpu_info);
        disk_input_dbuf = guppi_databuf_create(16, xgpu_info.matLength,
                            disk_args.input_buffer, DISK_INPUT_BUF);
    }

    /* If that also fails, exit */
    if (disk_input_dbuf==NULL) {
        fprintf(stderr, "Error connecting to disk_input_dbuf\n");
        exit(1);
    }

    guppi_databuf_clear(disk_input_dbuf);
#endif

    signal(SIGINT, cc);
    signal(SIGTERM, cc);

    /* Launch net thread */
    pthread_t net_thread_id;
#ifdef FAKE_NET
    rv = pthread_create(&net_thread_id, NULL, paper_fake_net_thread,
            (void *)&net_args);
#else
    rv = pthread_create(&net_thread_id, NULL, guppi_net_thread,
            (void *)&net_args);
#endif
    if (rv) { 
        fprintf(stderr, "Error creating net thread.\n");
        perror("pthread_create");
        exit(1);
    }

    /* Launch GPU thread */
    pthread_t gpu_thread_id;

    rv = pthread_create(&gpu_thread_id, NULL, paper_gpu_thread, (void *)&gpu_args);

    if (rv) { 
        fprintf(stderr, "Error creating GPU thread.\n");
        perror("pthread_create");
        exit(1);
    }

#if 0
    /* Launch accumulator thread */
    pthread_t accum_thread_id;

    rv = pthread_create(&accum_thread_id, NULL, guppi_accum_thread, (void *)&accum_args);

    if (rv) { 
        fprintf(stderr, "Error creating accumulator thread.\n");
        perror("pthread_create");
        exit(1);
    }

    /* Launch RAW_DISK thread, SDFITS disk thread, or PSRFITS disk thread */
    pthread_t disk_thread_id;
#ifdef RAW_DISK
    rv = pthread_create(&disk_thread_id, NULL, guppi_rawdisk_thread, 
        (void *)&disk_args);
#elif defined NULL_DISK
    rv = pthread_create(&disk_thread_id, NULL, guppi_null_thread, 
        (void *)&disk_args);
#elif defined EXT_DISK
    rv = 0;
#elif FITS_TYPE == PSRFITS
    rv = pthread_create(&disk_thread_id, NULL, guppi_psrfits_thread, 
        (void *)&disk_args);
#elif FITS_TYPE == SDFITS
    rv = pthread_create(&disk_thread_id, NULL, guppi_sdfits_thread, 
        (void *)&disk_args);
#endif
    if (rv) { 
        fprintf(stderr, "Error creating disk thread.\n");
        perror("pthread_create");
        exit(1);
    }
#endif

    /* Wait for SIGINT (i.e. control-c) or SIGTERM (aka "kill <pid>") */
    run=1;
    while (run) { 
        sleep(1); 
        //if (disk_args.finished) run=0;
    }
 
    //pthread_cancel(disk_thread_id);
    pthread_cancel(gpu_thread_id);
    //pthread_cancel(accum_thread_id);
    pthread_cancel(net_thread_id);
    //pthread_kill(disk_thread_id,SIGINT);
    //pthread_kill(accum_thread_id,SIGINT);
    pthread_kill(gpu_thread_id,SIGINT);
    pthread_kill(net_thread_id,SIGINT);
    pthread_join(net_thread_id,NULL);
    printf("Joined net thread\n"); fflush(stdout);
    pthread_join(gpu_thread_id,NULL);
    printf("Joined GPU thread\n"); fflush(stdout);
#if 0
    pthread_join(accum_thread_id,NULL);
    printf("Joined accumulator thread\n"); fflush(stdout);
    pthread_join(disk_thread_id,NULL);
    printf("Joined disk thread\n"); fflush(stdout);
#endif

    guppi_thread_args_destroy(&net_args);
    guppi_thread_args_destroy(&gpu_args);
#if 0
    guppi_thread_args_destroy(&accum_args);
    guppi_thread_args_destroy(&disk_args);
#endif

    exit(0);
}
