#include "paper_databuf.h"

// == paper_databuf_input_t ==
//
// * Net thread output
// * Fluffer thread inputs
// * Ordered sequence of packets:
//
//   +-- mcount
//   |
//   |       +-- xid
//   |       |
//   |       |       |<--feng-->|      |<-packet->|
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

// Fluffer thread output == GPU thread inputs == Multidimensional array:
//
//   +--time--+      +--chan--+      +-inputs-+
//   |        |      |        |      |        |
//   V        V      V        V      V        V
//   m        t      x        c      q        f
//   ==      ==      ==      ==      ==      ==
//   m0 }--> t0 }--> x0 }--> c0 }--> q0 }--> f0
//   m1      t1      x1      c1      q1      f1
//   m2      t2              c2      q2      f2
//   :       :               :       :       : 
//   ==      ==      ==      ==      ==      ==
//   Nm      Nt      Nx      Nc      Nq      Nf
//
//   Each group of eight 8 byte words (i.e. every 64 bytes) contains thirty-two
//   8bit+8bit complex inputs (8 inputs * four F engines) using complex block
//   size 32.
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

// Returns word (uint64_t) offset for complex data word (8 inputs)
// corresponding to the given parameters.
#define in_idx(m,x,q,f,t,c) \
  (c+Nc*(t+Nt*(f+Nf*(q+Nq*(x+Nx*m)))))

// Returns word (uint64_t) offset for real input data word (8 inputs)
// corresponding to the given parameters.  Corresponding imaginary data word is
// 4 words later (complex block size 32).
#define out_idx(m,x,q,f,t,c) \
  (f+2*Nf*(q+Nq*(c+Nc*(x+Nx*(t+Nt*m)))))

int paper_fluff(const uint64_t const * const in, uint64_t * out);
