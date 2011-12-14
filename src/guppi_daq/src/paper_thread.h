#ifndef _PAPER_THREAD
#define _PAPER_THREAD

#include "guppi_thread_args.h"

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
typedef int (* initfunc_t)(struct guppi_thread_args *);
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

// Preprocessor macros to simplify creation of loadable thread modules.

// Used to return error status via return from run
#define THREAD_ERROR ((void *)-1)

// Macros for the init function

// Attach to guppi status shared memory and set status_key to "init".
// Returns 1 from current function on error.
#define THREAD_INIT_STATUS(status_key)                         \
  do {                                                         \
    /* Attach to status shared mem area */                     \
    struct guppi_status st;                                    \
    int rv = guppi_status_attach(&st);                         \
    if (rv!=GUPPI_OK) {                                        \
        guppi_error(__FUNCTION__,                              \
                "Error attaching to status shared memory.");   \
        return 1;                                              \
    }                                                          \
    /* Init status */                                          \
    guppi_status_lock_safe(&st);                               \
    hputs(st.buf, STATUS_KEY, "init");                         \
    guppi_status_unlock_safe(&st);                             \
    /* Detach from status shared mem area */                   \
    rv = guppi_status_detach(&st);                             \
    if (rv!=GUPPI_OK) {                                        \
        guppi_error(__FUNCTION__,                              \
                "Error detaching from status shared memory."); \
        return 1;                                              \
    }                                                          \
  } while(0)

// Create paper databuf of <type>
#define THREAD_INIT_DATABUF(type, n_block, block_size, block_id)       \
  do {                                                                 \
    struct type *db;                                                   \
    db = type##_create(n_block, block_size, block_id);                 \
    if (db==NULL) {                                                    \
        char msg[256];                                                 \
        sprintf(msg, "Error attaching to databuf(%d) shared memory.",  \
                block_id);                                             \
        guppi_error(__FUNCTION__, msg);                                \
        return 1;                                                      \
    }                                                                  \
    /* Detach from paper_databuf */                                    \
    type##_detach(db);                                                 \
  } while(0)

// Macros for the run function

// Set CPU affinity and process priority from args.
// Does 1 pthread_cleanup_push.
// Use THREAD_RUN_POP_AFFINITY_PRIORITY to pop it.
#define THREAD_RUN_SET_AFFINITY_PRIORITY(args)                      \
  {                                                                 \
    /* Set cpu affinity */                                          \
    /* TODO Pass values in via args */                              \
    cpu_set_t cpuset, cpuset_orig;                                  \
    sched_getaffinity(0, sizeof(cpu_set_t), &cpuset_orig);          \
    /*CPU_ZERO(&cpuset);*/                                          \
    CPU_CLR(13, &cpuset);                                           \
    CPU_SET(11, &cpuset);                                           \
    int rv = sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);      \
    if (rv<0) {                                                     \
        guppi_error(__FUNCTION__, "Error setting cpu affinity.");   \
        perror("sched_setaffinity");                                \
        return THREAD_ERROR;                                        \
    }                                                               \
    /* Set priority */                                              \
    rv = setpriority(PRIO_PROCESS, 0,                               \
        ((struct guppi_thread_args *)args)->priority);              \
    if (rv<0) {                                                     \
        guppi_error(__FUNCTION__, "Error setting priority level."); \
        perror("set_priority");                                     \
        return THREAD_ERROR;                                        \
    }                                                               \
  }                                                                 \
  pthread_cleanup_push((void *)guppi_thread_set_finished, args);

// Pops pthread cleanup for THREAD_RUN_SET_AFFINITY_PRIORITY
#define THREAD_RUN_POP_AFFINITY_PRIORITY \
    pthread_cleanup_pop(0);

// Attaches to status memory and creates variable st for it.
// Does 2 pthread_cleanup_push.
// Use THREAD_RUN_DETACH_STATUS to pop them.
#define THREAD_RUN_ATTACH_STATUS(st)                          \
  struct guppi_status st;                                     \
  {                                                           \
    int rv = guppi_status_attach(&st);                        \
    if (rv!=GUPPI_OK) {                                       \
        guppi_error(__FUNCTION__,                             \
                "Error attaching to status shared memory.");  \
        return THREAD_ERROR;                                  \
    }                                                         \
  }                                                           \
  pthread_cleanup_push((void *)guppi_status_detach, &st);     \
  pthread_cleanup_push((void *)set_exit_status, &st);

// Pops pthread cleanup for THREAD_RUN_ATTACH_STATUS
#define THREAD_RUN_DETACH_STATUS \
    pthread_cleanup_pop(0);      \
    pthread_cleanup_pop(0);

// Attaches to paper databuf of <type> identified by databuf_id
// and creates variable db for it.
// Does 1 pthread_cleanup_push
// Use THREAD_RUN_DETACH_DATAUF to pop it.
#define THREAD_RUN_ATTACH_DATABUF(type,db,databuf_id)               \
  struct type *db = type##_attach(databuf_id);                      \
  if (db==NULL) {                                                   \
      char msg[256];                                                \
      sprintf(msg, "Error attaching to databuf(%d) shared memory.", \
              databuf_id);                                          \
      guppi_error(__FUNCTION__, msg);                               \
      return THREAD_ERROR;                                          \
  }                                                                 \
  pthread_cleanup_push((void *)type##_detach, db);

// Pops pthread cleanup for THREAD_RUN_ATTACH_STATUS
#define THREAD_RUN_DETACH_DATAUF \
    pthread_cleanup_pop(0);

#endif // _PAPER_THREAD
