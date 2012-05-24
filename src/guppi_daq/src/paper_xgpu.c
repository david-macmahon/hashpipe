/*
 * paper_xgpu.c
 *
 * The main PAPER xGPU program
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <poll.h>
#include <getopt.h>
#include <errno.h>

#include <xgpu.h>

#include "guppi_error.h"
#include "guppi_status.h"
#include "guppi_databuf.h"
#include "paper_databuf.h"
#include "guppi_params.h"
#include "guppi_thread_main.h"
#include "guppi_defines.h"
#include "fitshead.h"
#include "paper_thread.h"

#define MAX_THREADS (1024)

void usage(const char *argv0) {
    fprintf(stderr,
      "Usage: %s [options]\n"
      "\n"
      "Options:\n"
      "  -h,   --help          Show this message\n"
      "  -l,   --list          List all known thread modules\n"
      "  -I N, --instance=N    Set instance ID of this pipeline\n"
      "  -c N, --cpu=N         Set CPU number for subsequent threads\n"
      "  -m N, --mask=N        Set CPU mask for subsequent threads\n"
      "  -o K=V, --option=K=V  Store K=V in status buffer\n"
//    "  -b N, --size=N        Jump to input buffer B, output buffer B+1\n"
      , argv0
    );
}

int main(int argc, char *argv[])
{
    int opt, i, rv;
    char * cp;
    struct guppi_status st;
    int num_threads = 0;
    pthread_t threads[MAX_THREADS];
    pipeline_thread_module_t *modules[MAX_THREADS];
    struct guppi_thread_args args[MAX_THREADS];

    static struct option long_opts[] = {
      {"help",     0, NULL, 'h'},
      {"list",     0, NULL, 'l'},
      {"instance", 1, NULL, 'I'},
      {"cpu",      1, NULL, 'c'},
      {"mask",     1, NULL, 'm'},
      {"option",   1, NULL, 'o'},
      {"buffer",   1, NULL, 'b'},
      {0,0,0,0}
    };

    int instance_id  = 0;
    int input_buffer  = 0;
    int output_buffer = 1;

    guppi_thread_args_init(&args[num_threads]);
    args[num_threads].instance_id   = instance_id;
    args[num_threads].input_buffer  = input_buffer;
    args[num_threads].output_buffer = output_buffer;

    // Parse command line.  Leading '-' means treat non-option arguments as if
    // it were the argument of an option with character code 1.
    while((opt=getopt_long(argc,argv,"-hlI:m:c:b:o:",long_opts,NULL))!=-1) {
      switch (opt) {
        case 1:
          // optarg is name of thread
          modules[num_threads] = find_pipeline_thread_module(optarg);

          if (!modules[num_threads]) {
              fprintf(stderr, "Error finding '%s' module.\n", optarg);
              exit(1);
          }

          // Init thread
          printf("initing  thread '%s' with databufs %d and %d\n",
              modules[num_threads]->name, args[num_threads].input_buffer,
              args[num_threads].output_buffer);

          rv = modules[num_threads]->init(&args[num_threads]);

          if (rv) {
              fprintf(stderr, "Error initializing thread for '%s'.\n",
                  modules[num_threads]->name);
              exit(1);
          }

          // Setup for next thread
          num_threads++;
          input_buffer++;
          output_buffer++;
          guppi_thread_args_init(&args[num_threads]);
          args[num_threads].instance_id   = instance_id;
          args[num_threads].input_buffer  = input_buffer;
          args[num_threads].output_buffer = output_buffer;
          break;

        case 'h': // Help
          usage(argv[0]);
          return 0;
          break;

        case 'l': // List
          list_pipeline_thread_modules(stdout);
          return 0;
          break;

        case 'I': // Instance id
          instance_id = strtol(optarg, NULL, 0);
          if(instance_id < 0 || instance_id > 63) {
            fprintf(stderr, "warning: instance_id %d treated as %d\n",
                instance_id, instance_id&0x3f);
            instance_id &= 0x3f;
          }
          args[num_threads].instance_id = instance_id;
          break;

        case 'o': // K=V option to store in status memory
          // Attach to status memory for current instance_id value
          if (guppi_status_attach(instance_id, &st) !=GUPPI_OK) {
            // Should "never" happen
            fprintf(stderr,
                "Error connecting to status buffer instance %d.\n",
                instance_id);
            perror("guppi_status_attach");                           \
            exit(1);
          }
          // Look for equal sign
          cp = strchr(optarg, '=');
          // If found
          if(cp) {
            // Nul-terminate key
            *cp = '\0';
            // Store key and value (value starts right after '=')
            guppi_status_lock(&st);
            hputs(st.buf, optarg, cp+1);
            guppi_status_unlock(&st);
            // Restore '=' character
            *cp = '=';
          } else {
            // Valueless key, just store empty string
            guppi_status_lock(&st);
            hputs(st.buf, optarg, "");
            guppi_status_unlock(&st);
          }
          guppi_status_detach(&st);
          break;

        case 'm': // CPU mask
          args[num_threads].cpu_mask = strtoul(optarg, NULL, 0);
          break;

        case 'c': // CPU number
          i = strtol(optarg, NULL, 0);
          args[num_threads].cpu_mask = (1<<i);
          break;

        case 'b': // Set buffer
          // "-b B" jumps to input buffer B, output buffer B+1
          // TODO
          break;

        case '?': // Command line parsing error
        default:
          return 1;
          break;
      }
    }

    // If no threads specified
    if(num_threads == 0) {
      printf("No threads specified!\n");
      list_pipeline_thread_modules(stdout);
      return 1;
    }

#ifdef DEBUG_SEMS
    fprintf(stderr, "sed '\n");
#endif

    // Catch INT and TERM signals
    signal(SIGINT, cc);
    signal(SIGTERM, cc);

    // Start threads in reverse order
    for(i=num_threads-1; i >= 0; i--) {

      // Launch thread
      printf("starting thread '%s' with databufs %d and %d\n",
          modules[i]->name, args[i].input_buffer, args[i].output_buffer);
      rv = pthread_create(&threads[i], NULL,
          modules[i]->run, (void *)&args[i]);

      if (rv) {
          fprintf(stderr, "Error creating thread for '%s'.\n",
              modules[i]->name);
          exit(1);
      }

      sleep(3);
    }

    /* Wait for SIGINT (i.e. control-c) or SIGTERM (aka "kill <pid>") */
    run_threads=1;
    while (run_threads) {
        sleep(1);
    }

    for(i=num_threads-1; i>=0; i--) {
      pthread_cancel(threads[i]);
    }
    for(i=num_threads-1; i>=0; i--) {
      pthread_kill(threads[i], SIGINT);
    }
    for(i=num_threads-1; i>=0; i--) {
      pthread_join(threads[i], NULL);
      printf("Joined thread '%s'\n", modules[i]->name);
      fflush(stdout);
    }
    for(i=num_threads; i>=0; i--) {
      guppi_thread_args_destroy(&args[i]);
    }

    exit(0);
}
