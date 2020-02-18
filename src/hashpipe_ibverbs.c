#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <sys/ioctl.h>

#include "hashpipe_ibverbs.h"

// Need to include this after including hashpipe_ibverbs.h since
// HPIBV_USE_MMAP_PKTBUFS may be defined there.
#if HPIBV_USE_MMAP_PKTBUFS
#include <sys/mman.h>
#endif

#if HPIBV_USE_EXP_CQ
#define IBV_CREATE_CQ(ctx,cqe,cq_ctx,cc,cv,attr) \
  ibv_exp_create_cq(ctx,cqe,cq_ctx,cc,cv,attr)
#define IBV_POLL_CQ(cq,nce,wc) \
  ibv_exp_poll_cq(cq,nce,wc,sizeof(struct ibv_exp_wc))
#else
#define IBV_CREATE_CQ(ctx,cqe,cq_ctx,cc,cv,attr) \
  ibv_create_cq(ctx,cqe,cq_ctx,cc,cv)
#define IBV_POLL_CQ(cq,nce,wc) \
  ibv_poll_cq(cq,nce,wc)
#endif

// Adds or drops membership in a multicast group.  The `option` parameter
// must be IP_ADD_MEMBERSHIP or IP_DROP_MEMBERSHIP.  The `dst_ip_be` parameter
// must be in network byte order (i.e. big endian).  If it is not a multicast
// address, this function does nothing so it is safe to call this function
// regardless of whether `dst_ip_be` is a multicast address.  Returns 0 on
// success or `errno` on error.
static int hashpipe_ibv_mcast_membership(
    struct hashpipe_ibv_context * hibv_ctx, int option, uint32_t dst_ip_be)
{
  struct ip_mreqn mreqn;

  // Do nothing and return success if dst_ip_be is not multicast
  if((dst_ip_be & 0xf0) != 0xe0) {
    return 0;
  }

  // Return EINVAL if socket or option is invalid
  if(!hibv_ctx || hibv_ctx->mcast_subscriber == -1 ||
      (option != IP_ADD_MEMBERSHIP && option != IP_DROP_MEMBERSHIP)) {
    errno = EINVAL;
    return errno;
  }

  mreqn.imr_multiaddr.s_addr = dst_ip_be;
  mreqn.imr_address.s_addr = INADDR_ANY;
  if((mreqn.imr_ifindex = if_nametoindex(hibv_ctx->interface_name)) == 0) {
    return errno;
  }

  if(setsockopt(hibv_ctx->mcast_subscriber,
        IPPROTO_IP, option, &mreqn, sizeof(mreqn))) {
    return errno;
  }

  return 0;
}

