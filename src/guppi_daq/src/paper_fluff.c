#include <stdio.h>
//#include <stdlib.h>
//#include <unistd.h>
//#include <stdint.h>
#include <time.h>

#include "paper_fluff.h"

#define N_WORDS_PER_PACKET  (N_BYTES_PER_PACKET/sizeof(uint64_t))
#define PACKET_STRIDE (Nx*Nq*Nf)

int paper_fluff(const uint64_t const * const in, uint64_t * out)
{
  uint64_t v0, v1, v2, v3;
  int m, x, q, w;

  const uint64_t *p_in0 = in;
  const uint64_t *p_in1 = p_in0 + N_WORDS_PER_PACKET;
  const uint64_t *p_in2 = p_in1 + N_WORDS_PER_PACKET;
  const uint64_t *p_in3 = p_in2 + N_WORDS_PER_PACKET;
  uint64_t *p_out;

  for(m=0; m<Nm; m++) {
    for(x=0; x<Nx; x++) {
      for(q=0; q<Nq; q++) {
        p_out = out + (m*Nx*Nq*Nf + x*Nq*Nf + q*2*Nf);
        for(w=0; w<N_WORDS_PER_PACKET; w++) {
          v0 = p_in0[w];
          v1 = p_in1[w];
          v2 = p_in2[w];
          v3 = p_in3[w];
          p_out[PACKET_STRIDE*w  ] = (v0 & 0xf0f0f0f0f0f0f0f0LL);
          p_out[PACKET_STRIDE*w+1] = (v1 & 0xf0f0f0f0f0f0f0f0LL);
          p_out[PACKET_STRIDE*w+2] = (v2 & 0xf0f0f0f0f0f0f0f0LL);
          p_out[PACKET_STRIDE*w+3] = (v3 & 0xf0f0f0f0f0f0f0f0LL);
          p_out[PACKET_STRIDE*w+4] = (v0 & 0x0f0f0f0f0f0f0f0fLL) << 4;
          p_out[PACKET_STRIDE*w+5] = (v1 & 0x0f0f0f0f0f0f0f0fLL) << 4;
          p_out[PACKET_STRIDE*w+6] = (v2 & 0x0f0f0f0f0f0f0f0fLL) << 4;
          p_out[PACKET_STRIDE*w+7] = (v3 & 0x0f0f0f0f0f0f0f0fLL) << 4;
        }
      }
      p_in0 += Nf*N_WORDS_PER_PACKET;
      p_in1 += Nf*N_WORDS_PER_PACKET;
      p_in2 += Nf*N_WORDS_PER_PACKET;
      p_in3 += Nf*N_WORDS_PER_PACKET;
    }
  }

  return Nm*Nx*Nq*4*N_WORDS_PER_PACKET;
}
