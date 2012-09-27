#ifndef _PAPER_DATABUF_H
#define _PAPER_DATABUF_H

#include <stdint.h>
#include "guppi_databuf.h"
#include "config.h"

// Determined by F engine ADCs
#define N_INPUTS_PER_FENGINE 8

// Determined by F engine
#define N_CHAN_TOTAL 1024

// Determined by F engine packetizer
#define N_INPUTS_PER_PACKET  N_INPUTS_PER_FENGINE
// N_BYTES_PER_PACKET excludes header!
#define N_BYTES_PER_PACKET  8192

// X engine sizing (from xGPU)
#define N_INPUTS          (2*XGPU_NSTATION)
#define N_TIME_PER_BLOCK     XGPU_NTIME
#define N_CHAN_PER_X         XGPU_NFREQUENCY

// Derived from above quantities
#define N_FENGINES                   (N_INPUTS/N_INPUTS_PER_FENGINE)
#define N_CHAN_PER_F                 (N_CHAN_TOTAL/N_FENGINES)
#define N_CHAN_PER_PACKET            (N_CHAN_PER_F)
#define N_TIME_PER_PACKET            (N_BYTES_PER_PACKET/N_INPUTS_PER_PACKET/N_CHAN_PER_PACKET)
#define N_SUB_BLOCKS_PER_INPUT_BLOCK (N_TIME_PER_BLOCK / N_TIME_PER_PACKET)
#define N_BYTES_PER_BLOCK            (N_TIME_PER_BLOCK * N_CHAN_PER_X * N_INPUTS)
#define N_PACKETS_PER_BLOCK          (N_BYTES_PER_BLOCK / N_BYTES_PER_PACKET)
#define N_PACKETS_PER_BLOCK_PER_F    (N_PACKETS_PER_BLOCK / N_FENGINES)

// Validate packet dimensions
#if    N_BYTES_PER_PACKET != (N_TIME_PER_PACKET*N_CHAN_PER_PACKET*N_INPUTS_PER_PACKET)
#error N_BYTES_PER_PACKET != (N_TIME_PER_PACKET*N_CHAN_PER_PACKET*N_INPUTS_PER_PACKET)
#endif

#define N_FLUFFED_BYTES_PER_BLOCK  ((N_PACKETS_PER_BLOCK * 8192) * 2)
#define N_FLUFFED_WORDS_PER_BLOCK (N_FLUFFED_BYTES_PER_BLOCK / 8) 

// Number of floats in xGPU's "register tile order" output matrix.
#define N_OUTPUT_MATRIX (2 * N_CHAN_PER_X * (N_INPUTS/2 + 2) * N_INPUTS)

#define PAGE_SIZE (4096)
#define CACHE_ALIGNMENT (128)

/*
 * INPUT BUFFER STRUCTURES
 */

#define N_INPUT_BLOCKS 4
#ifndef N_DEBUG_INPUT_BLOCKS
#define N_DEBUG_INPUT_BLOCKS 0
#endif

typedef struct paper_input_header {
  int64_t good_data; // functions as a boolean, 64 bit to maintain word alignment
  uint64_t mcnt;     // mcount of first packet
} paper_input_header_t;

typedef uint8_t paper_input_header_cache_alignment[
  CACHE_ALIGNMENT - (sizeof(paper_input_header_t)%CACHE_ALIGNMENT)
];

// == paper_databuf_input_t ==
//
// * Net thread output
// * Fluffer thread input
// * Ordered sequence of packets in "data" field:
//
//   +-- mcount
//   |
//   |       +-- xid
//   |       |
//   |       |       |<--fid--->|      |<-packet->|
//   V       V       |          |      |          |
//   m       x       |q       f |      |t       c |
//   ==      ==      |==      ==|      |==      ==|
//   m0 }--> x0 }--> |q0 }--> f0| }--> |t0 }--> c0|
//   m1      x1      |q1      f1|      |t1      c1|
//   m2              |q2      f2|      |t2      c2|
//   :               |:       : |      |:       : |
//   ==      ==       ==      ==        ==      ==
//   Nm      Nx       Nq      Nf        Nt      Nc
//
//   Each 8 byte word represents eight complex inputs.
//   Each byte is a 4bit+4bit complex value.
//
// m = packet's mcount - block's first mcount
// x = packet's XID - X engine's xid_base
// q = packet's FID / 4 (4 is number of FIDs per complex block)
// f = packet's FID % 4 (4 is number of FIDs per complex block)
// t = time sample within packet
// c = channel within time sample within packet
// Nt * Nc == packet's payload

#define Nm (N_TIME_PER_BLOCK/N_TIME_PER_PACKET)
#define Nx (N_CHAN_PER_X/N_CHAN_PER_F)
#define Nq (N_FENGINES/4)
#define Nf 4
#define Nt N_TIME_PER_PACKET
#define Nc N_CHAN_PER_PACKET

