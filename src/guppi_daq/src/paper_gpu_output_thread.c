// paper_gpu_output_thread.c
//
// Sends integrated GPU output to "catcher" machine for assimilation into a
// dataset.

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <endian.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <xgpu.h>

#include "guppi_error.h"
#include "paper_databuf.h"

#define STATUS_KEY "OUTSTAT"  /* Define before guppi_threads.h */
#include "guppi_threads.h"
#include "paper_thread.h"

// The PAPER cn_rx.py script receives UDP packets from multiple X engines and
// assimilates them into a MIRIAD dataset.  The format of the packets appears
// to be an early version of SPEAD, but they are clearly *not* compatible with
// SPEAD verison 4.  The format of the packets, based on a reverse engineering
// of both the transmit and receive side of the existing code, is documented
// here.
//
// The packets consist of a header, some number of option values, and payload:
//
//   header
//   option_1
//   ...
//   option_N
//   payload
//
// ...where header and option_X are 64 bit values and payload is a sequence of
// 32 bit integers containing the integration buffer's contents.
//
// header consists of the following four 16 bit values...
//
//   pkt_type pkt_version 0x0000 N
//
// ...where pkt_type is 0x4b52, pkt_version is 0x0003, and N is the number of
// option values that follow (pkt_version is a guess at that field;s purpose).
// option_1 through option_N are each 64 bit values in the form...
//
//   optid optval
//
// ...where optid is a 16 bit value and optval is a 48 bit value.
//
// The receive code that unpacks the data is agnostic about the exact number
// and order of options, but the processing of the unpacked data requires the
// following options to be present...
//
//   INSTIDS == 0x00320003NNNNEEEE, where 0032 is 16 bit optid (decimal 50)
//                                        0003 is 16 bit instrument_id,
//                                        NNNN is 16 bit instance_id,
//                                    and EEEE is 16 bit engine_id.
//
//   TIMESTAMP == 0x0003TTTTTTTTTTTT, where 0003 is 16 bit optid (decimal 3),
//                                      and TTTTTTTTTTTT is 48 bit timestamp
//                                          (e.g. mcount).
//
//   HEAPOFF == 0x0005HHHHHHHHHHHH, where 0005 is 16 bit optid (decimal 5),
//                                    and HHHHHHHHHHHH is 48 bit heap offset.
//
//   PKTINFO == 0x0033LLLLLLCCCCCC, where 0033 is 16 bit optid (decimal 51),
//                                        LLLLLL is 24 bit length,
//                                    and CCCCCC is 24 bit count.
//
// Also recognized, but apparently unused, by the receiver, are...
//
//   CURRERR == 0x0034EEEEEEEEEEEE, where 0034 is 16 bit optid (decimal 52),
//                                    and EEEEEEEEEEEE is 48 bit error code.
//
//   HEAPPTR == 0x0035PPPPPPPPPPPP, where 0035 is 16 bit optid (decimal 53),
//                                    and PPPPPPPPPPPP is 48 bit heap pointer.
//
// Sent by corr_tx.py, but ignored by the receiver are...
//
//   HEAPLEN == 0x0004LLLLLLLLLLLL, where 0004 is the 16 bit optid (decimal 4),
//                                    and LLLLLLLLLLLL is 48 bit heap length.
//
// Notes for the various optval fields (some of the info below is speculative
// at the time of writing):
//
//   INSTIDS.instrument_id must be 0x0004, which tels the receive code that the
//   payload data are 32 bit little endian floats in contiguous channel order.
//
//   INSTIDS.instance_id is always sent as 0x0000.
//
//   TIMESTAMP.timestamp is scaled mcount used for collating packets from all X
//   engines.  Not clear whether it is mcount at begining, middle, or end of
//   integration.  The scaling factor is required to mimic the mcount values
//   output by the FPGA X engines.  This allows the same catcher software to
//   work with either FPGA or GPU X engines (or both).  The cacther software
//   expects the mcount to be a  sample count divided by 128.  In other words,
//   mcount * 128 = number of 200 Msps samples since the last sync-up with 1
//   PPS.  The scaling factor is thus:
//
//      timestamp = mcount * N_TIME_PER_PACKET * 2 * N_CHAN_TOTAL / 128
//
//   HEAPOFF.heap_offset is the offset within the integration buffer from which
//   the packet's payload originated (each integration buffer requires multiple
//   packets).  Note that the data order within the overall integration buffer
//   is pre-defined based on the readout order of the FPGA X engine's
//   integration buffer.
//
//   PKTINFO.legnth is the length in bytes of the packet payload.  Maximum
//   allowed value is theoretically 8192, but recevier imposes a limit of 4096.
//
//   PKTINFO.count is sent as 0x0000 and is unused by the receiver.
//
//   The corr_tx.py script sends the unused HEAPPTR option with the most
//   significant bit of optid set (i.e. optid 0x8035 rather than 0x0035) and
//   always sends HEAPPTR.heap_pointer as 0.  Since this optid is not
//   recognized by the receiver, this option is ignored completely by the
//   receiver (as opposed to being recognized but unused).
//
//   The corr_tx.py script always sends the ignored HEAPLEN.heap_length field
//   as 139264.
//
//   The corr_tx.py script sends the options in this order:
//
//     INSTIDS
//     PKTINFO
//     TIMESTAMP
//     HEAPLEN (ignored)
//     HEAPOFF
//     HEAPPTR (ignored)

