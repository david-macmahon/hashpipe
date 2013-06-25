/* guppi_databuf.c
 *
 * Routines for creating and accessing main data transfer
 * buffer in shared memory.
 */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <errno.h>
#include <time.h>

#include "fitshead.h"
#include "guppi_ipckey.h"
#include "hashpipe_status.h"
#include "guppi_databuf.h"
#include "guppi_error.h"

struct guppi_databuf *guppi_databuf_create(int instance_id,
        int databuf_id, size_t header_size, size_t block_size, int n_block)
{
    int rv = 0;
    int verify_sizing = 0;
    size_t total_size = header_size + block_size*n_block;

    if(header_size < sizeof(struct guppi_databuf)) {
        guppi_error(__FUNCTION__, "header size must be larger than %lu",
            sizeof(struct guppi_databuf));
        return NULL;
    }

    /* Get shared memory block */
    key_t key = guppi_databuf_key(instance_id);
    if(key == GUPPI_KEY_ERROR) {
        guppi_error(__FUNCTION__, "guppi_databuf_key error");
        return NULL;
    }
    int shmid;
    shmid = shmget(key + databuf_id - 1, total_size, 0666 | IPC_CREAT | IPC_EXCL);
    if (shmid==-1 && errno == EEXIST) {
        // Already exists, call shmget again without IPC_CREAT
        shmid = shmget(key + databuf_id - 1, total_size, 0666);
        // Verify buffer sizing
        verify_sizing = 1;
    }
    if (shmid==-1) {
        perror("shmget");
        guppi_error(__FUNCTION__, "shmget error");
        return NULL;
    }

    /* Attach */
    struct guppi_databuf *d;
    d = shmat(shmid, NULL, 0);
    if (d==(void *)-1) {
        guppi_error(__FUNCTION__, "shmat error");
        return NULL;
    }

    if(verify_sizing) {
        // Make sure existing sizes match expectaions
        if(d->header_size != header_size
        || d->block_size != block_size
        || d->n_block != n_block) {
            char msg[256];
            sprintf(msg, "existing databuf size mismatch "
                "(%lu + %lu x %d) != (%lu + %ld x %d)",
                d->header_size, d->block_size, d->n_block,
                header_size, block_size, n_block);
            guppi_error(__FUNCTION__, msg);
            if(shmdt(d)) {
                guppi_error(__FUNCTION__, "shmdt error");
            }
            return NULL;
        }
    }

    /* Try to lock in memory */
    rv = shmctl(shmid, SHM_LOCK, NULL);
    if (rv==-1) {
        perror("shmctl");
        guppi_error(__FUNCTION__, "Error locking shared memory.");
        return NULL;
    }

    /* Zero out memory */
    memset(d, 0, total_size);

    /* Fill params into databuf */
    d->shmid = shmid;
    d->semid = 0;
    d->header_size = header_size;
    d->n_block = n_block;
    d->block_size = block_size;
    sprintf(d->data_type, "unknown");

    /* Get semaphores set up */
    d->semid = semget(key + databuf_id - 1, n_block, 0666 | IPC_CREAT);
    if (d->semid==-1) { 
        guppi_error(__FUNCTION__, "semget error");
        return NULL;
    }

    /* Init semaphores to 0 */
    union semun arg;
    arg.array = (unsigned short *)malloc(sizeof(unsigned short)*n_block);
    memset(arg.array, 0, sizeof(unsigned short)*n_block);
    rv = semctl(d->semid, 0, SETALL, arg);
    if (rv==-1) {
        perror("semctl");
        guppi_error(__FUNCTION__, "Error clearing semaphores.");
        free(arg.array);
        return NULL;
    }
    free(arg.array);

    return d;
}

int guppi_databuf_detach(struct guppi_databuf *d)
{
    int rv = shmdt(d);
    if (rv!=0) {
        guppi_error(__FUNCTION__, "shmdt error");
        return GUPPI_ERR_SYS;
    }
    return GUPPI_OK;
}

