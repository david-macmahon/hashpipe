#define _GNU_SOURCE 1
#include <stdint.h>
#include <sys/types.h>

void fluff_32to64(uint64_t *in, uint64_t *real, uint64_t *imag, size_t n64) {
  size_t i;
  for(i=0; i<n64; i++) {
    uint64_t val = in[i];
    real[i] =  val & 0xf0f0f0f0f0f0f0f0LL;
    imag[i] = (val & 0x0f0f0f0f0f0f0f0fLL) << 4;
  }
}

