/*
 * paper_fake_net_thread.c
 *
 * Routine to write fake data into shared memory blocks.  This allows the
 * processing pipelines to be tested without the network portion of PAPER.
 */

#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <sched.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>

#include <xgpu.h>

#include "fitshead.h"
#include "guppi_params.h"
#include "guppi_error.h"
#include "guppi_status.h"
#include "paper_databuf.h"
#include "guppi_udp.h"
#include "guppi_time.h"
#include "spead_heap.h"

#define STATUS_KEY "NETSTAT"  /* Define before guppi_threads.h */
#include "guppi_threads.h"
#include "guppi_defines.h"
#include "paper_thread.h"

static int init(struct guppi_thread_args *args)
{
    /* Attach to status shared mem area */
    THREAD_INIT_STATUS(STATUS_KEY);

    // Get sizing parameters
    XGPUInfo xgpu_info;
    xgpuInfo(&xgpu_info);

    /* Create paper_input_databuf for output buffer */
    THREAD_INIT_DATABUF(paper_input_databuf, 4,
        xgpu_info.vecLength*sizeof(ComplexInput),
        args->output_buffer);

    // Success!
    return 0;
}

static void *run(void * _args)
{
    // Cast _args
    struct guppi_thread_args *args = (struct guppi_thread_args *)_args;

    THREAD_RUN_BEGIN(args);

    THREAD_RUN_SET_AFFINITY_PRIORITY(args);

    THREAD_RUN_ATTACH_STATUS(st);

    /* Attach to paper_input_databuf */
    THREAD_RUN_ATTACH_DATABUF(paper_input_databuf, db, args->output_buffer);

    /* Main loop */
    int i, rv;
    uint64_t mcnt = 0;
    int block_idx = 0;
    signal(SIGINT,cc);
    signal(SIGTERM,cc);
    while (run_threads) {

        guppi_status_lock_safe(&st);
        hputs(st.buf, STATUS_KEY, "waiting");
        guppi_status_unlock_safe(&st);
 
#if 0
        // xgpuRandomComplex is super-slow so no need to sleep
        // Wait for data
        //struct timespec sleep_dur, rem_sleep_dur;
        //sleep_dur.tv_sec = 0;
        //sleep_dur.tv_nsec = 0;
        //nanosleep(&sleep_dur, &rem_sleep_dur);
#endif
	
        /* Wait for new block to be free, then clear it
         * if necessary and fill its header with new values.
         */
        while ((rv=paper_input_databuf_wait_free(db, block_idx)) 
                != GUPPI_OK) {
            if (rv==GUPPI_TIMEOUT) {
                guppi_status_lock_safe(&st);
                hputs(st.buf, STATUS_KEY, "blocked");
                guppi_status_unlock_safe(&st);
                continue;
            } else {
                guppi_error(__FUNCTION__, "error waiting for free databuf");
                run_threads=0;
                pthread_exit(NULL);
                break;
            }
        }

        guppi_status_lock_safe(&st);
        hputs(st.buf, STATUS_KEY, "receiving");
        hputi4(st.buf, "NETBKOUT", block_idx);
        guppi_status_unlock_safe(&st);
 
        // Fill in sub-block headers
        for(i=0; i<N_SUB_BLOCKS_PER_INPUT_BLOCK; i++) {
          db->block[block_idx].header[i].mcnt = mcnt;
          db->block[block_idx].header[i].chan_present[0] = -1;
          db->block[block_idx].header[i].chan_present[1] = -1;
          mcnt++;
        }

        // Fill in random data
        xgpuRandomComplex((ComplexInput *)db->block[block_idx].complexity,
            N_SUB_BLOCKS_PER_INPUT_BLOCK*sizeof(paper_input_sub_block_t)/sizeof(ComplexInput));

#ifdef FAKE_TEST_INPUT
#ifndef FAKE_TEST_CHAN
#define FAKE_TEST_CHAN 0
#endif
        // For testing, zero out block and set input FAKE_TEST_INPUT, chan 0 to
        // all -16 (-1 * 16)
        memset(db->block[block_idx].sub_block, 0,
            N_SUB_BLOCKS_PER_INPUT_BLOCK*sizeof(paper_input_sub_block_t));
        for(i=0; i<N_SUB_BLOCKS_PER_INPUT_BLOCK; i++) {
          int j;
          for(j=0; j<N_TIME; j++) {
            db->block[block_idx].sub_block[i].time[j].chan[FAKE_TEST_CHAN].input[FAKE_TEST_INPUT].real = -16;
#ifdef FAKE_TEST_INPUT1
            db->block[block_idx].sub_block[i].time[j].chan[FAKE_TEST_CHAN].input[FAKE_TEST_INPUT1].real = -16;
#endif
          }
        }
#elif defined(DUP_ALL_INPUTS)
        // Duplicate input 0 to all other inputs
        for(i=0; i<N_SUB_BLOCKS_PER_INPUT_BLOCK; i++) {
          int j;
          for(j=0; j<N_TIME; j++) {
            int c;
            for(c=0; c<N_CHAN; c++) {
              int input;
              for(input=1; input<N_INPUT; input++) {
                db->block[block_idx].sub_block[i].time[j].chan[c].input[input].real =
                  db->block[block_idx].sub_block[i].time[j].chan[c].input[0].real;
                db->block[block_idx].sub_block[i].time[j].chan[c].input[input].imag =
                  db->block[block_idx].sub_block[i].time[j].chan[c].input[0].imag;
              }
            }
          }
        }
#endif

        // Mark block as full
        paper_input_databuf_set_filled(db, block_idx);

        // Setup for next block
        block_idx = (block_idx + 1) % db->header.n_block;

        /* Will exit if thread has been cancelled */
        pthread_testcancel();
    }

    // Have to close all pushes
    THREAD_RUN_DETACH_DATAUF;
    THREAD_RUN_DETACH_STATUS;
    THREAD_RUN_END;

    // Thread success!
    return NULL;
}

static pipeline_thread_module_t module = {
    name: "paper_fake_net_thread",
    type: PIPELINE_INPUT_THREAD,
    init: init,
    run:  run
};

static __attribute__((constructor)) void ctor()
{
  register_pipeline_thread_module(&module);
}
