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

int main(int argc, char *argv[])
{
    int i, rv;
    int num_threads = 0;
    pthread_t threads[MAX_THREADS];
    pipeline_thread_module_t *modules[MAX_THREADS];
    struct guppi_thread_args args[MAX_THREADS];

    // Handle initial -l option as request to list all known threads
    if(argv[1] && argv[1][0] == '-' && argv[1][1] == 'l') {
        list_pipeline_thread_modules(stdout);
        return 0;
    }

    // Catch INT and TERM signals
    signal(SIGINT, cc);
    signal(SIGTERM, cc);

    int input_buffer  = 0;
    int output_buffer = 1;

    guppi_thread_args_init(&args[num_threads]);
    args[num_threads].input_buffer  = input_buffer;
    args[num_threads].output_buffer = output_buffer;

    // Walk through command line for names of threads to instantiate
    for(i=1; i<argc; i++) {
      // Handle options
      if(argv[i][0] == '-') {
        switch(argv[i][1]) {
          case 'b':
            // "-b B" jumps to input buffer B, output buffer B+1
            // TODO
            break;
          case 'c':
            // "-c C" sets CPU core for next thread
            // (only single core supported)
            if(i < argc-1) {
              // TODO Warn on errors
              unsigned int cpu = strtoul(argv[++i], NULL, 0);
              args[num_threads].cpu_mask = (1<<cpu);
            }
            break;
          case 'm':
            // "-m M" sets CPU affinity mask for next thread
            if(i < argc-1) {
              // TODO Warn on errors
              args[num_threads].cpu_mask = strtoul(argv[++i], NULL, 0);
            }
            break;
        }
      } else {
        // argv[i] is name of thread to start
        modules[num_threads] = find_pipeline_thread_module(argv[i]);

        if (!modules[num_threads]) { 
            fprintf(stderr, "Error finding '%s' module.\n", argv[i]);
            exit(1);
        }

        // Init thread
        printf("initing  thread '%s' with databufs %d and %d\n",
            modules[num_threads]->name, args[num_threads].input_buffer, args[num_threads].output_buffer);

        rv = modules[num_threads]->init(&args[num_threads]);

        if (rv) { 
            fprintf(stderr, "Error initializing thread for '%s'.\n",
                modules[num_threads]->name);
            exit(1);
        }

        // Launch thread
        printf("starting thread '%s' with databufs %d and %d\n",
            modules[num_threads]->name, args[num_threads].input_buffer, args[num_threads].output_buffer);
        rv = pthread_create(&threads[num_threads], NULL,
            modules[num_threads]->run, (void *)&args[num_threads]);

        if (rv) { 
            fprintf(stderr, "Error creating thread for '%s'.\n",
                modules[num_threads]->name);
            exit(1);
        }

        // Setup for next thread
        num_threads++;
        input_buffer++;
        output_buffer++;
        guppi_thread_args_init(&args[num_threads]);
        args[num_threads].input_buffer  = input_buffer;
        args[num_threads].output_buffer = output_buffer;
      }
    }

    // If no threads started
    if(num_threads == 0) {
      printf("No threads specified!\n");
      list_pipeline_thread_modules(stdout);
      return 1;
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
