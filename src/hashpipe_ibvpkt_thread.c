// hashpipe_ibvpkt_thread.c
//
// A Hashpipe thread that receives packets from a network interface using IB
// Verbs.  This is purely a packet capture thread.  Packets are stored in
// blocks in the input data buffer in the order they are received.  A
// downstream thread can be used to process the packets further.
//
// The thread assumes a 9K frame size, but this can be overridden by the
// IBVPKTSZ status buffer keyword (see below).  The thread creates as many work
// requests as the NIC/driver will support and points them to sequential
// "slots" in the current data buffer block(s).  The work requests are posted
// and as the completion notifications are returned, the work requests are
// pointed to new locations in the data buffer and re-posted.  When the work
// requests are posted for a block beyond the currently "in use" blocks, the
// oldest block is "marked as filled" to pass that block to the downstream
// thread.
//
// The init() function sets up the packet sizing information and stores it in a
// pktbuf_info structure that is stored in the databuf's header.  This makes
// this information available to downstream tread/process as well as to the
// run() function which will use that information when allocating and
// initializing the work requests.  By default, the thread will assume a max
// packet size of 9K (a typical jumbo frame size), but this can be overridden
// by using "-o IBVPKTSZ=..." on the hashpipe command line.  The value of this
// parameter can be a single integer to specify a smaller max packet size
// (allowing more packets to fit in a data block), or it can be a comma
// separated list of packet "chunk sizes" whose sum should be equal to or
// greater than the max packet size.  When a list of chunk sizes is used, each
// work request will have one scatter/gather element for each chunk, and the
// chunks will be stored into consecutive 64 byte (512 bit) aligned locations.
// This can be used to effectively insert padding between the Ethernet/IP/UDP
// header and the packet payload (and optionally the payload's header and data
// portions) so that the packet payload will be 64-bit aligned to make it
// usable with AVX-512 (or other wide data) instructions.
//
// For example, the MeerKAT channelised antenna voltage data is sent as SPEAD
// packets which consist of a 42 byte Ethernet/IP/UDP header, a 96 byte SPEAD
// header, and a 1024 byte SPEAD payload.  Each packet is structured like this:
//
//     0x0000:  0100 5e09 02a8 248a 07cc 7e48 0800 4500
//     0x0010:  047c 0000 4000 fc11 7652 0a64 0809 ef09
//     0x0020:  02a8 1bec 1bec 0468 0000 5304 0206 0000
//     0x0030:  000b 8001 6068 752a 8032 8002 0000 0000
//     0x0040:  4000 8003 0000 0000 3c00 8004 0000 0000
//     0x0050:  0400 9600 6068 7520 0000 c101 0000 0000
//     0x0060:  0032 c103 0000 0000 0a80 4300 0000 0000
//     0x0070:  0000 8000 0000 0000 0000 8000 0000 0000
//     0x0080:  0000 8000 0000 0000 0000 fdfc 08f9 0a03
//     0x0090:  05fe 0509 0404 fdfa 06f5 0605 01fb 03fe
//     0x00a0:  fcfe 01fd ff02 fdff 02fb 000b fd09 05fc
//     ...
//
// By using IBVPKTSZ=42,96,1024, each packet gets stored in memory like this:
//
//     0x0000:  0100 5e09 02a8 248a 07cc 7e48 0800 4500
//     0x0010:  047c 0000 4000 fc11 7652 0a64 0809 ef09
//     0x0020:  02a8 1bec 1bec 0468 0000 ---- ---- ----
//     0x0030:  ---- ---- ---- ---- ---- ---- ---- ----
//     0x0040:  5304 0206 0000 000b 8001 6068 752a 8032
//     0x0050:  8002 0000 0000 4000 8003 0000 0000 3c00
//     0x0060:  8004 0000 0000 0400 9600 6068 7520 0000
//     0x0070:  c101 0000 0000 0032 c103 0000 0000 0a80
//     0x0080:  4300 0000 0000 0000 8000 0000 0000 0000
//     0x0090:  8000 0000 0000 0000 8000 0000 0000 0000
//     0x00a0:  ---- ---- ---- ---- ---- ---- ---- ----
//     0x00b0:  ---- ---- ---- ---- ---- ---- ---- ----
//     0x00c0:  fdfc 08f9 0a03 05fe 0509 0404 fdfa 06f5
//     0x00d0:  0605 01fb 03fe fcfe 01fd ff02 fdff 02fb
//     ...
//
// This aligns each section of the packet on a 64 byte boundary.  The padding
// bytes between sections are shown as "--" above.  The amount of memory used
// for each packet is therefore the sum of the chunk sizes after each one has
// been rounded up to the next multiple of 64.  In this example, each packet
// will use 64+128+1024 == 1216 bytes.  This is actually the same size that
// would be used if IBVPKTSZ were given as 1162 assuming each packet is aligned
// on a 64 byte boundary, but the extra padding bytes are redistributed to
// afford more optimal alignment of the SPEAD header and SPEAD data sections.

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <infiniband/verbs.h>

