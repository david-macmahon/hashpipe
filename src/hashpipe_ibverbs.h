#ifndef HASHPIPE_IBVERBS_H
#define HASHPIPE_IBVERBS_H

#include <stdint.h>
#include <time.h>
#include <net/if.h>

#include <infiniband/verbs.h>

// These defines control various aspects of the Hashpipe IB Verbs library.
#define HPIBV_USE_SEND_CC       0
#define HPIBV_USE_MMAP_PKTBUFS  1
#define HPIBV_USE_TIMING_DIAGS  0
#define HPIBV_USE_EXP_CQ        1

// The Mellanox installed infiniband/verbs.h file does not define
// IBV_DEVICE_IP_CSUM or IBV_SEND_IP_CSUM.  This was an attempt to utilize said
// functionality when these values are not defined, but it didn't seem to have
// any effect even though the reported capabilities indicated that this feature
// was supported by the hardware.  Not sure what to make of that, but for now
// this hack is not enabled.
#ifdef ENABLE_IP_CSUM_HACK
#ifndef HAVE_IBV_IP_CSUM
#define HAVE_IBV_IP_CSUM
#define IBV_DEVICE_IP_CSUM (1 << 18)
#define IBV_SEND_IP_CSUM   (1 <<  4)
#endif // HAVE_IBV_IP_CSUM
#endif // ENABLE_IP_CSUM_HACK

#define ELAPSED_NS(start,stop) \
    (((int64_t)stop.tv_sec-start.tv_sec)*1000*1000*1000+(stop.tv_nsec-start.tv_nsec))

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// The `struct hashpipe_ibv_send_pkt` structure is essentially a `struct
// ibv_send_wr`.  Because the first field is a `struct ibv_send_wr`, pointers
// to `struct hashpipe_ibv_send_pkt` can be cast as pointers to `struct
// ibv_send_wr`. This definition was chosen over a `typedef` or `#define` to
// mainain a degee of symmetry with `struct hashpipe_ibv_recv_pkt`.
//
// When using user managed buffers that require additional fields, the user
// MUST define a stucture that has a `struct hashpipe_ibv_send_pkt` as its
// first field.
struct hashpipe_ibv_send_pkt {
  struct ibv_send_wr wr;
#if HPIBV_USE_TIMING_DIAGS
  struct timespec ts;
  uint64_t elapsed_ns;
  uint64_t elapsed_ns_total;
  uint64_t elapsed_ns_count;
#endif
#if HPIBV_USE_EXP_CQ
  // Timestamp from work completion
  uint64_t timestamp;
#endif
};

// The `struct hashpipe_ibv_recv_pkt` structure is essentially a `struct
// ibv_recv_wr` with a `length` field tacked on to the end.  Because the first
// field is a `struct ibv_recv_wr`, pointers to `struct hashpipe_ibv_recv_pkt`
// can be cast as pointers to `struct ibv_recv_wr`.
//
// The `length` field stores the full length of the data copied into the
// scatter/gather memory buffers.  Each scatter/gather memory buffer is fully
// filled before moving onto the next scatter/gather memory buffer.  Consumers
// can iterate through the scatter/gather list comsuming bytes until `length`
// bytes have been consumed.
//
// When using user managed buffers that require additional fields, the user
// MUST define a stucture that has a `struct hashpipe_ibv_recv_pkt` as its
// first field.
struct hashpipe_ibv_recv_pkt {
  struct ibv_recv_wr wr;
  uint32_t length;
#if HPIBV_USE_EXP_CQ
  // Timestamp from work completion
  uint64_t timestamp;
#endif
};

// This structure holds pointers and data values for a hashpipe_ibverbs
// instance.  See the comments for `hashpipe_ibv_init` for more details.
struct hashpipe_ibv_context {
  // ibverbs structures managed by library.
  struct ibv_context           * ctx;      // ibverbs context
  struct ibv_pd                * pd;       // protection domain
  struct ibv_comp_channel      * send_cc;  // send completion channel
  struct ibv_comp_channel      * recv_cc;  // recv completion channel
  struct ibv_cq               ** send_cq;  // send completion queues
  struct ibv_cq               ** recv_cq;  // recv completion queues
  struct ibv_qp               ** qp;       // send/recv queue pairs
  struct ibv_device_attr         dev_attr; // device attributes
  uint32_t                       nqp;      // number of QPs

  // Physical port number on NIC.  Managed by library.
  uint8_t                        port_num;

  // MAC address of network interface in network byte order (i.e. big endian).
  // Managed by library.
  uint8_t                        mac[6];

