/* clean_hashpipe_shmem.c
 *
 * Mark all HASHPIPE shmem segs for deletion.
 */
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <semaphore.h>
#include <fcntl.h>
#include <getopt.h>

#include "hashpipe_error.h"
#include "hashpipe_status.h"
#include "hashpipe_databuf.h"

void usage() {
    printf(
            "Usage: hashpipe_clean_shmem [options]\n"
            "\n"
            "Clears status buffer and deletes data buffers for specified\n"
            "Hashpipe instance.  If -d is given, deletes status buffer\n"
            "instead of just clearing it.\n"
            "\n"
            "Options:\n"
            "  -I N, --instance=N    Instance number [0]\n"
            "  -d,   --delete        Delete status buffer [clear]\n"
            "  -h,   --help          This message\n"
            );
}

int main(int argc, char *argv[]) {
    int rv,ex=0;
    int instance_id = 0;
    int delete_status = 0;
    int opt;

    /* Loop over cmd line to fill in params */
    // "-d" as command line argument deletes status memory and semaphore.
    // Otherwise, it is simply re-initialized.
    static struct option long_opts[] = {
        {"del",      0, NULL, 'd'},
        {"help",     0, NULL, 'h'},
        {"instance", 1, NULL, 'I'},
        {0,0,0,0}
    };

    while ((opt=getopt_long(argc,argv,"dhI:",long_opts,NULL))!=-1) {
        switch (opt) {
            case 'd':
                delete_status = 1;
                break;
            case 'I':
                instance_id = atoi(optarg);
                break;
            case 'h':
                usage();
                exit(0);
                break;
            default:
                usage();
                exit(1);
        }
    }

    hashpipe_status_t s;
    char semname[NAME_MAX] = {'\0'};

    /*
     * Get the semaphore name.  Return error on truncation.
     */
    if(hashpipe_status_semname(instance_id, semname, NAME_MAX)) {
        fprintf(stderr, "Error: semaphore name truncated.\n");
        exit(1);
    }

    /* Status shared mem, force unlock first */
    sem_unlink(semname);
    rv = hashpipe_status_attach(instance_id, &s);
    if (rv!=HASHPIPE_OK) {
        fprintf(stderr, "Error connecting to status shared mem.\n");
        perror(NULL);
        exit(1);
    }

    if(delete_status) {
      rv = shmctl(s.shmid, IPC_RMID, NULL);
      if (rv==-1) {
          fprintf(stderr, "Error deleting status segment.\n");
          perror("shmctl");
          ex|=1;
      }
      rv = sem_unlink(semname);
      if (rv==-1) {
          fprintf(stderr, "Error unlinking status semaphore.\n");
          perror("sem_unlink");
          ex|=2;
      }
      switch(ex) {
        case 0:
          printf("Deleted status shared memory and semaphore.\n");
          break;
        case 1:
          printf("Deleted status semaphore.\n");
          break;
        case 2:
          printf("Deleted status shared memory.\n");
          break;
      }
    } else {
      hashpipe_status_clear(&s);
      printf("Cleared status shared memory.\n");
    }

    /* Databuf shared mem */
    hashpipe_databuf_t *d=NULL;
    int i = 0;
    for (i=1; i<=20; i++) {
        d = hashpipe_databuf_attach(instance_id, i); // Repeat for however many needed ..
        if (d==NULL) continue;
        if (d->semid) { 
            rv = semctl(d->semid, 0, IPC_RMID); 
            if (rv==-1) {
                fprintf(stderr, "Error removing databuf semaphore %u\n", d->semid);
                perror("semctl");
                ex=1;
            }
        }
        rv = shmctl(d->shmid, IPC_RMID, NULL);
        if (rv==-1) {
            fprintf(stderr, "Error deleting databuf segment %u.\n", d->shmid);
            perror("shmctl");
            ex=1;
        }
    }

    exit(ex);
}
