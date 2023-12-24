#include "icmpv6.h"

#include "config.h"
#include "ipv6.h"
#include "log.h"
#include "my_buf.h"
#include "nd.h"
#include "net.h"
#include "utils.h"
#include <cstring>

/* ICMPv6パケットの受信処理 */
void icmpv6_input(ipv6_device *v6dev, in6_addr source, in6_addr dstination, void *buffer, size_t len) {
  icmpv6_hdr *icmp_pkt = (icmpv6_hdr *)buffer;
  LOG_ICMPV6("received icmpv6 code=%d, type=%d\n", icmp_pkt->code, icmp_pkt->type);

  switch (icmp_pkt->type) {
  case ICMPV6_TYPE_NEIGHBOR_SOLICIATION: {
    if (len < sizeof(icmpv6_na)) {
      LOG_ICMPV6("received neighbor solicitation packet too short\n");
      return;
    }

    icmpv6_na *ns_pkt = (icmpv6_na *)buffer;
    char target_addr_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &ns_pkt->target_addr, target_addr_str, INET6_ADDRSTRLEN);

    LOG_ICMPV6("received neighbor solicitation (target:%s)\n", target_addr_str);

    if (memcmp(&ns_pkt->target_addr, &v6dev->address, 16) == 0) {
      LOG_ICMPV6("ns target match! %s\n", target_addr_str);
      LOG_ICMPV6("option mac address! %s\n", mac_addr_toa(ns_pkt->opt_mac_addr));

      update_nd_table_entry(v6dev->net_dev, ns_pkt->opt_mac_addr, source);

      my_buf *icmpv6_mybuf = my_buf::create(sizeof(icmpv6_na));
      icmpv6_na *napkt = (icmpv6_na *)icmpv6_mybuf->buffer;

      napkt->hdr.code = 0;
      napkt->hdr.type = ICMPV6_TYPE_NEIGHBOR_ADVERTISEMENT;
      napkt->hdr.checksum = 0;
      napkt->flags = ICMPV6_NA_FLAG_SOLICITED | ICMPV6_NA_FLAG_OVERRIDE;
      napkt->target_addr = ns_pkt->target_addr;

      napkt->opt_type = 2;
      napkt->opt_length = 1;
      memcpy(&napkt->opt_mac_addr, v6dev->net_dev->mac_addr, 6);

      ipv6_pseudo_header phdr;
      phdr.src_addr = v6dev->address;
      phdr.dst_addr = source;
      phdr.packet_length = htonl(sizeof(icmpv6_na));
      phdr.zero1 = 0;
      phdr.zero2 = 0;
      phdr.next_header = IPV6_PROTOCOL_NUM_ICMP;

      uint16_t psum = ~checksum_16((uint16_t *)&phdr, sizeof(ipv6_pseudo_header), 0);

      napkt->hdr.checksum = checksum_16((uint16_t *)napkt, sizeof(icmpv6_na), psum);

      ipv6_encap_dev_output(v6dev->net_dev, &ns_pkt->opt_mac_addr[0], source, icmpv6_mybuf, IPV6_PROTOCOL_NUM_ICMP);
    }
  } break;

  case ICMPV6_TYPE_NEIGHBOR_ADVERTISEMENT: {
    if (len < sizeof(icmpv6_na)) {
      LOG_ICMPV6("received neighbor advertisement packet too short\n");
      return;
    }

    icmpv6_na *napkt = (icmpv6_na *)buffer;

    char target_addr_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &napkt->target_addr, target_addr_str, INET6_ADDRSTRLEN);

    LOG_ICMPV6("updating nd entry %s => %s\n", target_addr_str, mac_addr_toa(napkt->opt_mac_addr));

    update_nd_table_entry(v6dev->net_dev, napkt->opt_mac_addr, napkt->target_addr);

  } break;

  case ICMPV6_TYPE_ECHO_REQUEST: {
    if (len < sizeof(icmpv6_echo)) {
      LOG_ICMPV6("received echo request packet too short\n");
      return;
    }

    icmpv6_echo *echo_packet = (icmpv6_echo *)buffer;

    LOG_ICMPV6("received echo request id=%d seq=%d\n", ntohs(echo_packet->id), ntohs(echo_packet->seq));

    uint32_t data_len = len - sizeof(icmpv6_echo);

    if (data_len > 256) {
      LOG_ICMPV6("echo size is too large\n");
      return;
    }

    my_buf *reply_buf = my_buf::create(sizeof(icmpv6_echo) + data_len);
    icmpv6_echo *reply_pkt = (icmpv6_echo *)reply_buf->buffer;
    reply_pkt->hdr.type = ICMPV6_TYPE_ECHO_REPLY;
    reply_pkt->hdr.code = 0;
    reply_pkt->hdr.checksum = 0;

    reply_pkt->id = echo_packet->id;
    reply_pkt->seq = echo_packet->seq;

    memcpy(&reply_pkt->data[0], &echo_packet->data[0], data_len);

    ipv6_pseudo_header phdr;
    phdr.src_addr = v6dev->address;
    phdr.dst_addr = source;
    phdr.packet_length = htonl(sizeof(icmpv6_echo) + data_len);
    phdr.zero1 = 0;
    phdr.zero2 = 0;
    phdr.next_header = IPV6_PROTOCOL_NUM_ICMP;

    uint16_t psum = ~checksum_16((uint16_t *)&phdr, sizeof(ipv6_pseudo_header), 0);

    reply_pkt->hdr.checksum = checksum_16((uint16_t *)reply_pkt, sizeof(icmpv6_echo) + data_len, psum);

    ipv6_encap_output(source, v6dev->address, reply_buf, IPV6_PROTOCOL_NUM_ICMP);

  } break;
  }
}