  // Interface ID portion of the link-local IPv6 address of the network
  // interface in host byte order (e.g.  little endian on x86).  Managed by
  // library.
  uint64_t                       interface_id;

  // Packet (and work request) buffers (arrays).  Managed by library or
  // advanced user.
  struct hashpipe_ibv_send_pkt * send_pkt_buf;
  struct hashpipe_ibv_recv_pkt * recv_pkt_buf;

  // The send packet buffers pointed to by `send_pkt_buf` are managed as a
  // linked list of unused entries.  `send_pkt_head` points to the head of this
  // unused list.  Managed by library.
  struct hashpipe_ibv_send_pkt * send_pkt_head;

  // Scatter/gather buffers (arrays).  Managed by library or advanced user.
  struct ibv_sge               * send_sge_buf;
  struct ibv_sge               * recv_sge_buf;

  // Send and receive memory region buffers (i.e.packet buffers).  Managed by
  // library or advanced user.
  uint8_t                      * send_mr_buf;
  uint8_t                      * recv_mr_buf;

  // Size of the send and receive memory region buffers (i.e. packet buffers).
  // Managed by library or advanced user.
  size_t                         send_mr_size;
  size_t                         recv_mr_size;

  // Send and receive memory regions that have been registered with `pd`.
  // Managed by library.
  struct ibv_mr                * send_mr;
  struct ibv_mr                * recv_mr;

  // Number of send and receive packets to buffer per QP.  Specified by user.
  // Passing 0 for these fields with user managed buffers is an error.
  // Passing 0 for these fields with library managed buffers means to use the
  // max allowed number of work requests (packets) per QP.
  uint32_t                       send_pkt_num;
  uint32_t                       recv_pkt_num;

  // Max size of each packet (including all RAW headers) for both send and
  // receive buffers.  Specified by user.
  uint32_t                       pkt_size_max;

  // Flag indicating user managed buffers/structures (non-zero == user managed)
  int                            user_managed_flag;

  // Max number of "flows" (packet selection criteria) to support.  A zero
  // value will be interpreted as and replaced by a value of 1.  Specified
  // by user.
  uint32_t                       max_flows;

  // Pointers to "flows" specified via calls to `hashpipe_ibv_create_flow()`.
  // Managed by library.
  struct ibv_flow             ** ibv_flows;

  // Array of dst_ip values specified when creating "flows".  These are
  // one-to-one mapped with the flow pointers in `ibv_flows`.  These need to be
  // stored so that we can drop multicast membership when destroying a flow.
  // The `flow_dst_ips` entry for a flow is set to 0 if there is no dst_ip
  // specified for the corresonding flow.  Managed by library.
  uint32_t                     * flow_dst_ips;

  // Socket used for multicast subscriptions.  This offloads IGMP management to
  // the kernel, which means that incoming IGMP packets (e.g. from the switch)
  // must be permitted to reach the kernel, which means that the NIC must be
  // allowed to pass non-ibverbs packets to the kernel.  The socket is opened
  // in hashpipe_ibv_init() and closed on hashpipe_ibv_shutdown().  When a flow
  // rule with a multicast dst_ip is created, an IP_ADD_MEMBERSHIP setsockopt()
  // call is made to subscribe to that multicast group.  Whenever a flow rule
  // with a multicast dst_ip is destroyed, an IP_DROP_MEMBERSHIP setsockopt()
  // call is made to unsubscribe from that multicast group.  Managed by
  // library.
  int                            mcast_subscriber;

  // Character buffer to hold interface name.  Contents supplied by user.
  char                           interface_name[IFNAMSIZ];
};

// Structure for defining various types of flow rules
struct hashpipe_ibv_flow {
  struct ibv_flow_attr         attr;
  struct ibv_flow_spec_eth     spec_eth;
  struct ibv_flow_spec_ipv4    spec_ipv4;
  struct ibv_flow_spec_tcp_udp spec_tcp_udp;
} __attribute__((packed));

// Stores hardware (aka MAC) address in `mac` and interface ID (i.e. lower 64
// bits of an ipv6 link-local address in modified EUI-64 format) in
// `interface_id` in host order.
//
// Returns 0 on success, non-zero on error (and errno will be set).
//
// Either `mac` or `interface_id` can be NULL if the corresponding value is not
// desired.  Both can be NULL to test the validity of the interface name.  If
// `mac` is non-null, it must point to a buffer at least 6 bytes long.
//
// `mac` is returned in network byte order (i.e. big endian).
// `interface_id` is returned in host byte order (e.g. little endian on x86).
int hashpipe_ibv_get_interface_info(
    const char * interface_name, uint8_t *mac, uint64_t *interface_id);

