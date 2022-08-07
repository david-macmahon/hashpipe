/* hashpipe_ibvpkt_databuf.h
 *
 * Defines input data buffer for use with ibvpkt_thread.
 */
#ifndef _HASHPIPE_IBVPKT_DATABUF_H
#define _HASHPIPE_IBVPKT_DATABUF_H

#include <stddef.h> // for NULL
#include <stdint.h>
#include "hashpipe_databuf.h"
#include "hashpipe_status.h"
#include "hashpipe_error.h"
#include "hashpipe_ibverbs.h"
#include "config.h"

#ifndef HASHPIPE_IBVPKT_DATABUF_NBLOCKS
#define HASHPIPE_IBVPKT_DATABUF_NBLOCKS (4)
#endif

// Default to 128 MiB
#ifndef HASHPIPE_IBVPKT_DATABUF_BLOCK_DATA_SIZE
#define HASHPIPE_IBVPKT_DATABUF_BLOCK_DATA_SIZE (128*1024*1024)
#endif

// Maximum number of chunks supported per packet
#ifndef HASHPIPE_IBVPKT_DATABUF_MAX_PKT_CHUNKS
#define HASHPIPE_IBVPKT_DATABUF_MAX_PKT_CHUNKS (8)
#endif

// Alignment size to use for packet chunks.  Currently set to 64 (== 512/8) for
// compatibility with AVX512 instructions.
#ifndef HASHPIPE_IBVPKT_PKT_CHUNK_ALIGNMENT_SIZE
#define HASHPIPE_IBVPKT_PKT_CHUNK_ALIGNMENT_SIZE (64)
#endif

// Structure that holds info about a "chunk".  A chunk is part of a packet that
// is stored at a PKT_CHUNK_ALIGNMENT_SIZE aligned address.  The chunk_size is
// the number of bytes from the packet that are stored in the chunk.  The
// chunk_aligned_size is chunk_size rounded up to the next multiple of
// PKT_CHUNK_ALIGNMENT_SIZE.  The chunk_offset is the offset of the chunk from
// the packet's first chunk.  The first chunk will have a chunk_offset of 0.
struct hashpipe_pktbuf_chunk {
  size_t chunk_size;
  size_t chunk_aligned_size;
  off_t chunk_offset;
};

// Structure that holds info about packet/slot/block sizing.  A block is divided
// into "slots".  Each slot holds one packet, possibly with internal and/or
// trailing padding added to align various sections of the packet to
// HASHPIPE_IBVPKT_PKT_CHUNK_ALIGNMENT_SIZE.  These sections are called chunks.
// num_chunks specifies the number of chunks that are being used.  The pkt_size
// is the sum of the (unaligned) sizes of all chunks.  The slot_size is the size
// of a slot and equals the sum of the chunk_aligned_sizes.  slots_per_block is
// the number of slots in a data block.  Note that slot_size * slots_per_block
// may be less than the size of data block by up
// HASHPIPE_IBVPKT_PKT_CHUNK_ALIGNMENT_SIZE-1 bytes.
typedef struct hashpipe_pktbuf_info {
  uint32_t num_chunks;
  size_t pkt_size;
  size_t slot_size;
  size_t slots_per_block;
  struct hashpipe_pktbuf_chunk chunks[HASHPIPE_IBVPKT_DATABUF_MAX_PKT_CHUNKS];
} hashpipe_pktbuf_info_t;

typedef struct hashpipe_ibvpkt_block {
  char data[HASHPIPE_IBVPKT_DATABUF_BLOCK_DATA_SIZE];
} hashpipe_ibvpkt_block_t;

// Technically we only need to align to 512 bytes,
// but this keeps things 4K (i.e. page) aligned.
#define HASHPIPE_IBVPKT_DATABUF_ALIGNMENT_SIZE (4096)

// Used to pad after hashpipe_databuf_t to maintain data alignment
typedef uint8_t hashpipe_databuf_alignment[
  HASHPIPE_IBVPKT_DATABUF_ALIGNMENT_SIZE -
    (sizeof(hashpipe_databuf_t)%HASHPIPE_IBVPKT_DATABUF_ALIGNMENT_SIZE)
];

// Used to pad before data blocks to maintain data alignment
typedef uint8_t hashpipe_ibvpkt_alignment[
  HASHPIPE_IBVPKT_DATABUF_ALIGNMENT_SIZE -
    ((sizeof(hashpipe_databuf_t) +
      sizeof(hashpipe_pktbuf_info_t) +
      sizeof(struct hashpipe_ibv_context)) %HASHPIPE_IBVPKT_DATABUF_ALIGNMENT_SIZE)
];

typedef struct hashpipe_ibvpkt_databuf {
  hashpipe_databuf_t header;
  hashpipe_pktbuf_info_t pktbuf_info;
  struct hashpipe_ibv_context hibv_ctx;
  hashpipe_ibvpkt_alignment _padding; // Maintain data alignment
  hashpipe_ibvpkt_block_t block[HASHPIPE_IBVPKT_DATABUF_NBLOCKS];
} hashpipe_ibvpkt_databuf_t;

/*
 * IBVPKT BUFFER FUNCTIONS
 */

hashpipe_databuf_t *hashpipe_ibvpkt_databuf_create(int instance_id, int databuf_id);

static inline hashpipe_ibvpkt_databuf_t * hashpipe_ibvpkt_databuf_attach(int instance_id, int databuf_id)
{
    return (hashpipe_ibvpkt_databuf_t *)hashpipe_databuf_attach(instance_id, databuf_id);
}

