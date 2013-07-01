#include <string.h>

#include "hashpipe.h"

#define MAX_THREADS 1024
static hashpipe_thread_desc_t *thread_list[MAX_THREADS];
static int num_threads = 0;

int
register_hashpipe_thread(hashpipe_thread_desc_t * ptm)
{
  int rc = 1;
  if(num_threads < MAX_THREADS) {
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
}
