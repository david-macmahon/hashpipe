/*
 * null_output_thread.c
 *
 * Routine to sink data from the end of a pipeline.  This is the thread module
 * analog of /dev/null for the output end of a pipeline.
 */

#define _GNU_SOURCE 1
#include <stdio.h>
#include <time.h>

#include "guppi_error.h"
#include "guppi_databuf.h"

#define STATUS_KEY "NULLOUT"  /* Define before guppi_threads.h */
#include "guppi_threads.h"
#include "paper_thread.h"

static int init(struct guppi_thread_args *args)
{
    /* Attach to status shared mem area */
    THREAD_INIT_STATUS(args->instance_id, STATUS_KEY);

    // Success!
    return 0;
}

static void *run(void * _args)
{
    // Cast _args
    struct guppi_thread_args *args = (struct guppi_thread_args *)_args;

    THREAD_RUN_BEGIN(args);

    THREAD_RUN_SET_AFFINITY_PRIORITY(args);

    THREAD_RUN_ATTACH_STATUS(args->instance_id, st);

    // Attach to databuf as a low-level guppi_databuf.  Since
    // null_output_thread can attach to any kind of databuf, we cannot create
    // the upstream databuf if it does not yet exist.  We have to simply wait
    // for it to be created by the upstream thread.  Give up after 1 second.
    int i;
    struct timespec ts = {0, 1000}; // One microsecond
    int max_tries = 1000000; // One million microseconds
    struct guppi_databuf *db;
    for(i = 0; i < max_tries; i++) {
        db = guppi_databuf_attach(args->instance_id, args->input_buffer);
        if(db) break;
        nanosleep(&ts, NULL);
    }

    if(!db) {
        char msg[256];
        sprintf(msg, "Error attaching to databuf(%d) shared memory.",
                args->input_buffer);
        guppi_error(__FUNCTION__, msg);
        return THREAD_ERROR;
    }
    pthread_cleanup_push((void *)guppi_databuf_detach, db);


    /* Main loop */
    int rv;
    int block_idx = 0;
    while (run_threads) {

        guppi_status_lock_safe(&st);
        hputs(st.buf, STATUS_KEY, "waiting");
        guppi_status_unlock_safe(&st);

        // Wait for new block to be filled
        while ((rv=guppi_databuf_wait_filled(db, block_idx)) != GUPPI_OK) {
            if (rv==GUPPI_TIMEOUT) {
                guppi_status_lock_safe(&st);
                hputs(st.buf, STATUS_KEY, "blocked");
                guppi_status_unlock_safe(&st);
                continue;
            } else {
                guppi_error(__FUNCTION__, "error waiting for filled databuf");
                run_threads=0;
                pthread_exit(NULL);
                break;
            }
        }

        // Note processing status, current input block
        guppi_status_lock_safe(&st);
        hputs(st.buf, STATUS_KEY, "processing");
        hputi4(st.buf, "NULBLKIN", block_idx);
        guppi_status_unlock_safe(&st);

        // Mark block as free
        guppi_databuf_set_free(db, block_idx);

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