// Structure for packet header
//
// Note that this only accounts for fields actually used by the receiver.
// Fields sent by corr_tx.py but ignored or not used by the receiver are not
// sent by this thread.
typedef struct pkthdr {
  uint64_t header;
  uint64_t instids;
  uint64_t pktinfo;
  uint64_t timestamp;
  uint64_t offset;
  uint64_t heaplen; // for padding to 128 byte header; ignored by receiver
} pkthdr_t;

// BYTES_PER_PACKET is limited by recevier code and must be multiple of 8
#define BYTES_PER_PACKET 4096

#ifdef SEND_BE32
typedef int32_t pktdata_t;
// TODO Handle Inf and NaNs?  They "should never happen", right?  :-)
static inline pktdata_t convert(float f)
{
  if(f > INT32_MAX) {
    return INT32_MAX;
  } else if(f < INT32_MIN+1) {
    // +1 to keep saturation symmetric
    return INT32_MIN+1;
  } else {
    return (pktdata_t)lroundf(f);
  }
}
#define CONVERT(x) (htobe32(convert(x)))
#else
typedef float pktdata_t;
#define CONVERT(x) (x)
#endif

typedef struct pkt {
  pkthdr_t hdr;
  pktdata_t data[BYTES_PER_PACKET/sizeof(pktdata_t)];
} pkt_t;

// Macros for generating values for the pkthdr_t fields
#define HEADER (htobe64(0x4b52000300000005))
#define INSTIDS(x)   (htobe64(0x0032000400000000 | ( (uint64_t)(x) &         0xffff       )))
#define PKTINFO(x)   (htobe64(0x0033000000000000 | (((uint64_t)(x) &       0xffffff) << 24)))
#define TIMESTAMP(x) (htobe64(0x0003000000000000 | ( (uint64_t)(x) & 0xffffffffffff       )))
#define OFFSET(x)    (htobe64(0x0005000000000000 | ( (uint64_t)(x) & 0xffffffffffff       )))
#define HEAPLEN      (htobe64(0x0004000000000000))

static XGPUInfo xgpu_info;

// PACKET_DELAY_NS is number of nanoseconds to delay between packets.  This is
// to prevent overflowing the network interface's TX queue.  The delay is 4
// times longer than necessary for a single packet so that up to 4 instances
// can dump simulatneously without problems.  For larger correlators running
// fewer instance per GPU host, this may be too long, but for now it shouldn't
// be a problem.  Note that this accounts only for the packet's payload (i.e.
// it considers the size of the packet header to be negligible).
//
// 1000 megabit per second =   1 nanosecond per bit
//  100 megabit per second =  10 nanosecond per bit
//   10 megabit per second = 100 nanosecond per bit
//#define PACKET_DELAY_NS (4 * 10 * 8 * BYTES_PER_PACKET)
// Really trickle it out...
#define PACKET_DELAY_NS (100 * 8 * BYTES_PER_PACKET)

