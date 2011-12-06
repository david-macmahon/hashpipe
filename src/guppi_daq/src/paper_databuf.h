#include "guppi_databuf.h"

#define NUM_HEADERS_PER_BLOCK 3

typedef struct paper_input_header {
  uint64_t mcnt;
  uint64_t chan_present[2];
} paper_input_header_t;

typedef struct paper_input_block {
  paper_input_header_t header[NUM_HEADERS_PER_BLOCK];
  char data[];
} paper_input_block_t;

typedef struct paper_input_databuf {
  struct guppi_databuf header;
  paper_input_block_t blocks[];
} paper_input_databuf_t;

struct paper_databuf *paper_databuf_create(int n_block, size_t block_size,
        int databuf_id, int buf_type);
struct paper_databuf *paper_databuf_attach(int databuf_id);

