/* hashpipe_status.h
 *
 * Routines dealing with the guppi status shared memory
 * segment.  Info is passed through this segment using 
 * a FITS-like keyword=value syntax.
 */
#ifndef _GUPPI_STATUS_H
#define _GUPPI_STATUS_H

#include <limits.h>
#include <semaphore.h>

#define GUPPI_STATUS_SIZE (2880*64) // FITS-style buffer
#define GUPPI_STATUS_CARD 80 // Size of each FITS "card"

#define GUPPI_LOCK 1
#define GUPPI_NOLOCK 0

/* Structure describes status memory area */
struct guppi_status {
    int instance_id; /* Instance ID of this status buffer (DO NOT SET/CHANGE!) */
    int shmid;   /* Shared memory segment id */
    sem_t *lock; /* POSIX semaphore descriptor for locking */
    char *buf;   /* Pointer to data area */
};

/*
 * Returns the guppi status (POSIX) semaphore name.
 *
 * The guppi status semaphore name is $GUPPI_STATUS_SEMNAME (if defined in the
 * environment) or ${GUPPI_KEYFILE}_guppi_status (if defined in the environment)
 * or ${HOME}_guppi_status (if defined in the environment) or "/tmp_guppi_status"
 * (global fallback).  Any slashes after the leading slash are converted to
 * underscores.
 */
const char * guppi_status_semname(int instance_id);

/* Return a pointer to the status shared mem area, 
 * creating it if it doesn't exist.  Attaches/creates 
 * lock semaphore as well.  Returns nonzero on error.
 */
int guppi_status_attach(int instance_id, struct guppi_status *s);

/* Detach from shared mem segment */
int guppi_status_detach(struct guppi_status *s); 

/* Lock/unlock the status buffer.  guppi_status_lock() will sleep while waiting
 * for the buffer to become unlocked.  guppi_status_lock_busywait will
 * busy-wait while waiting for the buffer to become unlocked.  Return non-zero
 * on errors.
 */
int guppi_status_lock(struct guppi_status *s);
int guppi_status_lock_busywait(struct guppi_status *s);
int guppi_status_unlock(struct guppi_status *s);

/* Check the buffer for appropriate formatting (existence of "END").
 * If not found, zero it out and add END.
 */
void guppi_status_chkinit(struct guppi_status *s);

/* Clear out whole buffer */
void guppi_status_clear(struct guppi_status *s);

#endif
