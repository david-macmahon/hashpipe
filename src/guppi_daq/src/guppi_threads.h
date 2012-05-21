/* guppi_threads.h
 *
 * Definitions, routines common to 
 * all thread functions.
 */
#ifndef _GUPPI_THREADS_H
#define _GUPPI_THREADS_H

#include "fitshead.h"
#include "guppi_status.h"
#include "guppi_thread_args.h"

/* SIGINT handling capability */
extern int run_threads;
extern void cc(int sig);

/* Safe lock/unlock functions for status shared mem. */
#define guppi_status_lock_safe(s) \
    pthread_cleanup_push((void *)guppi_status_unlock, s); \
    guppi_status_lock(s);

#define guppi_status_lock_busywait_safe(s) \
    pthread_cleanup_push((void *)guppi_status_unlock, s); \
    guppi_status_lock_busywait(s);

#define guppi_status_unlock_safe(s) \
    guppi_status_unlock(s); \
    pthread_cleanup_pop(0);

/* Exit handler that updates status buffer */
#ifndef STATUS_KEY
#  define STATUS_KEY "XXXSTAT"
#  define TMP_STATUS_KEY 1
#endif
static void set_exit_status(struct guppi_status *s) {
    guppi_status_lock_busywait(s);
    hputs(s->buf, STATUS_KEY, "exiting");
    guppi_status_unlock(s);
}
#if TMP_STATUS_KEY
#  undef STATUS_KEY
#  undef TMP_STATUS_KEY
#endif

#endif
