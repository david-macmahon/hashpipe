/* hashpipe_status.h
 *
 * Routines dealing with the guppi status shared memory
 * segment.  Info is passed through this segment using 
 * a FITS-like keyword=value syntax.
 */
#ifndef _HASHPIPE_STATUS_H
#define _HASHPIPE_STATUS_H

#include <limits.h>
#include <semaphore.h>

#define HASHPIPE_STATUS_SIZE (2880*64) // FITS-style buffer
#define HASHPIPE_STATUS_CARD 80 // Size of each FITS "card"

#define HASHPIPE_LOCK 1
#define HASHPIPE_NOLOCK 0

/* Structure describes status memory area */
struct hashpipe_status {
    int instance_id; /* Instance ID of this status buffer (DO NOT SET/CHANGE!) */
    int shmid;   /* Shared memory segment id */
    sem_t *lock; /* POSIX semaphore descriptor for locking */
    char *buf;   /* Pointer to data area */
};

/*
 * Stores the guppi status (POSIX) semaphore name in semid buffer of length
 * size.  Returns 0 (no error) if semaphore name fit in given size, returns 1
 * if semaphore name is truncated.
 *
 * The guppi status semaphore name is $HASHPIPE_STATUS_SEMNAME (if defined in
 * the environment) or ${HASHPIPE_KEYFILE}_hashpipe_status (if defined in the
 * environment) or ${HOME}_hashpipe_status (if defined in the environment) or
 * "/tmp_hashpipe_status" (global fallback).  Any slashes after the leading
 * slash are converted to underscores.
 */
int hashpipe_status_semname(int instance_id, char * semid, size_t size);

/*
 * Returns non-zero if the status buffer for instance_id already exists.
 */
int hashpipe_status_exists(int instance_id);

/* Return a pointer to the status shared mem area, 
 * creating it if it doesn't exist.  Attaches/creates 
 * lock semaphore as well.  Returns nonzero on error.
 */
int hashpipe_status_attach(int instance_id, struct hashpipe_status *s);

/* Detach from shared mem segment */
int hashpipe_status_detach(struct hashpipe_status *s); 

/* Lock/unlock the status buffer.  hashpipe_status_lock() will sleep while
 * waiting for the buffer to become unlocked.  hashpipe_status_lock_busywait
 * will busy-wait while waiting for the buffer to become unlocked.  Return
 * non-zero on errors.
 */
int hashpipe_status_lock(struct hashpipe_status *s);
int hashpipe_status_lock_busywait(struct hashpipe_status *s);
int hashpipe_status_unlock(struct hashpipe_status *s);

/* Check the buffer for appropriate formatting (existence of "END").
 * If not found, zero it out and add END.
 */
void hashpipe_status_chkinit(struct hashpipe_status *s);

/* Clear out whole buffer */
void hashpipe_status_clear(struct hashpipe_status *s);

#endif