// See comments in header file for details about this function.
int hashpipe_ibv_get_interface_info(
    const char * interface_name, uint8_t *mac, uint64_t *interface_id)
{
  int errsv = 0;
  uint8_t buf[8] = {0};
  struct ifreq ifr = {0};
  int fd;

  if(!interface_name) {
    errno = EINVAL;
    return 1;
  }

  strncpy(ifr.ifr_name , interface_name , IFNAMSIZ-1);
  ifr.ifr_name[IFNAMSIZ-1] = '\0';  // Ensure NUL termination

  if((fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
    return 2;
  }

  if((ioctl(fd, SIOCGIFHWADDR, &ifr))) {
    errsv = errno; // Save errno from ioctl()
    close(fd);     // Ignore errors from close()
    errno = errsv; // Restore errno from ioctl()
    return 3;
  }

  close(fd); // Ignore errors

  if(mac) {
    memcpy(mac, (unsigned char *)ifr.ifr_hwaddr.sa_data, 6);
  }

  if(interface_id) {
    memcpy(buf, (unsigned char *)ifr.ifr_hwaddr.sa_data, 3);
    buf[0] ^= 2; // Toggle G/L bit per modified EUI-64 spec
    buf[3] = 0xff;
    buf[4] = 0xfe;
    memcpy(buf+5, (unsigned char *)ifr.ifr_hwaddr.sa_data+3, 3);

    memcpy(interface_id, buf, 8);
    *interface_id = be64toh(*interface_id);
  }

  return 0;
}

// See comments in header file for details about this function.
int hashpipe_ibv_open_device_for_interface_id(
    uint64_t                 interface_id,
    struct ibv_context    ** found_ctx,
    struct ibv_device_attr * found_dev_attr,
    uint8_t                * found_port)
{
  int retval = 1;
  int devidx;
  int portidx;
  int gididx;
  int num_devices;
  struct ibv_device      ** dev_list;
  struct ibv_context      * ibv_ctx = NULL;
  struct ibv_device_attr    ibv_device_attr;
  struct ibv_port_attr      ibv_port_attr;
  union  ibv_gid            ibv_gid;

  if(!(dev_list = ibv_get_device_list(&num_devices))) {
    perror("ibv_get_device_list");
    return 2;
  }

  if(num_devices <= 0) {
    fprintf(stderr, "no IBV devices found\n");
    retval++;
    goto clean_devlist;
  }

  // Convert interface_id to big endian
  interface_id = htobe64(interface_id);

  // For each device
  for(devidx=0; devidx<num_devices; devidx++) {
    if(!(ibv_ctx = ibv_open_device(dev_list[devidx]))) {
      perror("ibv_open_device");
      fprintf(stderr, "error opening device %s\n", dev_list[devidx]->name);
      retval++;
      continue;
    }

    // Query device
    if(ibv_query_device(ibv_ctx, &ibv_device_attr)) {
      perror("ibv_query_device");
      fprintf(stderr, "error querying device %s\n", dev_list[devidx]->name);
      if(ibv_close_device(ibv_ctx) == -1) {
        perror("ibv_close_device");
      }
      retval++;
      continue;
    }

    // For each port of device
    for(portidx=1; portidx<=ibv_device_attr.phys_port_cnt; portidx++) {
      // Query port
      if(ibv_query_port(ibv_ctx, portidx, &ibv_port_attr)) {
        perror("ibv_query_port");
        fprintf(stderr, "error querying port %d of device %s\n",
            portidx, dev_list[devidx]->name);
        retval++;
        continue;
      }

      // For each GID of port
      for(gididx=0; gididx<ibv_port_attr.gid_tbl_len; gididx++) {
        // Query gid entry
        if(ibv_query_gid(ibv_ctx, portidx, gididx, &ibv_gid)) {
          perror("ibv_query_gid");
          fprintf(stderr,
              "error querying gid entry %d for port %d of device %s\n",
              gididx, portidx, dev_list[devidx]->name);
          retval++;
          continue;
        }

        if(ibv_gid.global.subnet_prefix == 0x80feUL
        && ibv_gid.global.interface_id  == interface_id) {
          // Found desired port!
          break;
        }
      } // for each GID

      if(gididx < ibv_port_attr.gid_tbl_len) {
        // Found desired port!
        break;
      }
    } // for each port

    // Break out of this loop if found, otherwise close device
    if(portidx <= ibv_device_attr.phys_port_cnt) {
      // Found desired port!
      break;
    } else if(ibv_close_device(ibv_ctx) == -1) {
      perror("ibv_close_device");
      retval++;
    }
  } // for each device

  // If we found the desired device/port
  if(devidx  < num_devices) {
    // Save context (or close it)
    if(found_ctx) {
      *found_ctx = ibv_ctx;
    } else if(ibv_close_device(ibv_ctx) == -1) {
      perror("ibv_close_device");
      retval++;
    }
    // Save device attributes
    if(found_dev_attr) {
      memcpy(found_dev_attr, &ibv_device_attr, sizeof(ibv_device_attr));
    }
    // Save port
    if(found_port) {
      *found_port = portidx;
    }
    // Success
    retval = 0;
  }

clean_devlist:
  ibv_free_device_list(dev_list);

  // Set errno if we are not returning success
  if(retval) {
    errno = ENODEV;
  }

  return retval;
}

// See comments in header file for details about this function.
// TODO Use checksum offload, if available
// TODO Create checksum calculating functions as backup
int hashpipe_ibv_init(struct hashpipe_ibv_context * hibv_ctx)
{
  int i, ii, j;
  int flags;
  int errsv;
  int send_flags = 0;
#ifdef HAVE_IBV_IP_CSUM
  struct ibv_device_attr device_attr;
#endif
  int max_send_sge = 1;
  int max_recv_sge = 1;
  struct ibv_recv_wr * recv_wr_bad;
#if HPIBV_USE_EXP_CQ
  struct ibv_exp_cq_init_attr cq_attr = {
    .comp_mask = IBV_EXP_CQ_INIT_ATTR_FLAGS,
    .flags = IBV_EXP_CQ_TIMESTAMP
  };
#endif

  // Sanity check hibv_ctx
  if(!hibv_ctx) {
    errno = EINVAL;
    perror("hashpipe_ibv_context(NULL)");
    return -1;
  }

  // Must have at least one QP
  if(hibv_ctx->nqp == 0) {
    hibv_ctx->nqp = 1;
  }

  // Sanity check user provided buffers
  if(hibv_ctx->user_managed_flag) {
    if(!hibv_ctx->send_pkt_buf) {
      errno = EINVAL;
      perror("hashpipe_ibv_init[send_pkt_buf]");
      return 1;
    }
    if(!hibv_ctx->recv_pkt_buf) {
      errno = EINVAL;
      perror("hashpipe_ibv_init[recv_pkt_buf]");
      return 1;
    }
    if(!hibv_ctx->send_sge_buf) {
      errno = EINVAL;
      perror("hashpipe_ibv_init[send_sge_buf]");
      return 1;
    }
    if(!hibv_ctx->recv_sge_buf) {
      errno = EINVAL;
      perror("hashpipe_ibv_init[recv_sge_buf]");
      return 1;
    }
    if(!hibv_ctx->send_mr_buf) {
      errno = EINVAL;
      perror("hashpipe_ibv_init[send_mr_buf]");
      return 1;
    }
    if(!hibv_ctx->recv_mr_buf) {
      errno = EINVAL;
      perror("hashpipe_ibv_init[recv_mr_buf]");
      return 1;
    }
  }

  // Get interface info
  if(hashpipe_ibv_get_interface_info(
        hibv_ctx->interface_name,
        hibv_ctx->mac,
        &hibv_ctx->interface_id)) {
    perror("hashpipe_ibv_get_interface_info");
    return 1;
  }

  // Init library managed fields to NULL or other invalid value.  This lets us
  // pass a partially initialized hibv_ctx to the hashpipe_ibv_shutdown()
  // function.
  hibv_ctx->ctx = NULL;
  hibv_ctx->pd = NULL;
#if HPIBV_USE_SEND_CC
  hibv_ctx->send_cc = NULL;
#endif
  hibv_ctx->recv_cc = NULL;
  hibv_ctx->send_cq = NULL;
  hibv_ctx->recv_cq = NULL;
  hibv_ctx->qp = NULL;
  hibv_ctx->send_pkt_head = NULL;
  hibv_ctx->send_mr = NULL;
  hibv_ctx->recv_mr = NULL;
  hibv_ctx->mcast_subscriber = -1;
  if(!hibv_ctx->user_managed_flag) {
    hibv_ctx->send_pkt_buf = NULL;
    hibv_ctx->recv_pkt_buf = NULL;
    hibv_ctx->send_sge_buf = NULL;
    hibv_ctx->recv_sge_buf = NULL;
    hibv_ctx->send_mr_buf = NULL;
    hibv_ctx->recv_mr_buf = NULL;
  }

  // Open IBV device for interface_id.
  if((errsv = hashpipe_ibv_open_device_for_interface_id(
        hibv_ctx->interface_id,
        &hibv_ctx->ctx,
        &hibv_ctx->dev_attr,
        &hibv_ctx->port_num))) {
    if(errsv == 1) {
      perror("hashpipe_ibv_init");
    }
    return 1;
  }

  // Use max wr per QP if send_pkt_num or recv_pkt_num is 0
  if(hibv_ctx->send_pkt_num == 0) {
    if(hibv_ctx->user_managed_flag) {
      errno = EINVAL;
      perror("must specify non-zero send_pkt_num with user managed buffers");
      goto cleanup_and_return_error;
    }
    hibv_ctx->send_pkt_num = hibv_ctx->dev_attr.max_qp_wr;
  }
  if(hibv_ctx->recv_pkt_num == 0) {
    if(hibv_ctx->user_managed_flag) {
      errno = EINVAL;
      perror("must specify non-zero recv_pkt_num with user managed buffers");
      goto cleanup_and_return_error;
    }
    hibv_ctx->recv_pkt_num = hibv_ctx->dev_attr.max_qp_wr;
  }

  // Create protection domain (aka PD)
  if(!(hibv_ctx->pd = ibv_alloc_pd(hibv_ctx->ctx))) {
    perror("ibv_alloc_pd");
    goto cleanup_and_return_error;
  }

#if HPIBV_USE_SEND_CC
  // Create send and receive completion channels
  if(!(hibv_ctx->send_cc = ibv_create_comp_channel(hibv_ctx->ctx))) {
    perror("ibv_create_comp_chan[send]");
    goto cleanup_and_return_error;
  }
#endif

  if(!(hibv_ctx->recv_cc = ibv_create_comp_channel(hibv_ctx->ctx))) {
    perror("ibv_create_comp_chan[recv]");
    goto cleanup_and_return_error;
  }

  // Change the blocking mode of the completion channel(s) to non-blocking
#if HPIBV_USE_SEND_CC
  flags = fcntl(hibv_ctx->send_cc->fd, F_GETFL);
  if(fcntl(hibv_ctx->send_cc->fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("fcntl[send_cc]");
    goto cleanup_and_return_error;
  }
#endif

  flags = fcntl(hibv_ctx->recv_cc->fd, F_GETFL);
  if(fcntl(hibv_ctx->recv_cc->fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("fcntl[recv_cc]");
    goto cleanup_and_return_error;
  }

  // Create send completion queues
  if(!(hibv_ctx->send_cq =
        (struct ibv_cq **)calloc(hibv_ctx->nqp, sizeof(struct ibv_cq *)))) {
    perror("malloc send_cq array");
    goto cleanup_and_return_error;
  }
  for(i=0; i<hibv_ctx->nqp; i++) {
    if(!(hibv_ctx->send_cq[i] = IBV_CREATE_CQ(hibv_ctx->ctx,
            hibv_ctx->send_pkt_num, (void *)(uintptr_t)i,
            hibv_ctx->send_cc, 0, &cq_attr))) {
      perror("ibv_create_cq[send]");
      goto cleanup_and_return_error;
    }
  }

#if HPIBV_USE_SEND_CC
  // Request notifications before any send completion can be created.
  // Do NOT restrict to solicited-only completions.
  if(ibv_req_notify_cq(hibv_ctx->send_cq, 0)) {
    perror("ibv_req_notify_cq[send]");
    goto cleanup_and_return_error;
  }
#endif

  // Create recv completion queues
  if(!(hibv_ctx->recv_cq =
        (struct ibv_cq **)calloc(hibv_ctx->nqp, sizeof(struct ibv_cq *)))) {
    perror("malloc recv_cq array");
    goto cleanup_and_return_error;
  }
  for(i=0; i<hibv_ctx->nqp; i++) {
    if(!(hibv_ctx->recv_cq[i] = IBV_CREATE_CQ(hibv_ctx->ctx,
            hibv_ctx->recv_pkt_num, (void *)(uintptr_t)i,
            hibv_ctx->recv_cc, 0, &cq_attr))) {
      perror("ibv_create_cq[recv]");
      goto cleanup_and_return_error;
    }

    // Request notifications before any receive completion can be created.
    // Do NOT restrict to solicited-only completions for receive.
    if(ibv_req_notify_cq(hibv_ctx->recv_cq[i], 0)) {
      perror("ibv_req_notify_cq[recv]");
      goto cleanup_and_return_error;
    }
  }

  // For user managed work requests, walk lists to get max sge
  if(hibv_ctx->user_managed_flag) {
    for(i=0; i<hibv_ctx->send_pkt_num*hibv_ctx->nqp; i++) {
      if(max_send_sge < hibv_ctx->send_pkt_buf[i].wr.num_sge) {
        max_send_sge = hibv_ctx->send_pkt_buf[i].wr.num_sge;
      }
    }

    for(i=0; i<hibv_ctx->recv_pkt_num*hibv_ctx->nqp; i++) {
      if(max_recv_sge < hibv_ctx->recv_pkt_buf[i].wr.num_sge) {
        max_recv_sge = hibv_ctx->recv_pkt_buf[i].wr.num_sge;
      }
    }
  }

  // Create queue pairs (aka QP, starts in RESET state)
  struct ibv_qp_init_attr ibv_qp_init_attr = {
    .qp_context = NULL,
    .srq = NULL,
    .cap = {
      .max_send_wr = hibv_ctx->send_pkt_num,
      .max_recv_wr = hibv_ctx->recv_pkt_num,
      .max_send_sge = max_send_sge,
      .max_recv_sge = max_recv_sge,
      .max_inline_data = 0,
    },
    .qp_type = IBV_QPT_RAW_PACKET,
    .sq_sig_all = 0
  };

  if(!(hibv_ctx->qp =
        (struct ibv_qp **)calloc(hibv_ctx->nqp, sizeof(struct ibv_qp *)))) {
    perror("malloc qp array");
    goto cleanup_and_return_error;
  }

  for(i=0; i<hibv_ctx->nqp; i++) {
    ibv_qp_init_attr.send_cq = hibv_ctx->send_cq[i];
    ibv_qp_init_attr.recv_cq = hibv_ctx->recv_cq[i];
    if(!(hibv_ctx->qp[i] = ibv_create_qp(hibv_ctx->pd, &ibv_qp_init_attr))) {
      perror("ibv_create_qp");
      goto cleanup_and_return_error;
    }

    // Transition QP to INIT state
    struct ibv_qp_attr ibv_qp_attr = {
      .qp_state = IBV_QPS_INIT,
      .port_num = hibv_ctx->port_num // NIC port (1 or 2 for dual port NIC)
    };

    if(ibv_modify_qp(hibv_ctx->qp[i], &ibv_qp_attr, IBV_QP_STATE|IBV_QP_PORT)) {
      perror("ibv_modify_qp[init]");
      goto cleanup_and_return_error;
    }
  }

  // Allocate buffers (unless user managed)
  if(!hibv_ctx->user_managed_flag) {
    if(!(hibv_ctx->send_pkt_buf = (struct hashpipe_ibv_send_pkt *)
          calloc(hibv_ctx->send_pkt_num * hibv_ctx->nqp,
            sizeof(struct hashpipe_ibv_send_pkt)))) {
      perror("calloc(hashpipe_ibv_send_pkt)");
      goto cleanup_and_return_error;
    }
#if 0
    if(mlock(hibv_ctx->send_pkt_buf, sizeof(struct hashpipe_ibv_send_pkt) *
          hibv_ctx->send_pkt_num * hibv_ctx->nqp)) {
      perror("mlock(send_pkt_buf)");
      goto cleanup_and_return_error;
    }
#endif
    if(!(hibv_ctx->recv_pkt_buf = (struct hashpipe_ibv_recv_pkt *)
          calloc(hibv_ctx->recv_pkt_num * hibv_ctx->nqp,
            sizeof(struct hashpipe_ibv_recv_pkt)))) {
      perror("calloc(hashpipe_ibv_recv_pkt)");
      goto cleanup_and_return_error;
    }
#if 0
    if(mlock(hibv_ctx->recv_pkt_buf, sizeof(struct hashpipe_ibv_recv_pkt) *
          hibv_ctx->recv_pkt_num * hibv_ctx->nqp)) {
      perror("mlock(recv_pkt_buf)");
      goto cleanup_and_return_error;
    }
#endif
    if(!(hibv_ctx->send_sge_buf = (struct ibv_sge *)
          calloc(hibv_ctx->send_pkt_num * hibv_ctx->nqp,
            sizeof(struct ibv_sge)))) {
      perror("calloc(ibv_send_sge)");
      goto cleanup_and_return_error;
    }
#if 0
    if(mlock(hibv_ctx->send_sge_buf, sizeof(struct ibv_sge) *
          hibv_ctx->send_pkt_num * hibv_ctx->nqp)) {
      perror("mlock(send_sge_buf)");
      goto cleanup_and_return_error;
    }
#endif
    if(!(hibv_ctx->recv_sge_buf = (struct ibv_sge *)
          calloc(hibv_ctx->recv_pkt_num * hibv_ctx->nqp,
            sizeof(struct ibv_sge)))) {
      perror("calloc(ibv_recv_sge)");
      goto cleanup_and_return_error;
    }
#if 0
    if(mlock(hibv_ctx->recv_sge_buf, sizeof(struct ibv_sge) *
          hibv_ctx->recv_pkt_num * hibv_ctx->nqp)) {
      perror("mlock(recv_sge_buf)");
      goto cleanup_and_return_error;
    }
#endif

    hibv_ctx->send_mr_size =
      (size_t)hibv_ctx->send_pkt_num * hibv_ctx->pkt_size_max * hibv_ctx->nqp;

    hibv_ctx->recv_mr_size =
      (size_t)hibv_ctx->recv_pkt_num * hibv_ctx->pkt_size_max * hibv_ctx->nqp;

#if HPIBV_USE_MMAP_PKTBUFS
    if((hibv_ctx->send_mr_buf = (uint8_t *)mmap(NULL, hibv_ctx->send_mr_size,
            PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_LOCKED,
            -1, 0)) == MAP_FAILED) {
      perror("mmap(send_mr_buf)");
      goto cleanup_and_return_error;
    }
    if(mlock(hibv_ctx->send_mr_buf, hibv_ctx->send_mr_size)) {
      perror("mlock(send_mr_buf)");
      goto cleanup_and_return_error;
    }
    if((hibv_ctx->recv_mr_buf = (uint8_t *)mmap(NULL, hibv_ctx->recv_mr_size,
            PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_LOCKED,
            -1, 0)) == MAP_FAILED) {
      perror("mmap(recv_mr_buf)");
      goto cleanup_and_return_error;
    }
    if(mlock(hibv_ctx->recv_mr_buf, hibv_ctx->recv_mr_size)) {
      perror("mlock(recv_mr_buf)");
      goto cleanup_and_return_error;
    }
#else
    if(!(hibv_ctx->send_mr_buf = (uint8_t *)
          calloc(1, hibv_ctx->send_mr_size))) {
      perror("calloc(ibv_send_mr)");
      goto cleanup_and_return_error;
    }
    if(!(hibv_ctx->recv_mr_buf = (uint8_t *)
          calloc(1, hibv_ctx->recv_mr_size))) {
      perror("calloc(ibv_recv_mr)");
      goto cleanup_and_return_error;
    }
#endif // HPIBV_USE_MMAP_PKTBUFS
  }

  // Register send and receive memory regions
  if(!(hibv_ctx->send_mr = ibv_reg_mr(hibv_ctx->pd, hibv_ctx->send_mr_buf,
          hibv_ctx->send_mr_size, 0))) {
    perror("ibv_reg_mr[send]");
    goto cleanup_and_return_error;
  }

#if 1
  if(!(hibv_ctx->recv_mr = ibv_reg_mr(hibv_ctx->pd, hibv_ctx->recv_mr_buf,
          hibv_ctx->recv_mr_size,
          IBV_ACCESS_LOCAL_WRITE))) {
    perror("ibv_reg_mr[recv]");
    fprintf(stderr, "addr %p length %lu\n",
        hibv_ctx->recv_mr_buf, hibv_ctx->recv_mr_size);
    goto cleanup_and_return_error;
  }
#else
  struct ibv_exp_reg_mr_in mr_info = {
    .pd = hibv_ctx->pd,
    .addr = hibv_ctx->recv_mr_buf,
    .length = hibv_ctx->recv_mr_size,
    .exp_access = IBV_EXP_ACCESS_LOCAL_WRITE,
    .comp_mask = 0
  };
  if(!(hibv_ctx->recv_mr = ibv_exp_reg_mr(&mr_info))) {
    perror("ibv_exp_reg_mr[recv]");
    fprintf(stderr, "addr %p length %lu\n",
        hibv_ctx->recv_mr_buf, hibv_ctx->recv_mr_size);
    goto cleanup_and_return_error;
  }
#endif

  // Initialize work requests and scatter/gather fields
  //
  // For user managed buffers, the `num_sge` field of the work requests (WRs)
  // and the `addr` and `length` fields of the scatter-gather elements (SGEs)
  // must be set by the user.  Note that the library will enforce a wr_id
  // scheme that sets the wr_id to the index of the WR element in the WR array.
  //
  // Send related elements
  for(i=0; i<hibv_ctx->send_pkt_num*hibv_ctx->nqp; i++) {
    hibv_ctx->send_pkt_buf[i].wr.wr_id = i;

    if(!hibv_ctx->user_managed_flag) {
      hibv_ctx->send_pkt_buf[i].wr.num_sge = 1;

      hibv_ctx->send_sge_buf[i].addr = (uint64_t)
        hibv_ctx->send_mr_buf + i * hibv_ctx->pkt_size_max;
      hibv_ctx->send_sge_buf[i].length = hibv_ctx->pkt_size_max;
    } // !user_managed

    if(i == 0) {
      hibv_ctx->send_pkt_buf[i].wr.sg_list = &hibv_ctx->send_sge_buf[0];
    } else {
      hibv_ctx->send_pkt_buf[i].wr.sg_list =
        hibv_ctx->send_pkt_buf[i-1].wr.sg_list +
        hibv_ctx->send_pkt_buf[i-1].wr.num_sge;
    }
    for(j=0; j<hibv_ctx->send_pkt_buf[i].wr.num_sge; j++) {
      hibv_ctx->send_pkt_buf[i].wr.sg_list[j].lkey = hibv_ctx->send_mr->lkey;
    }
  }

  // Receive related elements
  for(i=0; i<hibv_ctx->recv_pkt_num*hibv_ctx->nqp; i++) {
    hibv_ctx->recv_pkt_buf[i].wr.wr_id = i;

    if(!hibv_ctx->user_managed_flag) {
      hibv_ctx->recv_pkt_buf[i].wr.num_sge = 1;

      hibv_ctx->recv_sge_buf[i].addr = (uint64_t)
        hibv_ctx->recv_mr_buf + i * hibv_ctx->pkt_size_max;
      hibv_ctx->recv_sge_buf[i].length = hibv_ctx->pkt_size_max;
    } // !user_managed

    if(i == 0) {
      hibv_ctx->recv_pkt_buf[i].wr.sg_list = &hibv_ctx->recv_sge_buf[0];
    } else {
      hibv_ctx->recv_pkt_buf[i].wr.sg_list =
        hibv_ctx->recv_pkt_buf[i-1].wr.sg_list +
        hibv_ctx->recv_pkt_buf[i-1].wr.num_sge;
    }
    for(j=0; j<hibv_ctx->recv_pkt_buf[i].wr.num_sge; j++) {
      hibv_ctx->recv_pkt_buf[i].wr.sg_list[j].lkey = hibv_ctx->recv_mr->lkey;
    }
  }

// Mellanox installed infiniband/verbs.h file does not define
// IBV_DEVICE_IP_CSUM or IBV_SEND_IP_CSUM.
#ifdef HAVE_IBV_IP_CSUM
  // Query device to learn checksum offload capabilities
  if(!ibv_query_device(hibv_ctx->ctx, &device_attr)
  && (device_attr.device_cap_flags & IBV_DEVICE_IP_CSUM)) {
    send_flags |= IBV_SEND_IP_CSUM;
  }
#endif // HAVE_IBV_IP_CSUM

  // Link/initialize send work requests
  for(i=0; i<hibv_ctx->send_pkt_num*hibv_ctx->nqp-1; i++) {
    hibv_ctx->send_pkt_buf[i].wr.next = &hibv_ctx->send_pkt_buf[i+1].wr;
    hibv_ctx->send_pkt_buf[i].wr.opcode = IBV_WR_SEND;
    hibv_ctx->send_pkt_buf[i].wr.send_flags = send_flags;
  }
  // Initialize last element
  hibv_ctx->send_pkt_buf[i].wr.next = NULL;
  hibv_ctx->send_pkt_buf[i].wr.opcode = IBV_WR_SEND;
  hibv_ctx->send_pkt_buf[i].wr.send_flags = send_flags;
  // Set head pointer
  hibv_ctx->send_pkt_head = hibv_ctx->send_pkt_buf;

  // Link recv work requests
  for(i=0; i<hibv_ctx->nqp; i++) {
    ii = i*hibv_ctx->recv_pkt_num;
    for(j=0; j<hibv_ctx->recv_pkt_num-1; j++) {
      hibv_ctx->recv_pkt_buf[ii+j].wr.next = &hibv_ctx->recv_pkt_buf[ii+j+1].wr;
    }
    hibv_ctx->recv_pkt_buf[ii+j].wr.next = NULL;

    // Post receive work requests
    if(ibv_post_recv(hibv_ctx->qp[i],
          &hibv_ctx->recv_pkt_buf[ii].wr, &recv_wr_bad)) {
      perror("ibv_post_recv");
      goto cleanup_and_return_error;
    }
  }

  // Allocate space for `max_flows` flow pointers and dst_ip values
  if(hibv_ctx->max_flows == 0) {
    hibv_ctx->max_flows = 1;
  }
  if(!(hibv_ctx->ibv_flows = (struct ibv_flow **)
        calloc(hibv_ctx->max_flows*hibv_ctx->nqp, sizeof(struct ibv_flow *)))) {
    perror("calloc(ibv_flows)");
    goto cleanup_and_return_error;
  }
  if(!(hibv_ctx->flow_dst_ips = (uint32_t *)
        calloc(hibv_ctx->max_flows, sizeof(uint32_t)))) {
    perror("calloc(flow_dst_ips)");
    goto cleanup_and_return_error;
  }

  // Open socket for managing multicast group membership
  if((hibv_ctx->mcast_subscriber = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
    perror("socket [mcast_subscriber]");
    goto cleanup_and_return_error;
  }

  return 0;

cleanup_and_return_error:
  errsv = errno;
  // Cleanup any created objects
  if(hashpipe_ibv_shutdown(hibv_ctx)) {
    perror("hashpipe_ibv_shutdown");
  }
  // Restore errno to original value when error occured
  // rather than value set in hashpipe_ibv_shutdown().
  errno = errsv;
  // Return error
  return 1;
} // hashpipe_ibv_init

// See comments in header file for details about this function.
int hashpipe_ibv_shutdown(struct hashpipe_ibv_context * hibv_ctx)
{
  int i;
  int rc = 0;

  // Sanity check hibv_ctx
  if(!hibv_ctx) {
    errno = EINVAL;
    perror("hashpipe_ibv_context(NULL)");
    return -1;
  }

  if(hibv_ctx->ibv_flows) {
    // Destroy any flow rules
    for(i=0; i<hibv_ctx->max_flows*hibv_ctx->nqp; i++) {
      if(hibv_ctx->ibv_flows[i]) {
        if(i < hibv_ctx->max_flows) {
          // Drop multicast membership (if any)
          if(hashpipe_ibv_mcast_membership(hibv_ctx, IP_DROP_MEMBERSHIP,
                hibv_ctx->flow_dst_ips[i])) {
            rc++;
          } else {
            hibv_ctx->flow_dst_ips[i] = 0;
          }
        }

        // Destroy flow
        if(ibv_destroy_flow(hibv_ctx->ibv_flows[i])) {
          rc++;
        } else {
          hibv_ctx->ibv_flows[i] = NULL;
        }
      }
    }
  }

  // Deregister/destroy/dealloc/close any/all IBV structures
  if(hibv_ctx->send_mr && ibv_dereg_mr(hibv_ctx->send_mr)) {
    perror("ibv_dereg_mr(send_mr)");
    rc++;
  }
  hibv_ctx->send_mr = NULL;

  if(hibv_ctx->recv_mr && ibv_dereg_mr(hibv_ctx->recv_mr)) {
    perror("ibv_dereg_mr(recv_mr)");
    rc++;
  }
  hibv_ctx->recv_mr = NULL;

  if(hibv_ctx->qp) {
    for(i=0; i<hibv_ctx->nqp; i++) {
      if(hibv_ctx->qp[i] && ibv_destroy_qp(hibv_ctx->qp[i])) {
        perror("ibv_destroy_qp");
        rc++;
      }
    }
    free(hibv_ctx->qp);
    hibv_ctx->qp = NULL;
  }

  if(hibv_ctx->send_cq) {
    for(i=0; i<hibv_ctx->nqp; i++) {
      if(hibv_ctx->send_cq[i] && ibv_destroy_cq(hibv_ctx->send_cq[i])) {
        perror("ibv_destroy_cq[send]");
        rc++;
      }
    }
    free(hibv_ctx->send_cq);
    hibv_ctx->send_cq = NULL;
  }

  if(hibv_ctx->recv_cq) {
    for(i=0; i<hibv_ctx->nqp; i++) {
      if(hibv_ctx->recv_cq[i] && ibv_destroy_cq(hibv_ctx->recv_cq[i])) {
        perror("ibv_destroy_cq[recv]");
        rc++;
      }
    }
    free(hibv_ctx->recv_cq);
    hibv_ctx->recv_cq = NULL;
  }

#if HPIBV_USE_SEND_CC
  if(hibv_ctx->send_cc && ibv_destroy_comp_channel(hibv_ctx->send_cc)) {
    perror("ibv_destroy_comp_channel[send]");
    rc++;
  }
  hibv_ctx->send_cc = NULL;
#endif

  if(hibv_ctx->recv_cc && ibv_destroy_comp_channel(hibv_ctx->recv_cc)) {
    perror("ibv_destroy_comp_channel[recv]");
    rc++;
  }
  hibv_ctx->recv_cc = NULL;

  if(hibv_ctx->pd && ibv_dealloc_pd(hibv_ctx->pd)) {
    perror("ibv_dealloc_pd");
    rc++;
  }
  hibv_ctx->pd = NULL;

  if(hibv_ctx->ctx && ibv_close_device(hibv_ctx->ctx)) {
    perror("ibv_close_device");
    rc++;
  }
  hibv_ctx->ctx = NULL;

  // Forget about unused send work request list
  hibv_ctx->send_pkt_head = NULL;

  // Free library managed buffers
  if(!hibv_ctx->user_managed_flag) {
    // Work request buffers
    if(hibv_ctx->send_pkt_buf) {
      free(hibv_ctx->send_pkt_buf);
      hibv_ctx->send_pkt_buf = NULL;
    }
    if(hibv_ctx->recv_pkt_buf) {
      free(hibv_ctx->recv_pkt_buf);
      hibv_ctx->recv_pkt_buf = NULL;
    }
    // Scatter/gather buffers
    if(hibv_ctx->send_sge_buf) {
      free(hibv_ctx->send_sge_buf);
      hibv_ctx->send_sge_buf = NULL;
    }
    if(hibv_ctx->recv_sge_buf) {
      free(hibv_ctx->recv_sge_buf);
      hibv_ctx->recv_sge_buf = NULL;
    }
    // Memory regions (packet buffers)
    if(hibv_ctx->send_mr_buf) {
#if HPIBV_USE_MMAP_PKTBUFS
      munmap(hibv_ctx->send_mr_buf,
          hibv_ctx->send_pkt_num*hibv_ctx->pkt_size_max);
#else
      free(hibv_ctx->send_mr_buf);
#endif // HPIBV_USE_MMAP_PKTBUFS
      hibv_ctx->send_mr_buf = NULL;
    }
    if(hibv_ctx->recv_mr_buf) {
#if HPIBV_USE_MMAP_PKTBUFS
      munmap(hibv_ctx->recv_mr_buf,
          hibv_ctx->recv_pkt_num*hibv_ctx->pkt_size_max);
#else
      free(hibv_ctx->recv_mr_buf);
#endif // HPIBV_USE_MMAP_PKTBUFS
      hibv_ctx->recv_mr_buf = NULL;
    }
  } // not user managed

  // Free ibv_flows and flow_dst_ips
  if(hibv_ctx->ibv_flows) {
      free(hibv_ctx->ibv_flows);
      hibv_ctx->ibv_flows = NULL;
  }
  if(hibv_ctx->flow_dst_ips) {
      free(hibv_ctx->flow_dst_ips);
      hibv_ctx->flow_dst_ips = NULL;
  }

  // Close mcast_subscriber socket
  if(hibv_ctx->mcast_subscriber != -1 && close(hibv_ctx->mcast_subscriber)) {
    perror("close [mcast_subscriber]");
    rc++;
  }

  return rc;
} // hashpipe_ibv_shutdown

// See comments in header file for details about this function.
int hashpipe_ibv_flow(
    struct hashpipe_ibv_context * hibv_ctx,
    uint32_t  flow_idx,   enum ibv_flow_spec_type flow_type,
    uint8_t * dst_mac,    uint8_t * src_mac,
    uint16_t  ether_type, uint16_t  vlan_tag,
    uint32_t  src_ip,     uint32_t  dst_ip,
    uint16_t  src_port,   uint16_t  dst_port)
{
  int i, j;
  uint8_t mcast_dst_mac[ETH_ALEN];
  struct hashpipe_ibv_flow flow = {
    .attr = {
      .comp_mask      = 0,
      .type           = IBV_FLOW_ATTR_NORMAL,
      .size           = sizeof(flow.attr),
      .priority       = 0,
      .num_of_specs   = 0,
      .port           = hibv_ctx->port_num,
      .flags          = 0
    },
    .spec_eth = {
      .type   = IBV_FLOW_SPEC_ETH,
      .size   = sizeof(flow.spec_eth),
    },
    .spec_ipv4 = {
      .type   = IBV_FLOW_SPEC_IPV4,
      .size   = sizeof(flow.spec_ipv4),
    },
    .spec_tcp_udp = {
      .size   = sizeof(flow.spec_tcp_udp),
    }
  };

  // Sanity check context pointer and flow_idx
  if(!hibv_ctx || flow_idx >= hibv_ctx->max_flows) {
    errno = EINVAL;
    return 1;
  }

  // Sanity check flow_type
  if(flow_type != IBV_FLOW_SPEC_TCP
  && flow_type != IBV_FLOW_SPEC_UDP
  && flow_type != IBV_FLOW_SPEC_IPV4
  && flow_type != IBV_FLOW_SPEC_ETH) {
    errno = EINVAL;
    return 1;
  }

  // Sanity check ibv_flows and flow_dst_ips
  if(!hibv_ctx->ibv_flows || !hibv_ctx->flow_dst_ips) {
    errno = EFAULT;
    return 1;
  }

  // If there is already a flow in the specified index, destroy it for each QP.
  if(hibv_ctx->ibv_flows[flow_idx]) {
    // Drop multicast membership (if any) for first QP only
    if(hashpipe_ibv_mcast_membership(hibv_ctx, IP_DROP_MEMBERSHIP,
          hibv_ctx->flow_dst_ips[flow_idx])) {
      return 1;
    }
    hibv_ctx->flow_dst_ips[flow_idx] = 0;

    for(i=0; i<hibv_ctx->nqp; i++) {
      j = i*hibv_ctx->max_flows + flow_idx;
      if(ibv_destroy_flow(hibv_ctx->ibv_flows[j])) {
        return 1;
      }
      hibv_ctx->ibv_flows[j] = NULL;
    }
  }

  // If no criteria are given, do nothing but return success
  if(!dst_mac    && !src_mac
  && !ether_type && !vlan_tag
  && !src_ip     && !dst_ip
  && !src_port   && !dst_port) {
    return 0;
  }

  switch(flow_type) {
    case IBV_FLOW_SPEC_TCP:
    case IBV_FLOW_SPEC_UDP:
      flow.attr.size += sizeof(struct ibv_flow_spec_tcp_udp);
      flow.attr.num_of_specs++;
      flow.spec_tcp_udp.type = flow_type;

      if(src_port) {
        flow.spec_tcp_udp.val.src_port = htobe16(src_port);
        flow.spec_tcp_udp.mask.src_port = 0xffff;
      }

      if(dst_port) {
        flow.spec_tcp_udp.val.dst_port = htobe16(dst_port);
        flow.spec_tcp_udp.mask.dst_port = 0xffff;
      }
      // Fall through

    case IBV_FLOW_SPEC_IPV4:
      flow.attr.size += sizeof(struct ibv_flow_spec_ipv4);
      flow.attr.num_of_specs++;

      if(src_ip) {
        flow.spec_ipv4.val.src_ip = htobe32(src_ip);
        flow.spec_ipv4.mask.src_ip = 0xffffffff;
      }

      if(dst_ip) {
        flow.spec_ipv4.val.dst_ip = htobe32(dst_ip);
        flow.spec_ipv4.mask.dst_ip = 0xffffffff;
        // Remember big endian dst_ip
        hibv_ctx->flow_dst_ips[flow_idx] = flow.spec_ipv4.val.dst_ip;
        // If dst_ip is multicast
        if(IN_MULTICAST(dst_ip)) {
          // Add multicast membership (if needed)
          if(hashpipe_ibv_mcast_membership(hibv_ctx, IP_ADD_MEMBERSHIP,
                flow.spec_ipv4.val.dst_ip)) {
            return 1;
          }
          // Set multicast dst_mac
          ETHER_MAP_IP_MULTICAST(&flow.spec_ipv4.val.dst_ip, mcast_dst_mac);
          dst_mac = mcast_dst_mac;
        } else {
          hibv_ctx->flow_dst_ips[flow_idx] = flow.spec_ipv4.val.dst_ip;
        }
      }
      // Fall through

    case IBV_FLOW_SPEC_ETH:
      flow.attr.size += sizeof(struct ibv_flow_spec_eth);
      flow.attr.num_of_specs++;

      if(dst_mac) {
        memcpy(flow.spec_eth.val.dst_mac, dst_mac, 6);
        memset(flow.spec_eth.mask.dst_mac, 0xff, 6);
      }

      if(src_mac) {
        memcpy(flow.spec_eth.val.src_mac, src_mac, 6);
        memset(flow.spec_eth.mask.src_mac, 0xff, 6);
      }

      if(ether_type) {
        flow.spec_eth.val.ether_type = htobe16(ether_type);
        flow.spec_eth.mask.ether_type = 0xffff;
      }

      if(vlan_tag) {
        flow.spec_eth.val.vlan_tag = htobe16(vlan_tag);
        flow.spec_eth.mask.vlan_tag = 0xffff;
      }
      break;

    default:
      errno = EINVAL;
      return 1;
  } // switch(flow_type)

  // Create flow for each QP
  for(i=0; i<hibv_ctx->nqp; i++) {
    j = i*hibv_ctx->max_flows + flow_idx;

    if(!(hibv_ctx->ibv_flows[j] =
          ibv_create_flow(hibv_ctx->qp[i], (struct ibv_flow_attr *)&flow))) {
      return 1;
    }
  }

  return 0;
}

#define WC_BATCH_SIZE (16)

// See comments in header file for details about this function.
struct hashpipe_ibv_recv_pkt * hashpipe_ibv_recv_pkts(
    struct hashpipe_ibv_context * hibv_ctx, int timeout_ms)
{
  int i;
  int poll_rc = 0;
  int num_wce;
  uint64_t wr_id;
  struct pollfd pfd;
  struct ibv_qp *qp;
  struct ibv_qp_attr qp_attr;
  struct ibv_cq *ev_cq;
  int ev_cq_ctx;
#if HPIBV_USE_EXP_CQ
  struct ibv_exp_wc wc[WC_BATCH_SIZE];
#else
  struct ibv_wc wc[WC_BATCH_SIZE];
#endif
  struct hashpipe_ibv_recv_pkt * recv_head = NULL;
  struct ibv_recv_wr * recv_tail = NULL;

  // Sanity check hibv_ctx
  if(!hibv_ctx) {
    errno = EINVAL;
    perror("hashpipe_ibv_context(NULL)");
    return NULL;
  }

  // Ensure the QPs are in a state suitable for receiving
  for(i=0; i<hibv_ctx->nqp; i++) {
    qp = hibv_ctx->qp[i];
    switch(qp->state) {
    case IBV_QPS_RESET: // Unexpected, but maybe user reset it
      qp_attr.qp_state = IBV_QPS_INIT;
      qp_attr.port_num = hibv_ctx->port_num;
      if(ibv_modify_qp(qp, &qp_attr, IBV_QP_STATE|IBV_QP_PORT)) {
        return NULL;
      }
      // Fall through
    case IBV_QPS_INIT:
      qp_attr.qp_state = IBV_QPS_RTR;
      if(ibv_modify_qp(qp, &qp_attr, IBV_QP_STATE)) {
        return NULL;
      }
      break;
    case IBV_QPS_RTR:
    case IBV_QPS_RTS:
      // Already good to go
      break;
    default:
      fprintf(stderr, "unexpected QP state %d\n", qp->state);
      errno = EPROTO; // Protocol error
      return NULL;
    }
  }

  // Setup for poll
  pfd.fd = hibv_ctx->recv_cc->fd;
  pfd.events = POLLIN;
  pfd.revents = 0;

  // poll completion channel's fd with given timeout
  poll_rc = poll(&pfd, 1, timeout_ms);

  if(poll_rc <= 0) {
    // Timeout or error
    return NULL;
  }

  // Get the completion event
  if(ibv_get_cq_event(hibv_ctx->recv_cc, &ev_cq, (void **)&ev_cq_ctx)) {
    perror("ibv_get_cq_event");
    return NULL;
  }

  // Ack the event
  ibv_ack_cq_events(ev_cq, 1);

  // Request notification upon the next completion event
  // Do NOT restrict to solicited-only completions
  if(ibv_req_notify_cq(ev_cq, 0)) {
    perror("ibv_req_notify_cq");
    return NULL;
  }

  // Empty the CQ: poll all of the completions from the CQ (if any exist)
  do {
    num_wce = IBV_POLL_CQ(ev_cq, WC_BATCH_SIZE, wc);
    if(num_wce < 0) {
      perror("ibv_poll_cq");
      return NULL;
    }

    // Loop through all work completions
    for(i=0; i<num_wce; i++) {
      wr_id = wc[i].wr_id;
      // Set length to 0 for unsuccessful work requests
      if(wc[i].status != IBV_WC_SUCCESS) {
        fprintf(stderr,
            "wr %lu (%#016lx) got completion status 0x%x (%s) vendor error 0x%x (QP %d)\n",
            wr_id, wr_id, wc[i].status, ibv_wc_status_str(wc[i].status),
            wc[i].vendor_err, ev_cq_ctx);
        hibv_ctx->recv_pkt_buf[wr_id].length = 0;
      } else {
        // Copy byte_len from completion to length of pkt srtuct
        hibv_ctx->recv_pkt_buf[wr_id].length = wc[i].byte_len;
#if HPIBV_USE_EXP_CQ
        hibv_ctx->recv_pkt_buf[wr_id].timestamp = wc[i].timestamp;
#endif
      }
      // Add work requests to recv list
      if(!recv_head) {
        recv_head = &hibv_ctx->recv_pkt_buf[wr_id];
        recv_tail = &recv_head->wr;
      } else {
        recv_tail->next = &hibv_ctx->recv_pkt_buf[wr_id].wr;
        recv_tail = recv_tail->next;
      }
    } // for each work completion
  } while(num_wce);

  // Ensure list is NULL terminated (if we have a list)
  if(recv_tail) {
    recv_tail->next = NULL;
  }

  return recv_head;
} // hashpipe_ibv_recv_pkt

// See comments in header file for details about this function.
int hashpipe_ibv_release_pkts(struct hashpipe_ibv_context * hibv_ctx,
    struct hashpipe_ibv_recv_pkt * recv_pkt)
{
  int i;
  struct ibv_recv_wr * recv_wr_bad;

  if(!hibv_ctx) {
    errno = EINVAL;
    return -1;
  } else if(!recv_pkt) {
    return 0;
  }


  // Figure out which QP these packets belong to and repost to that QP
  i = recv_pkt->wr.wr_id / hibv_ctx->recv_pkt_num;
  return ibv_post_recv(hibv_ctx->qp[i], &recv_pkt->wr, &recv_wr_bad);
} // hashpipe_ibv_release_pkts

#ifdef VERBOSE_IBV_DEBUG
#define eprintf(...) fprintf(stderr, __VA_ARGS__)
#else
#define eprintf(...)
#endif // VERBOSE_IBV_DEBUG

#ifdef VERBOSE_IBV_DEBUG
// Debug function to print wr_id values from up to max elements of linked list
// pointed to by p.
static void print_send_pkt_list(struct hashpipe_ibv_send_pkt *p, size_t max)
{
  while(p && max) {
    fprintf(stderr, " %lu", p->wr.wr_id);
    p=(struct hashpipe_ibv_send_pkt *)p->wr.next;
    max--;
  }
  fprintf(stderr, "\n");
}
#endif // VERBOSE_IBV_DEBUG

// Utility function to pop up to num_to_pop packets from the hibv_ctx's
// send_pkt_head list, updating the send_pkt_head field as needed.
// The return value is a pointer to the first packet of a linked list of
// packets, or NULL if no packets are available or hibv_ctx is NULL or
// num_to_pop is zero.  The final element of the linked list (i.e. the one with
// "next" set to NULL) will have the IBV_SEND_SIGNALED flag set.
static struct hashpipe_ibv_send_pkt * pop_send_packets(
    struct hashpipe_ibv_context * hibv_ctx, size_t num_to_pop)
{
  int i;
  struct hashpipe_ibv_send_pkt *head;
  struct hashpipe_ibv_send_pkt *tail;

  // Validate that hibv_ctx is non-NULL
  if(!hibv_ctx) {
    fprintf(stderr, "pop_send_packets: NULL pointer passwd\n");
    return NULL;
  }

#ifdef VERBOSE_IBV_DEBUG
  eprintf("entering pop_send_packets for up to %lu packets\n", num_to_pop);
  eprintf("  pkt_head"); print_send_pkt_list(hibv_ctx->send_pkt_head, 32);
#endif // VERBOSE_IBV_DEBUG

  // If no packets requested or no packets are available, don't do anything
  if(num_to_pop == 0 || !hibv_ctx->send_pkt_head) {
    return NULL;
  }

  // Save head pointer
  head = hibv_ctx->send_pkt_head;
  // Walk tail down to last element (either the num_to_pop'th or the last
  // element in the list, which ever happens first)
  tail = hibv_ctx->send_pkt_head;
  for(i=0; i<num_to_pop-1 && tail->wr.next; i++) {
    tail = (struct hashpipe_ibv_send_pkt *)tail->wr.next;
  }

  // Update send_pkt_head and NULL terminate tail
  hibv_ctx->send_pkt_head = (struct hashpipe_ibv_send_pkt *)tail->wr.next;
  tail->wr.next = NULL;

  // Set the IBV_SEND_SIGNALED flag
  tail->wr.send_flags |= IBV_SEND_SIGNALED;

#ifdef VERBOSE_IBV_DEBUG
  eprintf("leaving pop_send_packets got %d packets\n", i+1);
  eprintf("  pkt_head"); print_send_pkt_list(hibv_ctx->send_pkt_head, 32);
  eprintf("  pop_head"); print_send_pkt_list(head, 32);
#endif // VERBOSE_IBV_DEBUG

  // Done
  return head;
}

// See comments in header file for details about this function.
struct hashpipe_ibv_send_pkt * hashpipe_ibv_get_pkts(
    struct hashpipe_ibv_context * hibv_ctx, uint32_t num_pkts)
{
  int i, j;
  int num_wce;
  struct ibv_qp *qp;
  struct ibv_qp_attr qp_attr;
#if HPIBV_USE_EXP_CQ
  struct ibv_exp_wc wc[WC_BATCH_SIZE];
#else
  struct ibv_wc wc[WC_BATCH_SIZE];
#endif
  struct hashpipe_ibv_send_pkt * send_pkt;
#if HPIBV_USE_TIMING_DAIGS
  struct timespec now;
#endif

  // Sanity check hibv_ctx
  // TODO Do we really want to check this each time?
  if(!hibv_ctx) {
    errno = EINVAL;
    perror("hashpipe_ibv_context(NULL)");
    return NULL;
  }

  // Sanity check num_pkts
  // TODO Do we really want to check this each time?
  if(num_pkts > hibv_ctx->send_pkt_num) {
    num_pkts = hibv_ctx->send_pkt_num;
  } else if(num_pkts == 0) {
    return NULL;
  }

  // Ensure the QPs are in a state suitable for sending
  // TODO Do we really want to check this each time?
  for(i=0; i<hibv_ctx->nqp; i++) {
    qp = hibv_ctx->qp[i];
    if(qp->state != IBV_QPS_RTS) {
      switch(qp->state) {
      case IBV_QPS_RESET: // Unexpected, but maybe user reset it
        qp_attr.qp_state = IBV_QPS_INIT;
        qp_attr.port_num = hibv_ctx->port_num;
        if(ibv_modify_qp(qp, &qp_attr, IBV_QP_STATE|IBV_QP_PORT)) {
          return NULL;
        }
        // Fall through
      case IBV_QPS_INIT:
        qp_attr.qp_state = IBV_QPS_RTR;
        if(ibv_modify_qp(qp, &qp_attr, IBV_QP_STATE)) {
          return NULL;
        }
        // Fall through
      case IBV_QPS_RTR:
        qp_attr.qp_state = IBV_QPS_RTS;
        if(ibv_modify_qp(qp, &qp_attr, IBV_QP_STATE)) {
          return NULL;
        }
        break;
      case IBV_QPS_RTS:
        // Already good to go
        break;
      default:
        fprintf(stderr, "unexpected QP state %d\n", qp->state);
        errno = EPROTO; // Protocol error
        return NULL;
      }
    }
  }

#if HPIBV_USE_TIMING_DAIGS
  // Used to update elapsed stats of packets
  clock_gettime(CLOCK_MONOTONIC, &now);
#endif

  // Empty the CQ: poll all of the completions from the CQ (if any exist)
  // TODO Do we really want to do this each time?
  for(i=0; i<hibv_ctx->nqp; i++) {
    do {
      num_wce = IBV_POLL_CQ(hibv_ctx->send_cq[i], WC_BATCH_SIZE, wc);
      if(num_wce < 0) {
        perror("ibv_poll_cq");
        return NULL;
      }
      eprintf("got %d work completions for QP %d\n", num_wce, i);

      // Process all completion events.
      for(j=0; j<num_wce; j++) {
        send_pkt = &hibv_ctx->send_pkt_buf[wc[j].wr_id];
#if HPIBV_USE_EXP_CQ
        send_pkt->timestamp = wc[i].timestamp;
#endif
#if HPIBV_USE_TIMING_DAIGS
        send_pkt->elapsed_ns = ELAPSED_NS( send_pkt->ts, now);
        send_pkt->elapsed_ns_total += send_pkt->elapsed_ns;
        send_pkt->elapsed_ns_count++;
#endif
        // Clear IBV_SEND_SIGNALED flag
        send_pkt->wr.send_flags &= ~IBV_SEND_SIGNALED;
        // Push onto send_pkt_head
        send_pkt->wr.next = &hibv_ctx->send_pkt_head->wr;
        hibv_ctx->send_pkt_head = send_pkt;
        eprintf("got work completion for send work request %lu\n",
            send_pkt->wr.wr_id);
      } // for each work completion
    } while(num_wce);
  }

  return pop_send_packets(hibv_ctx, num_pkts);
} // hashpipe_ibv_get_pkts

// See comments in header file for details about this function.
int hashpipe_ibv_send_pkts(struct hashpipe_ibv_context * hibv_ctx,
    struct hashpipe_ibv_send_pkt * send_pkt, uint32_t qp_idx)
{
  //uint64_t wr_id;
  struct ibv_send_wr * p;
#if HPIBV_USE_TIMING_DAIGS
  struct hashpipe_ibv_send_pkt * pkt;
#endif

  if(!hibv_ctx || !send_pkt || qp_idx >= hibv_ctx->nqp) {
    errno = EINVAL;
    return -1;
  }

#if HPIBV_USE_TIMING_DAIGS
  // Set ts field in each packet
  for(pkt=send_pkt; pkt; pkt = pkt->wr.next) {
    clock_gettime(CLOCK_MONOTONIC, &pkt->ts);
  }
#endif

  return ibv_post_send(hibv_ctx->qp[qp_idx], &send_pkt->wr, &p);
} // hashpipe_ibv_send_pkts