#include "hashpipe.h"
#include "hashpipe_ibvpkt_databuf.h"
//#include "hpguppi_time.h"
//include "hpguppi_mkfeng.h"
//#include "hashpipe_ibvpkt_thread.h"

// Milliseconds between periodic status buffer updates
#define PERIODIC_STATUS_BUFFER_UPDATE_MS (200)

#define DEFAULT_MAX_FLOWS (16)

// A bit of a hack...
#ifndef IBV_FLOW_ATTR_SNIFFER
#define IBV_FLOW_ATTR_SNIFFER (0x3)
#endif

// Forget previous (possibly less parenthesized) version
#ifdef ELAPSED_NS
#undef ELAPSED_NS
#endif

#define ELAPSED_NS(start,stop) \
  (((int64_t)(stop).tv_sec-(start).tv_sec)*1000*1000*1000 + \
   ((stop).tv_nsec-(start).tv_nsec))

// Wait for a block_info's databuf block to be free, then copy status buffer to
// block's header and clear block's data.  Calling thread will exit on error
// (should "never" happen).  Status buffer updates made after the copy to the
// block's header will not be seen in the block's header (e.g. by downstream
// threads).  Any status buffer fields that need to be updated for correct
// downstream processing of this block must be updated BEFORE calling this
// function.  Note that some of the block's header fields will be set when the
// block is finalized (see finalize_block() for details).
static void wait_for_block_free(hashpipe_ibvpkt_databuf_t *db, int block_idx,
    hashpipe_status_t * st, const char * status_key)
{
  int rv;
  char ibvstat[80] = {0};
  char ibvbuf_status[80];
  int ibvbuf_full = hashpipe_ibvpkt_databuf_total_status(db);
  sprintf(ibvbuf_status, "%d/%d", ibvbuf_full, db->header.n_block);

  hashpipe_status_lock_safe(st);
  {
    // Save original status
    hgets(st->buf, status_key, sizeof(ibvstat), ibvstat);
    // Set "waitfree" status
    hputs(st->buf, status_key, "waitfree");
    // Update IBVBUFST
    hputs(st->buf, "IBVBUFST", ibvbuf_status);
  }
  hashpipe_status_unlock_safe(st);

  while ((rv=hashpipe_ibvpkt_databuf_wait_free(db, block_idx))
      != HASHPIPE_OK) {
    if (rv==HASHPIPE_TIMEOUT) {
      ibvbuf_full = hashpipe_ibvpkt_databuf_total_status(db);
      sprintf(ibvbuf_status, "%d/%d", ibvbuf_full, db->header.n_block);
      hashpipe_status_lock_safe(st);
      {
        hputs(st->buf, status_key, "blocked");
        hputs(st->buf, "IBVBUFST", ibvbuf_status);
      }
      hashpipe_status_unlock_safe(st);
    } else {
      hashpipe_error(__FUNCTION__,
          "error waiting for free databuf (%s)", __FILE__);
      pthread_exit(NULL);
    }
  }
  hashpipe_status_lock_safe(st);
  {
    // Restore original status
    hputs(st->buf, status_key, ibvstat);
  }
  hashpipe_status_unlock_safe(st);
}

