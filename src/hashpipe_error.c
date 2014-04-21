/* hashpipe_error.c
 *
 * Error handling routine
 */
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include "hashpipe_error.h"

void log_timestamp(char * timestamp) {
    time_t now;
    time(&now);
    ctime_r(&now, timestamp);
    timestamp[strlen(timestamp)-1] = '\0';  // strip the newline
}

/* For now just put it all to stderr.
 * Maybe do something clever like a stack in the future?
 */
void hashpipe_error(const char *name, const char *msg, ...) {
    char timestamp[256];
    log_timestamp(timestamp);
    fprintf(stderr, "%s : ", timestamp);
    fprintf(stderr, "Error (%s)", name);
    if(msg) {
        va_list ap;
        va_start(ap, msg);
        fprintf(stderr, ": ");
        vfprintf(stderr, msg, ap);
        va_end(ap);
    }
    if(errno) {
        fprintf(stderr, " [%s]", strerror(errno));
    }
    fprintf(stderr, "\n");
    fflush(stderr);
}

void hashpipe_warn(const char *name, const char *msg, ...) {
    char timestamp[256];
    log_timestamp(timestamp);
    fprintf(stderr, "%s : ", timestamp);
    fprintf(stderr, "Warning (%s)", name);
    if(msg) {
        va_list ap;
        va_start(ap, msg);
        fprintf(stderr, ": ");
        vfprintf(stderr, msg, ap);
        va_end(ap);
    }
    fprintf(stderr, "\n");
    fflush(stderr);
}

void hashpipe_info(const char *name, const char *msg, ...) {
    char timestamp[256];
    log_timestamp(timestamp);
    fprintf(stderr, "%s : ", timestamp);
    fprintf(stderr, "Info (%s)", name);
    if(msg) {
        va_list ap;
        va_start(ap, msg);
        fprintf(stderr, ": ");
        vfprintf(stderr, msg, ap);
        va_end(ap);
    }
    fprintf(stderr, "\n");
    fflush(stderr);
}
