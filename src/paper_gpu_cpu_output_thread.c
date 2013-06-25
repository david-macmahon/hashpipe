/*
 * paper_gpu_cpu_output_thread.c
 *
 * Routine to sink data from the paper_gpu_cpu_thread.
 */

#define _GNU_SOURCE 1
#include <stdio.h>
#include <time.h>

#include <xgpu.h>

#include "hashpipe_error.h"
#include "paper_databuf.h"

#define STATUS_KEY "CGOUT"  /* Define before paper_thread.h */
#include "paper_thread.h"

#define TOL (1e-5)

static XGPUInfo xgpu_info;

static int init(struct guppi_thread_args *args)
{
    /* Attach to status shared mem area */
    THREAD_INIT_ATTACH_STATUS(args->instance_id, st, STATUS_KEY);

    hashpipe_status_lock_safe(&st);
    hputr4(st.buf, "CGMAXERR", 0.0);
    hputi4(st.buf, "CGERRCNT", 0);
    hputi4(st.buf, "CGMXERCT", 0);
    hashpipe_status_unlock_safe(&st);

    THREAD_INIT_DETACH_STATUS(st);

    // Create paper_ouput_databuf
    THREAD_INIT_DATABUF(args->instance_id, paper_output_databuf, args->input_buffer);

    // Success!
    return 0;
}

#define zabs(x,i) hypot(x[i],x[i+1])

static void *run(void * _args)
{
    // Cast _args
    struct guppi_thread_args *args = (struct guppi_thread_args *)_args;

    THREAD_RUN_BEGIN(args);

    THREAD_RUN_SET_AFFINITY_PRIORITY(args);

    THREAD_RUN_ATTACH_STATUS(args->instance_id, st);

    // Attach to paper_ouput_databuf
    THREAD_RUN_ATTACH_DATABUF(args->instance_id,
        paper_output_databuf, db, args->input_buffer);

    /* Main loop */
    int i, rv, debug=20;
    int block_idx[2] = {0, 1};
    int error_count, max_error_count = 0;
    float *gpu_data, *cpu_data;
    float error, max_error = 0.0;
    while (run_threads()) {

        hashpipe_status_lock_safe(&st);
        hputs(st.buf, STATUS_KEY, "waiting");
        hashpipe_status_unlock_safe(&st);

        // Wait for two new blocks to be filled
        for(i=0; i<2; i++) {
            while ((rv=paper_output_databuf_wait_filled(db, block_idx[i]))
                    != HASHPIPE_OK) {
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
        }

        // Note processing status, current input block
        hashpipe_status_lock_safe(&st);
        hputs(st.buf, STATUS_KEY, "processing");
        hputi4(st.buf, "CGBLKIN", block_idx[0]);
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
        hputr4(st.buf, "CGMAXERR", max_error);
        hputi4(st.buf, "CGERRCNT", error_count);
        hputi4(st.buf, "CGMXERCT", max_error_count);
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

    // Have to close all pushes
    pthread_cleanup_pop(0);
    THREAD_RUN_DETACH_STATUS;
    THREAD_RUN_END;

    // Thread success!
    return NULL;
}

static pipeline_thread_module_t module = {
    name: "paper_gpu_cpu_output_thread",
    type: PIPELINE_OUTPUT_THREAD,
    init: init,
    run:  run
};

static __attribute__((constructor)) void ctor()
{
  register_pipeline_thread_module(&module);
}