static inline int hashpipe_ibvpkt_databuf_detach(hashpipe_ibvpkt_databuf_t *d)
{
    return hashpipe_databuf_detach((hashpipe_databuf_t *)d);
}

static inline void hashpipe_ibvpkt_databuf_clear(hashpipe_ibvpkt_databuf_t *d)
{
    hashpipe_databuf_clear((hashpipe_databuf_t *)d);
}

static inline int hashpipe_ibvpkt_databuf_block_status(hashpipe_ibvpkt_databuf_t *d, int block_id)
{
    return hashpipe_databuf_block_status((hashpipe_databuf_t *)d, block_id);
}

static inline int hashpipe_ibvpkt_databuf_total_status(hashpipe_ibvpkt_databuf_t *d)
{
    return hashpipe_databuf_total_status((hashpipe_databuf_t *)d);
}

static inline int hashpipe_ibvpkt_databuf_wait_free_timeout(
    hashpipe_ibvpkt_databuf_t *d, int block_id, struct timespec *timeout)
{
    return hashpipe_databuf_wait_free_timeout((hashpipe_databuf_t *)d,
        block_id, timeout);
}

static inline int hashpipe_ibvpkt_databuf_wait_free(hashpipe_ibvpkt_databuf_t *d, int block_id)
{
    return hashpipe_databuf_wait_free((hashpipe_databuf_t *)d, block_id);
}

static inline int hashpipe_ibvpkt_databuf_busywait_free(hashpipe_ibvpkt_databuf_t *d, int block_id)
{
    return hashpipe_databuf_busywait_free((hashpipe_databuf_t *)d, block_id);
}

static inline int hashpipe_ibvpkt_databuf_wait_filled_timeout(
    hashpipe_ibvpkt_databuf_t *d, int block_id, struct timespec *timeout)
{
    return hashpipe_databuf_wait_filled_timeout((hashpipe_databuf_t *)d,
        block_id, timeout);
}

static inline int hashpipe_ibvpkt_databuf_wait_filled(hashpipe_ibvpkt_databuf_t *d, int block_id)
{
    return hashpipe_databuf_wait_filled((hashpipe_databuf_t *)d, block_id);
}

static inline int hashpipe_ibvpkt_databuf_busywait_filled(hashpipe_ibvpkt_databuf_t *d, int block_id)
{
    return hashpipe_databuf_busywait_filled((hashpipe_databuf_t *)d, block_id);
}

static inline int hashpipe_ibvpkt_databuf_set_free(hashpipe_ibvpkt_databuf_t *d, int block_id)
{
    return hashpipe_databuf_set_free((hashpipe_databuf_t *)d, block_id);
}

static inline int hashpipe_ibvpkt_databuf_set_filled(hashpipe_ibvpkt_databuf_t *d, int block_id)
{
    return hashpipe_databuf_set_filled((hashpipe_databuf_t *)d, block_id);
}

// Function to get a pointer to a hashpipe_ibvpkt_databuf's pktbuf_info
// structure.  Is this too trivial to be useful?
static inline hashpipe_pktbuf_info_t *
hashpipe_ibvpkt_databuf_pktbuf_info_ptr(hashpipe_ibvpkt_databuf_t *db)
{
  return &(db->pktbuf_info);
}

// Function to get a pointer to a hashpipe_ibvpkt_databuf's hibv_ctx
// structure.  Is this too trivial to be useful?
static inline struct hashpipe_ibv_context *
hashpipe_ibvpkt_databuf_hibv_ctx_ptr(hashpipe_ibvpkt_databuf_t *db)
{
  return &(db->hibv_ctx);
}

// Function to get a pointer to the data portion of block `block_id` of a
// hashpipe_ibvpkt_databuf.
static inline char *
hashpipe_ibvpkt_databuf_data(struct hashpipe_ibvpkt_databuf *d, int block_id) {
    if(block_id < 0 || d->header.n_block < block_id) {
        hashpipe_error(__FUNCTION__,
            "block_id %s out of range [0, %d)",
            block_id, d->header.n_block);
        return NULL;
    } else {
        return d->block[block_id].data;
    }
}

// Function to get the offset within a slot to an (unaligned) offset within a
// packet.  This accounts for the padding between chunks.  For example, if the
// chuck sizes are 14,20,1500 (e.g. MAC,IP,PAYLOAD) and the chunks are aligned
// on 64 byte boundaries, then (unaligned) packet offset 34 (e.g. start of
// PAYLOAD) would have (aligned) slot offset 64.
off_t
hashpipe_ibvpkt_databuf_slot_offset(hashpipe_ibvpkt_databuf_t *db, off_t pkt_offset);

// Function to get a pointer to slot "slot_id" in block "block_id" of databuf
// "db".
static inline
uint8_t *
hashpipe_ibvpkt_databuf_block_slot_ptr(hashpipe_ibvpkt_databuf_t *db,
    uint64_t block_id, uint32_t slot_id)
{
  block_id %= db->header.n_block;
  return (uint8_t *)db->block[block_id].data + slot_id * db->pktbuf_info.slot_size;
}

// Function that threads can call to wait for ibvpkt_thread to finalize
// ibverbs setup.  After this function returns, the underlying
// hashpipe_ibv_context structure used by ibvpkt_thread will be fully
// initialized and flows can be created/destroyed by calling
// hashpipe_ibvpkt_flow().
void hashpipe_ibvpkt_databuf_wait_running(hashpipe_status_t * st);

#endif // _HASHPIPE_IBVPKT_DATABUF_H
