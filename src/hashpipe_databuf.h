/* hashpipe_databuf.h
 *
 * Defines shared mem structure for data passing.
 * Includes routines to allocate / attach to shared
 * memory.
 */
#ifndef _HASHPIPE_DATABUF_H
#define _HASHPIPE_DATABUF_H

#include <stdint.h>
#include <sys/ipc.h>
#include <sys/sem.h>

// Define hashpipe_databuf structure
typedef struct {
    char data_type[64]; /* Type of data in buffer */
    size_t header_size; /* Size of each block header (bytes) */
    size_t block_size;  /* Size of each data block (bytes) */
    int n_block;        /* Number of data blocks in buffer */
    int shmid;          /* ID of this shared mem segment */
    int semid;          /* ID of locking semaphore set */
} hashpipe_databuf_t;

/*
 * Get the base key to use for *all* hashpipe databufs.  The base key is
 * obtained by calling the ftok function, using the value of $HASHPIPE_KEYFILE,
 * if defined, or $HOME from the environment or, if $HOME is not defined, by
 * using "/tmp".  By default (i.e. no HASHPIPE_KEYFILE in the environment),
 * this will create and connect to a user specific set of shared memory buffers
 * (provided $HOME exists in the environment), but if desired users can connect
 * to any other set of memory buffers by setting HASHPIPE_KEYFILE
 * appropraitely.
 */
key_t hashpipe_databuf_key();

/* Create a new shared mem area with given params.  Returns pointer to the new
 * area on success, or NULL on error.  Returns error if an existing shmem area
 * exists with the given shmid and different sizing parameters.
 */
hashpipe_databuf_t *hashpipe_databuf_create(int instance_id,
        int databuf_id, size_t header_size, size_t block_size, int n_block);

/* Return a pointer to a existing shmem segment with given id.
 * Returns error if segment does not exist 
 */
hashpipe_databuf_t *hashpipe_databuf_attach(int instance_id, int databuf_id);

/* Detach from shared mem segment */
int hashpipe_databuf_detach(hashpipe_databuf_t *d);

/* Set all semaphores to 0, 
 * TODO: memset to 0 as well?
 */
void hashpipe_databuf_clear(hashpipe_databuf_t *d);

/* Returns pointer to the beginning of the given data block.
 */
char *hashpipe_databuf_data(hashpipe_databuf_t *d, int block_id);

/* Returns lock status for given block_id, or total for
 * whole array.
 */
int hashpipe_databuf_block_status(hashpipe_databuf_t *d, int block_id);
int hashpipe_databuf_total_status(hashpipe_databuf_t *d);
uint64_t hashpipe_databuf_total_mask(hashpipe_databuf_t *d);

/* Databuf locking functions.  Each block in the buffer
 * can be marked as free or filled.  The "wait" functions
 * block (i.e. sleep) until the specified state happens.
 * The "busywait" functions busy-wait (i.e. do NOT sleep)
 * until the specified state happens.  The "set" functions
 * put the buffer in the specified state, returning error if
 * it is already in that state.
 */
int hashpipe_databuf_wait_filled(hashpipe_databuf_t *d, int block_id);
int hashpipe_databuf_busywait_filled(hashpipe_databuf_t *d, int block_id);
int hashpipe_databuf_set_filled(hashpipe_databuf_t *d, int block_id);
int hashpipe_databuf_wait_free(hashpipe_databuf_t *d, int block_id);
int hashpipe_databuf_busywait_free(hashpipe_databuf_t *d, int block_id);
int hashpipe_databuf_set_free(hashpipe_databuf_t *d, int block_id);


#endif // _HASHPIPE_DATABUF_H
