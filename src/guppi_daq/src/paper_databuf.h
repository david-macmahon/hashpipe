#include "guppi_databuf.h"

#define N_BLOCKS 4
#define N_SUB_BLOCKS_PER_BLOCK 3
#define N_TIME 128
#define N_CHAN 128
#define N_INPUT 64

typedef struct paper_input_input {
    uint8_t real;
    uint8_t imag;
} paper_input_input_t;

typedef struct paper_input_chan {
    paper_input_input_t input[N_INPUT];
} paper_input_chan_t;

typedef struct paper_input_time {
    paper_input_chan_t chan[N_CHAN];
} paper_input_time_t;

typedef struct paper_input_sub_block {
    paper_input_time_t time[N_TIME];
} paper_input_sub_block_t;

typedef struct paper_input_header {
  uint64_t mcnt;
  uint64_t chan_present[2];
} paper_input_header_t;

typedef struct paper_input_block {
  paper_input_header_t header[N_SUB_BLOCKS_PER_BLOCK];
  paper_input_sub_block_t sub_block[N_SUB_BLOCKS_PER_BLOCK];
} paper_input_block_t;

typedef struct paper_input_databuf {
  struct guppi_databuf header;
  paper_input_block_t block[N_BLOCKS];
} paper_input_databuf_t;

struct paper_input_databuf *paper_input_databuf_create(int n_block, size_t block_size,
        int databuf_id, int buf_type);

struct paper_input_databuf *paper_input_databuf_attach(int databuf_id);

/* Mimicking guppi_databuf's "detach" mispelling. */
int paper_input_databuf_detach(struct paper_input_databuf *d);

void paper_input_databuf_clear(struct paper_input_databuf *d);

int paper_input_databuf_block_status(struct paper_input_databuf *d, int block_id);

int paper_input_databuf_total_status(struct paper_input_databuf *d);

int paper_input_databuf_wait_free(struct paper_input_databuf *d, int block_id);

int paper_input_databuf_wait_filled(struct paper_input_databuf *d, int block_id);

int paper_input_databuf_set_free(struct paper_input_databuf *d, int block_id);

int paper_input_databuf_set_filled(struct paper_input_databuf *d, int block_id);
