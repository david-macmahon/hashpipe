#ifndef _HASHPIPE_THREAD_ARGS_H
#define _HASHPIPE_THREAD_ARGS_H
/* Generic thread args type with input/output buffer
 * id numbers.  Not all threads have both a input and a
 * output.
 */

#include "hashpipe.h"

void hashpipe_thread_args_init(hashpipe_thread_args_t *a);
void hashpipe_thread_args_destroy(hashpipe_thread_args_t *a);
void hashpipe_thread_set_finished(hashpipe_thread_args_t *a);
int hashpipe_thread_finished(hashpipe_thread_args_t *a, float timeout_sec);
#endif // _HASHPIPE_THREAD_ARGS_H
