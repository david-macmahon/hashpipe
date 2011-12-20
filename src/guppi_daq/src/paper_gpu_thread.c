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

static void *run(void * _args, int doCPU)
{
    // Cast _args
    struct guppi_thread_args *args = (struct guppi_thread_args *)_args;

    THREAD_RUN_BEGIN(args);

    THREAD_RUN_SET_AFFINITY_PRIORITY(args);

    /* Attach to status shared mem area */
    THREAD_RUN_ATTACH_STATUS(st);

    /* Attach to paper_input_databuf */
    THREAD_RUN_ATTACH_DATABUF(paper_input_databuf, db_in, args->input_buffer);

    /* Attach to paper_ouput_databuf */
    THREAD_RUN_ATTACH_DATABUF(paper_output_databuf, db_out, args->output_buffer);

    /* Loop */
    int rv;
    int xgpu_error = 0;
    int curblock_in=0;
    int curblock_out=0;

    // Initialize context to point at first input and output memory blocks.
    // This seems redundant since we do this just before calling
    // xgpuCudaXengine, but we need to pass something in for array_h and
    // matrix_x to prevent xgpuInit from allocating memory.
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

        // Wait for new input block to be filled
        while ((rv=paper_input_databuf_wait_filled(db_in, curblock_in)) != GUPPI_OK) {
            if (rv==GUPPI_TIMEOUT) {
                guppi_status_lock_safe(&st);
                hputs(st.buf, STATUS_KEY, "blocked_in");
                guppi_status_unlock_safe(&st);
                continue;
            } else {
                guppi_error(__FUNCTION__, "error waiting for filled databuf");
                run_threads=0;
                pthread_exit(NULL);
                break;
            }
        }

        // Wait for new output block to be free
        while ((rv=paper_output_databuf_wait_free(db_out, curblock_out)) != GUPPI_OK) {
            if (rv==GUPPI_TIMEOUT) {
                guppi_status_lock_safe(&st);
                hputs(st.buf, STATUS_KEY, "blocked gpu out");
                guppi_status_unlock_safe(&st);
                continue;
            } else {
                guppi_error(__FUNCTION__, "error waiting for free databuf");
                run_threads=0;
                pthread_exit(NULL);
                break;
            }
        }

        // Note processing status, current input block
        guppi_status_lock_safe(&st);
        hputs(st.buf, STATUS_KEY, "processing gpu");
        hputi4(st.buf, "GPUBLKIN", curblock_in);
        guppi_status_unlock_safe(&st);

        // Setup for current chunk
        context.array_h = (ComplexInput *)db_in->block[curblock_in].sub_block;
        context.matrix_h = (Complex *)db_out->block[curblock_out].data;
        xgpuSetHostInputBuffer(&context);
        xgpuSetHostOutputBuffer(&context);

        // Call CUDA X engine function (dump every time for now)
        // TODO Get start time and integration duration from somewhere
        xgpuCudaXengine(&context, 1);
        xgpuClearDeviceIntegrationBuffer(&context);
        //xgpuOmpXengine((Complex *)db_out->block[curblock_out].data, context.array_h);

        // Mark output block as full and advance
        paper_output_databuf_set_filled(db_out, curblock_out);
        curblock_out = (curblock_out + 1) % db_out->header.n_block;
        // TODO Need to handle or at least check for overflow!

        if(doCPU) {

            /* Note waiting status */
            guppi_status_lock_safe(&st);
            hputs(st.buf, STATUS_KEY, "waiting");
            guppi_status_unlock_safe(&st);

            // Wait for new output block to be free
            while ((rv=paper_output_databuf_wait_free(db_out, curblock_out)) != GUPPI_OK) {
                if (rv==GUPPI_TIMEOUT) {
                    guppi_status_lock_safe(&st);
                    hputs(st.buf, STATUS_KEY, "blocked cpu out");
                    guppi_status_unlock_safe(&st);
                    continue;
                } else {
                    guppi_error(__FUNCTION__, "error waiting for free databuf");
                    run_threads=0;
                    pthread_exit(NULL);
                    break;
                }
            }

            // Note "processing cpu" status, current input block
            guppi_status_lock_safe(&st);
            hputs(st.buf, STATUS_KEY, "processing cpu");
            guppi_status_unlock_safe(&st);

            /*
             * Call CPU X engine function
             */
            xgpuOmpXengine((Complex *)db_out->block[curblock_out].data, context.array_h);

            // Mark output block as full and advance
            paper_output_databuf_set_filled(db_out, curblock_out);
            curblock_out = (curblock_out + 1) % db_out->header.n_block;
            // TODO Need to handle or at least check for overflow!
        }

        // Mark input block as free and advance
        paper_input_databuf_set_free(db_in, curblock_in);
        curblock_in = (curblock_in + 1) % db_in->header.n_block;

        /* Check for cancel */
        pthread_testcancel();
    }
    run_threads=0;

    xgpuFree(&context);

    // Have to close all pushes
    THREAD_RUN_DETACH_DATAUF;
    THREAD_RUN_DETACH_DATAUF;
    THREAD_RUN_DETACH_STATUS;
    THREAD_RUN_END;

    // Thread success!
    return NULL;
}

static void *run_gpu_only(void * _args)
{
  return run(_args, 0);
}

static void *run_gpu_cpu(void * _args)
{
  return run(_args, 1);
}

static pipeline_thread_module_t module1 = {
    name: "paper_gpu_thread",
    type: PIPELINE_INOUT_THREAD,
    init: init,
    run:  run_gpu_only
};

static pipeline_thread_module_t module2 = {
    name: "paper_gpu_cpu_thread",
    type: PIPELINE_INOUT_THREAD,
    init: init,
    run:  run_gpu_cpu
};

static __attribute__((constructor)) void ctor()
{
  register_pipeline_thread_module(&module1);
  register_pipeline_thread_module(&module2);
}
