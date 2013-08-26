/* hashpipe_status.c
 *
 * Implementation of the status routines described 
 * in hashpipe_status.h
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <errno.h>

#include "hashpipe_ipckey.h"
#include "hashpipe_status.h"
#include "hashpipe_error.h"
#include "fitshead.h"

/*
 * Stores the hashpipe status (POSIX) semaphore name in semid buffer of length
 * size.  Returns 0 (no error) if semaphore name fit in given size, returns 1
 * if semaphore name is truncated.
 */
int hashpipe_status_semname(int instance_id, char * semid, size_t size)
{
    //static char semid[NAME_MAX-4] = {'\0'};
    size_t length_remaining = size;
    int bytes_written;
    char *s;
    int rc = 1;

    const char * envstr = getenv("HASHPIPE_STATUS_SEMNAME");
    if(envstr) {
        strncpy(semid, envstr, length_remaining);
        semid[length_remaining-1] = '\0';
    } else {
        envstr = getenv("HASHPIPE_KEYFILE");
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
            bytes_written = snprintf(semid+strlen(semid),
                length_remaining, "_hashpipe_status_%d", instance_id&0x3f);
            if(bytes_written < length_remaining) {
              // No truncation
              rc = 0;
            }
        }
    }
#ifdef HASHPIPE_VERBOSE
    fprintf(stderr, "using hashpipe status semaphore '%s'\n", semid);
#endif
    return rc;
}

int hashpipe_status_exists(int instance_id)
{
    instance_id &= 0x3f;

    /* Compute status buffer key for instance_id */
    key_t key = hashpipe_status_key(instance_id);
    if(key == HASHPIPE_KEY_ERROR) {
        hashpipe_error("hashpipe_status_attach", "hashpipe_status_key error");
        return 0;
    }
    int shmid = shmget(key, HASHPIPE_STATUS_TOTAL_SIZE, 0666);
    return (shmid==-1) ? 0 : 1;
}

int hashpipe_status_attach(int instance_id, hashpipe_status_t *s)
{
    char semid[NAME_MAX] = {'\0'};
    instance_id &= 0x3f;
    s->instance_id = instance_id;

    /* Get shared mem id (creating it if necessary) */
    key_t key = hashpipe_status_key(instance_id);
    if(key == HASHPIPE_KEY_ERROR) {
        hashpipe_error("hashpipe_status_attach", "hashpipe_status_key error");
        return(0);
    }
    s->shmid = shmget(key, HASHPIPE_STATUS_TOTAL_SIZE, 0666 | IPC_CREAT);
    if (s->shmid==-1) { 
        hashpipe_error("hashpipe_status_attach", "shmget error");
        return(HASHPIPE_ERR_SYS);
    }

    /* Now attach to the segment */
    s->buf = shmat(s->shmid, NULL, 0);
    if (s->buf == (void *)-1) {
        perror("shmat");
        printf("shmid=%d\n", s->shmid);
        hashpipe_error("hashpipe_status_attach", "shmat error");
        return(HASHPIPE_ERR_SYS);
    }

    /*
     * Get the semaphore name.  Return error on truncation.
     */
    if(hashpipe_status_semname(instance_id, semid, NAME_MAX)) {
        hashpipe_error("hashpipe_status_attach", "semname truncated");
        return(HASHPIPE_ERR_SYS);
    }

    /* Get the locking semaphore.
     * Final arg (1) means create in unlocked state (0=locked).
     */
    mode_t old_umask = umask(0);
    s->lock = sem_open(semid, O_CREAT, 0666, 1);
    umask(old_umask);
    if (s->lock==SEM_FAILED) {
        hashpipe_error("hashpipe_status_attach", "sem_open");
        return(HASHPIPE_ERR_SYS);
    }

    /* Init buffer if needed */
    hashpipe_status_chkinit(s);

    return(HASHPIPE_OK);
}

int hashpipe_status_detach(hashpipe_status_t *s) {
    if(s && s->buf) {
      int rv = shmdt(s->buf);
      if (rv!=0) {
          hashpipe_error("hashpipe_status_detach", "shmdt error");
          return HASHPIPE_ERR_SYS;
      }
      s->buf = NULL;
    }
    return HASHPIPE_OK;
}

/* TODO: put in some (long, ~few sec) timeout */
int hashpipe_status_lock(hashpipe_status_t *s) {
    return(sem_wait(s->lock));
}

/* TODO: put in some (long, ~few sec) timeout */
int hashpipe_status_lock_busywait(hashpipe_status_t *s) {
    int rv;
    do {
      rv = sem_trywait(s->lock);
    } while (rv == -1 && errno == EAGAIN);
    return rv;
}

int hashpipe_status_unlock(hashpipe_status_t *s) {
    return(sem_post(s->lock));
}

/* Return pointer to END key */
static
char *hashpipe_find_end(char *buf) {
    /* Loop over fixed size records */
    int offs;
    char *out=NULL;
    for (offs=0; offs<HASHPIPE_STATUS_TOTAL_SIZE; offs+=HASHPIPE_STATUS_RECORD_SIZE) {
        if (strncmp(&buf[offs], "END", 3)==0) { out=&buf[offs]; break; }
    }
    return(out);
}

/* So far, just checks for existence of "END" in the proper spot */
void hashpipe_status_chkinit(hashpipe_status_t *s)
{
    int instance_id = -1;

    /* Lock */
    hashpipe_status_lock(s);

    /* If no END, clear it out */
    if (hashpipe_find_end(s->buf)==NULL) {
        /* Zero bufer */
        memset(s->buf, 0, HASHPIPE_STATUS_TOTAL_SIZE);
        /* Fill first record w/ spaces */
        memset(s->buf, ' ', HASHPIPE_STATUS_RECORD_SIZE);
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
    hashpipe_status_unlock(s);
}

/* Clear out hashpipe status buf */
void hashpipe_status_clear(hashpipe_status_t *s) {

    /* Lock */
    hashpipe_status_lock(s);

    /* Zero bufer */
    memset(s->buf, 0, HASHPIPE_STATUS_TOTAL_SIZE);
    /* Fill first record w/ spaces */
    memset(s->buf, ' ', HASHPIPE_STATUS_RECORD_SIZE);
    /* add END */
    strncpy(s->buf, "END", 3);

    hputi4(s->buf, "INSTANCE", s->instance_id);

    /* Unlock */
    hashpipe_status_unlock(s);
}
