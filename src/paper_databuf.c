/* paper_databuf.c
 *
 * Routines for creating and accessing main data transfer
 * buffer in shared memory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <errno.h>
#include <time.h>

#ifdef DEBUG_SEMS
#include <pthread.h>
static struct timespec start;
static struct timespec now;
#define ELAPSED_NS(stop) \
  (((int64_t)stop.tv_sec-start.tv_sec)*1000*1000*1000+(stop.tv_nsec-start.tv_nsec))

#define SEMLOG(pd, msg)                                        \
  do {                                                         \
    int orig_errno = errno;                                    \
    clock_gettime(CLOCK_MONOTONIC, &now);                      \
    fprintf(stderr, "%13ld tid %lu " msg " %d (%lx)\n",        \
        ELAPSED_NS(now), pthread_self(), block_id,             \
        hashpipe_databuf_total_mask((struct hashpipe_databuf *)pd)); \
    errno = orig_errno;                                        \
  } while(0)

#else
#define SEMLOG(pd, msg)
#endif // DEBUG_SEMS

#include "fitshead.h"
#include "hashpipe_status.h"
#include "paper_databuf.h"
#include "hashpipe_error.h"

/*
 * Since the first element of struct paper_input_databuf is a struct
 * hashpipe_databuf, a pointer to a struct paper_input_databuf is also a pointer
 * to a struct hashpipe_databuf.  This allows a pointer to a struct
 * paper_input_databuf to be passed, with appropriate casting, to functions
 * that accept a pointer to a struct hashpipe_databuf.  This allows the reuse of
 * many of the functions in hashpipe_databuf.c.  This is a form of inheritence: a
 * struct paper_input_databuf is a struct hashpipe_databuf (plus additional
 * specializations).
 *
 * Many of the functions in hashpipe_databuf.c accept a pointer to a struct
 * hashpipe_databuf, but unfortunately some of them have non-generic code or
 * parameters which render them unsuitable for general use.
 *
 * For hashpipe_databuf.c function...
 *
 *   hashpipe_databuf_xyzzy(struct hashpipe_databuf *d...)
 *
 * ...that is suitable for general use, a corresponding paper_databuf.c
 * function...
 *
 *   paper_input_databuf_xyzzy(struct paper_input_databuf *d...)
 *
 * ...can be created that passes its d parameter to hashpipe_databuf_xyzzy with
 * appropraite casting.  In some cases (e.g. hashpipe_databuf_attach), that's all
 * that's needed, but other cases may require additional functionality in the
 * paper_input_buffer function.
 *
 * hashpipe_databuf.c functions that are not suitable for general use will have
 * to be duplicated in a paper-specific way (i.e. without calling the
 * hashpipe_databuf version) if they are in fact relevent to paper_input_databuf.
 * Functions that are duplicated for this reason should have a brief comment
 * indicating why they are bgin duplicated rather than simply calling the
 * hashpipe_databuf.c equivalent.
 *
 * The same comments apply to struct paper_output_databuf.
 *
 */

/*
 * Create, if needed, and attach to paper_input_databuf shared memory.
 */
paper_input_databuf_t *paper_input_databuf_create(int instance_id, int databuf_id)
{
#ifdef DEBUG_SEMS
    // Init clock variables
    if(databuf_id==1) {
        clock_gettime(CLOCK_MONOTONIC, &start);
        now.tv_sec = start.tv_sec;
        now.tv_nsec = start.tv_nsec;
    }
#endif

    /* Calc databuf sizes */
    size_t header_size = sizeof(struct hashpipe_databuf)
                       + sizeof(hashpipe_databuf_cache_alignment);
    size_t block_size  = sizeof(paper_input_block_t);
    int    n_block = N_INPUT_BLOCKS + N_DEBUG_INPUT_BLOCKS;

    return (paper_input_databuf_t *)hashpipe_databuf_create(
        instance_id, databuf_id, header_size, block_size, n_block);
}

int paper_input_databuf_wait_free(struct paper_input_databuf *d, int block_id)
{
    int rv;
    SEMLOG(d, "wait free");
    rv = hashpipe_databuf_wait_free((struct hashpipe_databuf *)d, block_id);
    SEMLOG(d, "got  free");
    return rv;
}

int paper_input_databuf_busywait_free(struct paper_input_databuf *d, int block_id)
{
    int rv;
    SEMLOG(d, "busy-wait free");
    rv = hashpipe_databuf_busywait_free((struct hashpipe_databuf *)d, block_id);
    SEMLOG(d, "busy-got  free");
    return rv;
}

int paper_input_databuf_wait_filled(struct paper_input_databuf *d, int block_id)
{
    int rv;
    SEMLOG(d, "wait fill");
    rv = hashpipe_databuf_wait_filled((struct hashpipe_databuf *)d, block_id);
    SEMLOG(d, "got  fill");
    return rv;
}

int paper_input_databuf_busywait_filled(struct paper_input_databuf *d, int block_id)
{
    int rv;
    SEMLOG(d, "busy-wait fill");
    rv = hashpipe_databuf_busywait_filled((struct hashpipe_databuf *)d, block_id);
    SEMLOG(d, "busy-got  fill");
    return rv;
}

int paper_input_databuf_set_free(struct paper_input_databuf *d, int block_id)
{
    SEMLOG(d, "set  free");
    return hashpipe_databuf_set_free((struct hashpipe_databuf *)d, block_id);
}

int paper_input_databuf_set_filled(struct paper_input_databuf *d, int block_id)
{
    SEMLOG(d, "set  fill");
    return hashpipe_databuf_set_filled((struct hashpipe_databuf *)d, block_id);
}

paper_gpu_input_databuf_t *paper_gpu_input_databuf_create(int instance_id, int databuf_id)
{
#ifdef DEBUG_SEMS
    // Init clock variables
    if(databuf_id==1) {
        clock_gettime(CLOCK_MONOTONIC, &start);
        now.tv_sec = start.tv_sec;
        now.tv_nsec = start.tv_nsec;
    }
#endif

    /* Calc databuf sizes */
    size_t header_size = sizeof(struct hashpipe_databuf)
                       + sizeof(hashpipe_databuf_cache_alignment);
    size_t block_size  = sizeof(paper_gpu_input_block_t);
    int    n_block = N_GPU_INPUT_BLOCKS;

    return (paper_gpu_input_databuf_t *)hashpipe_databuf_create(
        instance_id, databuf_id, header_size, block_size, n_block);
}

struct paper_output_databuf *paper_output_databuf_create(int instance_id, int databuf_id)
{
    /* Calc databuf sizes */
    size_t header_size = sizeof(struct hashpipe_databuf)
                       + sizeof(hashpipe_databuf_cache_alignment);
    size_t block_size  = sizeof(paper_output_block_t);
    int    n_block = N_OUTPUT_BLOCKS;

    return (paper_output_databuf_t *)hashpipe_databuf_create(
        instance_id, databuf_id, header_size, block_size, n_block);
}