// Copmutes paper_input_databuf_t.data word (uint64_t) offset for complex data
// word (8 inputs) corresponding to the given parameters.
#define paper_input_databuf_data_idx(m,x,q,f,t,c) \
  (c+Nc*(t+Nt*(f+Nf*(q+Nq*(x+Nx*m)))))

typedef struct paper_input_block {
  paper_input_header_t header;
  paper_input_header_cache_alignment padding; // Maintain cache alignment
  uint64_t data[N_BYTES_PER_BLOCK/sizeof(uint64_t)];
} paper_input_block_t;

// Used to pad after guppi_databuf to maintain cache alignment
typedef uint8_t guppi_databuf_cache_alignment[
  CACHE_ALIGNMENT - (sizeof(struct guppi_databuf)%CACHE_ALIGNMENT)
];

typedef struct paper_input_databuf {
  struct guppi_databuf header;
  guppi_databuf_cache_alignment padding; // Maintain cache alignment
  paper_input_block_t block[N_INPUT_BLOCKS+N_DEBUG_INPUT_BLOCKS];
} paper_input_databuf_t;

/*
 * GPU INPUT BUFFER STRUCTURES
 */

#define N_GPU_INPUT_BLOCKS 2

// == paper_gpu_input_databuf_t ==
//
// * Fluffer thread output
// * GPU thread input
// * Multidimensional array in "data" field:
//
//   +--time--+      +--chan--+      +--fid---+
//   |        |      |        |      |        |
//   V        V      V        V      V        V
//   m        t      c        x      q        f
//   ==      ==      ==      ==      ==      ==
//   m0 }--> t0 }--> c0 }--> x0 }--> q0 }--> f0
//   m1      t1      c1      x1      q1      f1
//   m2      t2      c2              q2      f2
//   :       :       :               :       :
//   ==      ==      ==      ==      ==      ==
//   Nm      Nt      Nc      Nx      Nq      Nf
//
//   Each group of eight 8 byte words (i.e. every 64 bytes) contains thirty-two
//   8bit+8bit complex inputs (8 inputs * four F engines) using complex block
//   size 32.

// Returns word (uint64_t) offset for real input data word (8 inputs)
// corresponding to the given parameters.  Corresponding imaginary data word is
// 4 words later (complex block size 32).
#define paper_gpu_input_databuf_data_idx(m,x,q,f,t,c) \
  (f+2*Nf*(q+Nq*(x+Nx*(c+Nc*(t+Nt*m)))))

typedef struct paper_gpu_input_block {
  paper_input_header_t header;
  paper_input_header_cache_alignment padding; // Maintain cache alignment
  uint64_t data[(2*N_BYTES_PER_BLOCK/sizeof(uint64_t))];
} paper_gpu_input_block_t;

typedef struct paper_gpu_input_databuf {
  struct guppi_databuf header;
  guppi_databuf_cache_alignment padding; // Maintain cache alignment
  paper_gpu_input_block_t block[N_GPU_INPUT_BLOCKS];
} paper_gpu_input_databuf_t;

/*
 * OUTPUT BUFFER STRUCTURES
 */

#define N_OUTPUT_BLOCKS 2

typedef struct paper_output_header {
  uint64_t mcnt;
  uint64_t flags[(N_CHAN_PER_X+63)/64];
} paper_output_header_t;

typedef uint8_t paper_output_header_cache_alignment[
  CACHE_ALIGNMENT - (sizeof(paper_output_header_t)%CACHE_ALIGNMENT)
];

typedef struct paper_output_block {
  paper_output_header_t header;
  paper_output_header_cache_alignment padding; // Maintain cache alignment
  float data[N_OUTPUT_MATRIX];
} paper_output_block_t;

typedef struct paper_output_databuf {
  struct guppi_databuf header;
  guppi_databuf_cache_alignment padding; // Maintain cache alignment
  paper_output_block_t block[N_OUTPUT_BLOCKS];
} paper_output_databuf_t;

/*
 * INPUT BUFFER FUNCTIONS
 */

paper_input_databuf_t *paper_input_databuf_create(int instance_id, int databuf_id);

static inline paper_input_databuf_t *paper_input_databuf_attach(int instance_id, int databuf_id)
{
    return (paper_input_databuf_t *)guppi_databuf_attach(instance_id, databuf_id);
}

/* Mimicking guppi_databuf's "detach" mispelling. */
static inline int paper_input_databuf_detach(paper_input_databuf_t *d)
{
    return guppi_databuf_detach((struct guppi_databuf *)d);
}

void paper_input_databuf_clear(paper_input_databuf_t *d);

static inline int paper_input_databuf_block_status(paper_input_databuf_t *d, int block_id)
{
    return guppi_databuf_block_status((struct guppi_databuf *)d, block_id);
}

static inline int paper_input_databuf_total_status(paper_input_databuf_t *d)
{
    return guppi_databuf_total_status((struct guppi_databuf *)d);
}


int paper_input_databuf_wait_free(paper_input_databuf_t *d, int block_id);