// bytes_per_dump depends on xgpu_info.triLength
static uint64_t bytes_per_dump = 0;
// packets_per_dump is bytes_per_dump / BYTES_PER_PACKET
static unsigned int packets_per_dump = 0;

// Open and connect a UDP socket to the given host and port.  Note that port is
// a string and can be a stringified integer (e.g. "7148") or a service name
// (e.g. "ntp").  Returns -1 on error, otherwise a valid descriptor for an open
// and connected socket.
static int
open_udp_socket(const char *host, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int s, sfd=-1;

    // Obtain address(es) matching host/port

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    s = getaddrinfo(host, port, &hints, &result);
    if (s != 0) {
        guppi_error("getaddrinfo", gai_strerror(s));
        return -1;
    }

    // getaddrinfo() returns a list of address structures.
    // Try each address until we successfully connect(2).
    // If socket(2) (or connect(2)) fails, we (close the socket
    // and) try the next address.

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (sfd == -1) {
            continue;
        }

        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            break; // Success
        }

        close(sfd);
        sfd = -1;
    }

    freeaddrinfo(result);

#if 0
    // Print send buffer size
    int bufsize;
    unsigned int bufsizesize = sizeof(bufsize);
    getsockopt(sfd, SOL_SOCKET, SO_SNDBUF, &bufsize, &bufsizesize);
    printf("send buffer size is %d\n", bufsize);
#endif

    return sfd;
}


// Computes the triangular index of an (i,j) pair as shown here...
// NB: Output is valid only if i >= j.
//
//      i=0  1  2  3  4..
//     +---------------
// j=0 | 00 01 03 06 10
//   1 |    02 04 07 11
//   2 |       05 08 12
//   3 |          09 13
//   4 |             14
//   :
static inline off_t tri_index(const int i, const int j)
{
  return (i * (i+1))/2 + j;
}

// casper_chan_length is the number of complex cross products per channel for
// the casper correlator output format with N_INPUTS.
// NB: N_INPUTS = N_STATION * N_POL
#define CASPER_CHAN_LENGTH (4 * N_INPUTS/2 * (N_INPUTS/2+1) / 2)
static inline size_t casper_chan_length()
{
  // Four cross products for each combination of input pairs
  return CASPER_CHAN_LENGTH;
}

// regtile_chan_length is the number of complex cross products per channel for
// the xGPU register tile order correlator output format with N_INPUTS.
// NB: N_INPUTS = N_STATION * N_POL
#define REGTILE_CHAN_LENGTH (4 * 4 * N_INPUTS/4 * (N_INPUTS/4+1) / 2)
static inline size_t regtile_chan_length()
{
  // Four cross products for each quadrant of 4 input x 4 input tile
  return REGTILE_CHAN_LENGTH;
}

// Returns index into the GPU's register tile ordered output buffer for the
// real component of the cross product of inputs in0 and in1.  Note that in0
// and in1 are input indexes (i.e. 0 based) and often represent antenna and
// polarization by passing (2*ant_idx+pol_idx) as the input number (NB: ant_idx
// and pol_idx are also 0 based).  Return value is valid if in1 >= in0.  The
// corresponding imaginary component is located xgpu_info.matLength words after
// the real component.
static off_t regtile_index(const int in0, const int in1)
{
  const int a0 = in0 >> 1;
  const int a1 = in1 >> 1;
  const int p0 = in0 & 1;
  const int p1 = in1 & 1;
  const int num_words_per_cell = 4;

  // Index within a quadrant
  const int quadrant_index = tri_index(a1/2, a0/2);
  // Quadrant for this input pair
  const int quadrant = 2*(a0&1) + (a1&1);
  // Size of quadrant
  const int quadrant_size = (xgpu_info.nstation/2 + 1) * xgpu_info.nstation/4;
  // Index of cell (in units of cells)
  const int cell_index = quadrant*quadrant_size + quadrant_index;
  //printf("%s: in0=%d, in1=%d, a0=%d, a1=%d, cell_index=%d\n", __FUNCTION__, in0, in1, a0, a1, cell_index);
  // Pol offset
  const int pol_offset = 2*p1 + p0;
  // Word index (in units of words (i.e. floats) of real component
  const int index = (cell_index * num_words_per_cell) + pol_offset;
  return index;
}

