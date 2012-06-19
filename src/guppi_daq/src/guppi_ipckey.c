/*
 *  guppi_ipckey.h
 *
 *  Declare function used to get IPC keys.
 */

#include <stdio.h>
#include <stdlib.h>
#include "guppi_ipckey.h"

/*
 * Get the base key to use for guppi databufs and statusbufs.
 *
 * The base key is obtained by calling the ftok function, using the value of
 * $GUPPI_KEYFILE, if defined, or $HOME from the environment or, if $HOME is
 * not defined, by using "/tmp".  By default (i.e. GUPPI_KEYFILE does not exist
 * in the environment), this will create and connect to a user specific set of
 * shared memory buffers (provided $HOME exists in the environment), but if
 * desired users can connect to any other set of memory buffers by setting
 * GUPPI_KEYFILE appropraitely.
 *
 * The proj_id key is used to allow the caller to have mulitple per-user keys.
 * This function is declared static, so only the functions in this file (i.e.
 * guppi_databuf_key() and guppi_status_key() can call it.
 *
 * GUPPI_KEY_ERROR is returned on error.
 */
static key_t guppi_ipckey(int proj_id)
{
    key_t key = -1;
    char * keyfile = getenv("GUPPI_KEYFILE");
    if(!keyfile) {
        keyfile = getenv("HOME");
        if(!keyfile) {
            keyfile = "/tmp";
        }
    }

#ifdef GUPPI_VERBOSE
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
 * Get the base key to use for guppi databufs.
 * The lower 6 bits of the instance_id parameter are used to allow multiple
 * instances to run under the same user without collision.  The same
 * instance_id can and should be used for databuf keys and status keys.
 */
key_t guppi_databuf_key(int instance_id)
{
    static key_t key = GUPPI_KEY_ERROR;
    // Lazy init
    if(key == GUPPI_KEY_ERROR) {
        char *databuf_key = getenv("GUPPI_DATABUF_KEY");
        if(databuf_key) {
            key = strtoul(databuf_key, NULL, 0);
        } else {
            // Use instance_id to generate proj_id for guppi_ipckey.
            // Databuf proj_id is 10XXXXXX (binary) where XXXXXX are the 6 LSbs
            // of instance_id.
            key = guppi_ipckey((instance_id&0x3f)|0x80);
        }
    }
    return key;
}

/*
 * Get the base key to use for the guppi status buffer.
 * The the comments for guppi_databuf_key for details on the instance_id
 * parameter.
 */
key_t guppi_status_key(int instance_id)
{
    static key_t key = GUPPI_KEY_ERROR;
    // Lazy init
    if(key == GUPPI_KEY_ERROR) {
        char *status_key = getenv("GUPPI_STATUS_KEY");
        if(status_key) {
            key = strtoul(status_key, NULL, 0);
        } else {
            // Use instance_id to generate proj_id for guppi_ipckey.
            // Status proj_id is 01XXXXXX (binary) where XXXXXX are the 6 LSbs
            // of instance_id.
            key = guppi_ipckey((instance_id&0x3f)|0x40);
        }
    }
    return key;
}
