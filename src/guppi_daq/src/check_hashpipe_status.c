/* check_hashpipe_status.c
 *
 * Basic prog to test status shared mem routines.
 */
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include "fitshead.h"
#include "guppi_error.h"
#include "hashpipe_status.h"
#include "guppi_thread_main.h"

static struct guppi_status *get_status_buffer(int instance_id)
{
    int rv;
    static int last_used_instance_id = -1;
    static struct guppi_status s;

    instance_id &= 0x3f;

    if(last_used_instance_id != instance_id) {
      rv = guppi_status_attach(instance_id, &s);
      if (rv!=GUPPI_OK) {
          // Should "never" happen
          fprintf(stderr, "Error connecting to status buffer instance %d.\n",
              instance_id);
          perror("guppi_status_attach");
          exit(1);
      }
    }

    return &s;
}

int main(int argc, char *argv[]) {

    int instance_id = 0;
    struct guppi_status *s;

    /* Loop over cmd line to fill in params */
    static struct option long_opts[] = {
        {"key",    1, NULL, 'k'},
        {"get",    1, NULL, 'g'},
        {"string", 1, NULL, 's'},
        {"float",  1, NULL, 'f'},
        {"double", 1, NULL, 'd'},
        {"int",    1, NULL, 'i'},
        {"verbose",  0, NULL, 'v'},
        {"clear",  0, NULL, 'C'},
        {"del",    0, NULL, 'D'},
        {"query",  1, NULL, 'Q'},
        {"instance", 1, NULL, 'I'},
        {0,0,0,0}
    };
    int opt,opti;
    char *key=NULL;
    char value[81];
    float flttmp;
    double dbltmp;
    int inttmp;
    int verbose=0, clear=0;
    while ((opt=getopt_long(argc,argv,"k:g:s:f:d:i:vCDQ:I:",long_opts,&opti))!=-1) {
        switch (opt) {
            case 'I':
                instance_id = atoi(optarg);
                break;
            case 'k':
                key = optarg;
                break;
            case 'Q':
                s = get_status_buffer(instance_id);
                guppi_status_lock(s);
                hgets(s->buf, optarg, 80, value);
                guppi_status_unlock(s);
                value[80] = '\0';
                printf("%s\n", value);
                break;
            case 'g':
                s = get_status_buffer(instance_id);
                guppi_status_lock(s);
                hgetr8(s->buf, optarg, &dbltmp);
                guppi_status_unlock(s);
                printf("%g\n", dbltmp);
                break;
            case 's':
                if (key) {
                    s = get_status_buffer(instance_id);
                    guppi_status_lock(s);
                    hputs(s->buf, key, optarg);
                    guppi_status_unlock(s);
                }
                break;
            case 'f':
                flttmp = atof(optarg);
                if (key) {
                    s = get_status_buffer(instance_id);
                    guppi_status_lock(s);
                    hputr4(s->buf, key, flttmp);
                    guppi_status_unlock(s);
                }
                break;
            case 'd':
                dbltmp = atof(optarg);
                if (key) {
                    s = get_status_buffer(instance_id);
                    guppi_status_lock(s);
                    hputr8(s->buf, key, dbltmp);
                    guppi_status_unlock(s);
                }
                break;
            case 'i':
                inttmp = atoi(optarg);
                if (key) {
                    s = get_status_buffer(instance_id);
                    guppi_status_lock(s);
                    hputi4(s->buf, key, inttmp);
                    guppi_status_unlock(s);
                }
                break;
            case 'D':
                if (key) {
                    s = get_status_buffer(instance_id);
                    guppi_status_lock(s);
                    hdel(s->buf, key);
                    guppi_status_unlock(s);
                }
                break;
            case 'C':
                clear=1;
                break;
            case 'v':
                verbose=1;
                break;
            case '?': // Command line parsing error
            default:
                exit(1);
                break;
        }
    }

    s = get_status_buffer(instance_id);

    /* If verbose, print out buffer */
    if (verbose) { 
        guppi_status_lock(s);
        printf("%s\n", s->buf);
        guppi_status_unlock(s);
    }

    if (clear) 
        guppi_status_clear(s);

    exit(0);
}
