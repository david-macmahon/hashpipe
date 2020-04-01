#ifndef _HASHPIPE_H
#define _HASHPIPE_H

#include <stdio.h>

#include "hashpipe_error.h"
#include "hashpipe_databuf.h"
#include "hashpipe_status.h"
#include "hashpipe_pktsock.h"
#include "hashpipe_udp.h"

#define HASHPIPE_VERSION "1.7"

#ifdef __cplusplus
extern "C" {
#endif

// This file defines types needed by hashpipe plugings.  A hashpipe plugin is
// an shared library that defines application specific processing threads and
// data buffers for use in a hashpipe pipeline.  The hashpipe executable loads
// these plugins dynamically at run time.  Hashpipe contructs the pipeline
// dynamically at run time based on command line arguments.

// Forward declare some structures
struct hashpipe_thread_args;
struct hashpipe_thread_desc;

// Create typedefs for convenience
typedef struct hashpipe_thread_args hashpipe_thread_args_t;
typedef struct hashpipe_thread_desc hashpipe_thread_desc_t;

// These typedefs are used to declare pointers to a pipeline thread's init and
// run functions.
typedef int (* initfunc_t)(hashpipe_thread_args_t *);
typedef void * (* runfunc_t)(hashpipe_thread_args_t *);

// This typedefs are used to declare pointers to a pipline thread's data buffer
// create function.
typedef hashpipe_databuf_t * (* databuf_createfunc_t)(int, int);

/// @private
typedef struct {
  databuf_createfunc_t create;
} databuf_desc_t;

// The hashpipe_thread_desc structure is used to store metadata describing a
// hashpipe thread.  Typically a hashpipe plugin will define one of these
// hashpipe thread descriptors per hashpipe thread.
struct hashpipe_thread_desc {
  const char * name;
  const char * skey;
  initfunc_t init;
  runfunc_t run;
  databuf_desc_t ibuf_desc;
  databuf_desc_t obuf_desc;
};

// This structure passed (via a pointer) to the application's thread
// initialization and run functions.  The `user_data` field can be used to pass
// info from the init function to the run function.
struct hashpipe_thread_args {
    hashpipe_thread_desc_t *thread_desc;
    int instance_id;
    int input_buffer;
    int output_buffer;
    unsigned int cpu_mask; // 0 means use inherited
    int finished;
    pthread_cond_t finished_c;
    pthread_mutex_t finished_m;
    hashpipe_status_t st;
    hashpipe_databuf_t *ibuf;
    hashpipe_databuf_t *obuf;
    void *user_data;
};

// Used to return OK status via return from run
#define THREAD_OK ((void *)0)
// Used to return error status via return from run
#define THREAD_ERROR ((void *)-1)
// Maximum number of threads that be defined by plugins
#define MAX_HASHPIPE_THREADS 1024

// Function threads use to determine whether to keep running.
int run_threads();

// This function is used by pipeline plugins to register threads with the
// pipeline executable.
int register_hashpipe_thread(hashpipe_thread_desc_t * ptm);

// This function can be used to find hashpipe threads by name.  It is generally
// used only by the hashpipe executable.  Returns a pointer to its
// hashpipe_thread_desc_t structure or NULL if a test with the given name is
// not found.
//
// NB: Names are case sensitive.
hashpipe_thread_desc_t * find_hashpipe_thread(char *name);

// List all known hashpipe threads to FILE f.
void list_hashpipe_threads(FILE * f);

// Get CPU affinity of calling thread
// Returns 0 on error
unsigned int get_cpu_affinity();

#ifdef __cplusplus
}
#endif

#endif // _HASHPIPE_H
