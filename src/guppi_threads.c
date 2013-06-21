#include "guppi_threads.h"

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
