/* dump_hashpipe_databuf.c
 *
 * Basic prog to test databuf shared mem routines.
 */
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "hashpipe_databuf.h"

void usage() { 
    printf(
            "Usage: hashpipe_dump_databuf [options]\n"
            "\n"
            "Options [defaults]:\n"
            "  -h, --help\n"
            "  -I N, --instance=N    Instance number           [0]\n"
            "  -d N, --databuf=N     Databuf ID                [1]\n"
            "  -b N, --block=N       Block number           [none]\n"
            "  -s N, --skip=N        Number of bytes to skip   [0]\n"
            "  -n N, --bytes=N       Number of bytes to dump [all]\n"
            "  -f,   --force         Dump data despite errors [no]\n"
            "\n"
            "If a block number is given, dump contents of block to stdout,\n"
            "else just print status of requested instance/databuf.\n"
            );
}

int main(int argc, char *argv[]) {

    /* Loop over cmd line to fill in params */
    static struct option long_opts[] = {
        {"help",     0, NULL, 'h'},
        {"instance", 1, NULL, 'I'},
        {"databuf",  1, NULL, 'd'},
        {"block",    1, NULL, 'b'},
        {"skip",     1, NULL, 's'},
        {"bytes",    1, NULL, 'n'},
        {"force",    1, NULL, 'f'},
        {0,0,0,0}
    };
    int opt;
    int instance_id=0;
    int db_id=1;
    int block = -1;
    int skip = 0;
    int num = 0;
    int force = 0;
    while ((opt=getopt_long(argc,argv,"hI:b:d:fn:s:",long_opts,NULL))!=-1) {
        switch (opt) {
            case 'I':
                instance_id=atoi(optarg);
                break;
            case 'd':
                db_id = atoi(optarg);
                break;
            case 'b':
                block = atoi(optarg);
                break;
            case 'f':
                force = 1;
                break;
            case 's':
                skip = strtol(optarg, NULL, 0);
                break;
            case 'n':
                num = strtol(optarg, NULL, 0);
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
    db = hashpipe_databuf_attach(instance_id, db_id);
    if (db==NULL) { 
      fprintf(stderr, 
          "Error attaching to instance %d databuf %d (may not exist).\n",
          instance_id, db_id);
      return 1;
    }

    /* Print basic info and exit if block not given */
    if(block == -1) {
      printf("Instance %d databuf %d stats:\n", instance_id, db_id);
      printf("  header_size=%zd (%#zx)\n", db->header_size, db->header_size);
      printf("  block_size=%zd (%#zx)\n", db->block_size, db->block_size);
      printf("  n_block=%d\n", db->n_block);
      printf("  shmid=%d\n", db->shmid);
      printf("  semid=%d\n", db->semid);
      return 0;
    }

    if(block >= db->n_block && !force) {
      fprintf(stderr, "Requested block does not exist (n_block=%d)\n",
          db->n_block);
      return 1;
    } else if(block >= db->n_block) {
      fprintf(stderr, "Warning: requested block does not exist (n_block=%d)\n",
          db->n_block);
    }

    if(skip > db->block_size && !force) {
      fprintf(stderr, "Cannot skip more than %zd bytes\n", db->block_size);
      return 1;
    } else if(skip > db->block_size) {
      fprintf(stderr, "Warning: cannot skip more than %zd bytes\n", db->block_size);
    }

    if(num == 0) {
      num = db->block_size - skip;
    } else if(num > db->block_size - skip && !force) {
      fprintf(stderr, "Cannot dump more than %zd bytes\n", db->block_size - skip);
      return 1;
    } else if(num > db->block_size - skip) {
      fprintf(stderr, "Warning: cannot dump more than %zd bytes\n", db->block_size - skip);
    }

    void *p = ((void *)db) + db->header_size + block*db->block_size + skip;

    // Dump block to stdout
    if(write(1, p, num) == -1) {
      perror("write");
      return 1;
    }

   return 0;
}
