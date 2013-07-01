/*
 * paper_gpu_cpu_output_thread.c
 *
 * Routine to sink data from the paper_gpu_cpu_thread.
 */

#define _GNU_SOURCE 1
#include <stdio.h>
#include <time.h>
#include <math.h>
#include <pthread.h>

#include <xgpu.h>

#include "hashpipe.h"
#include "paper_databuf.h"

#define TOL (1e-5)

static XGPUInfo xgpu_info;

static int init(hashpipe_thread_args_t *args)
{
    hashpipe_status_t st = args->st;

    hashpipe_status_lock_safe(&st);
    hputr4(st.buf, "CGOMXERR", 0.0);
    hputi4(st.buf, "CGOERCNT", 0);
    hputi4(st.buf, "CGOMXECT", 0);
    hashpipe_status_unlock_safe(&st);

    // Success!
    return 0;
}

#define zabs(x,i) hypot(x[i],x[i+1])

static void *run(hashpipe_thread_args_t * args)
{
    // Local aliases to shorten access to args fields
    // Our input buffer happens to be a paper_ouput_databuf
    paper_output_databuf_t *db = (paper_output_databuf_t *)args->ibuf;
    hashpipe_status_t st = args->st;
    const char * status_key = args->thread_desc->skey;

    /* Main loop */
    int i, rv, debug=20;
    int block_idx[2] = {0, 1};
    int error_count, max_error_count = 0;
    float *gpu_data, *cpu_data;
    float error, max_error = 0.0;
    while (run_threads()) {

        hashpipe_status_lock_safe(&st);
        hputs(st.buf, status_key, "waiting");
        hashpipe_status_unlock_safe(&st);

        // Wait for two new blocks to be filled
        for(i=0; i<2; i++) {
            while ((rv=paper_output_databuf_wait_filled(db, block_idx[i]))
                    != HASHPIPE_OK) {
                if (rv==HASHPIPE_TIMEOUT) {
                    hashpipe_status_lock_safe(&st);
                    hputs(st.buf, status_key, "blocked");
                    hashpipe_status_unlock_safe(&st);
                    continue;
                } else {
                    hashpipe_error(__FUNCTION__, "error waiting for filled databuf");
                    clear_run_threads();
                    pthread_exit(NULL);
                    break;
                }
            }
        }

        // Note processing status, current input block
        hashpipe_status_lock_safe(&st);
        hputs(st.buf, status_key, "processing");
        hputi4(st.buf, "CGOBLKIN", block_idx[0]);
        hashpipe_status_unlock_safe(&st);

        // Reorder GPU block
        if(debug==20) {
          fprintf(stderr, "GPU block in == %d\n", block_idx[0]);
          fprintf(stderr, "CPU block in == %d\n", block_idx[1]);
        }
        xgpuReorderMatrix((Complex *)db->block[block_idx[0]].data);

        // Compare blocks
        error_count = 0.0;
        gpu_data = db->block[block_idx[0]].data;
        cpu_data = db->block[block_idx[1]].data;
        for(i=0; i<xgpu_info.matLength; i+=2) {
            if(zabs(cpu_data,i) == 0) {
                error = zabs(gpu_data,i);
            } else {
                error = hypotf(gpu_data[i]-cpu_data[i], gpu_data[i+1]-cpu_data[i+1]);
                error /= zabs(cpu_data,i);
            }
            if(error > 3*TOL) {
                error_count++;
                if(debug) {
                    fprintf(stderr,
                        "%3d: GPU:(%+4e, %+4e) CPU:(%+4e, %+4e) err %+4e\n", i,
                        gpu_data[i], gpu_data[i+1],
                        cpu_data[i], cpu_data[i+1],
                        error
                        );
                    debug--;
                }
            }
            if(error > max_error) {
                max_error = error;
            }
        }
        if(error_count > max_error_count) {
            max_error_count = error_count;
        }

        // Update status values
        hashpipe_status_lock_safe(&st);
        hputr4(st.buf, "CGOMXERR", max_error);
        hputi4(st.buf, "CGOERCNT", error_count);
        hputi4(st.buf, "CGOMXECT", max_error_count);
        hashpipe_status_unlock_safe(&st);

        // Mark blocks as free
        for(i=0; i<2; i++) {
            paper_output_databuf_set_free(db, block_idx[i]);
        }

        // Setup for next block
        for(i=0; i<2; i++) {
            block_idx[i] = (block_idx[i] + 2) % db->header.n_block;
        }

        /* Will exit if thread has been cancelled */
        pthread_testcancel();
    }

    // Thread success!
    return NULL;
}

static hashpipe_thread_desc_t gpu_cpu_output_thread = {
    name: "paper_gpu_cpu_output_thread",
    skey: "CGOSTAT",
    init: init,
    run:  run,
    ibuf_desc: {paper_output_databuf_create},
    obuf_desc: {NULL}
};

static __attribute__((constructor)) void ctor()
{
  register_hashpipe_thread(&gpu_cpu_output_thread);
}
