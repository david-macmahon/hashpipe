/* check_hashpipe_databuf.c
 *
 * Basic prog to test dstabuf shared mem routines.
 */
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>

#include "fitshead.h"
#include "hashpipe_status.h"
#include "hashpipe_databuf.h"

void usage() { 
    printf(
            "Usage: hashpipe_check_databuf [options]\n"
            "Options:\n"
            "  -h, --help\n"
            "  -q,   --quiet         Quiet mode\n"
            "  -I N, --instance=N    Instance number  [0]\n"
            "  -d N, --databuf=N     Databuf ID       [1]\n"
            "  -c,   --create        Create databuf\n"
            "Extra options for use with -c or --create:\n"
            "  -s MB, --blksize=MB Block size in MiB  [32]\n"
            "  -n N,  --nblock=N   Number of blocks   [24]\n"
            "  -H N,  --hdrsize=N  Size of header [sizeof(hashpipe_databuf_t)]\n"
            );
}

int main(int argc, char *argv[]) {

    /* Loop over cmd line to fill in params */
    static struct option long_opts[] = {
        {"help",   0, NULL, 'h'},
        {"quiet",  0, NULL, 'q'},
        {"instance", 1, NULL, 'I'},
        {"create", 0, NULL, 'c'},
        {"databuf", 1, NULL, 'd'},
        {"blksize",   1, NULL, 's'},
        {"nblock", 1, NULL, 'n'},
        {"hdrsize", 1, NULL, 'H'},
        {0,0,0,0}
    };
    int opt,opti;
    int quiet=0;
    int instance_id=0;
    int create=0;
    int db_id=1;
    int blocksize = 32;
    int nblock = 24;
    size_t header_size = sizeof(hashpipe_databuf_t);
    while ((opt=getopt_long(argc,argv,"hqI:cd:s:n:t:H:",long_opts,&opti))!=-1) {
        switch (opt) {
            case 'I':
                instance_id=atoi(optarg);
                break;
            case 'c':
                create=1;
                break;
            case 'q':
                quiet=1;
                break;
            case 'd':
                db_id = atoi(optarg);
                break;
            case 's':
                blocksize = atoi(optarg);
                break;
            case 'n':
                nblock = atoi(optarg);
                break;
            case 'H':
                header_size = atoi(optarg);
                break;
            case 'h':
            default:
                usage();
                exit(0);
                break;
        }
    }

    /* Create mem if asked, otherwise attach */
    hashpipe_databuf_t *db=NULL;
    if (create) { 
        db = hashpipe_databuf_create(instance_id, header_size, nblock, blocksize*1024*1024, db_id);
        if (db==NULL) {
            fprintf(stderr, "Error creating databuf %d (may already exist).\n",
                    db_id);
            exit(1);
        }
    } else {
        db = hashpipe_databuf_attach(instance_id, db_id);
        if (db==NULL) { 
            fprintf(stderr, 
                    "Error attaching to databuf %d (may not exist).\n",
                    db_id);
            exit(1);
        }
    }

    if(quiet) {
      return 0;
    }

    /* Print basic info */
    printf("databuf %d stats:\n", db_id);
    printf("  header_size=%zd\n\n", db->header_size);
    printf("  block_size=%zd\n", db->block_size);
    printf("  n_block=%d\n", db->n_block);
    printf("  shmid=%d\n", db->shmid);
    printf("  semid=%d\n", db->semid);

    exit(0);
}