void guppi_databuf_clear(struct guppi_databuf *d)
{
    /* Zero out semaphores */
    union semun arg;
    arg.array = (unsigned short *)malloc(sizeof(unsigned short)*d->n_block);
    memset(arg.array, 0, sizeof(unsigned short)*d->n_block);
    semctl(d->semid, 0, SETALL, arg);
    free(arg.array);

    // TODO memset to 0?
}

char *guppi_databuf_data(struct guppi_databuf *d, int block_id)
{
    return (char *)d + d->header_size + d->block_size*block_id;
}

struct guppi_databuf *guppi_databuf_attach(int instance_id, int databuf_id)
{

    /* Get shmid */
    key_t key = guppi_databuf_key(instance_id);
    if(key == GUPPI_KEY_ERROR) {
        guppi_error(__FUNCTION__, "guppi_databuf_key error");
        return NULL;
    }
    int shmid;
    shmid = shmget(key + databuf_id - 1, 0, 0666);
    if (shmid==-1) {
        // Doesn't exist, exit quietly otherwise complain
        if (errno!=ENOENT)
            guppi_error(__FUNCTION__, "shmget error");
        return NULL;
    }

    /* Attach */
    struct guppi_databuf *d;
    d = shmat(shmid, NULL, 0);
    if (d==(void *)-1) {
        guppi_error(__FUNCTION__, "shmat error");
        return NULL;
    }

    return d;

}

int guppi_databuf_block_status(struct guppi_databuf *d, int block_id)
{
    return semctl(d->semid, block_id, GETVAL);
}

int guppi_databuf_total_status(struct guppi_databuf *d)
{

    /* Get all values at once */
    union semun arg;
    arg.array = (unsigned short *)malloc(sizeof(unsigned short)*d->n_block);
    memset(arg.array, 0, sizeof(unsigned short)*d->n_block);
    semctl(d->semid, 0, GETALL, arg);
    int i,tot=0;
    for (i=0; i<d->n_block; i++) tot+=arg.array[i];
    free(arg.array);
    return tot;

}

uint64_t guppi_databuf_total_mask(struct guppi_databuf *d)
{

    /* Get all values at once */
    union semun arg;
    arg.array = (unsigned short *)malloc(sizeof(unsigned short)*d->n_block);
    memset(arg.array, 0, sizeof(unsigned short)*d->n_block);
    semctl(d->semid, 0, GETALL, arg);
    int i;
    int n = d->n_block;
    if(n>64) n = 64;
    uint64_t tot=0;
    for (i=0; i<n; i++) {
      if(arg.array[i]) {
        tot |= (1<<i);
      }
    }
    free(arg.array);
    return tot;
}

int guppi_databuf_wait_free(struct guppi_databuf *d, int block_id)
{
    int rv;
    struct sembuf op;
    op.sem_num = block_id;
    op.sem_op = 0;
    op.sem_flg = 0;
    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = 250000000;
    rv = semtimedop(d->semid, &op, 1, &timeout);
    if (rv==-1) {
        if (errno==EAGAIN) {
#ifdef GUPPI_TRACE
            printf("%s(%p, %d) timeout (%016lx)\n",
                __FUNCTION__, d, block_id, guppi_databuf_total_mask(d));
#endif
            return GUPPI_TIMEOUT;
        }
        // Don't complain on a signal interruption
        if (errno==EINTR) return GUPPI_ERR_SYS;
        guppi_error(__FUNCTION__, "semop error");
        perror("semop");
        return GUPPI_ERR_SYS;
    }
    return 0;
}

int guppi_databuf_busywait_free(struct guppi_databuf *d, int block_id)
{
    int rv;
    struct sembuf op;
    op.sem_num = block_id;
    op.sem_op = 0;
    op.sem_flg = IPC_NOWAIT;
    //struct timespec timeout;
    //timeout.tv_sec = 0;
    //timeout.tv_nsec = 250000000;
    do {
      rv = semop(d->semid, &op, 1);
    } while(rv == -1 && errno == EAGAIN);
    if (rv==-1) { 
        // Don't complain on a signal interruption
        if (errno==EINTR) return GUPPI_ERR_SYS;
        guppi_error(__FUNCTION__, "semop error");
        perror("semop");
        return GUPPI_ERR_SYS;
    }
    return 0;
}

