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
#include <sys/resource.h> 

#include "fitshead.h"
#include "guppi_ipckey.h"
#include "guppi_status.h"
#include "paper_databuf.h"
#include "guppi_error.h"

/*
 * Since the first element of struct paper_input_databuf is a struct
 * guppi_databuf, a pointer to a struct paper_input_databuf is also a pointer
 * to a struct guppi_databuf.  This allows a pointer to a struct
 * paper_input_databuf to be passed, with appropriate casting, to functions
 * that accept a pointer to a struct guppi_databuf.  This allows the reuse of
 * many of the functions in guppi_databuf.c.  This is a form of inheritence: a
 * struct paper_input_databuf is a struct guppi_databuf (plus additional
 * specializations).
 *
 * Many of the functions in guppi_databuf.c accept a pointer to a struct
 * guppi_databuf, but unfortunately some of them have VEGAS-specific code or
 * parameters which render them unsuitable for general use.
 *
 * For guppi_databuf.c function...
 *
 *   guppi_databuf_xyzzy(struct guppi_databuf *d...)
 *
 * ...that is suitable for general use, a corresponding paper_databuf.c
 * function...
 *
 *   paper_input_databuf_xyzzy(struct paper_input_databuf *d...)
 *
 * ...can be created that passes its d parameter to guppi_databuf_xyzzy with
 * appropraite casting.  In some cases (e.g. guppi_databuf_attach), that's all
 * that's needed, but other cases may require additional functionality in the
 * paper_input_buffer function.
 *
 * guppi_databuf.c functions that are not suitable for general use will have
 * to be duplicated in a paper-specific way (i.e. without calling the
 * guppi_databuf version) if they are in fact relevent to paper_input_databuf.
 * Functions that are duplicated for this reason should have a brief comment
 * indicating why they are bgin duplicated rather than simply calling the
 * guppi_databuf.c equivalent.
 *
 * The same comments apply to struct paper_output_databuf.
 *
 */

/*
 * guppi_databuf_create is non-general.  Instead of n_block and block_size, it
 * should take a single overall size since in the general case it cannot know
 * what size to allocate.  It also performs some VEGAS specific initialization
 * which we do not replicate here.
 *
 * paper_input_databuf_create has different behavior if the databuf to be
 * created already exists.  Instead of erroring out immediately, it will attach
 * to the existing buffer and chek the sizes of the buffer with the sizes
 * passed in.  If they match, a pointer to the existing databuf is returned.
 * It the sizes do not match, NULL is returned (i.e. it is an error if the
 * databuf exists but the sizes do not match).
 */
struct paper_input_databuf *paper_input_databuf_create(int n_block, size_t block_size,
        int databuf_id)
{
    int rv = 0;
    int init_buffer = 1;

    /* Calc databuf size */
    size_t paper_input_databuf_size = sizeof(paper_input_databuf_t);
    struct rlimit rlim;
    getrlimit(RLIMIT_MEMLOCK, &rlim);
    if(rlim.rlim_cur < paper_input_databuf_size) {
	printf("Incresing RLIMIT_MEMLOCK from %lu to %lu for paper_input_databuf. Hard limit is %lu.\n", 
		rlim.rlim_cur, paper_input_databuf_size, rlim.rlim_max);
	rlim.rlim_cur = paper_input_databuf_size;
	rv = setrlimit(RLIMIT_MEMLOCK, &rlim);
	if(rv) {
		perror("setrlimit");
        	guppi_error(__FUNCTION__, "Error increasing RLIMIT_MEMLOCK");
	}
    }

    /* Get shared memory block */
    key_t key = guppi_databuf_key();
    if(key == GUPPI_KEY_ERROR) {
        guppi_error(__FUNCTION__, "guppi_databuf_key error");
        return(NULL);
    }
    int shmid;
    shmid = shmget(key + databuf_id - 1, paper_input_databuf_size, 0666 | IPC_CREAT | IPC_EXCL);
    if (shmid==-1 && errno == EEXIST) {
        // Already exists, call shmget again without IPC_CREAT
        shmid = shmget(key + databuf_id - 1, paper_input_databuf_size, 0666);
        // Do not init buffer
        init_buffer = 0;
    }
    if (shmid==-1) {
        perror("shmget");
        guppi_error(__FUNCTION__, "shmget error");
        return(NULL);
    }

    /* Attach */
    struct guppi_databuf *d;
    d = shmat(shmid, NULL, 0);
    if (d==(void *)-1) {
        guppi_error(__FUNCTION__, "shmat error");
        return(NULL);
    }

    if(!init_buffer) {
        // Make sure existing sizes match expectaions
        if(d->n_block != n_block || d->block_size != block_size) {
            guppi_error(__FUNCTION__, "existing databuf size mismatch");
            if(shmdt(d)) {
                guppi_error(__FUNCTION__, "shmdt error");
            }
            return(NULL);
        }
        return (struct paper_input_databuf *)d;
    }

    /* Try to lock in memory */
    rv = shmctl(shmid, SHM_LOCK, NULL);
    if (rv==-1) {
        perror("shmctl");
        guppi_error(__FUNCTION__, "Error locking shared memory.");
    }

    /* Zero out memory */
    memset(d, 0, paper_input_databuf_size);

    /* Fill params into databuf */
    d->shmid = shmid;
    d->semid = 0;
    d->n_block = n_block;
    d->block_size = block_size;

    /* Get semaphores set up */
    d->semid = semget(key + databuf_id - 1, n_block, 0666 | IPC_CREAT);
    if (d->semid==-1) { 
        guppi_error(__FUNCTION__, "semget error");
        return(NULL);
    }

    /* Init semaphores to 0 */
    union semun arg;
    arg.array = (unsigned short *)malloc(sizeof(unsigned short)*n_block);
    memset(arg.array, 0, sizeof(unsigned short)*n_block);
    rv = semctl(d->semid, 0, SETALL, arg);
    free(arg.array);

    return (struct paper_input_databuf *)d;
}

