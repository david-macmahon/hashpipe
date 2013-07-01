/*
 * hashpipe.c
 *
 * The main HASHPIPE program
 */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sched.h>
#include <signal.h>
#include <poll.h>
#include <getopt.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/resource.h> 

#include "hashpipe.h"
#include "hashpipe_thread_args.h"

// Functions defined in hashpipe_thread.c, but not declared/exposed in public
// hashpipe_thread.h.
void set_run_threads();
void clear_run_threads();

void usage(const char *argv0) {
    fprintf(stderr,
      "Usage: %s [options]\n"
      "\n"
      "Options:\n"
      "  -h,   --help          Show this message\n"
      "  -l,   --list          List all known threads\n"
      "  -I N, --instance=N    Set instance ID of this pipeline\n"
      "  -c N, --cpu=N         Set CPU number for subsequent threads\n"
      "  -m N, --mask=N        Set CPU mask for subsequent threads\n"
      "  -o K=V, --option=K=V  Store K=V in status buffer\n"
      "  -p P, --plugin=P      Load plugin P\n"
//    "  -b N, --buffer=N        Jump to input buffer B, output buffer B+1\n"
      , argv0
    );
}

// Control-C handler
static void cc(int sig)
{
    clear_run_threads();
}

/* Exit handler that updates status buffer */
inline void set_exit_status(hashpipe_thread_args_t *args) {
    if(args && args->st.buf && args->thread_desc->skey) {
        hashpipe_status_lock_safe(&args->st);
        hputs(args->st.buf, args->thread_desc->skey, "exit");
        hashpipe_status_unlock_safe(&args->st);
    }
}

// Function to set cpu affinity
int
set_cpu_affinity(unsigned int mask)
{
    int i, rv;

    if(mask != 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        // Only handle 32 cores (for now)
        for(i=0; i<32; i++) {
            if(mask&1) {
              CPU_SET(i, &cpuset);
            }
            mask >>= 1;
        }
        rv = sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
        if (rv<0) {
            hashpipe_error(__FUNCTION__, "Error setting cpu affinity.");
            return rv;
        }
    }
    return 0;
}

// General init function called for all threads.
static int
hashpipe_thread_init(hashpipe_thread_args_t *args)
{
    int rv = 1;
    args->ibuf = NULL;
    args->obuf = NULL;

    // Attach to status buffer
    rv = hashpipe_status_attach(args->instance_id, &args->st);
    if (rv != HASHPIPE_OK) {
        hashpipe_error(__FUNCTION__,
                "Error attaching to status shared memory.");
        goto status_error;
    }
    if(args->thread_desc->skey) {
        /* Init status */
        hashpipe_status_lock_safe(&args->st);
        hputs(args->st.buf, args->thread_desc->skey, "init");
        hashpipe_status_unlock_safe(&args->st);
    }

    // Create databufs
    if(args->thread_desc->ibuf_desc.create) {
        args->ibuf = args->thread_desc->ibuf_desc.create(args->instance_id, args->input_buffer);
        if(!args->ibuf) {
            hashpipe_error(__FUNCTION__,
                    "Error creating/attaching to databuf %d for %s input",
                    args->input_buffer, args->thread_desc->name);
            goto ibuf_error;
        }
    }
    if(args->thread_desc->obuf_desc.create) {
        args->obuf = args->thread_desc->obuf_desc.create(args->instance_id, args->output_buffer);
        if(!args->obuf) {
            hashpipe_error(__FUNCTION__,
                    "Error creating/attaching to databuf %d for %s output",
                    args->output_buffer, args->thread_desc->name);
            goto obuf_error;
        }
    }

    // Call user init function, if it exists
    if(args->thread_desc->init) {
        rv = args->thread_desc->init(args);
    }

    // Detach from output buffer
    if(hashpipe_databuf_detach(args->obuf)) {
        hashpipe_error(__FUNCTION__, "Error detaching from output databuf.");
        if(!rv) rv = 1;
    }
    args->obuf = NULL;

obuf_error:

    // Detach from input buffer
    if(hashpipe_databuf_detach(args->ibuf)) {
        hashpipe_error(__FUNCTION__, "Error detaching from input databuf.");
        if(!rv) rv = 1;
    }
    args->ibuf = NULL;

ibuf_error:

    // Detach from status buffer
    if(hashpipe_status_detach(&args->st)) {
        hashpipe_error(__FUNCTION__, "Error detaching from status buffer.");
        if(!rv) rv = 1;
    }

status_error:

    return rv;
}

