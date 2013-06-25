/* hashpipe_error.h
 *
 * Error handling routines for hashpipe.
 */
#ifndef _HASHPIPE_ERROR_H
#define _HASHPIPE_ERROR_H

/* Some exit codes */
#define HASHPIPE_OK          0
#define HASHPIPE_TIMEOUT     1 // Call timed out 
#define HASHPIPE_ERR_GEN    -1 // Super non-informative
#define HASHPIPE_ERR_SYS    -2 // Failed system call
#define HASHPIPE_ERR_PARAM  -3 // Parameter out of range
#define HASHPIPE_ERR_KEY    -4 // Requested key doesn't exist
#define HASHPIPE_ERR_PACKET -5 // Unexpected packet size

#define DEBUGOUT 0 

/* Call this to log an error message */
void hashpipe_error(const char *name, const char *msg, ...);

/* Call this to log an warning message */
void hashpipe_warn(const char *name, const char *msg, ...);

#endif
