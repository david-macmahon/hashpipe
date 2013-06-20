#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "paper_fluff.h"

#define TEST_ITERATIONS (500)

#define ELAPSED_NS(start,stop) \
  (((int64_t)stop.tv_sec-start.tv_sec)*1000*1000*1000+(stop.tv_nsec-start.tv_nsec))

#define N_WORDS_PER_BLOCK_IN (N_BYTES_PER_BLOCK/sizeof(uint64_t))
#define N_WORDS_PER_BLOCK_OUT (2*N_WORDS_PER_BLOCK_IN)
#define N_WORDS_PER_PACKET  (N_BYTES_PER_PACKET/sizeof(uint64_t))

int main(int argc, char *argv[])
{
  int i, j;
  uint64_t *in, *out;
  struct timespec start, stop;
  int fluffed;

  if(posix_memalign((void **)&in,  CACHE_ALIGNMENT,   N_BYTES_PER_BLOCK)
  || posix_memalign((void **)&out, CACHE_ALIGNMENT, 2*N_BYTES_PER_BLOCK)) {
    printf("cannot allocate memory\n");
    return 1;
  }

  //printf("in  = %p\n", in);
  //printf("out = %p\n", out);

#if 0
  in[0] = htobe64(0x0123456789abcdef);
  in[1] = htobe64(0xfedcba9876543210);

  uint8_t * cin = (uint8_t *)in;
  for(i=0; i<2; i++) {
    for(j=0; j<8; j++) {
      printf("%02x ", cin[8*i+j]);
    }
    printf("\n");
  }
  printf("\n");

  paper_fluff(in, out);

  uint8_t * cout = (uint8_t *)out;
  for(i=0; i<4; i++) {
    for(j=0; j<8; j++) {
      printf("%02x ", cout[8*i+j]);
    }
    printf("\n");
  }

  return 0;
}
#else



  printf("N_CHAN_PER_PACKET=%u\n", N_CHAN_PER_PACKET);
  printf("N_TIME_PER_PACKET=%u\n", N_TIME_PER_PACKET);
  printf("N_WORDS_PER_PACKET=%lu\n", N_WORDS_PER_PACKET);
  printf("N_PACKETS_PER_BLOCK=%u\n", N_PACKETS_PER_BLOCK);
  printf("N_BYTES_PER_BLOCK=%u\n", N_BYTES_PER_BLOCK);

#ifdef DEBUG_FLUFF
  fluffed = paper_fluff(in, out);
#else
  for(j=0; j<4; j++) {
    clock_gettime(CLOCK_MONOTONIC, &start);
    for(i=0; i<TEST_ITERATIONS; i++) {
      fluffed = paper_fluff(in, out);
    }
    clock_gettime(CLOCK_MONOTONIC, &stop);
    printf("fluffed %d words in %.6f ms (%.3f us per packet, %.3f Gbps)\n",
        fluffed, ELAPSED_NS(start, stop)/1e6/TEST_ITERATIONS,
        ELAPSED_NS(start, stop)/1e3/TEST_ITERATIONS/N_PACKETS_PER_BLOCK,
        (float)(fluffed*TEST_ITERATIONS*8*sizeof(uint64_t))/ELAPSED_NS(start, stop));
  }
#endif // DEBUG_FLUFF_GEN

  return 0;
}
#endif // 1
