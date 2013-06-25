#ifndef _HASHPIPE_THREAD_ARGS_H
#define _HASHPIPE_THREAD_ARGS_H
/* Generic thread args type with input/output buffer
 * id numbers.  Not all threads have both a input and a
 * output.
 */
#include <pthread.h>
#include <sys/time.h>
#include <math.h>
struct hashpipe_thread_args {
    int instance_id;
    int input_buffer;
    int output_buffer;
    unsigned int cpu_mask; // 0 means use inherited
    int priority;
    int finished;
    pthread_cond_t finished_c;
    pthread_mutex_t finished_m;
};
void hashpipe_thread_args_init(struct hashpipe_thread_args *a);
void hashpipe_thread_args_destroy(struct hashpipe_thread_args *a);
void hashpipe_thread_set_finished(struct hashpipe_thread_args *a);
int hashpipe_thread_finished(struct hashpipe_thread_args *a, 
        float timeout_sec);
#endif // _HASHPIPE_THREAD_ARGS_H
