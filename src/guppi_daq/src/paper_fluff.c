#include <stdio.h>
//#include <stdlib.h>
//#include <unistd.h>
//#include <stdint.h>
#include <time.h>

#include "paper_fluff.h"

int paper_fluff(const uint64_t const * const in, uint64_t * out)
{
  uint64_t val;
  int m, t, x, c, q, f;
  int i;
  int o;

  for(m=0; m<Nm; m++) {
    for(t=0; t<Nt; t++) {
      for(x=0; x<Nx; x++) {
        for(c=0; c<Nc; c++) {
          // Surprisingly, it's slightly faster to switch the f and q loops
          for(f=0; f<Nf; f++) {
            for(q=0; q<Nq; q++) {
              i = in_idx( m,x,q,f,t,c);
              o = out_idx(m,x,q,f,t,c);
#ifdef DEBUG_FLUFF_GEN
              printf("m=%02d t=%02d x=%d c=%02d q=%d f=%d : in=%d out=%d\n",
                  m, t, x, c, q, f, i, o);
#endif
              val = in[i];
              out[o  ] =  val & 0xf0f0f0f0f0f0f0f0LL;
              out[o+4] = (val & 0x0f0f0f0f0f0f0f0fLL) << 4;
            }
          }
        }
      }
    }
  }
  return (Nm*Nt*Nx*Nc*Nf*Nq);
}