struct paper_input_databuf *paper_input_databuf_attach(int databuf_id)
{
    return (struct paper_input_databuf *)guppi_databuf_attach(databuf_id);
}

/* Mimicking guppi_databuf's "detach" mispelling. */
int paper_input_databuf_detach(struct paper_input_databuf *d)
{
    return guppi_databuf_detach((struct guppi_databuf *)d);
}

/*
 * guppi_databuf_clear() does some VEGAS specific stuff so we have to duplicate
 * its non-VEGAS functionality here.
 */
void paper_input_databuf_clear(struct paper_input_databuf *d)
{
    struct guppi_databuf *g = (struct guppi_databuf *)d;

    /* Zero out semaphores */
    union semun arg;
    arg.array = (unsigned short *)malloc(sizeof(unsigned short)*g->n_block);
    memset(arg.array, 0, sizeof(unsigned short)*g->n_block);
    semctl(g->semid, 0, SETALL, arg);
    free(arg.array);

    // TODO memset to 0?
}

int paper_input_databuf_block_status(struct paper_input_databuf *d, int block_id)
{
    return guppi_databuf_block_status((struct guppi_databuf *)d, block_id);
}

int paper_input_databuf_total_status(struct paper_input_databuf *d)
{
    return guppi_databuf_total_status((struct guppi_databuf *)d);
}

int paper_input_databuf_wait_free(struct paper_input_databuf *d, int block_id)
{
    return guppi_databuf_wait_free((struct guppi_databuf *)d, block_id);
}

int paper_input_databuf_wait_filled(struct paper_input_databuf *d, int block_id)
{
    return guppi_databuf_wait_filled((struct guppi_databuf *)d, block_id);
}

int paper_input_databuf_set_free(struct paper_input_databuf *d, int block_id)
{
    return guppi_databuf_set_free((struct guppi_databuf *)d, block_id);
}

int paper_input_databuf_set_filled(struct paper_input_databuf *d, int block_id)
{
    return guppi_databuf_set_filled((struct guppi_databuf *)d, block_id);
}

/*
 * guppi_databuf_create is non-general.  Instead of n_block and block_size, it
 * should take a single overall size since in the general case it cannot know
 * what size to allocate.  It also performs some VEGAS specific initialization
 * which we do not replicate here.
 *
 * paper_output_databuf_create has different behavior if the databuf to be
 * created already exists.  Instead of erroring out immediately, it will attach
 * to the existing buffer and chek the sizes of the buffer with the sizes
 * passed in.  If they match, a pointer to the existing databuf is returned.
 * It the sizes do not match, NULL is returned (i.e. it is an error if the
 * databuf exists but the sizes do not match).
 */
