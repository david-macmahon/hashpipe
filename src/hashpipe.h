#ifndef _HASHPIPE_H
#define _HASHPIPE_H

#include "hashpipe_error.h"
#include "hashpipe_databuf.h"
#include "hashpipe_status.h"
#include "hashpipe_udp.h"

// This file defines types needed by "hashpipe modules".  A hashpipe module is
// an shared library that defines application specific processing threads and
// data buffers.  The hashpipe executable loads these modules dynamically at
// runtime.

// Forward declare some structures
struct hashpipe_thread_args;
struct pipeline_thread_module;

// Create typedefs for convenience
typedef struct hashpipe_thread_args hashpipe_thread_args_t;
typedef struct pipeline_thread_module pipeline_thread_module_t;

// A pipeline thread module encapsulates metadata and functionality for one or
// more threads that can be used in a processing pipeline.  The pipeline
// executable dynamically assembles a pipeline at runtime consisting of
// multiple pipeline threads.
//
// Pipeline thread modules must register themselves with the pipeline
// executable via a call to register_pipeline_thread_module().  This is
// typically performed from a static C function with the constructor attribute
// in the pipeline thread module's source file.
//
// Pipeline thread modules are identified by their name.  The pipeline
// executable can find (registered) pipeline thread modules by their name.  A
// pipeline thread can be a PIPELINE_INPUT_THREAD, a PIPELINE_OUTPUT_THREAD, or
// both.  A pipeline thread can have an associated input data buffer from which
// it sinks data and/or an associated output data buffer to which it sources
// data for further processing.
//
// PIPELINE_INPUT-only threads source data into the pipeline.  They do not get
// their input data from a shared memory ring buffer.  They get their data from
// external sources (e.g.  files or the network) or generate it internally
// (e.g.  for test vectors).  PIPELINE_INPUT-only threads have an output data
// buffer, but no input data buffer (their input does not come from a shared
// memory ring buffer).
//
// PIPELINE_OUTPUT-only threads sink data from the pipeline.  Thy do not put
// their output data into a shared memory ring buffer.  They send their data to
// external sinks (e.g. files or the network) of consume it internally (e.g.
// comparing against expected output).  PIPELINE_OUTPUT-only threads have an
// input data buffer, but no output data buffer (their output data does not go
// the a shared memory ring buffer).
//
// Threads that are both PIPELINE_INPUT and PIPELINE_OUTPUT get their input
// data from one shared memory region (their input data buffer), process it,
// and store the output data in another shared memory region (their output data
// buffer).
//
// The pipeline thread's metadata consists of the following information:
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

// These typedefs are used to declare pointers to a pipeline thread module's
// init and run functions.
typedef int (* initfunc_t)(hashpipe_thread_args_t *);
typedef void * (* runfunc_t)(hashpipe_thread_args_t *);

// This typedefs are used to declare pointers to a pipline thread's data buffer
// create function.
typedef hashpipe_databuf_t * (* databuf_createfunc_t)(int, int);

typedef struct {
  databuf_createfunc_t create;
} databuf_desc_t;

// This structure is used to store metadata about a pipeline thread module.
// Typically a pipeline thread module will define one of these per thread as a
// static (i.e. file private) variable.
struct pipeline_thread_module {
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
    pipeline_thread_module_t *module;
    int instance_id;
    int input_buffer;
    int output_buffer;
    unsigned int cpu_mask; // 0 means use inherited
    int priority;
    int finished;
    pthread_cond_t finished_c;
    pthread_mutex_t finished_m;
    hashpipe_status_t st;
    hashpipe_databuf_t *ibuf;
    hashpipe_databuf_t *obuf;
};

// This function is used by pipeline thread modules to register themselves with
// the pipeline executable.
int register_pipeline_thread_module(pipeline_thread_module_t * ptm);

/* Functions to query, set, and clear the "run_threads" flag. */
int run_threads();
void set_run_threads();
void clear_run_threads();

// Used to return OK status via return from run
#define THREAD_OK ((void *)0)
// Used to return error status via return from run
#define THREAD_ERROR ((void *)-1)

#endif // _HASHPIPE_H