// Parses the ibvpktsz string for chunk sizes and initializes db's pktbuf_info
// accordingly.  Returns 0 on success or -1 on error.
static
int
parse_ibvpktsz(struct hashpipe_pktbuf_info *pktbuf_info, char * ibvpktsz)
{
  int i;
  char * p;
  uint32_t nchunks = 0;
  size_t pkt_size = 0;
  size_t slot_size = 0;

  if(!ibvpktsz) {
    return -1;
  }

  // Look for commas
  while(nchunks < HASHPIPE_IBVPKT_DATABUF_MAX_PKT_CHUNKS && (p = strchr(ibvpktsz, ','))) {
    // Replace comma with nul
    *p = '\0';
    // Parse chuck size
    pktbuf_info->chunks[nchunks].chunk_size = strtoul(ibvpktsz, NULL, 0);
    // Replace nul with comma
    *p = ',';
    // If chunk_size is 0, return error
    if(pktbuf_info->chunks[nchunks].chunk_size == 0) {
      hashpipe_error("IBVPKTSZ", "chunk size must be non-zero");
      return -1;
    }
    // Increment nchunks
    nchunks++;
    // Advance ibvpktsz to character beyond p
    ibvpktsz = p+1;
  }

  // If nchunks is less than MAX_CHUNKS and ibvpktsz[0] is not nul
  if(nchunks < HASHPIPE_IBVPKT_DATABUF_MAX_PKT_CHUNKS && *ibvpktsz) {
    // If more commas remain, too many chunks!
    if(strchr(ibvpktsz, ',')) {
      hashpipe_error("IBVPKTSZ", "too many chunks");
      return -1;
    }
    // Parse final chunk size
    pktbuf_info->chunks[nchunks].chunk_size = strtoul(ibvpktsz, NULL, 0);
    // Increment nchunks
    nchunks++;
  } else if(nchunks == HASHPIPE_IBVPKT_DATABUF_MAX_PKT_CHUNKS && *ibvpktsz) {
    // Too many chunks
    hashpipe_error("IBVPKTSZ", "too many chunks");
    return -1;
  }

  // Calculate remaining fields
  for(i=0; i<nchunks; i++) {
    pktbuf_info->chunks[i].chunk_aligned_size = pktbuf_info->chunks[i].chunk_size +
      ((-pktbuf_info->chunks[i].chunk_size) % HASHPIPE_IBVPKT_PKT_CHUNK_ALIGNMENT_SIZE);
    pktbuf_info->chunks[i].chunk_offset = slot_size;
    // Accumulate pkt_size and slot_size
    pkt_size += pktbuf_info->chunks[i].chunk_size;
    slot_size += pktbuf_info->chunks[i].chunk_aligned_size;
  }

  // Store final values
  pktbuf_info->num_chunks = nchunks;
  pktbuf_info->pkt_size = pkt_size;
  pktbuf_info->slot_size = slot_size;
  pktbuf_info->slots_per_block = HASHPIPE_IBVPKT_DATABUF_BLOCK_DATA_SIZE / slot_size;

  return 0;
}

