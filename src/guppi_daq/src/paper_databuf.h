#ifndef _PAPER_DATABUF_H
#define _PAPER_DATABUF_H

#include <stdint.h>
#include "guppi_databuf.h"

#define N_INPUTS   64
#define N_FENGINES  8
#define N_INPUTS_PER_FENGINE  (N_INPUTS/N_FENGINES)

#define N_INPUT_BLOCKS 4
#define N_PACKETS_PER_BLOCK 512
#define N_SUB_BLOCKS_PER_INPUT_BLOCK 64
#define N_SUB_BLOCKS_PER_COMPLEXITY (N_SUB_BLOCKS_PER_INPUT_BLOCK/2)
#define N_TIME   4	// per sub_block
#define N_CHAN 256	// per sub_block
#define N_INPUT  8	// per sub_block

#define N_FLUFFED_BYTES_PER_BLOCK  ((N_PACKETS_PER_BLOCK * 8192) * 2)
#define N_FLUFFED_WORDS_PER_BLOCK (N_FLUFFED_BYTES_PER_BLOCK / 8) 

// Number of floats in xGPU's "register tile order" output matrix.
#define N_OUTPUT_MATRIX (2 * N_CHAN * (N_INPUT/2 + 2) * N_INPUT)

#define PAGE_SIZE (4096)

/*
 * INPUT BUFFER STRUCTURES
 */

typedef struct paper_input_input {
    int8_t sample;				
} paper_input_input_t;

typedef struct paper_input_chan {
    paper_input_input_t input[N_INPUT*8];	
} paper_input_chan_t;

typedef struct paper_input_time {
    paper_input_chan_t chan[N_CHAN];		
} paper_input_time_t;

typedef struct paper_input_sub_block {
    paper_input_time_t time[N_TIME];
} paper_input_sub_block_t;

typedef struct paper_input_complexity {
  paper_input_sub_block_t sub_block[N_SUB_BLOCKS_PER_INPUT_BLOCK];
} paper_input_complexity_t;

typedef struct paper_input_header {
  uint64_t mcnt;
  uint64_t chan_present[(N_CHAN+63)/64];
} paper_input_header_t;

typedef struct paper_input_block {
  paper_input_header_t header[N_SUB_BLOCKS_PER_INPUT_BLOCK];
  paper_input_complexity_t complexity[2];	// [0] is real, [1] is imag
} paper_input_block_t;

typedef struct paper_input_databuf {
  struct guppi_databuf header;
  paper_input_block_t block[N_INPUT_BLOCKS];
} paper_input_databuf_t;

/*
 * OUTPUT BUFFER STRUCTURES
 */

typedef struct paper_output_header {
  uint64_t mcnt;
  uint64_t flags[(N_CHAN+63)/64];
} paper_output_header_t;

typedef struct paper_output_block {
  paper_output_header_t header;
  float data[N_OUTPUT_MATRIX];
} paper_output_block_t;

typedef struct paper_output_databuf {
  struct guppi_databuf header;
  paper_output_block_t block[];
} paper_output_databuf_t;

/*
 * INPUT BUFFER FUNCTIONS
 */

struct paper_input_databuf *paper_input_databuf_create(int n_block, size_t block_size,
        int databuf_id);

struct paper_input_databuf *paper_input_databuf_attach(int databuf_id);

int paper_input_databuf_detach(struct paper_input_databuf *d);

void paper_input_databuf_clear(struct paper_input_databuf *d);

int paper_input_databuf_block_status(struct paper_input_databuf *d, int block_id);

int paper_input_databuf_total_status(struct paper_input_databuf *d);

int paper_input_databuf_wait_free(struct paper_input_databuf *d, int block_id);

int paper_input_databuf_wait_filled(struct paper_input_databuf *d, int block_id);

int paper_input_databuf_set_free(struct paper_input_databuf *d, int block_id);

int paper_input_databuf_set_filled(struct paper_input_databuf *d, int block_id);

/*
 * OUTPUT BUFFER FUNCTIONS
 */

struct paper_output_databuf *paper_output_databuf_create(int n_block, size_t block_size,
        int databuf_id);

struct paper_output_databuf *paper_output_databuf_attach(int databuf_id);

int paper_output_databuf_detach(struct paper_output_databuf *d);

void paper_output_databuf_clear(struct paper_output_databuf *d);

int paper_output_databuf_block_status(struct paper_output_databuf *d, int block_id);

int paper_output_databuf_total_status(struct paper_output_databuf *d);

int paper_output_databuf_wait_free(struct paper_output_databuf *d, int block_id);

int paper_output_databuf_wait_filled(struct paper_output_databuf *d, int block_id);

int paper_output_databuf_set_free(struct paper_output_databuf *d, int block_id);

int paper_output_databuf_set_filled(struct paper_output_databuf *d, int block_id);

#endif // _PAPER_DATABUF_H