// Searches for the IBV device and port corresponding to link-local IPv6
// address `fe80::<interface_id>`.  `hashpipe_ibv_get_interface_info` can be
// used to get `interface_id` for a given interface name.  If a matching IBV
// device is found, the `ibv_context` for the opened device is stored in
// `found_ctx` (if non-NULL), the corresponding (physical) port number is
// stored in `found_port` (if non-NULL), the corresponding ibv_device_attr
// structure is copied to `found_dev_attr` (if non_NULL), and 0 is returned.
// If not found, 1 is returned.  If some other error occurs, an error message
// is printed to stderr, a value greater than 1 is returned, and the contents
// of `found_ctx` and `found_port` are unchanged.  If an `ibv_context` is
// returned, the caller is responsible for closing it.  Note that
// `interface_id` is expected to be in host byte order, as returned by
// `hashpipe_ibv_get_interface_info()`.
//
// NOTE: This is a low-level function intended for internal use.  Most users
// will want to use `hashpipe_ibv_init()` instead.
int hashpipe_ibv_open_device_for_interface_id(
    uint64_t                 interface_id,
    struct ibv_context    ** found_ctx,
    struct ibv_device_attr * found_dev_attr,
    uint8_t                * found_port);

// The `hashpipe_ibv_init()` funciton sets up the data structures necessary for
// initiating raw packet flows using ibverbs.  When this function returns
// successfully, the underlying ibverbs "queue pair" will be in the INIT state,
// so the queue pair will drop incoming packets and outgoing packets.  The
// `hashpipe_ibverbs` library will automatially transition the queue pair to
// the appropriate state for receiving or sending packets on the first call to
// a `hashpipe_ibverbs` function that initiates such activity.  After this
// function returns successfully, the user can set the queue pair state
// explicitly, if desired, by calling ibv_modify_qp() directly.  The associated
// `send_wr` structs will have the IBV_SEND_IP_CSUM flag set if the underlying
// hardware supports checksum offloading, so take care if modifying these flag
// fields.
//
// This function returns 0 on success, non-zero on error.
//
// The caller must provide the following things in the `hashpipe_ibv_context`
// sturcture pointed to by `hibv_ctx`:
//
// 1. The desired interface name.
// 2. Packet buffer sizing info.
// 3. Number of "flow" rules to support.
// 4. User managed flag
// 5. User managed buffers (if user managed flag is set to true)
//
// * INTERFACE NAME
//   (`interface_name`)
//
//   The desired interface name MUST be copied into the `interface_name` buffer
//   in the `hashpipe_ibv_context` structure pointed to by `hibv_ctx`.  Be
//   aware that this buffer has space for at most `IFNAMSIZ` characters,
//   including the trailing NUL.  The recommended way to do this is:
//
//       struct hashpipe_ibv_context hibv_ctx = {0};
//       strncpy(hibv_ctx.interface_name, desired_interface_name, IFNAMSIZ);
//       hibv_ctx.interface_name[IFNAMSIZ-1] = '\0'; // Ensure NUL termination
//
// * PACKET BUFFER SIZING INFO
//   (`send_pkt_num`, `recv_pkt_num`, `pkt_size_max`)
//
//   The library uses two different packet buffers: one for sending and one for
//   receiving.  The send buffer holds up to `send_pkt_num` packets of up to
//   `pkt_size_max` bytes each.  The receive buffer holds up to `recv_pkt_num`
//   packets of up to `pkt_size_max` each.  These values must be always be
//   provided by the user even if the packet buffers and related structures
//   will be user managed.
//
// * NUMBER OF "FLOW" RULES
//   (`max_flows`)
//
//   So called "flow" rules are used to select which incoming packets will be
//   processed by this library.  The number of flow rules to support is
//   specified here by the user (though it obviously must be within the bounds
//   supported by the NIC).  A value of 0 will be treated (and changed to) a
//   value of 1.
//
// * USER MANAGED FLAG
//   (`user_managed_flag`)
//
//   This is an indication of whether packet buffers and related structures are
//   user managed (as opposed to library managed).  The `user_managed_flag`
//   should be set to 0 to allow the library to manage the packet buffers.  It
//   is envisioned that this will suffice for the vast majority of users.
//
//   When `user_managed_flag` is false (0), the library will allocate the
//   packet send and receive buffers, create work requests with a single
//   element scatter/gather list, and register the memory regions allocated for
//   the packet buffers.
//
//   When `user_managed_flag` is true (non-zero), the user must allocate the
//   packet send and receive buffers and allocate and initialize the work
//   requests with scatter/gather lists as desired.  See USER MANGED BUFFERS
//   below for more details.
//
// * USER MANAGED BUFFERS
//   (`send_pkt_buf`, `recv_pkt_buf`, `send_sge_buf`, `recv_sge_buf`,
//   `send_mr_buf`, `recv_mr_buf`)
//
//   When `user_managed_flag` is true (non-zero), the user must perform the
//   following additional steps prior to calling `hashpipe_ibv_init()`:
//
//   1. Allocate the packet send and receive buffers (memory regions) and store
//      the corresponding pointers in the `send_mr_buf` and `recv_mr_buf`
//      fields `hibv_ctx`.
//
//   2. Allocate the send and receive `struct hashpipe_ibv_{send,recv}_pkt`
//      (packet/work request) arrays and store the corresponding pointers in
//      the `send_pkt_buf` and `recv_pkt_buf` fields of `hibv_ctx`.  Note that
//      the send work requests in `send_pkt_buf` will be (re-)linked into a
//      single linked list when calling this function.  The `send_pkt_head`
//      field will be initialized to the same value as `send_pkt_buf`.  The
//      receive work requests in `recv_pkt_buf` will be explicitly un-linked
//      when calling this function.  The `wr_id` fields of each send and
//      receive work request will be set to the structure's index within the
//      array of `hashpipe_ibv_*_pkt` structures.  This is necessary because
//      the `wr_id` value from a work completion is used to access the
//      `hashpipe_ibv_*pkt` structure containing the corresponding work
//      request.
//
//   3. Allocate the send and receive `struct ibv_sge` (scatter/gather
//      elements) arrays and the corresponding pointers in the `send_sge_buf`
//      and `recv_sge_buf` fields of `hibv_ctx`.
//
//   4. Initialize the work requests and scatter/gather elements as desired.
//
//   If the user would like the library to free the user allocated memory for
//   all of these buffers or structures, then the `user_managed_flag` can be
//   set to false (0) before calling `hashpipe_ibv_shutdown()`.  Otherwise the
//   user will remain responsible for freeing these memory allocations after
//   `hashpipe_ibv_shutdown()` returns.
int hashpipe_ibv_init(struct hashpipe_ibv_context * hibv_ctx);