// The hashpipe_ibvpkt_init() function sets up the hashpipe_ibv_context
// structure and then call hashpipe_ibv_init().  This uses the "user-managed
// buffers" feature of hashpipe_ibverbs so that packets will be stored directly
// into the data blocks of the shared memory databuf.  It initializes receive
// scatter/gather lists to point to slots in the first data block of the shared
// memory databuf.  Due to how the current hashpipe versions works, the virtual
// address mappings for the shared memory databuf change between init() and
// run(), so this function must be called from run() only.  It initializes
// receive scatter/gather lists to point to slots in the first data block of
// the shared memory databuf.  Returns HASHPIPE_OK on success, other values on
// error.
static
int
hashpipe_ibvpkt_init(hashpipe_status_t * st,
                     hashpipe_ibvpkt_databuf_t * db)
{
  int i, j;
  struct hashpipe_pktbuf_info * pktbuf_info = hashpipe_ibvpkt_databuf_pktbuf_info_ptr(db);
  struct hashpipe_ibv_context * hibv_ctx = hashpipe_ibvpkt_databuf_hibv_ctx_ptr(db);
  uint32_t num_chunks = pktbuf_info->num_chunks;
  struct hashpipe_pktbuf_chunk * chunks = pktbuf_info->chunks;
  uint64_t base_addr;

  memset(hibv_ctx, 0, sizeof(struct hashpipe_ibv_context));

  // MAXFLOWS got initialized by init() if needed, but we setup the default
  // value again just in case some (buggy) downstream thread removed it from
  // the status buffer.
  hibv_ctx->max_flows = DEFAULT_MAX_FLOWS;

  hashpipe_status_lock_safe(st);
  {
    // Get info from status buffer if present (no change if not present)
    hgets(st->buf,  "IBVIFACE",
        sizeof(hibv_ctx->interface_name), hibv_ctx->interface_name);
    if(hibv_ctx->interface_name[0] == '\0') {
      hgets(st->buf,  "BINDHOST",
          sizeof(hibv_ctx->interface_name), hibv_ctx->interface_name);
      if(hibv_ctx->interface_name[0] == '\0') {
        strcpy(hibv_ctx->interface_name, "eth4");
        hputs(st->buf, "IBVIFACE", hibv_ctx->interface_name);
      }
    }

    hgetu4(st->buf, "MAXFLOWS", &hibv_ctx->max_flows);
    if(hibv_ctx->max_flows == 0) {
      hibv_ctx->max_flows = 1;
    }
    hputu4(st->buf, "MAXFLOWS", hibv_ctx->max_flows);
  }
  hashpipe_status_unlock_safe(st);

  // General fields
  hibv_ctx->nqp = 1;
  hibv_ctx->pkt_size_max = 9*1024; // Not really used with user managed buffers
  hibv_ctx->user_managed_flag = 1;

  // Number of send/recv packets (i.e. number of send/recv WRs)
  hibv_ctx->send_pkt_num = 1;
  int num_recv_wr = hashpipe_ibv_query_max_wr(hibv_ctx->interface_name);
  if(num_recv_wr > pktbuf_info->slots_per_block) {
    num_recv_wr = pktbuf_info->slots_per_block;
  }
  hibv_ctx->recv_pkt_num = num_recv_wr;

  // Allocate packet buffers
  if(!(hibv_ctx->send_pkt_buf = (struct hashpipe_ibv_send_pkt *)calloc(
      hibv_ctx->send_pkt_num, sizeof(struct hashpipe_ibv_send_pkt)))) {
    return HASHPIPE_ERR_SYS;
  }
  if(!(hibv_ctx->recv_pkt_buf = (struct hashpipe_ibv_recv_pkt *)calloc(
      hibv_ctx->recv_pkt_num, sizeof(struct hashpipe_ibv_recv_pkt)))) {
    return HASHPIPE_ERR_SYS;
  }

  // Allocate sge buffers.  We allocate num_chunks SGEs per receive WR.
  if(!(hibv_ctx->send_sge_buf = (struct ibv_sge *)calloc(
      hibv_ctx->send_pkt_num, sizeof(struct ibv_sge)))) {
    return HASHPIPE_ERR_SYS;
  }
  if(!(hibv_ctx->recv_sge_buf = (struct ibv_sge *)calloc(
      hibv_ctx->recv_pkt_num * num_chunks, sizeof(struct ibv_sge)))) {
    return HASHPIPE_ERR_SYS;
  }

  // Specify size of send and recv memory regions.
  // Send memory region is just one packet.  Recv memory region spans all data blocks.
  hibv_ctx->send_mr_size = (size_t)hibv_ctx->send_pkt_num * hibv_ctx->pkt_size_max;
  hibv_ctx->recv_mr_size = sizeof(db->block);

  // Allocate memory for send_mr_buf
  if(!(hibv_ctx->send_mr_buf = (uint8_t *)calloc(
      hibv_ctx->send_pkt_num, hibv_ctx->pkt_size_max))) {
    return HASHPIPE_ERR_SYS;
  }
  // Point recv_mr_buf to starts of block 0
  hibv_ctx->recv_mr_buf = (uint8_t *)db->block;

  // Setup send WR's num_sge and SGEs' addr/length fields
  hibv_ctx->send_pkt_buf[0].wr.num_sge = 1;
  hibv_ctx->send_sge_buf[0].addr = (uint64_t)hibv_ctx->send_mr_buf;
  hibv_ctx->send_sge_buf[0].length = hibv_ctx->pkt_size_max;

  // Setup recv WRs' num_sge and SGEs' addr/length fields
  for(i=0; i<hibv_ctx->recv_pkt_num; i++) {
    hibv_ctx->recv_pkt_buf[i].wr.num_sge = num_chunks;

    base_addr = (uint64_t)hashpipe_ibvpkt_databuf_block_slot_ptr(db, 0, i);
    for(j=0; j<num_chunks; j++) {
      hibv_ctx->recv_sge_buf[num_chunks*i+j].addr = base_addr + chunks[j].chunk_offset;
      hibv_ctx->recv_sge_buf[num_chunks*i+j].length = chunks[j].chunk_size;
    }
  }

  // Initialize ibverbs
  return hashpipe_ibv_init(hibv_ctx);
}

