#ifndef _HASHPIPE_THREAD_H
#define _HASHPIPE_THREAD_H

#include <stdio.h>
#include "hashpipe.h"

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

#endif // _HASHPIPE_THREAD_H
