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
    THREAD_INIT_STATUS(args->instance_id, STATUS_KEY);

    // Get sizing parameters
    XGPUInfo xgpu_info;
    xgpuInfo(&xgpu_info);

    /* Create paper_input_databuf */
    THREAD_INIT_DATABUF(args->instance_id, paper_input_databuf, 4,
        xgpu_info.vecLength*sizeof(ComplexInput),
        args->input_buffer);

    /* Create paper_ouput_databuf */
    THREAD_INIT_DATABUF(args->instance_id, paper_output_databuf, 16,
        xgpu_info.matLength*sizeof(Complex),
        args->output_buffer);

    // Success!
    return 0;
}

#define ELAPSED_NS(start,stop) \
  (((int64_t)stop.tv_sec-start.tv_sec)*1000*1000*1000+(stop.tv_nsec-start.tv_nsec))

static void *run(void * _args, int doCPU)
{
    // Cast _args
    struct guppi_thread_args *args = (struct guppi_thread_args *)_args;

#ifdef DEBUG_SEMS
    fprintf(stderr, "s/tid %lu/                  GPU/\n", pthread_self());
#endif

    THREAD_RUN_BEGIN(args);

    THREAD_RUN_SET_AFFINITY_PRIORITY(args);

    /* Attach to status shared mem area */
    THREAD_RUN_ATTACH_STATUS(args->instance_id, st);

    /* Attach to paper_input_databuf */
    THREAD_RUN_ATTACH_DATABUF(args->instance_id,
        paper_input_databuf, db_in, args->input_buffer);

    /* Attach to paper_ouput_databuf */
    THREAD_RUN_ATTACH_DATABUF(args->instance_id,
        paper_output_databuf, db_out, args->output_buffer);

    // Init integration control status variables
    int gpu_dev = 0;
    guppi_status_lock_safe(&st);
    hputs(st.buf,  "INTSTAT", "off");
    hputi8(st.buf, "INTSYNC", 0);
    hputi4(st.buf, "INTCOUNT", N_SUB_BLOCKS_PER_INPUT_BLOCK);
    hputi8(st.buf, "GPUDUMPS", 0);
    hgeti4(st.buf, "GPUDEV", &gpu_dev); // No change if not found
    hputi4(st.buf, "GPUDEV", gpu_dev);
    guppi_status_unlock_safe(&st);

    /* Loop */
    int rv;
    char integ_status[17];
    uint64_t start_mcount, last_mcount=0;
    uint64_t gpu_dumps=0;
    int int_count; // Number of blocks to integrate per dump
    int xgpu_error = 0;
    int curblock_in=0;
    int curblock_out=0;
    size_t input_offset;
    size_t output_offset;

    struct timespec start, finish;

    // Initialize context to point at first input and output memory blocks.
    // This seems redundant since we do this just before calling
    // xgpuCudaXengine, but we need to pass something in for array_h and
    // matrix_x to prevent xgpuInit from allocating memory.
    XGPUContext context;
    context.array_h = (ComplexInput *)db_in->block[0].complexity;
    context.array_len = (db_in->header.n_block * sizeof(paper_input_block_t) - sizeof(paper_input_header_t)) / sizeof(ComplexInput);
    context.matrix_h = (Complex *)db_out->block[0].data;
    context.matrix_len = (db_out->header.n_block * sizeof(paper_output_block_t) - sizeof(paper_output_header_t)) / sizeof(Complex);

    xgpu_error = xgpuInit(&context, gpu_dev);
    if (XGPU_OK != xgpu_error) {
        fprintf(stderr, "ERROR: xGPU initialization failed (error code %d)\n", xgpu_error);
        return THREAD_ERROR;
    }

    while (run_threads) {

        // Note waiting status,
        // query integrating status
        // and, if armed, start count
        guppi_status_lock_safe(&st);
        hputs(st.buf, STATUS_KEY, "waiting");
        hgets(st.buf,  "INTSTAT", 16, integ_status);
        hgeti8(st.buf, "INTSYNC", (long long*)&start_mcount);
        guppi_status_unlock_safe(&st);

        // Wait for new input block to be filled
        if ((rv=paper_input_databuf_filled(db_in, curblock_in)) != GUPPI_OK) {
            if (rv==GUPPI_TIMEOUT) {
                goto done;
            } else {
                guppi_error(__FUNCTION__, "error waiting for filled databuf");
                run_threads=0;
                pthread_exit(NULL);
                break;
            }
        }

        // Got a new data block, update status and determine how to handle it
        guppi_status_lock_safe(&st);
        hputi4(st.buf, "GPUBLKIN", curblock_in);
        hputu8(st.buf, "GPUMCNT", db_in->block[curblock_in].header.mcnt[0]);
        guppi_status_unlock_safe(&st);

        // If integration status "off"
        if(!strcmp(integ_status, "off")) {
            // Mark input block as free and advance
            paper_input_databuf_set_free(db_in, curblock_in);
            curblock_in = (curblock_in + 1) % db_in->header.n_block;
            // Skip to next input buffer
            continue;
        }

        // If integration status is "start"
        if(!strcmp(integ_status, "start")) {
            // If buffer mcount < start_mcount (i.e. not there yet)
            if(db_in->block[curblock_in].header.mcnt[0] < start_mcount) {
              // Drop input buffer
              // Mark input block as free and advance
              paper_input_databuf_set_free(db_in, curblock_in);
              curblock_in = (curblock_in + 1) % db_in->header.n_block;
              // Skip to next input buffer
              continue;
            // Else if mcount == start_mcount (time to start)
            } else if(db_in->block[curblock_in].header.mcnt[0] == start_mcount) {
              // Set integration status to "on"
              // Read integration count (INTCOUNT)
              fprintf(stderr, "--- integration on ---\n");
              strcpy(integ_status, "on");
              guppi_status_lock_safe(&st);
              hputs(st.buf,  "INTSTAT", integ_status);
              hgeti4(st.buf, "INTCOUNT", &int_count);
              guppi_status_unlock_safe(&st);
              // Compute last mcount
              last_mcount = start_mcount + (int_count-1) * N_SUB_BLOCKS_PER_INPUT_BLOCK;
            // Else (missed starting mcount)
            } else {
              // Handle missed start of integration
              // TODO!
              fprintf(stderr, "--- mcnt=%06lx > start_mcnt=%06lx ---\n",
                  db_in->block[curblock_in].header.mcnt[0], start_mcount);
            }
        }

        // Integration status is "on" or "stop"

        // Note processing status
        guppi_status_lock_safe(&st);
        hputs(st.buf, STATUS_KEY, "processing gpu");
        guppi_status_unlock_safe(&st);


        // Setup for current chunk
        input_offset = curblock_in * sizeof(paper_input_block_t) / sizeof(ComplexInput);
        output_offset = curblock_out * sizeof(paper_output_block_t) / sizeof(Complex);

        // Call CUDA X engine function
        int doDump = 0;
        // Dump if this is the last block or we are doing both CPU and GPU
        // (GPU and CPU test mode always dumps every input block)
        if(db_in->block[curblock_in].header.mcnt[0] == last_mcount || doCPU) {
          doDump = 1;
          // Wait for new output block to be free
          if ((rv=paper_output_databuf_busywait_free(db_out, curblock_out)) != GUPPI_OK) {
              if (rv==GUPPI_TIMEOUT) {
                  goto done;
              } else {
                  guppi_error(__FUNCTION__, "error waiting for free databuf");
                  run_threads=0;
                  pthread_exit(NULL);
                  break;
              }
          }
        } else if(db_in->block[curblock_in].header.mcnt[0] > last_mcount) {
          // Missed end of integration
          // TODO Handle in a better way thn just logging
          fprintf(stderr, "--- mcnt=%06lx > last_mcnt=%06lx ---\n",
              db_in->block[curblock_in].header.mcnt[0], last_mcount);
        }

        clock_gettime(CLOCK_MONOTONIC, &start);

        xgpuCudaXengine(&context, input_offset, output_offset, doDump);

        clock_gettime(CLOCK_MONOTONIC, &finish);

        // Note processing time
        guppi_status_lock_safe(&st);
        hputi4(st.buf, "GPU_NS", ELAPSED_NS(start,finish));
        guppi_status_unlock_safe(&st);

        if(doDump) {
          xgpuClearDeviceIntegrationBuffer(&context);
          // TODO Maybe need to subtract all or half the integration time here
          // depending on recevier's expectations.
          db_out->block[curblock_out].header.mcnt = last_mcount;
          // If integration status if "stop"
          if(!strcmp(integ_status, "stop")) {
            // Set integration status to "off"
            strcpy(integ_status, "off");
            guppi_status_lock_safe(&st);
            hputs(st.buf,  "INTSTAT", integ_status);
            guppi_status_unlock_safe(&st);
          } else {
            // Advance last_mcount for end of next integration
            last_mcount += int_count * N_SUB_BLOCKS_PER_INPUT_BLOCK;
          }

          // Mark output block as full and advance
          paper_output_databuf_set_filled(db_out, curblock_out);
          curblock_out = (curblock_out + 1) % db_out->header.n_block;
          // TODO Need to handle or at least check for overflow!

          // Update GPU dump counter
          gpu_dumps++;
          guppi_status_lock_safe(&st);
          hputi8(st.buf, "GPUDUMPS", gpu_dumps);
          guppi_status_unlock_safe(&st);
        }

        if(doCPU) {

            /* Note waiting status */
            guppi_status_lock_safe(&st);
            hputs(st.buf, STATUS_KEY, "waiting");
            guppi_status_unlock_safe(&st);

            // Wait for new output block to be free
            if ((rv=paper_output_databuf_busywait_free(db_out, curblock_out)) != GUPPI_OK) {
                if (rv==GUPPI_TIMEOUT) {
                    goto done;
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

done:
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