// Create sniffer flow
// Use with caution!!!
static
struct ibv_flow *
create_sniffer_flow(struct hashpipe_ibv_context * hibv_ctx)
{
  struct ibv_flow_attr sniffer_flow_attr = {
    .comp_mask      = 0,
    .type           = IBV_FLOW_ATTR_SNIFFER,
    .size           = sizeof(sniffer_flow_attr),
    .priority       = 0,
    .num_of_specs   = 0,
    .port           = hibv_ctx->port_num,
    .flags          = 0
  };

  return ibv_create_flow(hibv_ctx->qp[0], &sniffer_flow_attr);
}

// Destroy sniffer flow
// Use with caution!!!
static
int
destroy_sniffer_flow(struct ibv_flow * sniffer_flow)
{
  return ibv_destroy_flow(sniffer_flow);
}

// Update status buffer fields
static
void
update_status_buffer(hashpipe_status_t *st, int nfull, int nblocks,
    uint64_t nbytes, uint64_t npkts, uint64_t ns_elapsed,
    int32_t * sniffer_flag)
{
  char ibvbufst[80];
  double gbps;
  double pps;

  // Make IBVBUFST string
  sprintf(ibvbufst, "%d/%d", nfull, nblocks);

  // Calculate stats
  gbps = 8.0 * nbytes / ns_elapsed;
  pps = 1e9 * npkts / ns_elapsed;

  // Update status buffer fields
  hashpipe_status_lock_safe(st);
  {
    hputs(st->buf, "IBVBUFST", ibvbufst);
    hputnr8(st->buf, "IBVGBPS", 6, gbps);
    hputnr8(st->buf, "IBVPPS", 3, pps);
    if(*sniffer_flag > -1) {
      hgeti4(st->buf, "IBVSNIFF", sniffer_flag);
    }
  }
  hashpipe_status_unlock_safe(st);
}

