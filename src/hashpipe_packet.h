#ifndef HASHPIPE_PACKET_H
#define HASHPIPE_PACKET_H

#include <stdint.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#define ETHER_BROADCAST (0xffffffffffffULL)
#define IP_BROADCAST (0xffffffffUL)

// UDP Packet (with link layer header)
struct __attribute__ ((__packed__)) udppkt {
  struct ethhdr ethhdr;
  struct iphdr iphdr;
  struct udphdr udphdr;
  uint8_t payload[];
};

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

static inline void hashpipe_packet_ethhdr_init(struct ethhdr * ethhdr,
    uint64_t dst_mac,
    uint64_t src_mac,
    uint16_t ether_type)
{
  int i;
  for(i=0; i<ETH_ALEN; i++) {
    ethhdr->h_dest[i]   = (dst_mac >> (8*(5-i))) & 0xff;
    ethhdr->h_source[i] = (src_mac >> (8*(5-i))) & 0xff;
  }
  ethhdr->h_proto = htons(ether_type);
}

static inline void hashpipe_packet_iphdr_init(struct iphdr * iphdr,
    uint64_t src_ip,
    uint64_t dst_ip)
{
  iphdr->version = IPVERSION;
  iphdr->ihl = 5;
  iphdr->tos = 0;
  iphdr->tot_len = htons(sizeof(struct iphdr));
  iphdr->id = 0;
  iphdr->frag_off = htons(IP_DF); // Do not fragment
  iphdr->ttl = IPDEFTTL;
  iphdr->protocol = IPPROTO_UDP;
  iphdr->check = 0;
  iphdr->saddr = htonl(src_ip);
  iphdr->daddr = htonl(dst_ip);
}

static inline void hashpipe_packet_udphdr_init(struct udphdr * udphdr,
    uint16_t src_port,
    uint16_t dst_port)
{
  // Init udp header
  udphdr->uh_sport = htons(src_port);
  udphdr->uh_dport = htons(dst_port);
  udphdr->uh_ulen = htons(sizeof(struct udphdr));
  udphdr->uh_sum = 0;
}

static inline void hashpipe_packet_udppkt_length(struct udppkt * udppkt,
    uint16_t payload_length)
{
  udppkt->iphdr.tot_len =
    htons(sizeof(struct iphdr) + sizeof(struct udphdr) + payload_length);
  udppkt->udphdr.uh_ulen =
    htons(sizeof(struct udphdr) + payload_length);
}

// Computes the IP header checksum and stores the computed value in the
// header.  This uses the `ihl` field in the header to compute the length, so
// that field must be pre-populated for correct usage.
static inline uint16_t hashpipe_packet_iphdr_checksum(struct iphdr * iphdr)
{
  int i;

  // Cast iphdr pointer to uint16_t pointer.
  uint16_t * p = (uint16_t *)iphdr;

  // Start 32 bit sum with inverse of checksum so we don't have to avoid
  // checksum field while adding 16 bit values.
  uint32_t sum32 = -ntohs(iphdr->check);

  // Add all the 16-bit header words
  for(i=0; i<2*iphdr->ihl; i++) {
    sum32 += ntohs(p[i]);
  }

  // While any upper bits are set, add them into lower bits
  while(sum32 & 0xffff0000) {
    sum32 = ((sum32 >> 16) & 0xffff) +
            ( sum32        & 0xffff);
  }

  // Set the field and return the value
  iphdr->check = ~((uint16_t)(sum32 & 0xffff));
  return iphdr->check;
}

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // HASHPIPE_PACKET_H
