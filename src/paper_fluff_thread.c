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

#include "hashpipe.h"
#include "paper_databuf.h"
#include "paper_fluff.h"

#define ELAPSED_NS(start,stop) \
  (((int64_t)stop.tv_sec-start.tv_sec)*1000*1000*1000+(stop.tv_nsec-start.tv_nsec))

static void *run(hashpipe_thread_args_t * args)
{
    // Local aliases to shorten access to args fields
    // Our input buffer is a paper_input_databuf
    // Our output buffer is a paper_gpu_input_databuf
    paper_input_databuf_t *db_in = (paper_input_databuf_t *)args->ibuf;
    paper_gpu_input_databuf_t *db_out = (paper_gpu_input_databuf_t *)args->obuf;
    hashpipe_status_t st = args->st;
    const char * status_key = args->thread_desc->skey;

#ifdef DEBUG_SEMS
    fprintf(stderr, "s/tid %lu/                      FLUFf/\n", pthread_self());
#endif

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
        hputs(st.buf, status_key, "waiting");
        hashpipe_status_unlock_safe(&st);

        // Wait for new input block to be filled
        while ((rv=paper_input_databuf_wait_filled(db_in, curblock_in)) != HASHPIPE_OK) {
            if (rv==HASHPIPE_TIMEOUT) {
                hashpipe_status_lock_safe(&st);
                hputs(st.buf, status_key, "blocked_in");
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
                hputs(st.buf, status_key, "blocked gpu input");
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
        hputs(st.buf, status_key, "fluffing");
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

    // Thread success!
    return NULL;
}

static hashpipe_thread_desc_t fluff_thread = {
    name: "paper_fluff_thread",
    skey: "FLUFSTAT",
    init: NULL,
    run:  run,
    ibuf_desc: {paper_input_databuf_create},
    obuf_desc: {paper_gpu_input_databuf_create}
};

static __attribute__((constructor)) void ctor()
{
  register_hashpipe_thread(&fluff_thread);
}