// This thread's init() function, if provided, is called by the Hashpipe
// framework at startup to allow the thread to perform initialization tasks
// such as setting up network connections or GPU devices.
//
// This thread's init() function reads the IBVPKTSZ field from the status buffer and
// uses the packet sizing information to initialize the pktbuf_info structure
// in the databuf's header so that it will be available to the init() functions
// of downstream threads.  If the IBVPKTSZ field is not present, the
// pktbuf_info structure will be initialized assuming a 9K packet size in one
// chunk.
//
// This function also ensures that other status fields used by this thread are
// present in the status buffer so that they will be available to the init()
// functions of downstream threads.  Hashpipe calls init() functions in
// pipeline order, but calls run() functions in reverse pipeline order.
static int init(hashpipe_thread_args_t *args)
{
  // Local aliases to shorten access to args fields
  // Our output buffer happens to be a hashpipe_ibvpkt_databuf
  hashpipe_ibvpkt_databuf_t *db = (hashpipe_ibvpkt_databuf_t *)args->obuf;
  hashpipe_status_t * st = &args->st;
  const char * status_key = args->thread_desc->skey;

  // Get pointer to hashpipe_pktbuf_info
  struct hashpipe_pktbuf_info * pktbuf_info = hashpipe_ibvpkt_databuf_pktbuf_info_ptr(db);

  // Variables to get/set status buffer fields
  uint32_t max_flows = DEFAULT_MAX_FLOWS;
  char ifname[80] = {0};
  char ibvpktsz[80];
  strcpy(ibvpktsz, "9216"); // 9216 == 9*1024

  hashpipe_status_lock_safe(st);
  {
    // Get info from status buffer if present (no change if not present)
    hgets(st->buf,  "IBVIFACE", sizeof(ifname), ifname);
    if(ifname[0] == '\0') {
      hgets(st->buf,  "BINDHOST", sizeof(ifname), ifname);
      if(ifname[0] == '\0') {
        strcpy(ifname, "eth4");
        hputs(st->buf, "IBVIFACE", ifname);
      }
    }

    hgets(st->buf,  "IBVPKTSZ", sizeof(ibvpktsz), ibvpktsz);
    hgetu4(st->buf, "MAXFLOWS", &max_flows);

    if(max_flows == 0) {
      max_flows = 1;
    }

    // Store ibvpktsz in status buffer (in case it was not there before).
    hputs(st->buf, "IBVPKTSZ", ibvpktsz);
    hputu4(st->buf, "MAXFLOWS", max_flows);

    // Set status_key to init
    hputs(st->buf, status_key, "init");
  }
  hashpipe_status_unlock_safe(st);

  // Parse ibvpktsz
  if(parse_ibvpktsz(pktbuf_info, ibvpktsz)) {
    return HASHPIPE_ERR_PARAM;
  }

  // Success!
  return HASHPIPE_OK;
}

