#ifndef _PAPER_THREAD_H
#define _PAPER_THREAD_H

#include <stdio.h>
#include "fitshead.h"
#include "hashpipe_status.h"
#include "hashpipe_thread_args.h"

/* Functions to query, set, and clear the "run_threads" flag. */
int run_threads();
void set_run_threads();
void clear_run_threads();

// A pipeline thread module encapsulates metadata and functionality for onr or more
// threads that can be used in a processing pipeline.  The pipeline executable
// dynamically assembles a pipeline at runtime consisting of multiple pipeline
// threads.
//
// Pipeline thread modules must register themselves with the pipeline
// executable via a call to register_pipeline_thread_module().  This is
// typically performed from a static C function with the constructor attribute
// in the pipeline thread module's source file.
//
// Pipeline thread modules are identified by their name.  The pipeline
// executable can find (registered) pipeline thread modules by their name.
// A pipeline thread can be a PIPELINE_INPUT_THREAD, a PIPELINE_OUTPUT_THREAD,
// or both.
//
// PIPELINE_INPUT-only threads source data into the pipeline.  They do not get
// their input data from a shared memory ring buffer.  They get their data from
// external sources (e.g.  files or the network) or generate it internally
// (e.g.  for test vectors).
//
// PIPELINE_OUTPUT-only threads sink data from the pipeline.  Thy do not put
// their output data into a shared memory ring buffer.  They send theor data to
// external sinks (e.g. files or the network) of consume it internally (e.g.
// comparing against expected output).
//
// Threads that are both PIPELINE_INPUT and PIPELINE_OUTPUT get their input
// data from one shared memory region, process it, and store the output data in
// another shared memory region.

#define PIPELINE_INPUT_THREAD  (1)
#define PIPELINE_OUTPUT_THREAD (2)
#define PIPELINE_INOUT_THREAD  (PIPELINE_OUTPUT_THREAD|PIPELINE_INPUT_THREAD)

// These typedefs are used to declare pointers to a pipeline thread module's
// init and run functions.
typedef int (* initfunc_t)(struct hashpipe_thread_args *);
typedef void *(* runfunc_t)(void *);

// This structure is used to store metadata about a pipeline thread module.
// Typically a pipeline thread module will define one of these as a static
// (i.e. file private) variable.
typedef struct {
  char * name;
  int type;
  initfunc_t init;
  runfunc_t run;
} pipeline_thread_module_t;

// This function is used by pipeline thread modules to register themselves with
// the pipeline executable.
int register_pipeline_thread_module(pipeline_thread_module_t * ptm);

// This function is used by the pipeline executable to find pipeline thread
// modules by name.  Returns a pointer to its pipeline_thread_module_t
// structure or NULL if a test with the given name is not found.
//
// NB: Names are case sensitive.
pipeline_thread_module_t * find_pipeline_thread_module(char *name);

// List all known pipeline thread modules to FILE f.
void list_pipeline_thread_modules(FILE * f);

// Set CPU affinity of calling thread
int set_cpu_affinity(unsigned int mask);

// Get CPU affinity of calling thread
// Returns 0 on error
unsigned int get_cpu_affinity();

// Set priority of calling thread
int set_priority(int priority);

// Preprocessor macros to simplify creation of loadable thread modules.

// Used to return error status via return from run
#define THREAD_ERROR ((void *)-1)

// Macros for the init function

// Attach to guppi status shared memory and set status_key to "init".
// Returns 1 from current function on error.
#define THREAD_INIT_ATTACH_STATUS(instance_id, st, status_key) \
  /* Attach to status shared mem area */                       \
  struct hashpipe_status st;                                   \
  do {                                                         \
    int rv = hashpipe_status_attach(instance_id, &st);         \
    if (rv!=HASHPIPE_OK) {                                     \
        hashpipe_error(__FUNCTION__,                           \
                "Error attaching to status shared memory.");   \
        return 1;                                              \
    }                                                          \
    /* Init status */                                          \
    hashpipe_status_lock_safe(&st);                            \
    hputs(st.buf, STATUS_KEY, "init");                         \
    hashpipe_status_unlock_safe(&st);                          \
  } while(0)

// Detach from guppi status shared memory.
// Returns 1 from current function on error.
#define THREAD_INIT_DETACH_STATUS(st)                          \
  do {                                                         \
    /* Detach from status shared mem area */                   \
    int rv = hashpipe_status_detach(&st);                      \
    if (rv!=HASHPIPE_OK) {                                     \
        hashpipe_error(__FUNCTION__,                           \
                "Error detaching from status shared memory."); \
        return 1;                                              \
    }                                                          \
  } while(0)