struct paper_output_databuf *paper_output_databuf_create(int n_block, size_t block_size,
        int databuf_id)
{
    int init_buffer = 1;

    if(block_size != N_OUTPUT_MATRIX*sizeof(float)) {
        char msg[256];
        sprintf(msg, "block_size %lu != expected block size of %lu",
                block_size, N_OUTPUT_MATRIX*sizeof(float));
        guppi_error(__FUNCTION__, msg);
        return(NULL);
    }
    /* Calc databuf size */
    size_t paper_output_block_size = sizeof(paper_output_block_t);
printf("paper_output_block_size %lu\n", paper_output_block_size);
    size_t paper_output_databuf_size = sizeof(paper_output_databuf_t);
printf("paper_output_databuf_size %lu\n", paper_output_databuf_size);
    size_t databuf_size = paper_output_databuf_size +
                          paper_output_block_size * n_block;
printf("databuf_size %lu\n", databuf_size);

    /* Get shared memory block, error if it already exists */
    key_t key = guppi_databuf_key();
    if(key == GUPPI_KEY_ERROR) {
        guppi_error(__FUNCTION__, "guppi_databuf_key error");
        return(NULL);
    }
    int shmid;
    shmid = shmget(key + databuf_id - 1, databuf_size, 0666 | IPC_CREAT | IPC_EXCL);
    if (shmid==-1 && errno == EEXIST) {
        // Already exists, call shmget again without IPC_CREAT
        shmid = shmget(key + databuf_id - 1, databuf_size, 0666);
        // Do not init buffer
        init_buffer = 0;
    }
    if (shmid==-1) {
        perror("shmget");
        guppi_error(__FUNCTION__, "shmget error");
        return(NULL);
    }

    /* Attach */
    struct guppi_databuf *d;
    d = shmat(shmid, NULL, 0);
    if (d==(void *)-1) {
        guppi_error(__FUNCTION__, "shmat error");
        return(NULL);
    }

    if(!init_buffer) {
        // Make sure existing sizes match expectaions
        if(d->n_block != n_block || d->block_size != block_size) {
            guppi_error(__FUNCTION__, "existing databuf size mismatch");
            if(shmdt(d)) {
                guppi_error(__FUNCTION__, "shmdt error");
            }
            return(NULL);
        }
        return (struct paper_output_databuf *)d;
    }

    /* Try to lock in memory */
    int rv = shmctl(shmid, SHM_LOCK, NULL);
    if (rv==-1) {
        perror("shmctl");
        guppi_error(__FUNCTION__, "Error locking shared memory.");
    }

    /* Zero out memory */
    memset(d, 0, databuf_size);

    /* Fill params into databuf */
    d->shmid = shmid;
    d->semid = 0;
    d->n_block = n_block;
    d->block_size = block_size;

    /* Get semaphores set up */
    d->semid = semget(key + databuf_id - 1, n_block, 0666 | IPC_CREAT);
    if (d->semid==-1) {
        guppi_error(__FUNCTION__, "semget error");
        return(NULL);
    }

    /* Init semaphores to 0 */
    union semun arg;
    arg.array = (unsigned short *)malloc(sizeof(unsigned short)*n_block);
    memset(arg.array, 0, sizeof(unsigned short)*n_block);
    rv = semctl(d->semid, 0, SETALL, arg);
    free(arg.array);

    return (struct paper_output_databuf *)d;
}

struct paper_output_databuf *paper_output_databuf_attach(int databuf_id)
{
    return (struct paper_output_databuf *)guppi_databuf_attach(databuf_id);
}

/* Mimicking guppi_databuf's "detach" mispelling. */
int paper_output_databuf_detach(struct paper_output_databuf *d)
{
    return guppi_databuf_detach((struct guppi_databuf *)d);
}

/*
 * guppi_databuf_clear() does some VEGAS specific stuff so we have to duplicate
 * its non-VEGAS functionality here.
 */
void paper_output_databuf_clear(struct paper_output_databuf *d)
{
    struct guppi_databuf *g = (struct guppi_databuf *)d;

    /* Zero out semaphores */
    union semun arg;
    arg.array = (unsigned short *)malloc(sizeof(unsigned short)*g->n_block);
    memset(arg.array, 0, sizeof(unsigned short)*g->n_block);
    semctl(g->semid, 0, SETALL, arg);
    free(arg.array);

    // TODO memset to 0?
}

int paper_output_databuf_block_status(struct paper_output_databuf *d, int block_id)
{
    return guppi_databuf_block_status((struct guppi_databuf *)d, block_id);
}

int paper_output_databuf_total_status(struct paper_output_databuf *d)
{
    return guppi_databuf_total_status((struct guppi_databuf *)d);
}

int paper_output_databuf_wait_free(struct paper_output_databuf *d, int block_id)
{
    return guppi_databuf_wait_free((struct guppi_databuf *)d, block_id);
}

int paper_output_databuf_wait_filled(struct paper_output_databuf *d, int block_id)
{
    return guppi_databuf_wait_filled((struct guppi_databuf *)d, block_id);
}

int paper_output_databuf_set_free(struct paper_output_databuf *d, int block_id)
{
    return guppi_databuf_set_free((struct guppi_databuf *)d, block_id);
}

int paper_output_databuf_set_filled(struct paper_output_databuf *d, int block_id)
{
    return guppi_databuf_set_filled((struct guppi_databuf *)d, block_id);
}
