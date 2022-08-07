/* hpguppi_ibvpkt_databuf.c */
 
#include <time.h>
#include "hashpipe_ibvpkt_databuf.h"

hashpipe_databuf_t *hashpipe_ibvpkt_databuf_create(int instance_id, int databuf_id)
{
    int i;

    /* Calc databuf sizes */
    size_t header_size = sizeof(hashpipe_databuf_t)
                       + sizeof(hashpipe_pktbuf_info_t)
                       + sizeof(struct hashpipe_ibv_context)
                       + sizeof(hashpipe_ibvpkt_alignment);

    size_t block_size  = sizeof(hashpipe_ibvpkt_block_t);
    int n_block = HASHPIPE_IBVPKT_DATABUF_NBLOCKS;

    hashpipe_ibvpkt_databuf_t * d = (hashpipe_ibvpkt_databuf_t *)
        hashpipe_databuf_create(
            instance_id, databuf_id, header_size, block_size, n_block);

    if(!d) {
      return NULL;
    }

    /* Zero out blocks */
    for(i=0; i<n_block; i++) {
      memset(&(d->block[i]), 0, sizeof(hashpipe_ibvpkt_block_t));
    }

    return (hashpipe_databuf_t *)d;
}

// Function to get the offset within a slot corresponding to an (unaligned)
// offset within a packet.  This accounts for the padding between chunks.  For
// example, if the chuck sizes are 14,20,1500 (e.g. MAC,IP,PAYLOAD) and the
// chunks are aligned on 64 byte boundaries, then (unaligned) packet offset 34
// (e.g. start of PAYLOAD) would have (aligned) slot offset 64.
off_t
hashpipe_ibvpkt_databuf_slot_offset(hashpipe_ibvpkt_databuf_t *db, off_t pkt_offset)
{
  int i;
  off_t slot_offset;
  hashpipe_pktbuf_info_t * pktbuf_info = hashpipe_ibvpkt_databuf_pktbuf_info_ptr(db);
  for(i=0; i<pktbuf_info->num_chunks; i++) {
    // If pkt_offset is within this chunk, break out of loop
    if(pkt_offset < pktbuf_info->chunks[i].chunk_size) {
      break;
    }
    pkt_offset -= pktbuf_info->chunks[i].chunk_size;
  }

  // If pkt_offset exceeds sum of chunk sizes (i.e. if we didn't break out of
  // the loop early), it's an error, but we recurse until we get down to a
  // pkt_offset that doesn't exceed the sum of chuck sizes.
  if(i == pktbuf_info->num_chunks) {
    slot_offset = hashpipe_ibvpkt_databuf_slot_offset(db, pkt_offset);
  } else {
    slot_offset = pktbuf_info->chunks[i].chunk_offset + pkt_offset;
  }

  return slot_offset;
}

// Function that the downstream thread can call to wait for ibvpkt_thread to
// finalize ibverbs setup.  After this function returns, the underlying
// hashpipe_ibv_context structure used by ibvpkt_thread will be fully
// initialized and flows can be created/destroyed by calling hashpipe_ibv_flow()
// with a pointer to the hashpipe_ibvpkt_datavuf's `hpibv_ctx` field.
void
hashpipe_ibvpkt_databuf_wait_running(hashpipe_status_t * st)
{
  char ibvstat[80] = {0};
  struct timespec ts_sleep = {
    .tv_sec  = 0,
    .tv_nsec = 100*1000*1000 // 100 ms
  };

  // Loop until break when IBVSTAT is "running"
  for(;;) {
    hashpipe_status_lock_safe(st);
    {
      hgets(st->buf,  "IBVSTAT", sizeof(ibvstat), ibvstat);
    }
    hashpipe_status_unlock_safe(st);

    if(!strcmp(ibvstat, "running")) {
      break;
    }

    nanosleep(&ts_sleep, NULL);
  }
}