// This function releases all library managed resources and frees all library
// managed memory associated with `hibv_ctx`.  If `user_managed_flag` is false
// (0), this will include freeing buffers associated with send and receive work
// requests, scatter/gather lists, and packet buffers.  The user can set
// `user_managed_flag` to 0 before calling this function to pass ownership of
// user managed buffers to the library, if desired.  This function returns the
// number of errors returned by the cleanup functions it calls.  A return value
// of 0 indicates success.
int hashpipe_ibv_shutdown(struct hashpipe_ibv_context * hibv_ctx);

// `hashpipe_ibv_flow() is used to setup flow rules on the NIC to
// select which incoming packets will be passed to us by the NIC.  Flows are
// specified by providing values that various fields in the packet headers must
// match.  Fields that can be matched exist at the Ethernet level, the IPv4
// level, and the TCP/UDP level.  The fields available for matching are:
//
//   - dst_mac    Ethernet destination MAC address (uint8_t *)
//   - src_mac    Ethernet source MAC address      (uint8_t *)
//   - ether_type Ethernet type field              (uint16_t)
//   - vlan_tag   Ethernet VLAN tag                (uint16_t)
//   - src_ip     IP source address                (uint32_t)
//   - dst_ip     IP destination address           (uint32_t)
//   - src_port   TCP/UDP source port              (uint16_t)
//   - dst_port   TCP/UDP destination port         (uint16_t)
//
// The `flow_idx` parameter specifies which flow rule to assign this flow to.
// The user specifies `max_flows` when initializing the `hashpipe_ibv_context`
// structure and `flow_idx` must be less than that number.  If a flow already
// exists at the index `flow_idx`, that flow is destroyed before the new flow
// is created and stored at the same index.
//
// The `flow_type` field specifies the type pf the flow.  Supported values are:
//
// IBV_FLOW_SPEC_ETH   This matches packets only at the Ethernet layer.  Match
//                     fields for IP/TCP/UDP are ignored.
//
// IBV_FLOW_SPEC_IPV4  This matches at the Ethernet and IPv4 layers.  Match
//                     fields for TCP/UDP are ignored.  Flow rules at this
//                     level include an implicit match on the Ethertype field
//                     (08 00) to select only IP packets.
//
// IBV_FLOW_SPEC_TCP   These match at the Ethernet, IPv4, and TCP/UDP layers.
// IBV_FLOW_SPEC_UDP   Flow rules of these types include an implicit match on
//                     the Ethertype field to select only IP packets and the IP
//                     protocol field to select only TCP or UDP packets.
//
// Not all fields need to be matched.  For fields for which a match is not
// desired, simply pass NULL or 0 for the corresponding parameter to
// `hashpipe_ibv_flow` and that field will be excluded from the matching
// process.  This means that it is not possible to match against zero valued
// fields except for the bizarre case of a zero valued MAC address.  In
// practice this is unlikely to be a problem.
//
// Passing NULL/0 for all the match fields will result in the destruction of
// any flow at the `flow_idx` location, but no new flow will be stored there.
//
// The `src_mac` and `dst_mac` pointers, if non-NULL, must point to a 6 byte
// buffer containing the desired MAC address in network byte order.  Note that
// the `mac` field of `hibv_ctx` will contain the MAC address of the NIC port
// being used.  Some NICs may require a `dst_mac` match in order to enable any
// packet reception at all.  This can be the unicast MAC address of the NIC
// port or a multicast Ethernet MAC address for receiving multicast packets.
// If a multicast `dst_ip` is given, `dst_mac` will be ignored and the
// multicast MAC address corresponding to `dst_ip` will be used.  If desired,
// multicast MAC addresses can be generated from multicast IP addresses using
// the ETHER_MAP_IP_MULTICAST macro defined in <netinet/if_ether.h>.
//
// The non-MAC  parameters are passed as values and must be in host byte order.
int hashpipe_ibv_flow(
    struct hashpipe_ibv_context * hibv_ctx,
    uint32_t  flow_idx,   enum ibv_flow_spec_type flow_type,
    uint8_t * dst_mac,    uint8_t * src_mac,
    uint16_t  ether_type, uint16_t  vlan_tag,
    uint32_t  src_ip,     uint32_t  dst_ip,
    uint16_t  src_port,   uint16_t  dst_port);

