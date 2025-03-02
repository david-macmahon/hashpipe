#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "hashpipe.h"

static int run_threads_flag = 1;

static hashpipe_thread_desc_t *thread_list[MAX_HASHPIPE_THREADS];
static int num_threads = 0;

// Functions to query the run threads flag
int run_threads()
{
  return run_threads_flag;
}

// Functions to set and clear the run threads flag
void set_run_threads()
{
  run_threads_flag = 1;
}

void clear_run_threads()
{
  run_threads_flag = 0;
}

// Register a thread descriptor
int
register_hashpipe_thread(hashpipe_thread_desc_t * ptm)
{
  int rc = 1;
  if(num_threads < MAX_HASHPIPE_THREADS) {
    // Copy ptm since caller might reuse structure for multiple calls
    thread_list[num_threads] = malloc(sizeof(hashpipe_thread_desc_t));
    if(thread_list[num_threads]) {
      memcpy(thread_list[num_threads], ptm, sizeof(hashpipe_thread_desc_t));
      num_threads++;
      rc = 0;
    }
  }
  return rc;
}

// Find a thread descriptor by name
hashpipe_thread_desc_t *
find_hashpipe_thread(char *name)
{
  int i;
  for(i=0; i<num_threads; i++) {
    if(!strcmp(name, thread_list[i]->name)) {
      return thread_list[i];
    }
  }
  return NULL;
}

// List all hashpipe threads to FILE * f
void
list_hashpipe_threads(FILE * f)
{
  int i;
  printf("Known input-only threads:\n");
  for(i=0; i<num_threads; i++) {
    if(!thread_list[i]->ibuf_desc.create && thread_list[i]->obuf_desc.create) {
      fprintf(f, "  %s\n", thread_list[i]->name);
    }
  }
  printf("Known input/output threads:\n");
  for(i=0; i<num_threads; i++) {
    if(thread_list[i]->ibuf_desc.create && thread_list[i]->obuf_desc.create) {
      fprintf(f, "  %s\n", thread_list[i]->name);
    }
  }
  printf("Known output-only threads:\n");
  // Need to explicitly show null_output_thread
  // because it has neither ibof nor obuf.
  fprintf(f, "  null_output_thread\n");
  for(i=0; i<num_threads; i++) {
    if(thread_list[i]->ibuf_desc.create && !thread_list[i]->obuf_desc.create) {
      fprintf(f, "  %s\n", thread_list[i]->name);
    }
  }
  printf("Known utility threads:\n");
  for(i=0; i<num_threads; i++) {
    // Need to explicitly NOT show null_output_thread
    if(strcmp(thread_list[i]->name, "null_output_thread")) {
      if(!thread_list[i]->ibuf_desc.create && !thread_list[i]->obuf_desc.create) {
        fprintf(f, "  %s\n", thread_list[i]->name);
      }
    }
  }
}
// Function to get CPU affinity
unsigned int
get_cpu_affinity()
{
    int i;
    unsigned int mask=0;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    i = sched_getaffinity(0, sizeof(cpu_set_t), &cpuset);
    if (i<0) {
        hashpipe_error(__FUNCTION__, "Error getting cpu affinity.");
        return 0;
    }
    // Only handle 32 cores (for now)
    for(i=31; i>=0; i--) {
        mask <<= 1;
        if(CPU_ISSET(i, &cpuset)) {
          mask |= 1;            }
    }
    return mask;
}
