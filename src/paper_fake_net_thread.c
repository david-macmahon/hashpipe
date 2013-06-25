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
#include <sched.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>

#include <xgpu.h>

#include "fitshead.h"
#include "hashpipe_error.h"
#include "hashpipe_status.h"
#include "paper_databuf.h"

#define STATUS_KEY "NETSTAT"  /* Define before hashpipe_thread.h */
#include "hashpipe_thread.h"

static int init(struct hashpipe_thread_args *args)
{
    /* Attach to status shared mem area */
    THREAD_INIT_STATUS(args->instance_id, STATUS_KEY);

    /* Create paper_input_databuf for output buffer */
    THREAD_INIT_DATABUF(args->instance_id, paper_input_databuf,
        args->output_buffer);

    // Success!
    return 0;
}

static void *run(void * _args)
{
    // Cast _args
    struct hashpipe_thread_args *args = (struct hashpipe_thread_args *)_args;

    THREAD_RUN_BEGIN(args);

    THREAD_RUN_SET_AFFINITY_PRIORITY(args);

    THREAD_RUN_ATTACH_STATUS(args->instance_id, st);

    /* Attach to paper_input_databuf */
    THREAD_RUN_ATTACH_DATABUF(args->instance_id,
        paper_input_databuf, db, args->output_buffer);

    /* Main loop */
    int i, rv;
    uint64_t mcnt = 0;
    uint64_t *data;
    int m,f,t,c;
#ifdef FAKE_TEST_INPUT1
    int f1;
#endif
    int block_idx = 0;
    while (run_threads()) {

        hashpipe_status_lock_safe(&st);
        hputs(st.buf, STATUS_KEY, "waiting");
        hashpipe_status_unlock_safe(&st);
 
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
                != HASHPIPE_OK) {
            if (rv==HASHPIPE_TIMEOUT) {
                hashpipe_status_lock_safe(&st);
                hputs(st.buf, STATUS_KEY, "blocked");
                hashpipe_status_unlock_safe(&st);
                continue;
            } else {
                hashpipe_error(__FUNCTION__, "error waiting for free databuf");
                clear_run_threads();
                pthread_exit(NULL);
                break;
            }
        }

        hashpipe_status_lock_safe(&st);
        hputs(st.buf, STATUS_KEY, "receiving");
        hputi4(st.buf, "NETBKOUT", block_idx);
        hashpipe_status_unlock_safe(&st);
 
        // Fill in sub-block headers
        for(i=0; i<N_SUB_BLOCKS_PER_INPUT_BLOCK; i++) {
          db->block[block_idx].header.good_data = 1;
          db->block[block_idx].header.mcnt = mcnt;
          mcnt+=Nm;
        }

#ifndef FAKE_TEST_INPUT
#define FAKE_TEST_INPUT 0
#endif

#define FAKE_TEST_FID (FAKE_TEST_INPUT/N_INPUTS_PER_PACKET)

#ifndef FAKE_TEST_CHAN
#define FAKE_TEST_CHAN 0
#endif

        // For testing, zero out block and set input FAKE_TEST_INPUT, FAKE_TEST_CHAN to
        // all -16 (-1 * 16)
        data = db->block[block_idx].data;
        memset(data, 0, N_BYTES_PER_BLOCK);

        c = FAKE_TEST_CHAN;
        f = FAKE_TEST_FID;
#ifdef FAKE_TEST_INPUT1
#define FAKE_TEST_FID1 (FAKE_TEST_INPUT1/N_INPUTS_PER_PACKET)
        f1 = FAKE_TEST_FID1;
#endif
        for(m=0; m<Nm; m++) {
          for(t=0; t<Nt; t++) {
            data[paper_input_databuf_data_idx(m,f,t,c)] =
              ((uint64_t)0xf0) << (8*(7-(FAKE_TEST_INPUT%N_INPUTS_PER_PACKET)));
#ifdef FAKE_TEST_INPUT1
            data[paper_input_databuf_data_idx(m,f1,t,c)] =
              ((uint64_t)0xf0) << (8*(7-(FAKE_TEST_INPUT1%N_INPUTS_PER_PACKET)));
#endif
          }
        }

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
