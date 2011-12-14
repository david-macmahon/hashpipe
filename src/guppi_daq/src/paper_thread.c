#include <string.h>
#include "paper_thread.h"

#define MAX_MODULES 1024
static pipeline_thread_module_t *module_list[MAX_MODULES];
static int num_modules = 0;

int
register_pipeline_thread_module(pipeline_thread_module_t * ptm)
{
  int rc = 1;
  if(num_modules < MAX_MODULES) {
    module_list[num_modules] = ptm;
    num_modules++;
    rc = 0;
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