int paper_input_databuf_busywait_free(paper_input_databuf_t *d, int block_id);

int paper_input_databuf_wait_filled(paper_input_databuf_t *d, int block_id);

int paper_input_databuf_busywait_filled(paper_input_databuf_t *d, int block_id);

int paper_input_databuf_set_free(paper_input_databuf_t *d, int block_id);

int paper_input_databuf_set_filled(paper_input_databuf_t *d, int block_id);

/*
 * GPU INPUT BUFFER FUNCTIONS
 */

paper_gpu_input_databuf_t *paper_gpu_input_databuf_create(int instance_id, int databuf_id);

void paper_gpu_input_databuf_clear(paper_gpu_input_databuf_t *d);

static inline paper_gpu_input_databuf_t *paper_gpu_input_databuf_attach(int instance_id, int databuf_id)
{
    return (paper_gpu_input_databuf_t *)guppi_databuf_attach(instance_id, databuf_id);
}

/* Mimicking guppi_databuf's "detach" mispelling. */
static inline int paper_gpu_input_databuf_detach(paper_gpu_input_databuf_t *d)
{
    return guppi_databuf_detach((struct guppi_databuf *)d);
}

static inline int paper_gpu_input_databuf_block_status(paper_gpu_input_databuf_t *d, int block_id)
{
    return guppi_databuf_block_status((struct guppi_databuf *)d, block_id);
}

static inline int paper_gpu_input_databuf_total_status(paper_gpu_input_databuf_t *d)
{
    return guppi_databuf_total_status((struct guppi_databuf *)d);
}

static inline int paper_gpu_input_databuf_wait_free(paper_gpu_input_databuf_t *d, int block_id)
{
    return guppi_databuf_wait_free((struct guppi_databuf *)d, block_id);
}

static inline int paper_gpu_input_databuf_busywait_free(paper_gpu_input_databuf_t *d, int block_id)
{
    return guppi_databuf_busywait_free((struct guppi_databuf *)d, block_id);
}

static inline int paper_gpu_input_databuf_wait_filled(paper_gpu_input_databuf_t *d, int block_id)
{
    return guppi_databuf_wait_filled((struct guppi_databuf *)d, block_id);
}

static inline int paper_gpu_input_databuf_busywait_filled(paper_gpu_input_databuf_t *d, int block_id)
{
    return guppi_databuf_busywait_filled((struct guppi_databuf *)d, block_id);
}

static inline int paper_gpu_input_databuf_set_free(paper_gpu_input_databuf_t *d, int block_id)
{
    return guppi_databuf_set_free((struct guppi_databuf *)d, block_id);
}

static inline int paper_gpu_input_databuf_set_filled(paper_gpu_input_databuf_t *d, int block_id)
{
    return guppi_databuf_set_filled((struct guppi_databuf *)d, block_id);
}

/*
 * OUTPUT BUFFER FUNCTIONS
 */

paper_output_databuf_t *paper_output_databuf_create(int instance_id, int databuf_id);

void paper_output_databuf_clear(paper_output_databuf_t *d);

static inline paper_output_databuf_t *paper_output_databuf_attach(int instance_id, int databuf_id)
{
    return (paper_output_databuf_t *)guppi_databuf_attach(instance_id, databuf_id);
}

/* Mimicking guppi_databuf's "detach" mispelling. */
static inline int paper_output_databuf_detach(paper_output_databuf_t *d)
{
    return guppi_databuf_detach((struct guppi_databuf *)d);
}

static inline int paper_output_databuf_block_status(paper_output_databuf_t *d, int block_id)
{
    return guppi_databuf_block_status((struct guppi_databuf *)d, block_id);
}

static inline int paper_output_databuf_total_status(paper_output_databuf_t *d)
{
    return guppi_databuf_total_status((struct guppi_databuf *)d);
}

static inline int paper_output_databuf_wait_free(paper_output_databuf_t *d, int block_id)
{
    return guppi_databuf_wait_free((struct guppi_databuf *)d, block_id);
}

static inline int paper_output_databuf_busywait_free(paper_output_databuf_t *d, int block_id)
{
    return guppi_databuf_busywait_free((struct guppi_databuf *)d, block_id);
}

static inline int paper_output_databuf_wait_filled(paper_output_databuf_t *d, int block_id)
{
    return guppi_databuf_wait_filled((struct guppi_databuf *)d, block_id);
}

static inline int paper_output_databuf_busywait_filled(paper_output_databuf_t *d, int block_id)
{
    return guppi_databuf_busywait_filled((struct guppi_databuf *)d, block_id);
}

static inline int paper_output_databuf_set_free(paper_output_databuf_t *d, int block_id)
{
    return guppi_databuf_set_free((struct guppi_databuf *)d, block_id);
}

static inline int paper_output_databuf_set_filled(paper_output_databuf_t *d, int block_id)
{
    return guppi_databuf_set_filled((struct guppi_databuf *)d, block_id);
}

#endif // _PAPER_DATABUF_H