// Attach to guppi status shared memory and set status_key to "init".
// Returns 1 from current function on error.
#define THREAD_INIT_STATUS(instance_id, status_key) \
  do {                                 \
    THREAD_INIT_ATTACH_STATUS(instance_id, st, status_key);     \
    THREAD_INIT_DETACH_STATUS(st);     \
  } while(0)

// Create paper databuf of <type>
#define THREAD_INIT_DATABUF(instance_id, type, block_id)     \
  do {                                                       \
    struct type *db;                                         \
    db = type##_create(instance_id, block_id);               \
    if (db==NULL) {                                          \
        hashpipe_error(__FUNCTION__,                         \
            "Error attaching to databuf(%d) shared memory.", \
            block_id);                                       \
        return 1;                                            \
    }                                                        \
    /* Detach from paper_databuf */                          \
    type##_detach(db);                                       \
  } while(0)

// Macros for the run function

// Sets up call to set state to finished on thread exit
// Does 1 pthread_cleanup_push.
// Use THREAD_RUN_END to pop it.
#define THREAD_RUN_BEGIN(args) \
  pthread_cleanup_push((void *)hashpipe_thread_set_finished, args);

// Pops pthread cleanup for THREAD_RUN_BEGIN
#define THREAD_RUN_END pthread_cleanup_pop(0);

// Set CPU affinity and process priority from args.
#define THREAD_RUN_SET_AFFINITY_PRIORITY(args)                           \
  do {                                                                   \
    unsigned int mask = ((struct hashpipe_thread_args *)args)->cpu_mask; \
    int priority = ((struct hashpipe_thread_args *)args)->priority;      \
    if(set_cpu_affinity(mask) < 0) {                                     \
        perror("set_cpu_affinity");                                      \
        return THREAD_ERROR;                                             \
    }                                                                    \
    if(set_priority(priority) < 0) {                                     \
        perror("set_priority");                                          \
        return THREAD_ERROR;                                             \
    }                                                                    \
  } while(0)

// Attaches to status memory and creates variable st for it.
// Does 3 pthread_cleanup_push.
// Use THREAD_RUN_DETACH_STATUS to pop them.
#define THREAD_RUN_ATTACH_STATUS(instance_id, st)                \
  struct hashpipe_status st;                                     \
  {                                                              \
    int rv = hashpipe_status_attach(instance_id, &st);           \
    if (rv!=HASHPIPE_OK) {                                       \
        hashpipe_error(__FUNCTION__,                             \
                "Error attaching to status shared memory.");     \
        return THREAD_ERROR;                                     \
    }                                                            \
  }                                                              \
  pthread_cleanup_push((void *)hashpipe_status_detach, &st);     \
  pthread_cleanup_push((void *)set_exit_status, &st);

// Pops pthread cleanup for THREAD_RUN_ATTACH_STATUS
#define THREAD_RUN_DETACH_STATUS \
    pthread_cleanup_pop(0);      \
    pthread_cleanup_pop(0);

// Attaches to paper databuf of <type> identified by databuf_id
// and creates variable db for it.
// Does 1 pthread_cleanup_push
// Use THREAD_RUN_DETACH_DATAUF to pop it.
#define THREAD_RUN_ATTACH_DATABUF(instance_id, type,db,databuf_id) \
  struct type *db = type##_attach(instance_id, databuf_id);        \
  if (db==NULL) {                                                  \
      hashpipe_error(__FUNCTION__,                                 \
          "Error attaching to databuf(%d) shared memory.",         \
          databuf_id);                                             \
      return THREAD_ERROR;                                         \
  }                                                                \
  pthread_cleanup_push((void *)type##_detach, db);

// Pops pthread cleanup for THREAD_RUN_ATTACH_STATUS
#define THREAD_RUN_DETACH_DATAUF \
    pthread_cleanup_pop(0);

/* Safe lock/unlock functions for status shared mem. */
#define hashpipe_status_lock_safe(s) \
    pthread_cleanup_push((void *)hashpipe_status_unlock, s); \
    hashpipe_status_lock(s);

#define hashpipe_status_lock_busywait_safe(s) \
    pthread_cleanup_push((void *)hashpipe_status_unlock, s); \
    hashpipe_status_lock_busywait(s);

#define hashpipe_status_unlock_safe(s) \
    hashpipe_status_unlock(s); \
    pthread_cleanup_pop(0);

#ifdef STATUS_KEY
/* Exit handler that updates status buffer */
static inline void set_exit_status(struct hashpipe_status *s) {
    hashpipe_status_lock(s);
    hputs(s->buf, STATUS_KEY, "exiting");
    hashpipe_status_unlock(s);
}
#endif // STATUS_KEY

#endif // _PAPER_THREAD_H