static void *
hashpipe_thread_run(void *vp_args)
{
    // Cast void pointer to hashpipe_thread_run_t
    hashpipe_thread_args_t *args = (hashpipe_thread_args_t *)vp_args;
    void * rv = THREAD_OK;

    // Set CPU affinity
    if(set_cpu_affinity(args->cpu_mask) < 0) {
        perror("set_cpu_affinity");
        rv = THREAD_ERROR;
        goto done;
    }

    // Attach to status buffer
    if(hashpipe_status_attach(args->instance_id, &args->st) != HASHPIPE_OK) {
        hashpipe_error(__FUNCTION__,
                "Error attaching to status shared memory.");
        rv = THREAD_ERROR;
        goto done;
    }

    // No more goto statements now that we're using pthread_cleanup_push!
    pthread_cleanup_push((void *)hashpipe_status_detach, &args->st);
    pthread_cleanup_push((void *)set_exit_status, &args->st);

    // Attach to data buffers
    if(args->thread_desc->ibuf_desc.create) {
        args->ibuf = hashpipe_databuf_attach(args->instance_id, args->input_buffer);
        if (args->ibuf==NULL) {
            hashpipe_error(__FUNCTION__,
                    "Error attaching to databuf %d for %s input",
                    args->input_buffer, args->thread_desc->name);
            rv = THREAD_ERROR;
        }
    }
    pthread_cleanup_push((void *)hashpipe_databuf_detach, args->ibuf);
    if(args->thread_desc->obuf_desc.create) {
        args->obuf = hashpipe_databuf_attach(args->instance_id, args->output_buffer);
        if (args->obuf==NULL) {
            hashpipe_error(__FUNCTION__,
                    "Error attaching to databuf %d for %s output",
                    args->output_buffer, args->thread_desc->name);
            rv = THREAD_ERROR;
        }
    }
    pthread_cleanup_push((void *)hashpipe_databuf_detach, args->obuf);


    // Sets up call to set state to finished on thread exit
    pthread_cleanup_push((void *)hashpipe_thread_set_finished, args);

    // Call user run function
    if(rv == THREAD_OK) {
        rv = args->thread_desc->run(args);
    }

    // Set thread state to finished
    hashpipe_thread_set_finished(args);
    pthread_cleanup_pop(0);

    // User thread returned (or was not run), stop other threads
    clear_run_threads();

    // Detach from output buffer
    if(hashpipe_databuf_detach(args->obuf)) {
        hashpipe_error(__FUNCTION__, "Error detaching from output databuf.");
        rv = THREAD_ERROR;
    }
    pthread_cleanup_pop(0); // detach obuf
    args->obuf = NULL;

    // Detach from input buffer
    if(hashpipe_databuf_detach(args->ibuf)) {
        hashpipe_error(__FUNCTION__, "Error detaching from input databuf.");
        rv = THREAD_ERROR;
    }
    pthread_cleanup_pop(0); // detach ibuf
    args->ibuf = NULL;

    // Set exit status
    set_exit_status(args);
    pthread_cleanup_pop(0); // set exit status

    // Detach from status buffer
    hashpipe_status_detach(&args->st);
    pthread_cleanup_pop(0); // detach status

done:

    return rv;
}

#define MAX_PLUGIN_NAME (1024)
#define MAX_PLUGIN_EXT  (7)
#define PLUGIN_EXT ".so"

