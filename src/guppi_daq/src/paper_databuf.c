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
 * what size to allocate.
 */
struct paper_input_databuf *paper_input_databuf_create(int n_block, size_t block_size,
        int databuf_id, int buf_type)
{

    /* Calc databuf size */
    size_t guppi_databuf_header_size = sizeof(struct guppi_databuf);
printf("guppi_databuf_header_size %lu\n", guppi_databuf_header_size);
    size_t paper_input_header_size = sizeof(paper_input_header_t);    
printf("paper_input_header_size %lu\n", paper_input_header_size);
    size_t paper_input_block_size = sizeof(paper_input_block_t);
printf("paper_input_block_size %lu\n", paper_input_block_size);
    size_t paper_input_databuf_size = sizeof(paper_input_databuf_t);
printf("paper_input_databuf_size %lu\n", paper_input_databuf_size);
      size_t databuf_size = paper_input_databuf_size + 
                            (paper_input_block_size + N_SUB_BLOCKS_PER_INPUT_BLOCK * block_size) * n_block;
printf("databuf_size %lu\n", databuf_size);
//exit(1);	// debug exit

    /* Get shared memory block, error if it already exists */
    int shmid;
    shmid = shmget(GUPPI_DATABUF_KEY + databuf_id - 1, 
            databuf_size, 0666 | IPC_CREAT | IPC_EXCL);
    if (shmid==-1) {
	perror("guppi_databuf_create()");
        guppi_error("guppi_databuf_create", "shmget error");
        return(NULL);
    }

    /* Attach */
    struct guppi_databuf *d;
    d = shmat(shmid, NULL, 0);
    if (d==(void *)-1) {
        guppi_error("guppi_databuf_create", "shmat error");
        return(NULL);
    }

    /* Try to lock in memory */
    int rv = shmctl(shmid, SHM_LOCK, NULL);
    if (rv==-1) {
	printf("errno %d\n", errno);
        guppi_error("guppi_databuf_create", "Error locking shared memory.");
        perror("shmctl");
    }

    /* Zero out memory */
    memset(d, 0, databuf_size);

    /* Fill params into databuf */
    int i;
    char end_key[81];
    memset(end_key, ' ', 80);
    strncpy(end_key, "END", 3);
    end_key[80]='\0';
    d->shmid = shmid;
    d->semid = 0;
    d->n_block = n_block;
    //d->struct_size = struct_size;
    d->block_size = block_size;
    //d->header_size = header_size;
    //d->index_size = index_size;
    //sprintf(d->data_type, "unknown");
    //d->buf_type = buf_type;

    for (i=0; i<n_block; i++) { 
        memcpy(guppi_databuf_header(d,i), end_key, 80); 
    }

    /* Get semaphores set up */
    d->semid = semget(GUPPI_DATABUF_KEY + databuf_id - 1, 
            n_block, 0666 | IPC_CREAT);
    if (d->semid==-1) { 
        guppi_error("guppi_databuf_create", "semget error");
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
 */
struct paper_output_databuf *paper_output_databuf_create(int n_block, size_t block_size,
        int databuf_id)
{

    /* Calc databuf size */
    size_t paper_output_block_size = sizeof(paper_output_block_t);
printf("paper_output_block_size %lu\n", paper_output_block_size);
    size_t paper_output_databuf_size = sizeof(paper_output_databuf_t);
printf("paper_output_databuf_size %lu\n", paper_output_databuf_size);
    size_t databuf_size = paper_output_databuf_size +
                            (paper_output_block_size + block_size) * n_block;
printf("databuf_size %lu\n", databuf_size);

    /* Get shared memory block, error if it already exists */
    int shmid;
    shmid = shmget(GUPPI_DATABUF_KEY + databuf_id - 1,
            databuf_size, 0666 | IPC_CREAT | IPC_EXCL);
    if (shmid==-1) {
        perror("guppi_databuf_create()");
        guppi_error("guppi_databuf_create", "shmget error");
        return(NULL);
    }

    /* Attach */
    struct guppi_databuf *d;
    d = shmat(shmid, NULL, 0);
    if (d==(void *)-1) {
        guppi_error("guppi_databuf_create", "shmat error");
        return(NULL);
    }

    /* Try to lock in memory */
    int rv = shmctl(shmid, SHM_LOCK, NULL);
    if (rv==-1) {
        printf("errno %d\n", errno);
        guppi_error("guppi_databuf_create", "Error locking shared memory.");
        perror("shmctl");
    }

    /* Zero out memory */
    memset(d, 0, databuf_size);

    /* Fill params into databuf */
    d->shmid = shmid;
    d->semid = 0;
    d->n_block = n_block;
    //d->struct_size = struct_size;
    d->block_size = block_size;
    //d->header_size = header_size;
    //d->index_size = index_size;
    //sprintf(d->data_type, "unknown");
    //d->buf_type = buf_type;

    /* Get semaphores set up */
    d->semid = semget(GUPPI_DATABUF_KEY + databuf_id - 1,
            n_block, 0666 | IPC_CREAT);
    if (d->semid==-1) {
        guppi_error("guppi_databuf_create", "semget error");
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
