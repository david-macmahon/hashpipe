/*
 * null_output_thread.c
 *
 * Routine to sink data from the end of a pipeline.  This is the thread module
 * analog of /dev/null for the output end of a pipeline.
 */

#define _GNU_SOURCE 1
#include <stdio.h>
#include <time.h>

#include "hashpipe_error.h"
#include "hashpipe_databuf.h"

#define STATUS_KEY "NULLOUT"  /* Define before paper_thread.h */
#include "paper_thread.h"

static int init(struct hashpipe_thread_args *args)
{
    /* Attach to status shared mem area */
    THREAD_INIT_STATUS(args->instance_id, STATUS_KEY);

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

    // Attach to databuf as a low-level hashpipe_databuf.  Since
    // null_output_thread can attach to any kind of databuf, we cannot create
    // the upstream databuf if it does not yet exist.  We have to simply wait
    // for it to be created by the upstream thread.  Give up after 1 second.
    int i;
    struct timespec ts = {0, 1000}; // One microsecond
    int max_tries = 1000000; // One million microseconds
    struct hashpipe_databuf *db;
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
        hputs(st.buf, STATUS_KEY, "waiting");
        hashpipe_status_unlock_safe(&st);

        // Wait for new block to be filled
        while ((rv=hashpipe_databuf_wait_filled(db, block_idx)) != HASHPIPE_OK) {
            if (rv==HASHPIPE_TIMEOUT) {
                hashpipe_status_lock_safe(&st);
                hputs(st.buf, STATUS_KEY, "blocked");
                hashpipe_status_unlock_safe(&st);
                continue;
            } else {
                hashpipe_error(__FUNCTION__, "error waiting for filled databuf");
                clear_run_threads();
                pthread_exit(NULL);
                break;
            }
        }

        // Note processing status, current input block
        hashpipe_status_lock_safe(&st);
        hputs(st.buf, STATUS_KEY, "processing");
        hputi4(st.buf, "NULBLKIN", block_idx);
        hashpipe_status_unlock_safe(&st);

        // Mark block as free
        hashpipe_databuf_set_free(db, block_idx);

        // Setup for next block
        block_idx = (block_idx + 1) % db->n_block;

        /* Will exit if thread has been cancelled */
        pthread_testcancel();
    }

    // Have to close all pushes
    pthread_cleanup_pop(0);
    THREAD_RUN_DETACH_STATUS;
    THREAD_RUN_END;

    // Thread success!
    return NULL;
}

static pipeline_thread_module_t module = {
    name: "null_output_thread",
    type: PIPELINE_OUTPUT_THREAD,
    init: init,
    run:  run
};

static __attribute__((constructor)) void ctor()
{
  register_pipeline_thread_module(&module);
}
