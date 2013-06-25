/*
 * paper_fluff_thread.c
 *
 * Fluffs 4bit+4bit complex data into 8bit+8bit complex data
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

#include "fitshead.h"
#include "hashpipe_error.h"
#include "hashpipe_status.h"
#include "paper_databuf.h"
#include "paper_fluff.h"

#define STATUS_KEY "FLUFSTAT"  /* Define before hashpipe_thread.h */
#include "hashpipe_thread.h"

static int init(struct hashpipe_thread_args *args)
{
    /* Attach to status shared mem area */
    THREAD_INIT_STATUS(args->instance_id, STATUS_KEY);

    /* Create paper_input_databuf */
    THREAD_INIT_DATABUF(args->instance_id, paper_input_databuf,
        args->input_buffer);

    /* Create paper_gpu_input_databuf */
    THREAD_INIT_DATABUF(args->instance_id, paper_gpu_input_databuf,
        args->output_buffer);

    // Success!
    return 0;
}

#define ELAPSED_NS(start,stop) \
  (((int64_t)stop.tv_sec-start.tv_sec)*1000*1000*1000+(stop.tv_nsec-start.tv_nsec))

static void *run(void * _args)
{
    // Cast _args
    struct hashpipe_thread_args *args = (struct hashpipe_thread_args *)_args;

#ifdef DEBUG_SEMS
    fprintf(stderr, "s/tid %lu/                      FLUFf/\n", pthread_self());
#endif

    THREAD_RUN_BEGIN(args);

    THREAD_RUN_SET_AFFINITY_PRIORITY(args);

    /* Attach to status shared mem area */
    THREAD_RUN_ATTACH_STATUS(args->instance_id, st);

    /* Attach to paper_input_databuf */
    THREAD_RUN_ATTACH_DATABUF(args->instance_id,
        paper_input_databuf, db_in, args->input_buffer);

    /* Attach to paper_gpu_input_databuf */
    THREAD_RUN_ATTACH_DATABUF(args->instance_id,
        paper_gpu_input_databuf, db_out, args->output_buffer);

    // Init status variables
    hashpipe_status_lock_safe(&st);
    hputi8(st.buf, "FLUFMCNT", 0);
    hashpipe_status_unlock_safe(&st);

    /* Loop */
    int rv;
    int curblock_in=0;
    int curblock_out=0;
    float gbps, min_gbps;

    struct timespec start, finish;

    while (run_threads()) {

        // Note waiting status,
        // query integrating status
        // and, if armed, start count
        hashpipe_status_lock_safe(&st);
        hputs(st.buf, STATUS_KEY, "waiting");
        hashpipe_status_unlock_safe(&st);

        // Wait for new input block to be filled
        while ((rv=paper_input_databuf_wait_filled(db_in, curblock_in)) != HASHPIPE_OK) {
            if (rv==HASHPIPE_TIMEOUT) {
                hashpipe_status_lock_safe(&st);
                hputs(st.buf, STATUS_KEY, "blocked_in");
                hashpipe_status_unlock_safe(&st);
                continue;
            } else {
                hashpipe_error(__FUNCTION__, "error waiting for filled databuf");
                clear_run_threads();
                pthread_exit(NULL);
                break;
            }
        }

        // Wait for new gpu_input block (our output block) to be free
        while ((rv=paper_gpu_input_databuf_wait_free(db_out, curblock_out)) != HASHPIPE_OK) {
            if (rv==HASHPIPE_TIMEOUT) {
                hashpipe_status_lock_safe(&st);
                hputs(st.buf, STATUS_KEY, "blocked gpu input");
                hashpipe_status_unlock_safe(&st);
                continue;
            } else {
                hashpipe_error(__FUNCTION__, "error waiting for free databuf");
                clear_run_threads();
                pthread_exit(NULL);
                break;
            }
        }

        // Got a new data block, update status
        hashpipe_status_lock_safe(&st);
        hputs(st.buf, STATUS_KEY, "fluffing");
        hputi4(st.buf, "FLUFBKIN", curblock_in);
        hputu8(st.buf, "FLUFMCNT", db_in->block[curblock_in].header.mcnt);
        hashpipe_status_unlock_safe(&st);

        // Copy header and call fluff function
        clock_gettime(CLOCK_MONOTONIC, &start);

        memcpy(&db_out->block[curblock_out].header, &db_in->block[curblock_in].header, sizeof(paper_input_header_t));

        paper_fluff(db_in->block[curblock_in].data, db_out->block[curblock_out].data);

        clock_gettime(CLOCK_MONOTONIC, &finish);

        // Note processing time
        hashpipe_status_lock_safe(&st);
        // Bits per fluff / ns per fluff = Gbps
        hgetr4(st.buf, "FLUFMING", &min_gbps);
        gbps = (float)(8*N_BYTES_PER_BLOCK)/ELAPSED_NS(start,finish);
        hputr4(st.buf, "FLUFGBPS", gbps);
        if(min_gbps == 0 || gbps < min_gbps) {
          hputr4(st.buf, "FLUFMING", gbps);
        }
        hashpipe_status_unlock_safe(&st);

        // Mark input block as free and advance
        paper_input_databuf_set_free(db_in, curblock_in);
        curblock_in = (curblock_in + 1) % db_in->header.n_block;

        // Mark output block as full and advance
        paper_gpu_input_databuf_set_filled(db_out, curblock_out);
        curblock_out = (curblock_out + 1) % db_out->header.n_block;

        /* Check for cancel */
        pthread_testcancel();
    }
    clear_run_threads();

    // Have to close all pushes
    THREAD_RUN_DETACH_DATAUF;
    THREAD_RUN_DETACH_DATAUF;
    THREAD_RUN_DETACH_STATUS;
    THREAD_RUN_END;

    // Thread success!
    return NULL;
}

static pipeline_thread_module_t module1 = {
    name: "paper_fluff_thread",
    type: PIPELINE_INOUT_THREAD,
    init: init,
    run:  run
};

static __attribute__((constructor)) void ctor()
{
  register_pipeline_thread_module(&module1);
}
