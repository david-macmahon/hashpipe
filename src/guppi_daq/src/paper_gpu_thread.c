/*
 * paper_gpu_thread.c
 *
 * Performs correlation of incoming data using xGPU
 */

#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <sched.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>

#include <xgpu.h>

#include "fitshead.h"
#include "sdfits.h"
#include "guppi_error.h"
#include "guppi_status.h"
#include "paper_databuf.h"
#include "guppi_params.h"

#define STATUS_KEY "GPUSTAT"
#include "guppi_threads.h"

int init(struct guppi_thread_args *args)
{
    int rv;

    /* Attach to status shared mem area */
    struct guppi_status st;
    rv = guppi_status_attach(&st);
    if (rv!=GUPPI_OK) {
        guppi_error(__FUNCTION__,
                "Error attaching to status shared memory.");
        return 1;
    }

    /* Init status */
    guppi_status_lock_safe(&st);
    hputs(st.buf, STATUS_KEY, "init");
    guppi_status_unlock_safe(&st);

    /* Detach from status shared mem area */
    rv = guppi_status_detach(&st);
    if (rv!=GUPPI_OK) {
        guppi_error(__FUNCTION__,
                "Error detaching from status shared memory.");
        return 1;
    }

    // Get sizing parameters
    XGPUInfo xgpu_info;
    xgpuInfo(&xgpu_info);

    /* Create (and attach to) paper_input_databuf */
    struct paper_input_databuf *db_in;
    db_in = paper_input_databuf_create(4,
        xgpu_info.vecLength*sizeof(ComplexInput),
        args->input_buffer, GPU_INPUT_BUF);
    if (db_in==NULL) {
        char msg[256];
        sprintf(msg, "Error attaching to databuf(%d) shared memory.",
                args->input_buffer);
        guppi_error(__FUNCTION__, msg);
        return 1;
    }
    // Detach from paper_input_databuf
    paper_input_databuf_detach(db_in);

    /* Create (and attach to) paper_ouput_databuf */
    struct paper_output_databuf *db_out;
    db_out = paper_output_databuf_create(16,
        xgpu_info.matLength*sizeof(Complex),
        args->output_buffer);
    if (db_out==NULL) {
        char msg[256];
        sprintf(msg, "Error attaching to databuf(%d) shared memory.",
                args->output_buffer);
        guppi_error(__FUNCTION__, msg);
        return 1;
    }
    // Detach from paper_output_databuf
    paper_output_databuf_detach(db_out);

    // Success!
    return 0;
}