// Returns index into a CASPER ordered buffer for the real component of the
// cross product of inputs in0 and in1.  Note that in0 and in1 are input
// indexes (i.e. 0 based) and often represent antenna and polarization by
// passing (2*ant_idx+pol_idx) as the input number (NB: ant_idx ad pol_idx are
// also 0 based).  Return value is valid if in1 >= in0.  The corresponding
// imaginary component is located in the word immediately following the real
// component.  A casper ordered buffer consists of four complex values for each
// pair of input pairs.  Thus, the number of complex values in a casper ordered
// buffer are: 4 * (N/2 * (N/2 + 1)) / 2 = N * (N/2 + 1)
static off_t casper_index(const int in0, const int in1, const int n)
{
  const int a0 = in0 >> 1;
  const int a1 = in1 >> 1;
  const int p0 = in0 & 1;
  const int p1 = in1 & 1;
  const int delta = a1-a0;
  const int num_words_per_cell = 8;
  const int nant_2 = (n/2) / 2;

  // Three cases: top triangle, middle rectangle, bottom triangle
  const int triangle_size = ((nant_2 + 1) * nant_2)/2;
  const int middle_rect_offset = triangle_size;
  const int last_cell_offset = 4*middle_rect_offset - nant_2 - 1;
  int cell_index;

  if(delta > nant_2) {
    // bottom triangle
    cell_index = last_cell_offset - tri_index(nant_2-2-a0, (n/2)-1-a1);
  } else if (a1 < (n/2)/2) {
    // top triangle
    cell_index = tri_index(a1, a0);
  } else {
    // middle rectangle
    cell_index = middle_rect_offset + (a1-nant_2)*(nant_2+1) + (nant_2-delta);
  }
  //printf("%s: a0=%d, a1=%d, delta=%d, cell_index=%d\n", __FUNCTION__, a0, a1, delta, cell_index);
  // Pol offset
  const int pol_offset = 2*(2*(p0^p1) + p0);
  // Word index (in units of words (i.e. floats) of real component
  const int index = (cell_index * num_words_per_cell) + pol_offset;
  return index;
}

// For each channel, a casper ordered buffer contains four complex values for
// each pair of input pairs.  Thus, the number of complex values in a casper
// ordered buffer are: 4 * (N/2 * (N/2 + 1)) / 2 = N * (N/2 + 1)
#define N_CASPER_COMPLEX_PER_CHAN (N_INPUTS * (N_INPUTS/2 + 1))

// Lookup table mapping casper_idx to regtile_idx
static off_t *idx_map;

static int init_idx_map()
{
  int a0, a1, p0, p1, i, j;
  idx_map = malloc(N_CASPER_COMPLEX_PER_CHAN * sizeof(off_t));
  if(!idx_map) {
    return -1;
  }

  for(a1=0; a1<N_INPUTS/2; a1++) {
    for(a0=0; a0<=a1; a0++) {
      for(p0=0; p0<2; p0++) {
        for(p1=0; p1<2; p1++) {
          i = 2*a0 + p0;
          j = 2*a1 + p1;
          idx_map[casper_index(i,j,N_INPUTS)/2] = regtile_index(i,j);
        }
      }
    }
  }
  return 0;
}

static int init(struct guppi_thread_args *args)
{
    /* Attach to status shared mem area */
    THREAD_INIT_ATTACH_STATUS(args->instance_id, st, STATUS_KEY);

    THREAD_INIT_DETACH_STATUS(st);

    // Get sizing parameters
    xgpuInfo(&xgpu_info);
    bytes_per_dump = xgpu_info.triLength * sizeof(Complex);
    packets_per_dump = bytes_per_dump / BYTES_PER_PACKET;
    printf("bytes_per_dump = %lu\n", bytes_per_dump);

    // Create paper_ouput_databuf
    THREAD_INIT_DATABUF(args->instance_id, paper_output_databuf,
        args->input_buffer);

    if(init_idx_map()) {
      return -1;
    }

    // Success!
    return 0;
}

