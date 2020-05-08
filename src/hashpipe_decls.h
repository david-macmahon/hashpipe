#ifndef _HASHPIPE_DECLS_H
#define _HASHPIPE_DECLS_H

#include "hashpipe.h"

#ifdef __cplusplus
extern "C" {
#endif


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
