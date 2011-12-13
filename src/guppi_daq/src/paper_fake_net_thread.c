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

/* This thread is passed a single arg, pointer
 * to the guppi_udp_params struct.  This thread should 
 * be cancelled and restarted if any hardware params
 * change, as this potentially affects packet size, etc.
 */
void *paper_fake_net_thread(void *_args) {

    /* Get arguments */
    struct guppi_thread_args *args = (struct guppi_thread_args *)_args;

    /* Set cpu affinity */
    cpu_set_t cpuset, cpuset_orig;
    sched_getaffinity(0, sizeof(cpu_set_t), &cpuset_orig);
    CPU_ZERO(&cpuset);
    //CPU_SET(2, &cpuset);
    CPU_SET(3, &cpuset);
    int rv = sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
    if (rv<0) { 
        guppi_error(__FUNCTION__, "Error setting cpu affinity.");
        perror("sched_setaffinity");
    }

    /* Set priority */
    rv = setpriority(PRIO_PROCESS, 0, args->priority);
    if (rv<0) {
        guppi_error(__FUNCTION__, "Error setting priority level.");
        perror("set_priority");
    }

    /* Attach to status shared mem area */
    struct guppi_status st;
    rv = guppi_status_attach(&st);
    if (rv!=GUPPI_OK) {
        guppi_error(__FUNCTION__, "Error attaching to status shared memory.");
        pthread_exit(NULL);
    }
    pthread_cleanup_push((void *)guppi_status_detach, &st);
    pthread_cleanup_push((void *)set_exit_status, &st);

    /* Init status, read info */
    guppi_status_lock_safe(&st);
    hputs(st.buf, STATUS_KEY, "init");
    guppi_status_unlock_safe(&st);

    /* Attach to databuf shared mem */
    struct paper_input_databuf *db;
    db = paper_input_databuf_attach(args->output_buffer); 
    if (db==NULL) {
        guppi_error(__FUNCTION__, "Error attaching to databuf shared memory.");
        pthread_exit(NULL);
    }
    pthread_cleanup_push((void *)paper_input_databuf_detach, db);

    /* Main loop */
    int i;
    uint64_t mcnt = 0;
    int block_idx = 0;
    signal(SIGINT,cc);
    signal(SIGTERM,cc);
    while (run_threads) {

        guppi_status_lock_safe(&st);
        hputs(st.buf, STATUS_KEY, "waiting");
        guppi_status_unlock_safe(&st);
 
        /* Wait for data */
        struct timespec sleep_dur, rem_sleep_dur;
        sleep_dur.tv_sec = 0;
        sleep_dur.tv_nsec = 2e6; // TODO replace with correct value
        nanosleep(&sleep_dur, &rem_sleep_dur);
	
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

        // Zero out block
        memset(&(db->block[block_idx]), 0, sizeof(paper_input_block_t));

        guppi_status_lock_safe(&st);
        hputs(st.buf, STATUS_KEY, "receiving");
        guppi_status_unlock_safe(&st);
 
        // Fill in sub-block headers
        for(i=0; i<N_SUB_BLOCKS_PER_INPUT_BLOCK; i++) {
          db->block[block_idx].header[i].mcnt = mcnt;
          db->block[block_idx].header[i].chan_present[0] = -1;
          db->block[block_idx].header[i].chan_present[1] = -1;
          mcnt += N_CHAN;
        }

        // Fill in random data
        xgpuRandomComplex((ComplexInput *)db->block[block_idx].sub_block,
            N_SUB_BLOCKS_PER_INPUT_BLOCK*sizeof(paper_input_sub_block_t)/sizeof(ComplexInput));

        // Mark block as full
        paper_input_databuf_set_filled(db, block_idx);

        // Setup for next block
        block_idx = (block_idx + 1) % db->header.n_block;

        /* Will exit if thread has been cancelled */
        pthread_testcancel();
    }

    pthread_exit(NULL);

    /* Have to close all push's */
    pthread_cleanup_pop(0); /* Closes set_exit_status */
    pthread_cleanup_pop(0); /* Closes guppi_status_detach */
    pthread_cleanup_pop(0); /* Closes paper_input_databuf_detach */
}