#define ELAPSED_NS(start,stop) \
  (((int64_t)stop.tv_sec-start.tv_sec)*1000*1000*1000+(stop.tv_nsec-start.tv_nsec))

static void *run(void * _args)
{
    // Cast _args
    struct guppi_thread_args *args = (struct guppi_thread_args *)_args;

    THREAD_RUN_BEGIN(args);

    THREAD_RUN_SET_AFFINITY_PRIORITY(args);

    THREAD_RUN_ATTACH_STATUS(args->instance_id, st);

    // Attach to paper_ouput_databuf
    THREAD_RUN_ATTACH_DATABUF(args->instance_id,
        paper_output_databuf, db, args->input_buffer);

    // Setup socket and message structures
    int sockfd;
    unsigned int xengine_id = 0;
    struct timespec packet_delay = {
      .tv_sec = 0,
      .tv_nsec = PACKET_DELAY_NS
    };

    guppi_status_lock_safe(&st);
    hgetu4(st.buf, "XID", &xengine_id); // No change if not found
    hputu4(st.buf, "XID", xengine_id);
    hputu4(st.buf, "OUTDUMPS", 0);
    guppi_status_unlock_safe(&st);

    pkt_t pkt;
    pkt.hdr.header = HEADER;
    pkt.hdr.instids = INSTIDS(xengine_id);
    pkt.hdr.pktinfo = PKTINFO(BYTES_PER_PACKET);
    pkt.hdr.heaplen = HEAPLEN;

    // TODO Get catcher hostname and port from somewhere

#ifndef CATCHER_PORT
#define CATCHER_PORT 7148
#endif
#define stringify2(x) #x
#define stringify(x) stringify2(x)

    // Open socket
    sockfd = open_udp_socket("catcher", stringify(CATCHER_PORT));
    if(sockfd == -1) {
        guppi_error(__FUNCTION__, "error opening socket");
        run_threads=0;
        pthread_exit(NULL);
    }

#ifdef TEST_INDEX_CALCS
    int i, j;
    for(i=0; i<32; i++) {
      for(j=i; j<32; j++) {
        regtile_index(2*i, 2*j);
      }
    }
    for(i=0; i<32; i++) {
      for(j=i; j<32; j++) {
        casper_index(2*i, 2*j);
      }
    }
    run_threads=0;
#endif

    /* Main loop */
    int rv;
    int casper_chan, gpu_chan;
    int baseline;
    unsigned int dumps = 0;
    int block_idx = 0;
    struct timespec start, stop;
    signal(SIGINT,cc);
    signal(SIGTERM,cc);
    while (run_threads) {

        guppi_status_lock_safe(&st);
        hputs(st.buf, STATUS_KEY, "waiting");
        guppi_status_unlock_safe(&st);

        // Wait for new block to be filled
        while ((rv=paper_output_databuf_wait_filled(db, block_idx))
                != GUPPI_OK) {
            if (rv==GUPPI_TIMEOUT) {
                guppi_status_lock_safe(&st);
                hputs(st.buf, STATUS_KEY, "blocked");
                guppi_status_unlock_safe(&st);
                continue;
            } else {
                guppi_error(__FUNCTION__, "error waiting for filled databuf");
                run_threads=0;
                pthread_exit(NULL);
                break;
            }
        }

        clock_gettime(CLOCK_MONOTONIC, &start);

        // Note processing status, current input block
        guppi_status_lock_safe(&st);
        hputs(st.buf, STATUS_KEY, "processing");
        hputi4(st.buf, "OUTBLKIN", block_idx);
        guppi_status_unlock_safe(&st);

        // Update header's timestamp for this dump.  For historic/unknown
        // reasons, the catcher expects timestamps to be in units of PFB
        // samples (even though its source claims ADC samples!) divided by a
        // mysterious constant value of 128 that is no doubt related to some
        // FPGA based X engine from the past.
        pkt.hdr.timestamp = TIMESTAMP(db->block[block_idx].header.mcnt *
            N_TIME_PER_PACKET * N_CHAN_TOTAL / 128);

        // Init header's offset for this dump
        uint32_t nbytes = 0;
        pkt.hdr.offset = OFFSET(nbytes);

        // Unpack and convert in packet sized chunks
        float * pf_re  = db->block[block_idx].data;
        float * pf_im  = db->block[block_idx].data + xgpu_info.matLength;
        pktdata_t * p_out = pkt.data;
        for(casper_chan=0; casper_chan<N_CHAN_PER_X; casper_chan++) {
          // De-interleave the channels
          gpu_chan = (casper_chan/Nc) + ((casper_chan%Nc)*Nx);
          for(baseline=0; baseline<N_CASPER_COMPLEX_PER_CHAN; baseline++) {
            off_t idx_regtile = idx_map[baseline];
            pktdata_t re = CONVERT(pf_re[gpu_chan*REGTILE_CHAN_LENGTH+idx_regtile]);
            pktdata_t im = CONVERT(pf_im[gpu_chan*REGTILE_CHAN_LENGTH+idx_regtile]);
            *p_out++ = re;
            *p_out++ = -im; // Conjugate data to match downstream expectations
            nbytes += 2*sizeof(pktdata_t);
            if(nbytes % BYTES_PER_PACKET == 0) {
              int bytes_sent = send(sockfd, &pkt, sizeof(pkt.hdr)+BYTES_PER_PACKET, 0);
              if(bytes_sent == -1) {
                // Send all packets even if cactcher is not listening (i.e. we
                // we get a connection refused error), but abort sending this
                // dump if we get any other error.
                if(errno != ECONNREFUSED) {
                  perror("send");
                  // Update stats
                  guppi_status_lock_safe(&st);
                  hputu4(st.buf, "OUTDUMPS", ++dumps);
                  hputr4(st.buf, "OUTSECS", 0.0);
                  hputr4(st.buf, "OUTMBPS", 0.0);
                  guppi_status_unlock_safe(&st);
                  // Break out of both for loops
                  goto done_sending;
                }
              } else if(bytes_sent != sizeof(pkt.hdr)+BYTES_PER_PACKET) {
                printf("only sent %d of %lu bytes!!!\n", bytes_sent, sizeof(pkt.hdr)+BYTES_PER_PACKET);
              }

              // Delay to prevent overflowing network TX queue
              nanosleep(&packet_delay, NULL);

              // Setup for next packet
              p_out = pkt.data;
              // Update header's byte_offset for this chunk
              pkt.hdr.offset = OFFSET(nbytes);
            }
          }
        }

        clock_gettime(CLOCK_MONOTONIC, &stop);

        guppi_status_lock_safe(&st);
        hputu4(st.buf, "OUTDUMPS", ++dumps);
        hputr4(st.buf, "OUTSECS", (float)ELAPSED_NS(start,stop)/1e9);
        hputr4(st.buf, "OUTMBPS", (1e3*8*bytes_per_dump)/ELAPSED_NS(start,stop));
        guppi_status_unlock_safe(&st);

done_sending:

        // Mark block as free
        paper_output_databuf_set_free(db, block_idx);

        // Setup for next block
        block_idx = (block_idx + 1) % db->header.n_block;

        /* Will exit if thread has been cancelled */
        pthread_testcancel();
    }

    // Have to close all pushes
    THREAD_RUN_DETACH_DATAUF;
    THREAD_RUN_DETACH_STATUS;
    THREAD_RUN_END;

    // Thread success!
    return NULL;
}

static pipeline_thread_module_t module = {
    name: "paper_gpu_output_thread",
    type: PIPELINE_OUTPUT_THREAD,
    init: init,
    run:  run
};

static __attribute__((constructor)) void ctor()
{
  register_pipeline_thread_module(&module);
}
