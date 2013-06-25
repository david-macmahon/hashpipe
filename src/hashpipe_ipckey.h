/*
 *  hashpipe_ipckey.h
 *
 *  Declare functions used to get IPC keys for HASHPIPE.
 */

#ifndef _HASHPIPE_IPCKEY_H
#define _HASHPIPE_IPCKEY_H

#include <sys/ipc.h>

#define HASHPIPE_KEY_ERROR ((key_t)-1)
/*
 * Get the base key to use for hashpipe databufs.
 *
 * If HASHPIPE_DATABUF_KEY is defined in the environment, its value is used as
 * the base databuf key.  Otherwise, the base key is obtained by calling the
 * ftok function, using the value of $HASHPIPE_KEYFILE, if defined, or $HOME
 * from the environment or, if $HOME is not defined, "/tmp" for the pathname
 * and a databuf specific proj_id derived from the lower 6 bits of the
 * instance_id.  Use of instance_id allows a user to run multiple instances of
 * a pipeline without having to alter the environment (even from within the
 * same process).
 *
 * By default (i.e. HASHPIPE_DATABUF_KEY and HASHPIPE_KEYFILE do not exist in
 * the environment), this will create and connect to a user specific set of
 * shared memory buffers (provided $HOME exists in the environment), but if
 * desired users can connect to any other set of memory buffers by setting
 * HASHPIPE_KEYFILE appropraitely.
 *
 * HASHPIPE_KEY_ERROR is returned on error.
 */
key_t hashpipe_databuf_key(int instance_id);

/*
 * Get the base key to use for the hashpipe status buffer.
 *
 * If HASHPIPE_STATUS_KEY is defined in the environment, its value is used as
 * the base databuf key.  Otherwise, the base key is obtained by calling the
 * ftok function, using the value of $HASHPIPE_KEYFILE, if defined, or $HOME
 * from the environment or, if $HOME is not defined, "/tmp" for the pathname
 * and a status buffer specific proj_id derived from the lower 6 bits of the
 * instance_id.  Use of instance_id allows a user to run multiple instances of
 * a pipeline without having to alter the environment (even from within the
 * same process).
 *
 * By default (i.e. HASHPIPE_STATUS_KEY and HASHPIPE_KEYFILE do not exist in
 * the environment), this will create and connect to a user specific set of
 * shared memory buffers (provided $HOME exists in the environment), but if
 * desired users can connect to any other set of memory buffers by setting
 * HASHPIPE_KEYFILE appropraitely.
 *
 * HASHPIPE_KEY_ERROR is returned on error.
 */
key_t hashpipe_status_key(int instance_id);

#endif // _HASHPIPE_IPCKEY_H