static
void *
run(hashpipe_thread_args_t * args)
{
#if 0
int debug_i=0, debug_j=0;
#endif
  // Local aliases to shorten access to args fields
  // Our output buffer happens to be a hashpipe_ibvpkt_databuf
  hashpipe_ibvpkt_databuf_t *db = (hashpipe_ibvpkt_databuf_t *)args->obuf;
  hashpipe_status_t * st = &args->st;
  const char * thread_name = args->thread_desc->name;
  const char * status_key = args->thread_desc->skey;

  // pktbuf_info related variables
  struct hashpipe_pktbuf_info * pktbuf_info = hashpipe_ibvpkt_databuf_pktbuf_info_ptr(db);
  uint32_t num_chunks = pktbuf_info->num_chunks;
  struct hashpipe_pktbuf_chunk * chunks = pktbuf_info->chunks;
  size_t slots_per_block = pktbuf_info->slots_per_block;

  // The all important hashpipe_ibv_context
  struct hashpipe_ibv_context * hibv_ctx = hashpipe_ibvpkt_databuf_hibv_ctx_ptr(db);

  // Variables for handing received packets
  struct hashpipe_ibv_recv_pkt * hibv_rpkt = NULL;
  struct hashpipe_ibv_recv_pkt * curr_rpkt;

  // Misc counters, etc
  int i;
  uint64_t base_addr;
  int got_wc_error = 0;

  // We maintain two active blocks at all times.  curblk is the number of the
  // older of the two blocks with block number curblk+1 being the other of the
  // two active blocks.  Work requests are first posted with slot locations in
  // curblk as the destination.  Enough WRs are posted to cover up to the
  // entire first block or as many WRs as the NIC can have outstanding at ones,
  // whichever is less.  When packets are received, their WRs' SGEs are updated
  // to point to the next free slot, which could be in the same block or the
  // next block higher (with wrapping).  If any of these new SGEs point to a
  // block that is greater than curblk+1, then block curblk is marked filled
  // and curblk is incremented.  This continues indefinitely.
  uint64_t curblk = 0;

  // Used to track next block/slot to be assigned to a work request.
  // next_slot is always within the range 0 to slots_per_block-1, but
  // next_block is allowed to grow "forever".
  uint64_t next_block = 0;
  uint32_t next_slot = 0;

  // This ibv_flow pointer is used to support a diagnostic "sniffer" mode.
  // This is enabled by setting IBVSNIFF=1 in the status buffer.
  // Use with caution!!!
  struct ibv_flow * sniffer_flow = NULL;
  // sniffer_flag <0 disabled completely, 0 enabled but off, >0 enabled and on
  // The disabled completely state is to completely avoid the sniffer if an
  // error in encountered with it.  We also disable it completely unless
  // IBVSNIFF>-1 in the status buffer at startup.
  int32_t sniffer_flag = -1;

  // Wait until the first two blocks are marked as free
  // (should already be free)
  for(i=0; i<2; i++) {
    wait_for_block_free(db,
        (curblk+i) % HASHPIPE_IBVPKT_DATABUF_NBLOCKS, st, status_key);
  }

  // Initialize IBV
  if(hashpipe_ibvpkt_init(st, db)) {
    hashpipe_error(thread_name, "hashpipe_ibvpkt_init failed");
    return NULL;
  }

  // Initialize next slot
  next_slot = hibv_ctx->recv_pkt_num + 1;
  if(next_slot > pktbuf_info->slots_per_block) {
    next_slot = 0;
    next_block++;
  }

  // Variables for counting packets and bytes as well as elapsed time
  uint64_t bytes_received = 0;
  uint64_t pkts_received = 0;
  struct timespec ts_start;
  struct timespec ts_now;
  uint64_t ns_elapsed;

  // Update status_key with running state
  hashpipe_status_lock_safe(st);
  {
    hgeti4(st->buf, "IBVSNIFF", &sniffer_flag);
    hputs(st->buf, status_key, "running");
  }
  hashpipe_status_unlock_safe(st);

  if(sniffer_flag > 0) {
    if(!(sniffer_flow = create_sniffer_flow(hibv_ctx))) {
      hashpipe_error(thread_name, "create_sniffer_flow failed");
      errno = 0;
      sniffer_flag = -1;
    } else {
      hashpipe_info(thread_name, "create_sniffer_flow succeeded");
    }
  } else {
    hashpipe_info(thread_name, "sniffer_flow disabled");
  }

  // Initialize ts_start with current time
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts_start);

  // Main loop
  while (run_threads()) {
    hibv_rpkt = hashpipe_ibv_recv_pkts(hibv_ctx, 50); // 50 ms timeout

    // If no packets and errno is non-zero
    if(!hibv_rpkt && errno) {
      // Print error, reset errno, and continue receiving
      hashpipe_error(thread_name, "hashpipe_ibv_recv_pkts");
      errno = 0;
      continue;
    }

    // Check for periodic status buffer update interval
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts_now);
    ns_elapsed = ELAPSED_NS(ts_start, ts_now);
    if(ns_elapsed >= PERIODIC_STATUS_BUFFER_UPDATE_MS*1000*1000) {
      // Save now as the new start
      ts_start = ts_now;

      // Timeout, update status buffer
      update_status_buffer(st, hashpipe_ibvpkt_databuf_total_status(db),
          db->header.n_block, bytes_received, pkts_received, ns_elapsed,
          &sniffer_flag);

      // Reset counters
      bytes_received = 0;
      pkts_received = 0;

      // Manage sniffer_flow as needed
      if(sniffer_flag > 0 && !sniffer_flow) {
        if(!(sniffer_flow = create_sniffer_flow(hibv_ctx))) {
          hashpipe_error(thread_name, "create_sniffer_flow failed");
          errno = 0;
          sniffer_flag = -1;
        } else {
          hashpipe_info(thread_name, "create_sniffer_flow succeeded");
        }
      } else if (sniffer_flag < 1 && sniffer_flow) {
        if(destroy_sniffer_flow(sniffer_flow)) {
          hashpipe_error(thread_name, "destroy_sniffer_flow failed");
          errno = 0;
          sniffer_flag = -1;
        } else {
          hashpipe_info(thread_name, "destroy_sniffer_flow succeeded");
        }
        sniffer_flow = NULL;
      }
    } // end periodic status buffer update

    // If no packets
    if(!hibv_rpkt) {
      // Wait for more packets
      continue;
    }

    // Got packets!

    // For each packet: update SGE addr
    for(curr_rpkt = hibv_rpkt; curr_rpkt;
        curr_rpkt = (struct hashpipe_ibv_recv_pkt *)curr_rpkt->wr.next) {

      if(curr_rpkt->length == 0) {
        hashpipe_error(thread_name,
            "WR %d got error when using address: %p (databuf %p +%lu)",
            curr_rpkt->wr.wr_id,
            curr_rpkt->wr.sg_list->addr,
            db->block, sizeof(db->block));
        // Set flag to break out of main loop and then break out of for loop
        got_wc_error = 1;
        break;
      }

      // If time to advance the ring buffer block
      if(next_block > curblk+1) {
        // Mark curblk as filled
        hashpipe_ibvpkt_databuf_set_filled(db,
            curblk % HASHPIPE_IBVPKT_DATABUF_NBLOCKS);

        // Increment curblk
        curblk++;

        // Wait for curblk+1 to be free
        wait_for_block_free(db,
            (curblk+1) % HASHPIPE_IBVPKT_DATABUF_NBLOCKS, st, status_key);
      } // end block advance

      // Count packet and bytes
      pkts_received++;
      bytes_received += curr_rpkt->length;

      // Update current WR with new destination addresses for all SGEs
      base_addr = (uint64_t)hashpipe_ibvpkt_databuf_block_slot_ptr(db, next_block, next_slot);
      for(i=0; i<num_chunks; i++) {
        curr_rpkt->wr.sg_list[i].addr = base_addr + chunks[i].chunk_offset;
      }

      // Advance slot
      next_slot++;
      if(next_slot >= slots_per_block) {
        next_slot = 0;
        next_block++;
      }
    } // end for each packet

    // Break out of main loop if we got a work completion error
    if(got_wc_error) {
      break;
    }

    // Release packets (i.e. repost work requests)
    if(hashpipe_ibv_release_pkts(hibv_ctx,
          (struct hashpipe_ibv_recv_pkt *)hibv_rpkt)) {
      hashpipe_error(thread_name, "hashpipe_ibv_release_pkts");
      errno = 0;
    }

    // Will exit if thread has been cancelled
    pthread_testcancel();
  } // end main loop

  // Update status_key with exiting state
  hashpipe_status_lock_safe(st);
  {
    hputs(st->buf, status_key, "exiting");
  }
  hashpipe_status_unlock_safe(st);

  hashpipe_info(thread_name, "exiting!");
  pthread_exit(NULL);

  return NULL;
}

static hashpipe_thread_desc_t hp_thread_desc = {
    name: "ibvpkt_thread",
    skey: "IBVSTAT",
    init: init,
    run:  run,
    ibuf_desc: {NULL},
    obuf_desc: {hashpipe_ibvpkt_databuf_create}
};

static __attribute__((constructor)) void ctor()
{
  register_hashpipe_thread(&hp_thread_desc);
}

// vi: set ts=2 sw=2 et :
