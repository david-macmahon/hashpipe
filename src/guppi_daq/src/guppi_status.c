/* guppi_status.c
 *
 * Implementation of the status routines described 
 * in guppi_status.h
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <errno.h>

#include "guppi_ipckey.h"
#include "guppi_status.h"
#include "guppi_error.h"
#include "fitshead.h"

// TODO Do '#include "guppi_threads.h"' instead?
extern int run_threads;

/* Returns the guppi status (POSIX) semaphore name. */
const char * guppi_status_semname(int instance_id)
{
    static char semid[NAME_MAX-4] = {'\0'};
    int length_remaining = NAME_MAX-4;
    char *s;
    // Lazy init
    if(semid[0] == '\0') {
        const char * envstr = getenv("GUPPI_STATUS_SEMNAME");
        if(envstr) {
            strncpy(semid, envstr, length_remaining);
            semid[length_remaining-1] = '\0';
        } else {
            envstr = getenv("GUPPI_KEYFILE");
            if(!envstr) {
                envstr = getenv("HOME");
                if(!envstr) {
                    envstr = "/tmp";
                }
            }
            strncpy(semid, envstr, length_remaining);
            semid[length_remaining-1] = '\0';
            // Convert all but the leading / to _
            s = semid + 1;
            while((s = strchr(s, '/'))) {
              *s = '_';
            }
            length_remaining -= strlen(semid);
            if(length_remaining > 0) {
                snprintf(semid+strlen(semid), length_remaining,
                    "_guppi_status_%d", instance_id&0x3f);
            }
        }
#ifdef GUPPI_VERBOSE
        fprintf(stderr, "using guppi status semaphore '%s'\n", semid);
#endif
    }
    return semid;
}

int guppi_status_attach(int instance_id, struct guppi_status *s)
{
    instance_id &= 0x3f;
    s->instance_id = instance_id;

    /* Get shared mem id (creating it if necessary) */
    key_t key = guppi_status_key(instance_id);
    if(key == GUPPI_KEY_ERROR) {
        guppi_error("guppi_status_attach", "guppi_status_key error");
        return(0);
    }
    s->shmid = shmget(key, GUPPI_STATUS_SIZE, 0666 | IPC_CREAT);
    if (s->shmid==-1) { 
        guppi_error("guppi_status_attach", "shmget error");
        return(GUPPI_ERR_SYS);
    }

    /* Now attach to the segment */
    s->buf = shmat(s->shmid, NULL, 0);
    if (s->buf == (void *)-1) {
        perror("shmat");
        printf("shmid=%d\n", s->shmid);
        guppi_error("guppi_status_attach", "shmat error");
        return(GUPPI_ERR_SYS);
    }

    /* Get the locking semaphore.
     * Final arg (1) means create in unlocked state (0=locked).
     */
    mode_t old_umask = umask(0);
    s->lock = sem_open(guppi_status_semname(instance_id), O_CREAT, 0666, 1);
    umask(old_umask);
    if (s->lock==SEM_FAILED) {
        guppi_error("guppi_status_attach", "sem_open");
        return(GUPPI_ERR_SYS);
    }

    /* Init buffer if needed */
    guppi_status_chkinit(s);

    return(GUPPI_OK);
}

int guppi_status_detach(struct guppi_status *s) {
    int rv = shmdt(s->buf);
    if (rv!=0) {
        guppi_error("guppi_status_detach", "shmdt error");
        return(GUPPI_ERR_SYS);
    }
    s->buf = NULL;
    return(GUPPI_OK);
}

/* TODO: put in some (long, ~few sec) timeout */
int guppi_status_lock(struct guppi_status *s) {
    return(sem_wait(s->lock));
}

/* TODO: put in some (long, ~few sec) timeout */
int guppi_status_lock_busywait(struct guppi_status *s) {
    int rv;
    do {
      rv = sem_trywait(s->lock);
    } while (rv == -1 && errno == EAGAIN && run_threads);
    return rv;
}

int guppi_status_unlock(struct guppi_status *s) {
    return(sem_post(s->lock));
}

/* Return pointer to END key */
char *guppi_find_end(char *buf) {
    /* Loop over 80 byte cards */
    int offs;
    char *out=NULL;
    for (offs=0; offs<GUPPI_STATUS_SIZE; offs+=GUPPI_STATUS_CARD) {
        if (strncmp(&buf[offs], "END", 3)==0) { out=&buf[offs]; break; }
    }
    return(out);
}

/* So far, just checks for existence of "END" in the proper spot */
void guppi_status_chkinit(struct guppi_status *s)
{
    int instance_id = -1;

    /* Lock */
    guppi_status_lock(s);

    /* If no END, clear it out */
    if (guppi_find_end(s->buf)==NULL) {
        /* Zero bufer */
        memset(s->buf, 0, GUPPI_STATUS_SIZE);
        /* Fill first card w/ spaces */
        memset(s->buf, ' ', GUPPI_STATUS_CARD);
        /* add END */
        strncpy(s->buf, "END", 3);
        // Add INSTANCE record
        hputi4(s->buf, "INSTANCE", s->instance_id);
    } else {
        // Check INSTANCE record
        if(!hgeti4(s->buf, "INSTANCE", &instance_id)) {
            // No INSTANCE record, so add one
            hputi4(s->buf, "INSTANCE", s->instance_id);
        } else if(instance_id != s->instance_id) {
            // Print warning message
            fprintf(stderr,
                "Existing INSTANCE value %d != desired value %d\n",
                instance_id, s->instance_id);
            // Fix it (Really?  Why did this condition exist anyway?)
            hputi4(s->buf, "INSTANCE", s->instance_id);
        }
    }

    /* Unlock */
    guppi_status_unlock(s);
}

/* Clear out guppi status buf */
void guppi_status_clear(struct guppi_status *s) {

    /* Lock */
    guppi_status_lock(s);

    /* Zero bufer */
    memset(s->buf, 0, GUPPI_STATUS_SIZE);
    /* Fill first card w/ spaces */
    memset(s->buf, ' ', GUPPI_STATUS_CARD);
    /* add END */
    strncpy(s->buf, "END", 3);

    hputi4(s->buf, "INSTANCE", s->instance_id);

    /* Unlock */
    guppi_status_unlock(s);
}
