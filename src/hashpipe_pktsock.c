#include <string.h>
#include <unistd.h>
#include <errno.h>

// From `man ioctl`
#include <sys/ioctl.h>

// From `man htons`
#include <arpa/inet.h>

// From `man 7 packet`
#include <sys/socket.h>
//#include <netpacket/packet.h>
#include <net/ethernet.h> /* the L2 protocols */

// From `man netdevice`
#include <sys/ioctl.h>
#include <net/if.h>

// From packet_mmap.txt
#include <linux/if_packet.h>

// From `man mmap`
#include <sys/mman.h>

// From `man poll`
#include <poll.h>

#include "hashpipe_pktsock.h"

//#define PKTSOCK_PROTO ETH_P_ALL
#define PKTSOCK_PROTO ETH_P_IP

#define BLOCK_SIZE(p_ps) \
  (p_ps->frame_size * p_ps->nframes / p_ps->nblocks)

// p_ps->s_tpr should be initialized by caller with desired ring parameters.
// ifname should specify the name of the interface to bind to (e.g. "eth2").
// ring_type should be PACKET_RX_RING or PACKET_TX_RING.
//
// Returns 0 for success, non-zero for failure.  On failure, errno will be set.
//
// Upon successful compltetion, p_ps->fd will the the file descriptor of the
// socket, and p_ps->p_ring will be a poitner to the start of the ring buffer.
int hashpipe_pktsock_open(struct hashpipe_pktsock *p_ps, const char *ifname, int ring_type)
{
  struct ifreq s_ifr;
  struct sockaddr_ll my_addr;
  struct tpacket_req s_tpr;

  // Validate that nframes is multiple of nblocks
  // and that block size is a multiple of page size
  if(p_ps->nframes % p_ps->nblocks != 0
  || BLOCK_SIZE(p_ps) % sysconf(_SC_PAGESIZE) != 0) {
    errno = EINVAL;
    return -1;
  }

  // Create socket
  p_ps->fd = socket(PF_PACKET, SOCK_RAW, htons(PKTSOCK_PROTO));
  if(p_ps->fd == -1) {
    return -2;
  }

  /* get interface index of ifname */
  strncpy (s_ifr.ifr_name, ifname, sizeof(s_ifr.ifr_name));
  ioctl(p_ps->fd, SIOCGIFINDEX, &s_ifr);

  /* fill sockaddr_ll struct to prepare binding */
  my_addr.sll_family = AF_PACKET;
  my_addr.sll_protocol = htons(PKTSOCK_PROTO);
  my_addr.sll_ifindex =  s_ifr.ifr_ifindex;

  /* bind socket to interface */
  if(bind(p_ps->fd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr_ll))
      == -1) {
    return -3;
  }

  // Set socket options
  s_tpr.tp_block_size = p_ps->frame_size * p_ps->nframes / p_ps->nblocks;
  s_tpr.tp_block_nr = p_ps->nblocks;
  s_tpr.tp_frame_size = p_ps->frame_size;
  s_tpr.tp_frame_nr = p_ps->nframes;
  if(setsockopt(p_ps->fd, SOL_PACKET,
        ring_type, (void *) &s_tpr, sizeof(s_tpr)) == -1) {
    return -4;
  }

  // Map ring into memory space
  size_t size = p_ps->frame_size * p_ps->nframes;
  p_ps->p_ring = mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, p_ps->fd, 0);
  if(p_ps->p_ring == (void *)-1) {
    return -5;
  }
  p_ps->next_idx = 0;

  return 0;
}

// Return NULL on timeout (or error), otherwise returns pointer to frame.
char * hashpipe_pktsock_recv_frame(struct hashpipe_pktsock *p_ps, int timeout_ms)
{
  struct pollfd pfd;
  char * frame;

  frame = p_ps->p_ring + p_ps->next_idx * p_ps->frame_size;

  // If frame has not yet been received. poll
  if(!(TPACKET_HDR(frame, tp_status) & TP_STATUS_USER)) {
    pfd.fd = p_ps->fd;
    pfd.revents = 0;
    pfd.events = POLLIN|POLLRDNORM|POLLERR;
    if(poll(&pfd, 1, timeout_ms) == 0) {
      // Timeout
      return NULL;
    }
    // Should "never" happen
    if(!(TPACKET_HDR(frame, tp_status) & TP_STATUS_USER)) {
      // Error condition
      return NULL;
    }
  }

  // Advance next_idx
  p_ps->next_idx++;
  if(p_ps->next_idx >= p_ps->nframes) {
    p_ps->next_idx = 0;
  }

  return frame;
}

// Return NULL on timeout (or error), otherwise returns pointer to the next
// frame that contains a UDP packet with specified destination port.
char * hashpipe_pktsock_recv_udp_frame(struct hashpipe_pktsock *p_ps, int dst_port, int timeout_ms)
{
  char * p_frame = hashpipe_pktsock_recv_frame(p_ps, timeout_ms);
  // While we get packets, but they aren't what we want...
  while(p_frame && !(PKT_IS_UDP(p_frame) && PKT_UDP_DST(p_frame) == dst_port)) {
    // ...release them and get another.
    hashpipe_pktsock_release_frame(p_frame);
    p_frame = hashpipe_pktsock_recv_frame(p_ps, timeout_ms);
  }
  // Return the packet we got (or NULL if we timed out waiting for any packet)
  // TODO Timeout even if we are getting unwanted packets?
  return p_frame;
}

// Releases frame back to the kernel
void hashpipe_pktsock_release_frame(char * frame)
{
  TPACKET_HDR(frame, tp_status) = TP_STATUS_KERNEL;
}

// Unmaps kernel ring buffer and closes socket
int hashpipe_pktsock_close(struct hashpipe_pktsock *p_ps)
{
  size_t size = p_ps->frame_size * p_ps->nframes;
  if(munmap(p_ps->p_ring, size) == -1) {
    return -1;
  }
  if(close(p_ps->fd) == -1) {
    return -1;
  }
  return 0;
}
