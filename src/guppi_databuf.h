/* guppi_databuf.h
 *
 * Defines shared mem structure for data passing.
 * Includes routines to allocate / attach to shared
 * memory.
 */
#ifndef _GUPPI_DATABUF_H
#define _GUPPI_DATABUF_H

#include <stdint.h>
#include <sys/ipc.h>
#include <sys/sem.h>

struct guppi_databuf {
    char data_type[64]; /* Type of data in buffer */
    size_t struct_size; /* Size alloced for this struct (bytes) */
    size_t block_size;  /* Size of each data block (bytes) */
    size_t header_size; /* Size of each block header (bytes) */
    size_t index_size;  /* Size of each block's index (bytes) */
    int shmid;          /* ID of this shared mem segment */
    int semid;          /* ID of locking semaphore set */
    int n_block;        /* Number of data blocks in buffer */
};

/* union for semaphore ops.  Is this really needed? */
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
    struct seminfo *__buf;
};

/*
 * Get the base key to use for *all* guppi databufs.  The base key is obtained
 * by calling the ftok function, using the value of $GUPPI_KEYFILE, if defined,
 * or $HOME from the environment or, if $HOME is not defined, by using "/tmp".
 * By default (i.e. no GUPPI_KEYFILE in the environment), this will create and
 * connect to a user specific set of shared memory buffers (provided $HOME
 * exists in the environment), but if desired users can connect to any other
 * set of memory buffers by setting GUPPI_KEYFILE appropraitely.
 */
key_t guppi_databuf_key();

/* Create a new shared mem area with given params.  Returns 
 * pointer to the new area on success, or NULL on error.  Returns
 * error if an existing shmem area exists with the given shmid (or
 * if other errors occured trying to allocate it).
 */
struct guppi_databuf *guppi_databuf_create(int instance_id, int n_block, size_t block_size,
        int databuf_id);

/* Return a pointer to a existing shmem segment with given id.
 * Returns error if segment does not exist 
 */
struct guppi_databuf *guppi_databuf_attach(int instance_id, int databuf_id);

/* Detach from shared mem segment */
int guppi_databuf_detach(struct guppi_databuf *d);

/* Clear out either the whole databuf (set all sems to 0, 
 * clear all header blocks) or a single FITS-style
 * header block.
 */
void guppi_databuf_clear(struct guppi_databuf *d);
void guppi_fitsbuf_clear(char *buf);

/* Returns pointer to the beginning of the given data block.
 */
char *guppi_databuf_data(struct guppi_databuf *d, int block_id);

/* Returns lock status for given block_id, or total for
 * whole array.
 */
int guppi_databuf_block_status(struct guppi_databuf *d, int block_id);
int guppi_databuf_total_status(struct guppi_databuf *d);
uint64_t guppi_databuf_total_mask(struct guppi_databuf *d);

/* Databuf locking functions.  Each block in the buffer
 * can be marked as free or filled.  The "wait" functions
 * block (i.e. sleep) until the specified state happens.
 * The "busywait" functions busy-wait (i.e. do NOT sleep)
 * until the specified state happens.  The "set" functions
 * put the buffer in the specified state, returning error if
 * it is already in that state.
 */
int guppi_databuf_wait_filled(struct guppi_databuf *d, int block_id);
int guppi_databuf_busywait_filled(struct guppi_databuf *d, int block_id);
int guppi_databuf_set_filled(struct guppi_databuf *d, int block_id);
int guppi_databuf_wait_free(struct guppi_databuf *d, int block_id);
int guppi_databuf_busywait_free(struct guppi_databuf *d, int block_id);
int guppi_databuf_set_free(struct guppi_databuf *d, int block_id);


#endif
