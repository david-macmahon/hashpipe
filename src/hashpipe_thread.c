#define _GNU_SOURCE 1
#include <stdio.h>
#include <string.h>
#include <sched.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "hashpipe.h"
#include "hashpipe_error.h"
#include "hashpipe_thread.h"

#define MAX_MODULES 1024
static pipeline_thread_module_t *module_list[MAX_MODULES];
static int num_modules = 0;

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
register_pipeline_thread_module(pipeline_thread_module_t * ptm)
{
  int rc = 1;
  if(num_modules < MAX_MODULES) {
    // Copy ptm since caller might reuse structure for multiple calls
    module_list[num_modules] = malloc(sizeof(pipeline_thread_module_t));
    if(module_list[num_modules]) {
      memcpy(module_list[num_modules], ptm, sizeof(pipeline_thread_module_t));
      num_modules++;
      rc = 0;
    }
  }
  return rc;
}

pipeline_thread_module_t *
find_pipeline_thread_module(char *name)
{
  int i;
  for(i=0; i<num_modules; i++) {
    if(!strcmp(name, module_list[i]->name)) {
      return module_list[i];
    }
  }
  return NULL;
}

void
list_pipeline_thread_modules(FILE * f)
{
  int i;
  printf("Known input-only thread modules:\n");
  for(i=0; i<num_modules; i++) {
    if(!module_list[i]->ibuf_desc.create && module_list[i]->obuf_desc.create) {
      fprintf(f, "  %s\n", module_list[i]->name);
    }
  }
  printf("Known input/output thread modules:\n");
  for(i=0; i<num_modules; i++) {
    if(module_list[i]->ibuf_desc.create && module_list[i]->obuf_desc.create) {
      fprintf(f, "  %s\n", module_list[i]->name);
    }
  }
  printf("Known output-only thread modules:\n");
  // Need to explicitly show null_output_thread
  // because it has neither ibof nor obuf.
  fprintf(f, "  null_output_thread\n");
  for(i=0; i<num_modules; i++) {
    if(module_list[i]->ibuf_desc.create && !module_list[i]->obuf_desc.create) {
      fprintf(f, "  %s\n", module_list[i]->name);
    }
  }
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