int guppi_databuf_wait_filled(struct guppi_databuf *d, int block_id)
{
    /* This needs to wait for the semval of the given block
     * to become > 0, but NOT immediately decrement it to 0.
     * Probably do this by giving an array of semops, since
     * (afaik) the whole array happens atomically:
     * step 1: wait for val=1 then decrement (semop=-1)
     * step 2: increment by 1 (semop=1)
     */
    int rv;
    struct sembuf op[2];
    op[0].sem_num = op[1].sem_num = block_id;
    op[0].sem_flg = op[1].sem_flg = 0;
    op[0].sem_op = -1;
    op[1].sem_op = 1;
    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = 250000000;
    rv = semtimedop(d->semid, op, 2, &timeout);
    if (rv==-1) {
        if (errno==EAGAIN) return GUPPI_TIMEOUT;
        // Don't complain on a signal interruption
        if (errno==EINTR) return GUPPI_ERR_SYS;
        guppi_error(__FUNCTION__, "semop error");
        perror("semop");
        return GUPPI_ERR_SYS;
    }
    return 0;
}

int guppi_databuf_busywait_filled(struct guppi_databuf *d, int block_id)
{
    /* This needs to wait for the semval of the given block
     * to become > 0, but NOT immediately decrement it to 0.
     * Probably do this by giving an array of semops, since
     * (afaik) the whole array happens atomically:
     * step 1: wait for val=1 then decrement (semop=-1)
     * step 2: increment by 1 (semop=1)
     */
    int rv;
    struct sembuf op[2];
    op[0].sem_num = op[1].sem_num = block_id;
    op[0].sem_flg = IPC_NOWAIT;
    op[1].sem_flg = IPC_NOWAIT;
    op[0].sem_op = -1;
    op[1].sem_op = 1;
    //struct timespec timeout;
    //timeout.tv_sec = 0;
    //timeout.tv_nsec = 250000000;
    do {
      rv = semop(d->semid, op, 2);
    } while(rv == -1 && errno == EAGAIN);
    if (rv==-1) { 
        // Don't complain on a signal interruption
        if (errno==EINTR) return GUPPI_ERR_SYS;
        guppi_error(__FUNCTION__, "semop error");
        perror("semop");
        return GUPPI_ERR_SYS;
    }
    return 0;
}

int guppi_databuf_set_free(struct guppi_databuf *d, int block_id)
{
    /* This function should always succeed regardless of the current
     * state of the specified databuf.  So we use semctl (not semop) to set
     * the value to zero.
     */
    int rv;
    union semun arg;
    arg.val = 0;
    rv = semctl(d->semid, block_id, SETVAL, arg);
#ifdef GUPPI_TRACE
    printf("after %s(%p, %d) %016lx\n",
        __FUNCTION__, d, block_id, guppi_databuf_total_mask(d));
#endif
    if (rv==-1) { 
        guppi_error(__FUNCTION__, "semctl error");
        return GUPPI_ERR_SYS;
    }
    return 0;
}

int guppi_databuf_set_filled(struct guppi_databuf *d, int block_id)
{
    /* This function should always succeed regardless of the current
     * state of the specified databuf.  So we use semctl (not semop) to set
     * the value to one.
     */
    int rv;
    union semun arg;
    arg.val = 1;
    rv = semctl(d->semid, block_id, SETVAL, arg);
#ifdef GUPPI_TRACE
    printf("after %s(%p, %d) %016lx\n",
        __FUNCTION__, d, block_id, guppi_databuf_total_mask(d));
#endif
    if (rv==-1) { 
        guppi_error(__FUNCTION__, "semctl error");
        return GUPPI_ERR_SYS;
    }
    return 0;
}