// This function returns a pointer to a linked list of work request structures
// with received packets if any, otherwise it will return NULL.  If no received
// packets are already queued, it will wait no longer than `timeout_ms`
// milliseconds for packets to arrive.  Because NULL is returned for both a
// normal "no packets" condition and an error condition, callers who wish to
// distinguish between no packet and an error condition can set `errno` to 0
// prioer to calling this function and then check `errno` if NULL is returned.
// For a normal "no packets" NULL return value, errno will be unchanged; if an
// error occured errno will ne non-zero.  A timeout of zero returns
// immediately.  A negative timeout waits "forever".
//
// Because work completion notifications are asynchronous with work completion
// handling, it is possible that work completions for a notification will be
// handled while handling the previous notification.  When handing the latter
// notification, it is possible that there will be no packets left unhandled.
// This means that a timeout cannot be detected/inferred from a normal "no
// packet" NULL return.
//
// After processing all of the packets, the caller must release the work
// requests by passing the list to `hashpipe_ibv_release_pkts()`.
struct hashpipe_ibv_recv_pkt * hashpipe_ibv_recv_pkts(
    struct hashpipe_ibv_context * hibv_ctx, int timeout_ms);

// This function releases a list of received packets after the consumer has
// processed the packet contents.  It returns the return value of
// `ibv_post_recv()` or -1 if hibv_ctx or recv_pkt is NULL.
int hashpipe_ibv_release_pkts(struct hashpipe_ibv_context * hibv_ctx,
    struct hashpipe_ibv_recv_pkt * recv_pkt);

// This function requests a set of `num_pkts` free "send packets".  These are
// returned as a pointer to a linked list of `hashpipe_ibv_send_pkt`
// structures.  If no packets are available (or if an error occurs) NULL is
// returned.  It is possible for the returned list to contain fewer than
// `num_pkts`.
//
// The work requests associated with these structures may have been used to
// send previous packets, so the `length` field of the associated
// scatter/gather lists will contain values for the previous packets' sizing
// and may not reflect the actual size of the associated memory region.
struct hashpipe_ibv_send_pkt * hashpipe_ibv_get_pkts(
    struct hashpipe_ibv_context * hibv_ctx, uint32_t num_pkts);

// This function sends the list of packets pointed to by `send_pkt` using the
// QP indicated by qp_idx.  qp_idx must be greater than or equal to 0 and less
// than hibv_ctx->nqp.  This function posts the packets for transmission and
// then returns; it does not wait for them to be transmitted.  It returns the
// value of `ibv_post_send()` or -1 if `hibv_ctx` or `send_pkt` is NULL.
int hashpipe_ibv_send_pkts(struct hashpipe_ibv_context * hibv_ctx,
    struct hashpipe_ibv_send_pkt * send_pkt, uint32_t qp_idx);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // HASHPIPE_IBVERBS_H
