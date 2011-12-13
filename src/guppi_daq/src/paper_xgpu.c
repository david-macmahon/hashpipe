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
//void *guppi_net_thread(void *args);
void *paper_net_thread(void *args);
#endif
void *paper_gpu_thread(void *args);

int main(int argc, char *argv[]) {

    /* thread args */
    struct guppi_thread_args net_args, gpu_args;
    guppi_thread_args_init(&net_args);
    guppi_thread_args_init(&gpu_args);

    net_args.output_buffer = 1;
    gpu_args.input_buffer = net_args.output_buffer;
    gpu_args.output_buffer = 2;

    /* Init status shared mem */
    struct guppi_status stat;
    int rv = guppi_status_attach(&stat);
    if (rv!=GUPPI_OK) {
        fprintf(stderr, "Error connecting to guppi_status\n");
        exit(1);
    }

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

    signal(SIGINT, cc);
    signal(SIGTERM, cc);

    /* Launch net thread */
    pthread_t net_thread_id;
#ifdef FAKE_NET
    rv = pthread_create(&net_thread_id, NULL, paper_fake_net_thread,
            (void *)&net_args);
#else
    //rv = pthread_create(&net_thread_id, NULL, guppi_net_thread,
    //        (void *)&net_args);
    rv = pthread_create(&net_thread_id, NULL, paper_net_thread,
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

    /* Wait for SIGINT (i.e. control-c) or SIGTERM (aka "kill <pid>") */
    run=1;
    while (run) { 
        sleep(1); 
    }
 
    pthread_cancel(gpu_thread_id);
    pthread_cancel(net_thread_id);
    pthread_kill(gpu_thread_id,SIGINT);
    pthread_kill(net_thread_id,SIGINT);
    pthread_join(net_thread_id,NULL);
    printf("Joined net thread\n"); fflush(stdout);
    pthread_join(gpu_thread_id,NULL);
    printf("Joined GPU thread\n"); fflush(stdout);

    guppi_thread_args_destroy(&net_args);
    guppi_thread_args_destroy(&gpu_args);

    exit(0);
}
