#ifndef _HASHPIPE_THREAD_H
#define _HASHPIPE_THREAD_H

#include <stdio.h>
#include "hashpipe.h"

// Set CPU affinity of calling thread
int set_cpu_affinity(unsigned int mask);

// Get CPU affinity of calling thread
// Returns 0 on error
unsigned int get_cpu_affinity();

// Set priority of calling thread
int set_priority(int priority);

#endif // _HASHPIPE_THREAD_H