int main(int argc, char *argv[])
{
    int opt, i, rv;
    char * cp;
    hashpipe_status_t st;
    int num_threads = 0;
    pthread_t threads[MAX_HASHPIPE_THREADS];
    struct hashpipe_thread_args args[MAX_HASHPIPE_THREADS];
    char plugin_name[MAX_PLUGIN_NAME+MAX_PLUGIN_EXT+1];

    static struct option long_opts[] = {
      {"help",     0, NULL, 'h'},
      {"list",     0, NULL, 'l'},
      {"instance", 1, NULL, 'I'},
      {"cpu",      1, NULL, 'c'},
      {"mask",     1, NULL, 'm'},
      {"option",   1, NULL, 'o'},
      {"plugin",   1, NULL, 'p'},
//    {"buffer",   1, NULL, 'b'},
      {0,0,0,0}
    };

    int instance_id  = 0;
    int input_buffer  = 0;
    int output_buffer = 1;

    // Preemptively set RLIMIT_MEMLOCK to max
    struct rlimit rlim;
    getrlimit(RLIMIT_MEMLOCK, &rlim);
    rlim.rlim_cur = rlim.rlim_max;
    if(setrlimit(RLIMIT_MEMLOCK, &rlim)) {
      perror("setrlimit(RLIMIT_MEMLOCK)");
    }

#ifdef RTPRIO
    // This should not be needed and is disabled by default, but since it was
    // tried once I thought it best to leave it in here as a compile-time
    // option.

    // Set RLIMIT_RTPRIO to 1
    getrlimit(RLIMIT_RTPRIO, &rlim);
    rlim.rlim_cur = 1;
    if(setrlimit(RLIMIT_RTPRIO, &rlim)) {
      perror("setrlimit(RLIMIT_RTPRIO)");
    }

    struct sched_param sched_param = {
      .sched_priority = 1
    };
    if(sched_setscheduler(0, SCHED_RR, &sched_param)) {
      perror("sched_setscheduler");
    }
#endif // RTPRIO

    hashpipe_thread_args_init(&args[num_threads]);
    args[num_threads].instance_id   = instance_id;
    args[num_threads].input_buffer  = input_buffer;
    args[num_threads].output_buffer = output_buffer;

    // Parse command line.  Leading '-' means treat non-option arguments as if
    // it were the argument of an option with character code 1.
    while((opt=getopt_long(argc,argv,"-hlI:m:c:b:o:p:",long_opts,NULL))!=-1) {
      switch (opt) {
        case 1:
          // optarg is name of thread
          args[num_threads].thread_desc = find_hashpipe_thread(optarg);

          if (!args[num_threads].thread_desc) {
              fprintf(stderr, "Error finding '%s' thread.\n", optarg);
              exit(1);
          }

          // Init thread
          printf("initing  thread '%s' with databufs %d and %d\n",
              args[num_threads].thread_desc->name, args[num_threads].input_buffer,
              args[num_threads].output_buffer);

          rv = hashpipe_thread_init(&args[num_threads]);

          if (rv) {
              fprintf(stderr, "Error initializing thread for '%s'.\n",
                  args[num_threads].thread_desc->name);
              exit(1);
          }

          // Setup for next thread
          num_threads++;
          input_buffer++;
          output_buffer++;
          hashpipe_thread_args_init(&args[num_threads]);
          args[num_threads].instance_id   = instance_id;
          args[num_threads].input_buffer  = input_buffer;
          args[num_threads].output_buffer = output_buffer;
          break;

        case 'h': // Help
          usage(argv[0]);
          return 0;
          break;

        case 'l': // List
          list_hashpipe_threads(stdout);
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
          if (hashpipe_status_attach(instance_id, &st) != HASHPIPE_OK) {
            // Should "never" happen
            fprintf(stderr,
                "Error connecting to status buffer instance %d.\n",
                instance_id);
            perror("hashpipe_status_attach");                           \
            exit(1);
          }
          // Look for equal sign
          cp = strchr(optarg, '=');
          // If found
          if(cp) {
            // Nul-terminate key
            *cp = '\0';
            // Store key and value (value starts right after '=')
            hashpipe_status_lock(&st);
            hputs(st.buf, optarg, cp+1);
            hashpipe_status_unlock(&st);
            // Restore '=' character
            *cp = '=';
          } else {
            // Valueless key, just store empty string
            hashpipe_status_lock(&st);
            hputs(st.buf, optarg, "");
            hashpipe_status_unlock(&st);
          }
          hashpipe_status_detach(&st);
          break;

        case 'm': // CPU mask
          args[num_threads].cpu_mask = strtoul(optarg, NULL, 0);
          break;

        case 'c': // CPU number
          i = strtol(optarg, NULL, 0);
          args[num_threads].cpu_mask = (1<<i);
          break;

        case 'p': // Load plugin
          // Copy plugin name from optarg
          strncpy(plugin_name, optarg, MAX_PLUGIN_NAME);
          plugin_name[MAX_PLUGIN_NAME] = '\0';
          // Look for last dot
          char * dot = strrchr(plugin_name, '.');
          // If dot not found, or plugin_name does not end with PLUGIN_EXT
          if(!dot || strcmp(dot, PLUGIN_EXT)) {
            // Append PLUGIN_EXT
            strcat(plugin_name, PLUGIN_EXT);
          }
          // Load plugin
          if(!dlopen(plugin_name, RTLD_NOW)) {
            fprintf(stderr, "Error loading plugin '%s' (%s)\n",
                optarg, dlerror());
            exit(1);
          }
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
      list_hashpipe_threads(stdout);
      return 1;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

#ifdef DEBUG_SEMS
    fprintf(stderr, "sed '\n");
#endif

    // Catch INT and TERM signals
    signal(SIGINT, cc);
    signal(SIGTERM, cc);
    set_run_threads();

    // Start threads in reverse order
    for(i=num_threads-1; i >= 0; i--) {

      // Launch thread
      printf("starting thread '%s' with databufs %d and %d\n",
          args[i].thread_desc->name, args[i].input_buffer, args[i].output_buffer);
      rv = pthread_create(&threads[i], NULL,
          hashpipe_thread_run, (void *)&args[i]);

      if (rv) {
          fprintf(stderr, "Error creating thread for '%s'.\n",
              args[i].thread_desc->name);
          exit(1);
      }

      sleep(3);
    }

    /* Wait for SIGINT (i.e. control-c) or SIGTERM (aka "kill <pid>") */
    while (run_threads()) {
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
      printf("Joined thread '%s'\n", args[i].thread_desc->name);
      fflush(stdout);
    }
    for(i=num_threads; i>=0; i--) {
      hashpipe_thread_args_destroy(&args[i]);
    }

    exit(0);
}
