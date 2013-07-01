/*
 * null_output_thread.c
 *
 * Routine to sink data from the end of a pipeline.  This is the hashpipe thread
 * analog of /dev/null for the output end of a pipeline.
 */

#define _GNU_SOURCE 1
#include <stdio.h>
#include <time.h>
#include <pthread.h>

#include "hashpipe.h"

static void *run(hashpipe_thread_args_t * args)
{
    hashpipe_databuf_t *db;
    hashpipe_status_t st = args->st;
    const char * status_key = args->thread_desc->skey;

    // Attach to databuf as a low-level hashpipe databuf.  Since
    // null_output_thread can attach to any kind of databuf, we cannot create
    // the upstream databuf if it does not yet exist.  We have to simply wait
    // for it to be created by the upstream thread.  Give up after 1 second.
    int i;
    struct timespec ts = {0, 1000}; // One microsecond
    int max_tries = 1000000; // One million microseconds
    for(i = 0; i < max_tries; i++) {
        db = hashpipe_databuf_attach(args->instance_id, args->input_buffer);
        if(db) break;
        nanosleep(&ts, NULL);
    }

    if(!db) {
        char msg[256];
        sprintf(msg, "Error attaching to databuf(%d) shared memory.",
                args->input_buffer);
        hashpipe_error(__FUNCTION__, msg);
        return THREAD_ERROR;
    }
    pthread_cleanup_push((void *)hashpipe_databuf_detach, db);

    /* Main loop */
    int rv;
    int block_idx = 0;
    while (run_threads()) {

        hashpipe_status_lock_safe(&st);
        hputs(st.buf, status_key, "waiting");
        hashpipe_status_unlock_safe(&st);

        // Wait for new block to be filled
        while ((rv=hashpipe_databuf_wait_filled(db, block_idx)) != HASHPIPE_OK) {
            if (rv==HASHPIPE_TIMEOUT) {
                hashpipe_status_lock_safe(&st);
                hputs(st.buf, status_key, "blocked");
                hashpipe_status_unlock_safe(&st);
                continue;
            } else {
                hashpipe_error(__FUNCTION__, "error waiting for filled databuf");
                pthread_exit(NULL);
                break;
            }
        }

        // Note processing status, current input block
        hashpipe_status_lock_safe(&st);
        hputs(st.buf, status_key, "processing");
        hputi4(st.buf, "NULBLKIN", block_idx);
        hashpipe_status_unlock_safe(&st);

        // Mark block as free
        hashpipe_databuf_set_free(db, block_idx);

        // Setup for next block
        block_idx = (block_idx + 1) % db->n_block;

        /* Will exit if thread has been cancelled */
        pthread_testcancel();
    }

    // Detach from databuf
    hashpipe_databuf_detach(db);
    pthread_cleanup_pop(0); // databuf detach

    // Thread success!
    return THREAD_OK;
}

static hashpipe_thread_desc_t null_thread = {
    name: "null_output_thread",
    skey: "NULLSTAT",
    init: NULL,
    run:  run,
    ibuf_desc: {NULL},
    obuf_desc: {NULL}
};

static __attribute__((constructor)) void ctor()
{
  register_hashpipe_thread(&null_thread);
}
