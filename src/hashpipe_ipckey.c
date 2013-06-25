/*
 *  hashpipe_ipckey.h
 *
 *  Declare function used to get IPC keys.
 */

#include <stdio.h>
#include <stdlib.h>
#include "hashpipe_ipckey.h"

/*
 * Get the base key to use for hashpipe databufs and statusbufs.
 *
 * The base key is obtained by calling the ftok function, using the value of
 * $HASHPIPE_KEYFILE, if defined, or $HOME from the environment or, if $HOME is
 * not defined, by using "/tmp".  By default (i.e. HASHPIPE_KEYFILE does not
 * exist in the environment), this will create and connect to a user specific
 * set of shared memory buffers (provided $HOME exists in the environment), but
 * if desired users can connect to any other set of memory buffers by setting
 * HASHPIPE_KEYFILE appropraitely.
 *
 * The proj_id key is used to allow the caller to have mulitple per-user keys.
 * This function is declared static, so only the functions in this file (i.e.
 * hashpipe_databuf_key() and hashpipe_status_key() can call it.
 *
 * HASHPIPE_KEY_ERROR is returned on error.
 */
static key_t hashpipe_ipckey(int proj_id)
{
    key_t key = -1;
    char * keyfile = getenv("HASHPIPE_KEYFILE");
    if(!keyfile) {
        keyfile = getenv("HOME");
        if(!keyfile) {
            keyfile = "/tmp";
        }
    }

#ifdef HASHPIPE_VERBOSE
    fprintf(stderr,
            "using pathname '%s' and proj_id '%d' to generate base IPC key\n",
            keyfile, proj_id&0xff);
#endif

    key = ftok(keyfile, proj_id);

    if(key == -1) {
        perror("ftok");
    }

    return key;
}

/*
 * Get the base key to use for hashpipe databufs.
 * The lower 6 bits of the instance_id parameter are used to allow multiple
 * instances to run under the same user without collision.  The same
 * instance_id can and should be used for databuf keys and status keys.
 */
key_t hashpipe_databuf_key(int instance_id)
{
    key_t key = HASHPIPE_KEY_ERROR;
    char *databuf_key = getenv("HASHPIPE_DATABUF_KEY");
    if(databuf_key) {
        key = strtoul(databuf_key, NULL, 0);
    } else {
        // Use instance_id to generate proj_id for hashpipe_ipckey.
        // Databuf proj_id is 10XXXXXX (binary) where XXXXXX are the 6 LSbs
        // of instance_id.
        key = hashpipe_ipckey((instance_id&0x3f)|0x80);
    }
    return key;
}

/*
 * Get the base key to use for the hashpipe status buffer.
 * The the comments for hashpipe_databuf_key for details on the instance_id
 * parameter.
 */
key_t hashpipe_status_key(int instance_id)
{
    key_t key = HASHPIPE_KEY_ERROR;
    char *status_key = getenv("HASHPIPE_STATUS_KEY");
    if(status_key) {
        key = strtoul(status_key, NULL, 0);
    } else {
        // Use instance_id to generate proj_id for hashpipe_ipckey.
        // Status proj_id is 01XXXXXX (binary) where XXXXXX are the 6 LSbs
        // of instance_id.
        key = hashpipe_ipckey((instance_id&0x3f)|0x40);
    }
    return key;
}
