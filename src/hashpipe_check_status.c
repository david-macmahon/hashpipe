/* check_hashpipe_status.c
 *
 * Basic prog to test status shared mem routines.
 */
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include "fitshead.h"
#include "hashpipe_error.h"
#include "hashpipe_status.h"

static void usage() { 
    printf(
        "Usage: hashpipe_check_status [options]\n"
        "General options:\n"
        "  -h,     --help         Show this message\n"
        "  -I N,   --instance=N   Specify hashpipe instance [0]\n"
        "  -v,     --verbose      Be verbose [false]\n"
        "Query options:\n"
        "  -Q KEY, --query=KEY    Query string value of KEY\n"
        "  -g KEY, --get=KEY      Query double value of KEY\n"
        "Update options:\n"
        "  -k KEY, --key=KEY      Specify KEY to be updated\n"
        "  -s VAL, --string=VAL   Update key with string value VAL\n"
        "  -f VAL, --float=VAL    Update key with float value VAL\n"
        "  -d VAL, --double=VAL   Update key with double value VAL\n"
        "  -i VAL, --int=VAL      Update key with int value VAL\n"
        "Delete options:\n"
        "  -C,     --clear        Remove all key/value pairs\n"
        "  -D KEY, --del=KEY      Delete KEY (and its value)\n"
    );
}

static hashpipe_status_t *get_status_buffer(int instance_id)
{
    int rv;
    static int last_used_instance_id = -1;
    static hashpipe_status_t s;

    instance_id &= 0x3f;

    if(last_used_instance_id != instance_id) {
      rv = hashpipe_status_attach(instance_id, &s);
      if (rv != HASHPIPE_OK) {
          // Should "never" happen
          fprintf(stderr, "Error connecting to status buffer instance %d.\n",
              instance_id);
          perror("hashpipe_status_attach");
          exit(1);
      }
    }

    return &s;
}

int main(int argc, char *argv[]) {

    int instance_id = 0;
    hashpipe_status_t *s;

    /* Loop over cmd line to fill in params */
    static struct option long_opts[] = {
        {"help",   0, NULL, 'h'},
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
    while ((opt=getopt_long(argc,argv,"hk:g:s:f:d:i:vCDQ:I:",long_opts,&opti))!=-1) {
        switch (opt) {
            case 'I':
                instance_id = atoi(optarg);
                break;
            case 'k':
                key = optarg;
                break;
            case 'Q':
                s = get_status_buffer(instance_id);
                hashpipe_status_lock(s);
                hgets(s->buf, optarg, 80, value);
                hashpipe_status_unlock(s);
                value[80] = '\0';
                printf("%s\n", value);
                break;
            case 'g':
                s = get_status_buffer(instance_id);
                hashpipe_status_lock(s);
                hgetr8(s->buf, optarg, &dbltmp);
                hashpipe_status_unlock(s);
                printf("%g\n", dbltmp);
                break;
            case 's':
                if (key) {
                    s = get_status_buffer(instance_id);
                    hashpipe_status_lock(s);
                    hputs(s->buf, key, optarg);
                    hashpipe_status_unlock(s);
                }
                break;
            case 'f':
                flttmp = atof(optarg);
                if (key) {
                    s = get_status_buffer(instance_id);
                    hashpipe_status_lock(s);
                    hputr4(s->buf, key, flttmp);
                    hashpipe_status_unlock(s);
                }
                break;
            case 'd':
                dbltmp = atof(optarg);
                if (key) {
                    s = get_status_buffer(instance_id);
                    hashpipe_status_lock(s);
                    hputr8(s->buf, key, dbltmp);
                    hashpipe_status_unlock(s);
                }
                break;
            case 'i':
                inttmp = atoi(optarg);
                if (key) {
                    s = get_status_buffer(instance_id);
                    hashpipe_status_lock(s);
                    hputi4(s->buf, key, inttmp);
                    hashpipe_status_unlock(s);
                }
                break;
            case 'D':
                if (key) {
                    s = get_status_buffer(instance_id);
                    hashpipe_status_lock(s);
                    hdel(s->buf, key);
                    hashpipe_status_unlock(s);
                }
                break;
            case 'C':
                clear=1;
                break;
            case 'v':
                verbose=1;
                break;
            case 'h':
                usage();
                return 0;
            case '?': // Command line parsing error
            default:
                usage();
                exit(1);
                break;
        }
    }

    s = get_status_buffer(instance_id);

    /* If verbose, print out buffer */
    if (verbose) { 
        hashpipe_status_lock(s);
        printf("%s\n", s->buf);
        hashpipe_status_unlock(s);
    }

    if (clear) 
        hashpipe_status_clear(s);

    exit(0);
}
