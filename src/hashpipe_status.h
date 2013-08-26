/* hashpipe_status.h
 *
 * Routines dealing with the hashpipe status shared memory
 * segment.  Info is passed through this segment using
 * a FITS-like keyword=value syntax.
 */
#ifndef _HASHPIPE_STATUS_H
#define _HASHPIPE_STATUS_H

#include <semaphore.h>

// fitshead.h does not need to be included here, but it is likely to be
// replaced at some point in the future so including it here hides it from the
// client.
#include "fitshead.h"

#define HASHPIPE_STATUS_TOTAL_SIZE (2880*64) // FITS-style buffer
#define HASHPIPE_STATUS_RECORD_SIZE 80 // Size of each record (e.g. FITS "card")

/* Structure describes status memory area */
typedef struct {
    int instance_id; /* Instance ID of this status buffer (DO NOT SET/CHANGE!) */
    int shmid;   /* Shared memory segment id */
    sem_t *lock; /* POSIX semaphore descriptor for locking */
    char *buf;   /* Pointer to data area */
} hashpipe_status_t;

/*
 * Stores the hashpipe status (POSIX) semaphore name in semid buffer of length
 * size.  Returns 0 (no error) if semaphore name fit in given size, returns 1
 * if semaphore name is truncated.
 *
 * The hashpipe status semaphore name is $HASHPIPE_STATUS_SEMNAME (if defined
 * in the environment) or ${HASHPIPE_KEYFILE}_hashpipe_status (if defined in
 * the environment) or ${HOME}_hashpipe_status (if defined in the environment)
 * or "/tmp_hashpipe_status" (global fallback).  Any slashes after the leading
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
int hashpipe_status_attach(int instance_id, hashpipe_status_t *s);

/* Detach from shared mem segment */
int hashpipe_status_detach(hashpipe_status_t *s);

/* Lock/unlock the status buffer.  hashpipe_status_lock() will sleep while
 * waiting for the buffer to become unlocked.  hashpipe_status_lock_busywait
 * will busy-wait while waiting for the buffer to become unlocked.  Return
 * non-zero on errors.
 */
int hashpipe_status_lock(hashpipe_status_t *s);
int hashpipe_status_lock_busywait(hashpipe_status_t *s);
int hashpipe_status_unlock(hashpipe_status_t *s);

/* Check the buffer for appropriate formatting (existence of "END").
 * If not found, zero it out and add END.
 */
void hashpipe_status_chkinit(hashpipe_status_t *s);

/* Clear out whole buffer */
void hashpipe_status_clear(hashpipe_status_t *s);

// Thread-safe lock/unlock macros for status buffer used to ensure that the
// status buffer is not left in a locked state.  Each hashpipe_status_lock_safe
// or hashpipe_status_lock_busywait_safe must be paired with a
// hashpipe_status_unlock_safe in the same function and at the same lexical
// nesting level.  See "man pthread_cleanup_push" for more details.
//
// NOTE: This header file does not include pthread.h where pthread_cleanup_push
//       and pthread_cleanup_pop are defined.  Users of the macros defined here
//       must explicitly include pthread.h themselves.
#define hashpipe_status_lock_safe(s) \
    pthread_cleanup_push((void *)hashpipe_status_unlock, s); \
    hashpipe_status_lock(s);

#define hashpipe_status_lock_busywait_safe(s) \
    pthread_cleanup_push((void *)hashpipe_status_unlock, s); \
    hashpipe_status_lock_busywait(s);

#define hashpipe_status_unlock_safe(s) \
    hashpipe_status_unlock(s); \
    pthread_cleanup_pop(0);

#endif
