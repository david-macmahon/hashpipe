#ifndef _HASHPIPE_H
#define _HASHPIPE_H

#include <stdio.h>

#include "hashpipe_error.h"
#include "hashpipe_databuf.h"
#include "hashpipe_status.h"
#include "hashpipe_udp.h"

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

// A hashpipe_thread structure encapsulates metadata and functionality for one
// or more threads that can be used in a processing pipeline.  The hashpipe
// executable dynamically assembles a pipeline at runtime consisting of
// multiple hashpipe threads.
//
// Hashpipe threads must register themselves with the hashpipe executable via a
// call to register_hashpipe_thread().  This is typically performed from a
// static C function with the constructor attribute in the hashpipe thread's
// source file.
//
// Hashpipe threads are identified by their name.  The hashpipe executable
// finds (registered) hashpipe threads by their name.  A hashpipe thread can be
// input-only, output-only, or both input and output.  An input thread has an
// associated output data buffer into which it writes data.  An output thread
// has an associated input data buffer from which it reads data.  An
// input/output thread has both.
//
// Input-only threads source data into the pipeline.  They do not get their
// input data from a shared memory ring buffer.  They get their data from
// external sources (e.g.  files or the network) or generate it internally
// (e.g.  for test vectors).  Input-only threads have an output data buffer,
// but no input data buffer (their input does not come from a shared memory
// ring buffer).
//
// Output-only threads sink data from the pipeline.  Thy do not put their
// output data into a shared memory ring buffer.  They send their data to
// external sinks (e.g. files or the network) of consume it internally (e.g.
// comparing against expected output).  Output-only threads have an input data
// buffer, but no output data buffer (their output data does not go the a
// shared memory ring buffer).
//
// Input/output threads get their input data from one shared memory region
// (their input data buffer), process it, and store the output data in another
// shared memory region (their output data buffer).
//
// The hashpipe's thread's metadata consists of the following information:
//
//   name - A string containing the thread's name
//   skey - A string containing the thread's status buffer "status" key
//   init - A pointer to the thread's initialization function
//   run  - A pointer to the thread's run function
//   ibuf - A structure describing the thread's input data buffer (if any)
//   obuf - A structure describing the thread's output data buffer (if any)
//
// "name" is used to match command line thread spcifiers to thread metadata so
// that the pipeline can be constructed as specified on the command line.
//
// "skey" is typically 8 characters or less, uppercase, and ends with "STAT".
// If it is non-NULL and non-empty, HASHPIPE will automatically store/update
// this key in the status buffer with the thread's status at initialization
// ("init") and exit ("exit").
//
// The thread initialization function can be null if no special initialization
// is needed.  If provided, it must point to a function with the following
// signature:
//
//   int my_thread_init_funtion(hashpipe_thread_args_t *args)
//
// The thread run function must have the following signature:
//
//   void my_thread_run_funtion(hashpipe_thread_args_t *args)
//
// The data buffer description structure used for ibuf and obuf currently
// contains one function pointer:
//
//   create - A pointer to a function that creates the data buffer
//
// Future HASHPIPE versions may introduce additional data buffer fields.
//
// ibuf.create should be NULL for input-only threads and obuf.create should
// NULL for output-only threads.  Having both ibuf.create and obuf.create set
// to NULL is invalid and the thread will not be used.
//
// The create function must have the following signature:
//
//   hashpipe_databuf_t * my_create_function(int instance_id, int databuf_id)

// These typedefs are used to declare pointers to a pipeline thread's init and
// run functions.
typedef int (* initfunc_t)(hashpipe_thread_args_t *);
typedef void * (* runfunc_t)(hashpipe_thread_args_t *);

// This typedefs are used to declare pointers to a pipline thread's data buffer
// create function.
typedef hashpipe_databuf_t * (* databuf_createfunc_t)(int, int);

typedef struct {
  databuf_createfunc_t create;
} databuf_desc_t;

// The hashpipe_thread_desc structure is used to store metadata describing a
// hashpipe thread.  Typically a hashpipe plugin will define one of these
// hashpipe thread descriptors per hashpipe thread.
struct hashpipe_thread_desc {
  char * name;
  char * skey;
  initfunc_t init;
  runfunc_t run;
  databuf_desc_t ibuf_desc;
  databuf_desc_t obuf_desc;
};

// This structure passed (via a pointer) to the application's thread
// initialization and run functions.
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

#endif // _HASHPIPE_H
