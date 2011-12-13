#ifndef _PAPER_THREAD
#define _PAPER_THREAD

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

// Set CPU affinity and process priority from args
// Does 1 pthread_cleanup_push
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

// Attaches to status memory and creates variable st for it.
// Does 2 pthread_cleanup_push
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

// Attaches to paper databuf of <type> identified by databuf_id
// and creates variable db for it.
// Does 1 pthread_cleanup_push
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

#endif // _PAPER_THREAD
