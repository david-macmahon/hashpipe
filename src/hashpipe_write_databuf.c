/* hashpipe_write_databuf.c
 *
 * Basic prog to test write to databuf shared mem segments.
 */
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>

// For open()
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "hashpipe_databuf.h"

void usage() { 
    printf(
            "Usage: hashpipe_write_databuf [options]\n"
            "\n"
            "Options [defaults]:\n"
            "  -h, --help\n"
            "  -I N, --instance=N    Instance number            [0]\n"
            "  -d N, --databuf=N     Databuf ID                 [1]\n"
            "  -b N, --block=N       Block number               [0]\n"
            "  -s N, --skip=N        Number of bytes to skip    [0]\n"
            "  -n N, --bytes=N       Number of bytes to write [all]\n"
            "  -f,   --force         Write data despite errors [no]\n"
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
    int block = 0;
    int skip = 0;
    int num = 0;
    int force = 0;
    ssize_t num_read;
    int fd_urandom;
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

    /* Attach to shared mem segment */
    hashpipe_databuf_t *db=NULL;
    db = hashpipe_databuf_attach(instance_id, db_id);
    if (db==NULL) { 
      fprintf(stderr, 
          "Error attaching to instance %d databuf %d (may not exist).\n",
          instance_id, db_id);
      return 1;
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
      fprintf(stderr, "Cannot write more than %zd bytes\n", db->block_size - skip);
      return 1;
    } else if(num > db->block_size - skip) {
      fprintf(stderr, "Warning: cannot write more than %zd bytes\n", db->block_size - skip);
    }

    fd_urandom = open("/dev/urandom", O_RDONLY);
    // TODO Check fd_urandom!

    void *p = ((void *)db) + db->header_size + block*db->block_size + skip;

    while(num > 0) {
      num_read = read(fd_urandom, p, num);
      if(num_read <= 0) {
        break;
      }
      num -= num_read;
    }

    return 0;
}
