/*
 * paper_gpu_thread.c
 *
 * Performs correlation of incoming data using xGPU
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>

#include <xgpu.h>
// For compatibility with pre-v1.5.0 xGPU.
// SYNCOP_SYNC_TRANSFER=0 is incorrect but that's
// all that was supported pre-v1.5.0
#ifndef SYNCOP_DUMP
#define SYNCOP_DUMP 1
#endif
#ifndef SYNCOP_SYNC_TRANSFER
#define SYNCOP_SYNC_TRANSFER 0
#endif

#include "hashpipe.h"
#include "paper_databuf.h"

#define ELAPSED_NS(start,stop) \
  (((int64_t)stop.tv_sec-start.tv_sec)*1000*1000*1000+(stop.tv_nsec-start.tv_nsec))

static void *run(hashpipe_thread_args_t * args, int doCPU)
{
    // Local aliases to shorten access to args fields
    paper_input_databuf_t *db_in = (paper_input_databuf_t *)args->ibuf;
    paper_output_databuf_t *db_out = (paper_output_databuf_t *)args->obuf;
    hashpipe_status_t st = args->st;
    const char * status_key = args->thread_desc->skey;

#ifdef DEBUG_SEMS
    fprintf(stderr, "s/tid %lu/                      GPU/\n", pthread_self());
#endif

    // Init integration control status variables
    int gpu_dev = 0;
    hashpipe_status_lock_safe(&st);
    hputs(st.buf,  "INTSTAT", "off");
    hputi8(st.buf, "INTSYNC", 0);
    hputi4(st.buf, "INTCOUNT", N_SUB_BLOCKS_PER_INPUT_BLOCK);
    hputi8(st.buf, "GPUDUMPS", 0);
    hgeti4(st.buf, "GPUDEV", &gpu_dev); // No change if not found
    hputi4(st.buf, "GPUDEV", gpu_dev);
    hashpipe_status_unlock_safe(&st);

    /* Loop */
    int rv;
    char integ_status[17];
    uint64_t start_mcount, last_mcount=0;
    uint64_t gpu_dumps=0;
    int int_count; // Number of blocks to integrate per dump
    int xgpu_error = 0;
    int curblock_in=0;
    int curblock_out=0;

    struct timespec start, stop;
    uint64_t elapsed_gpu_ns  = 0;
    uint64_t gpu_block_count = 0;

    // Initialize context to point at first input and output memory blocks.
    // This seems redundant since we do this just before calling
    // xgpuCudaXengine, but we need to pass something in for array_h and
    // matrix_x to prevent xgpuInit from allocating memory.
    XGPUContext context;
    context.array_h = (ComplexInput *)db_in->block[0].data;
    context.array_len = (db_in->header.n_block * sizeof(paper_gpu_input_block_t) - sizeof(paper_input_header_t)) / sizeof(ComplexInput);
    context.matrix_h = (Complex *)db_out->block[0].data;
    context.matrix_len = (db_out->header.n_block * sizeof(paper_output_block_t) - sizeof(paper_output_header_t)) / sizeof(Complex);

    xgpu_error = xgpuInit(&context, gpu_dev);
    if (XGPU_OK != xgpu_error) {
        fprintf(stderr, "ERROR: xGPU initialization failed (error code %d)\n", xgpu_error);
        return THREAD_ERROR;
    }

    while (run_threads()) {

        // Note waiting status,
        // query integrating status
        // and, if armed, start count
        hashpipe_status_lock_safe(&st);
        hputs(st.buf, status_key, "waiting");
        hgets(st.buf,  "INTSTAT", 16, integ_status);
        hgeti8(st.buf, "INTSYNC", (long long*)&start_mcount);
        hashpipe_status_unlock_safe(&st);

        // Wait for new input block to be filled
        while ((rv=hashpipe_databuf_wait_filled((hashpipe_databuf_t *)db_in, curblock_in)) != HASHPIPE_OK) {
            if (rv==HASHPIPE_TIMEOUT) {
                hashpipe_status_lock_safe(&st);
                hputs(st.buf, status_key, "blocked_in");
                hashpipe_status_unlock_safe(&st);
                continue;
            } else {
                hashpipe_error(__FUNCTION__, "error waiting for filled databuf");
                pthread_exit(NULL);
                break;
            }
        }

        // Got a new data block, update status and determine how to handle it
        hashpipe_status_lock_safe(&st);
        hputi4(st.buf, "GPUBLKIN", curblock_in);
        hputu8(st.buf, "GPUMCNT", db_in->block[curblock_in].header.mcnt);
        hashpipe_status_unlock_safe(&st);

        // If integration status "off"
        if(!strcmp(integ_status, "off")) {
            // Mark input block as free and advance
            hashpipe_databuf_set_free((hashpipe_databuf_t *)db_in, curblock_in);
            curblock_in = (curblock_in + 1) % db_in->header.n_block;
            // Skip to next input buffer
            continue;
        }

        // If integration status is "start"
        if(!strcmp(integ_status, "start")) {
            // If buffer mcount < start_mcount (i.e. not there yet)
            if(db_in->block[curblock_in].header.mcnt < start_mcount) {
              // Drop input buffer
              // Mark input block as free and advance
              hashpipe_databuf_set_free((hashpipe_databuf_t *)db_in, curblock_in);
              curblock_in = (curblock_in + 1) % db_in->header.n_block;
              // Skip to next input buffer
              continue;
            // Else if mcount == start_mcount (time to start)
            } else if(db_in->block[curblock_in].header.mcnt == start_mcount) {
              // Set integration status to "on"
              // Read integration count (INTCOUNT)
              fprintf(stderr, "--- integration on ---\n");
              strcpy(integ_status, "on");
              hashpipe_status_lock_safe(&st);
              hputs(st.buf,  "INTSTAT", integ_status);
              hgeti4(st.buf, "INTCOUNT", &int_count);
              hashpipe_status_unlock_safe(&st);
              // Compute last mcount
              last_mcount = start_mcount + (int_count-1) * N_SUB_BLOCKS_PER_INPUT_BLOCK;
            // Else (missed starting mcount)
            } else {
              // Handle missed start of integration
              // TODO!
              fprintf(stderr, "--- mcnt=%06lx > start_mcnt=%06lx ---\n",
                  db_in->block[curblock_in].header.mcnt, start_mcount);
            }
        }

        // Integration status is "on" or "stop"

        // Note processing status
        hashpipe_status_lock_safe(&st);
        hputs(st.buf, status_key, "processing gpu");
        hashpipe_status_unlock_safe(&st);


        // Setup for current chunk
        context.input_offset = curblock_in * sizeof(paper_gpu_input_block_t) / sizeof(ComplexInput);
        context.output_offset = curblock_out * sizeof(paper_output_block_t) / sizeof(Complex);

        // Call CUDA X engine function
        int doDump = 0;
        // Dump if this is the last block or we are doing both CPU and GPU
        // (GPU and CPU test mode always dumps every input block)
        if(db_in->block[curblock_in].header.mcnt == last_mcount || doCPU) {
          doDump = 1;
          // Wait for new output block to be free
          while ((rv=paper_output_databuf_wait_free(db_out, curblock_out)) != HASHPIPE_OK) {
              if (rv==HASHPIPE_TIMEOUT) {
                  hashpipe_status_lock_safe(&st);
                  hputs(st.buf, status_key, "blocked gpu out");
                  hashpipe_status_unlock_safe(&st);
                  continue;
              } else {
                  hashpipe_error(__FUNCTION__, "error waiting for free databuf");
                  pthread_exit(NULL);
                  break;
              }
          }
        } else if(db_in->block[curblock_in].header.mcnt > last_mcount) {
          // Missed end of integration
          // TODO Handle in a better way thn just logging
          fprintf(stderr, "--- mcnt=%06lx > last_mcnt=%06lx ---\n",
              db_in->block[curblock_in].header.mcnt, last_mcount);
        }

        clock_gettime(CLOCK_MONOTONIC, &start);

        xgpuCudaXengine(&context, doDump ? SYNCOP_DUMP : SYNCOP_SYNC_TRANSFER);

        clock_gettime(CLOCK_MONOTONIC, &stop);
        elapsed_gpu_ns += ELAPSED_NS(start, stop);
        gpu_block_count++;

        if(doDump) {
          clock_gettime(CLOCK_MONOTONIC, &start);
          xgpuClearDeviceIntegrationBuffer(&context);
          clock_gettime(CLOCK_MONOTONIC, &stop);
          elapsed_gpu_ns += ELAPSED_NS(start, stop);

          // TODO Maybe need to subtract all or half the integration time here
          // depending on recevier's expectations.
          db_out->block[curblock_out].header.mcnt = last_mcount;
          // If integration status if "stop"
          if(!strcmp(integ_status, "stop")) {
            // Set integration status to "off"
            strcpy(integ_status, "off");
            hashpipe_status_lock_safe(&st);
            hputs(st.buf,  "INTSTAT", integ_status);
            hashpipe_status_unlock_safe(&st);
          } else {
            // Advance last_mcount for end of next integration
            last_mcount += int_count * N_SUB_BLOCKS_PER_INPUT_BLOCK;
          }

          // Mark output block as full and advance
          paper_output_databuf_set_filled(db_out, curblock_out);
          curblock_out = (curblock_out + 1) % db_out->header.n_block;
          // TODO Need to handle or at least check for overflow!

          // Update GPU dump counter and GPU Gbps
          gpu_dumps++;
          hashpipe_status_lock_safe(&st);
          hputi8(st.buf, "GPUDUMPS", gpu_dumps);
          hputr4(st.buf, "GPUGBPS", (float)(8*N_FLUFFED_BYTES_PER_BLOCK*gpu_block_count)/elapsed_gpu_ns);
          hashpipe_status_unlock_safe(&st);

          // Start new average
          elapsed_gpu_ns  = 0;
          gpu_block_count = 0;
        }

        if(doCPU) {

            /* Note waiting status */
            hashpipe_status_lock_safe(&st);
            hputs(st.buf, status_key, "waiting");
            hashpipe_status_unlock_safe(&st);

            // Wait for new output block to be free
            while ((rv=paper_output_databuf_wait_free(db_out, curblock_out)) != HASHPIPE_OK) {
                if (rv==HASHPIPE_TIMEOUT) {
                    hashpipe_status_lock_safe(&st);
                    hputs(st.buf, status_key, "blocked cpu out");
                    hashpipe_status_unlock_safe(&st);
                    continue;
                } else {
                    hashpipe_error(__FUNCTION__, "error waiting for free databuf");
                    pthread_exit(NULL);
                    break;
                }
            }

            // Note "processing cpu" status, current input block
            hashpipe_status_lock_safe(&st);
            hputs(st.buf, status_key, "processing cpu");
            hashpipe_status_unlock_safe(&st);

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
        hashpipe_databuf_set_free((hashpipe_databuf_t *)db_in, curblock_in);
        curblock_in = (curblock_in + 1) % db_in->header.n_block;

        /* Check for cancel */
        pthread_testcancel();
    }

    xgpuFree(&context);

    // Thread success!
    return NULL;
}

static void *run_gpu_only(hashpipe_thread_args_t * args)
{
  return run(args, 0);
}

static void *run_gpu_cpu(hashpipe_thread_args_t * args)
{
  return run(args, 1);
}

static hashpipe_thread_desc_t gpu_thread = {
    name: "paper_gpu_thread",
    skey: "GPUSTAT",
    init: NULL,
    run:  run_gpu_only,
    ibuf_desc: {paper_gpu_input_databuf_create},
    obuf_desc: {paper_output_databuf_create}
};

static hashpipe_thread_desc_t gpu_cpu_thread = {
    name: "paper_gpu_cpu_thread",
    skey: "GPUSTAT",
    init: NULL,
    run:  run_gpu_cpu,
    ibuf_desc: {paper_gpu_input_databuf_create},
    obuf_desc: {paper_output_databuf_create}
};

static __attribute__((constructor)) void ctor()
{
  register_hashpipe_thread(&gpu_thread);
  register_hashpipe_thread(&gpu_cpu_thread);
}
