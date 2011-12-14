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
#include "paper_thread.h"

static int init(struct guppi_thread_args *args)
{
    /* Attach to status shared mem area */
    THREAD_INIT_STATUS(STATUS_KEY);

    // Get sizing parameters
    XGPUInfo xgpu_info;
    xgpuInfo(&xgpu_info);

    /* Create paper_input_databuf */
    THREAD_INIT_DATABUF(paper_input_databuf, 4,
        xgpu_info.vecLength*sizeof(ComplexInput),
        args->input_buffer);

    /* Create paper_ouput_databuf */
    THREAD_INIT_DATABUF(paper_output_databuf, 16,
        xgpu_info.matLength*sizeof(Complex),
        args->output_buffer);

    // Success!
    return 0;
}

static void *run(void * _args)
{
    // Cast _args
    struct guppi_thread_args *args = (struct guppi_thread_args *)_args;

    THREAD_RUN_SET_AFFINITY_PRIORITY(args);

    /* Attach to status shared mem area */
    THREAD_RUN_ATTACH_STATUS(st);

    /* Attach to paper_input_databuf */
    THREAD_RUN_ATTACH_DATABUF(paper_input_databuf, db_in, args->input_buffer);

    /* Attach to paper_ouput_databuf */
    THREAD_RUN_ATTACH_DATABUF(paper_output_databuf, db_out, args->output_buffer);

    /* Loop */
    int xgpu_error = 0;
    int curblock_in=0;
    int curblock_out=0;

    // Initialize context to point at first input and output memory blocks
    XGPUContext context;
    context.array_h = (ComplexInput *)db_in->block[curblock_in].sub_block;
    context.matrix_h = (Complex *)db_out->block[curblock_out].data;

    xgpu_error = xgpuInit(&context);
    if (XGPU_OK != xgpu_error) {
        fprintf(stderr, "ERROR: xGPU initialisation failed (error code %d)\n", xgpu_error);
        return THREAD_ERROR;
    }

    while (run_threads) {

        /* Note waiting status */
        guppi_status_lock_safe(&st);
        hputs(st.buf, STATUS_KEY, "waiting");
        guppi_status_unlock_safe(&st);

        /* Wait for buf to have data */
        int rv = paper_input_databuf_wait_filled(db_in, curblock_in);
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

    // Have to close all pushes
    THREAD_RUN_DETACH_DATAUF;
    THREAD_RUN_DETACH_DATAUF;
    THREAD_RUN_DETACH_STATUS;
    THREAD_RUN_POP_AFFINITY_PRIORITY;

    // Thread success!
    return NULL;
}

static pipeline_thread_module_t module = {
    name: "paper_gpu_thread",
    type: PIPELINE_INOUT_THREAD,
    init: init,
    run:  run
};

static __attribute__((constructor)) void ctor()
{
  register_pipeline_thread_module(&module);
}