void send_ns_packet(net_device *dev, in6_addr target_addr) {

  // 要請ノードマルチキャストアドレスを生成
  in6_addr mcast_addr;
  inet_pton(AF_INET6, "ff02::1:ff00:0000", &mcast_addr);
  mcast_addr.s6_addr[13] = target_addr.s6_addr[13];
  mcast_addr.s6_addr[14] = target_addr.s6_addr[14];
  mcast_addr.s6_addr[15] = target_addr.s6_addr[15];

  my_buf *ns_buf = my_buf::create(sizeof(icmpv6_na));
  icmpv6_na *ns_pkt = (icmpv6_na *)ns_buf->buffer;
  ns_pkt->hdr.type = ICMPV6_TYPE_NEIGHBOR_SOLICIATION;
  ns_pkt->hdr.code = 0;
  ns_pkt->hdr.checksum = 0;

  ns_pkt->flags = 0;
  ns_pkt->reserved1 = 0;
  ns_pkt->reserved2 = 0;
  ns_pkt->target_addr = target_addr;

  ns_pkt->opt_length = 1;
  ns_pkt->opt_type = ICMPV6_OPTION_SOURCE_LINK_LAYER_ADDRESS;
  memcpy(&ns_pkt->opt_mac_addr, dev->mac_addr, 6);

  ipv6_pseudo_header phdr;
  phdr.src_addr = dev->ipv6_dev->address;
  phdr.dst_addr = mcast_addr;
  phdr.packet_length = htonl(sizeof(icmpv6_na));
  phdr.zero1 = 0;
  phdr.zero2 = 0;
  phdr.next_header = IPV6_PROTOCOL_NUM_ICMP;

  uint16_t psum = ~checksum_16((uint16_t *)&phdr, sizeof(ipv6_pseudo_header), 0);

  ns_pkt->hdr.checksum = checksum_16((uint16_t *)ns_pkt, sizeof(icmpv6_na), psum);

  LOG_ICMPV6("sending NS...\n");

  ipv6_encap_dev_mcast_output(dev, mcast_addr, ns_buf, IPV6_PROTOCOL_NUM_ICMP);
}