void *run(void * _args)
{
    // Cast _args
    struct guppi_thread_args *args = (struct guppi_thread_args *)_args;

    int rv;

    /* Set cpu affinity */
    // TODO Pass values in via args
    cpu_set_t cpuset, cpuset_orig;
    sched_getaffinity(0, sizeof(cpu_set_t), &cpuset_orig);
    //CPU_ZERO(&cpuset);
    CPU_CLR(13, &cpuset);
    CPU_SET(11, &cpuset);
    rv = sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
    if (rv<0) {
        guppi_error(__FUNCTION__, "Error setting cpu affinity.");
        perror("sched_setaffinity");
        pthread_exit(args);
    }

    /* Set priority */
    rv = setpriority(PRIO_PROCESS, 0, args->priority);
    if (rv<0) {
        guppi_error(__FUNCTION__, "Error setting priority level.");
        perror("set_priority");
        pthread_exit(args);
    }

    pthread_cleanup_push((void *)guppi_thread_set_finished, args);

    /* Attach to status shared mem area */
    struct guppi_status st;
    rv = guppi_status_attach(&st);
    if (rv!=GUPPI_OK) {
        guppi_error(__FUNCTION__,
                "Error attaching to status shared memory.");
        pthread_exit(args);
    }
    pthread_cleanup_push((void *)guppi_status_detach, &st);
    pthread_cleanup_push((void *)set_exit_status, &st);

    /* Attach to paper_input_databuf */
    struct paper_input_databuf *db_in;
    db_in = paper_input_databuf_attach(args->input_buffer);
    if (db_in==NULL) {
        char msg[256];
        sprintf(msg, "Error attaching to databuf(%d) shared memory.",
                args->input_buffer);
        guppi_error(__FUNCTION__, msg);
        pthread_exit(args);
    }
    pthread_cleanup_push((void *)paper_input_databuf_detach, db_in);

    /* Create (and attach) to paper_ouput_databuf */
    struct paper_output_databuf *db_out;
    db_out = paper_output_databuf_attach(args->output_buffer);
    if (db_out==NULL) {
        char msg[256];
        sprintf(msg, "Error attaching to databuf(%d) shared memory.",
                args->output_buffer);
        guppi_error(__FUNCTION__, msg);
        pthread_exit(args);
    }
    pthread_cleanup_push((void *)paper_output_databuf_detach, db_out);

    /* Loop */
    int xgpu_error = 0;
    int curblock_in=0;
    int curblock_out=0;

    // Get sizing parameters
    XGPUInfo xgpu_info;
    xgpuInfo(&xgpu_info);
    // TODO Check sizes against data buffer sizes

    // Initialize context to point at first input and output memory blocks
    XGPUContext context;
    context.array_h = (ComplexInput *)db_in->block[curblock_in].sub_block;
    context.matrix_h = (Complex *)db_out->block[curblock_out].data;

    xgpu_error = xgpuInit(&context);
    if (XGPU_OK != xgpu_error) {
        fprintf(stderr, "ERROR: xGPU initialisation failed (error code %d)\n", xgpu_error);
        pthread_exit(args);
    }

    while (run_threads) {

        /* Note waiting status */
        guppi_status_lock_safe(&st);
        hputs(st.buf, STATUS_KEY, "waiting");
        guppi_status_unlock_safe(&st);

        /* Wait for buf to have data */
        rv = paper_input_databuf_wait_filled(db_in, curblock_in);
        if (rv!=0) continue;

        /* Note waiting status, current input block */
        guppi_status_lock_safe(&st);
        hputs(st.buf, STATUS_KEY, "processing");
        hputi4(st.buf, "GPUBLKIN", curblock_in);
        guppi_status_unlock_safe(&st);

        // TODO Get start time and integration duration from somewhere

        /*
         * Call CUDA X engine function
         * (dump every time for now)
         */
        xgpuCudaXengine(&context, 1);

        /* Mark input block as free */
        paper_input_databuf_set_free(db_in, curblock_in);
        /* Go to next input block */
        curblock_in = (curblock_in + 1) % db_in->header.n_block;

        /* Mark output block as full */
        paper_output_databuf_set_filled(db_out, curblock_out);
        /* Go to next output block */
        curblock_out = (curblock_out + 1) % db_out->header.n_block;
        // TODO Need to handle or at least check for overflow!

        // Setup for next chunk
        context.array_h = (ComplexInput *)db_in->block[curblock_in].sub_block;
        context.matrix_h = (Complex *)db_out->block[curblock_out].data;
        xgpuSetHostInputBuffer(&context);
        xgpuSetHostOutputBuffer(&context);

        /* Check for cancel */
        pthread_testcancel();
    }
    run_threads=0;

    xgpuFree(&context);

    pthread_exit(NULL);

    /*
     * Have to close all push's since they are preprocessor macros that open
     * but do not close new lexical scopes.
     */
    pthread_cleanup_pop(0); /* Closes paper_output_databuf_detach */
    pthread_cleanup_pop(0); /* Closes paper_input_databuf_detach */
    pthread_cleanup_pop(0); /* Closes set_exit_status */
    pthread_cleanup_pop(0); /* Closes guppi_status_detach */
    pthread_cleanup_pop(0); /* Closes guppi_thread_set_finished */

    return NULL;
}
