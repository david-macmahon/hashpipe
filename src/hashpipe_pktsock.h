/* hashpipe_pktsock.h
 *
 * Routines for dealing with packet sockets.
 * See `man 7 packet` and `packet_mmap.txt` for more info.
 */
#ifndef _HASHPIPE_PKTSOCK_H
#define _HASHPIPE_PKTSOCK_H

// Clients will need PACKET_RX_RING and/or PACKET_TX_RING
#include <linux/if_packet.h>

#ifdef __cplusplus
extern "C" {
#endif

struct hashpipe_pktsock {
  // These sizing fields should be initialized by the caller.  See the comments
  // for `pktsock_open` for details.
  unsigned int frame_size;
  unsigned int nframes;
  unsigned int nblocks;
  // The following fields are "private" and should not be modified by the
  // caller.
  int fd;
  char *p_ring;
  int next_idx;
};

// Return header field `h` from packet frame pointed to by `p`
#define TPACKET_HDR(p,h) (((struct tpacket_hdr *)p)->h)

// Return pointer to MAC header inside packet frame pointed to by `p`.
#define PKT_MAC(p) (p+TPACKET_HDR(p, tp_mac))

// Return pointer to network (e.g. IP) packet inside
// packet frame pointed to by `p`.
#define PKT_NET(p) (p+TPACKET_HDR(p, tp_net))

// `p_ps` should point to a `struct pktsock` that has been initialized by
// caller with desired values for the sizing parmaters `frame_size`, `nframes`,
// and `nblocks`.  `nblocks` MUST be a multiple of `nframes` and the resulting
// block size (i.e. `frame_size * nframes / nblocks`) MUST be a multiple of
// PAGE_SIZE.
//
// `ifname` should specify the name of the interface to bind to (e.g. "eth2").
//
// `ring_type` should be `PACKET_RX_RING` or `PACKET_TX_RING`.
//
// Returns 0 for success, non-zero for failure.  On failure, errno will be set.
//
// Upon successful compltetion, p_ps->fd will the the file descriptor of the
// socket, and p_ps->p_ring will be a poitner to the start of the ring buffer.
int hashpipe_pktsock_open(struct hashpipe_pktsock *p_ps, const char *ifname, int ring_type);


// Return NULL on timeout (or error), otherwise returns pointer to frame.
// The caller MUST release the frame back to the kernel (via
// `pktsock_release_frame`) once it is finished with the frame.
char * hashpipe_pktsock_recv_frame(struct hashpipe_pktsock *p_ps, int timeout_ms);

// Releases frame back to the kernel.  The caller must be release each frame
// back to the kernel once the caller is done with the frame.
void hashpipe_pktsock_release_frame(char * frame);

// Unmaps kernel ring buffer and closes socket.
int hashpipe_pktsock_close(struct hashpipe_pktsock *p_ps);

#ifdef __cplusplus
}
#endif

#endif // _HASHPIPE_PKTSOCK_H
