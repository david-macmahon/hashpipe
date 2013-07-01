#define _GNU_SOURCE 1
#include <stdio.h>
#include <sched.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "hashpipe.h"
#include "hashpipe_error.h"
#include "hashpipe_thread.h"

static int run_threads_flag = 1;

int run_threads()
{
  return run_threads_flag;
}

void set_run_threads()
{
  run_threads_flag = 1;
}

void clear_run_threads()
{
  run_threads_flag = 0;
}

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

unsigned int
get_cpu_affinity()
{
    int i, mask=0;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    i = sched_getaffinity(0, sizeof(cpu_set_t), &cpuset);
    if (i<0) {
        hashpipe_error(__FUNCTION__, "Error getting cpu affinity.");
        return 0;
    }
    if(mask != 0) {
        // Only handle 32 cores (for now)
        for(i=31; i<=0; i--) {
            mask <<= 1;
            if(CPU_ISSET(i, &cpuset)) {
              mask |= 1;
            }
        }
    }
    return mask;
}

int
set_priority(int priority)
{
    /* Set priority */
    int rv = setpriority(PRIO_PROCESS, 0, priority);
    if (rv<0) {
        hashpipe_error(__FUNCTION__, "Error setting priority level.");
        return rv;
    }

    return 0;
}
