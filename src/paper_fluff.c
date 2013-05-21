#include <stdio.h>
//#include <stdlib.h>
//#include <unistd.h>
//#include <stdint.h>
#include <time.h>
#include <smmintrin.h>

#include "paper_fluff.h"

#define N_WORDS_PER_PACKET  (N_BYTES_PER_PACKET/sizeof(uint64_t))
// OUTPUT_STRIDE in is units of __m128i
#define OUTPUT_STRIDE (Nx*Nq*Nf)

typedef union {
  __m128i m128;
  __m64   m64[2];
} vec_t;
#define m128(v) (v.m128)
#define m64(v,i) (v.m64[i])

int paper_fluff(const uint64_t const * const in, uint64_t * out)
{
  //uint64_t v0, v1, v2, v3;
  vec_t in0, in1, in2, in3;
  vec_t v0, v1, v2, v3;
  int m, x, q, w;

  const vec_t *p_in0 = (vec_t *)in;
  const vec_t *p_in1 = p_in0 + N_WORDS_PER_PACKET/2;
  const vec_t *p_in2 = p_in1 + N_WORDS_PER_PACKET/2;
  const vec_t *p_in3 = p_in2 + N_WORDS_PER_PACKET/2;
  //uint64_t *p_out;
  __m128i *p_out;
  const __m128i mask = _mm_set_epi64((__m64)0xf0f0f0f0f0f0f0f0ULL, (__m64)0xf0f0f0f0f0f0f0f0ULL);

  for(m=0; m<Nm; m++) {
    for(x=0; x<Nx; x++) {
      for(q=0; q<Nq; q++) {
        p_out = (__m128i *)out + Nf*(q+Nq*(x+m*Nt*Nc*Nx));
        for(w=0; w<N_WORDS_PER_PACKET/2; w++) {
          m128(in0) = _mm_stream_load_si128((__m128i *)p_in0+w);
          m128(in1) = _mm_stream_load_si128((__m128i *)p_in1+w);
          m128(in2) = _mm_stream_load_si128((__m128i *)p_in2+w);
          m128(in3) = _mm_stream_load_si128((__m128i *)p_in3+w);

          m128(v0) = _mm_set_epi64(m64(in1,0), m64(in0,0));
          m128(v1) = _mm_set_epi64(m64(in3,0), m64(in2,0));
          m128(v2) = _mm_slli_epi64(m128(v0), 4); // Shift left 4
          m128(v3) = _mm_slli_epi64(m128(v1), 4); // Shift left 4

          _mm_stream_si128(p_out++, _mm_and_si128(m128(v0), mask));
          _mm_stream_si128(p_out++, _mm_and_si128(m128(v1), mask));
          _mm_stream_si128(p_out++, _mm_and_si128(m128(v2), mask));
          _mm_stream_si128(p_out  , _mm_and_si128(m128(v3), mask));

          p_out += OUTPUT_STRIDE-3;

          m128(v0) = _mm_set_epi64(m64(in1,1), m64(in0,1));
          m128(v1) = _mm_set_epi64(m64(in3,1), m64(in2,1));
          m128(v2) = _mm_slli_epi64(m128(v0), 4); // Shift left 4
          m128(v3) = _mm_slli_epi64(m128(v1), 4); // Shift left 4

          _mm_stream_si128(p_out++, _mm_and_si128(m128(v0), mask));
          _mm_stream_si128(p_out++, _mm_and_si128(m128(v1), mask));
          _mm_stream_si128(p_out++, _mm_and_si128(m128(v2), mask));
          _mm_stream_si128(p_out  , _mm_and_si128(m128(v3), mask));

          p_out += OUTPUT_STRIDE-3;
        }
        p_in0 += Nf*N_WORDS_PER_PACKET/2;
        p_in1 += Nf*N_WORDS_PER_PACKET/2;
        p_in2 += Nf*N_WORDS_PER_PACKET/2;
        p_in3 += Nf*N_WORDS_PER_PACKET/2;
      }
    }
  }

  return Nm*Nx*Nq*4*N_WORDS_PER_PACKET;
}
