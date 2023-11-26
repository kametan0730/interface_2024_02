#ifndef CURO_IPV6_H
#define CURO_IPV6_H

#include "config.h"
#include <arpa/inet.h>
#include <iostream>
#include <queue>

#define IPV6_PROTOCOL_NUM_ICMP 0x3a
#define IPV6_PROTOCOL_NUM_NONXT 0x3b
#define IPV6_PROTOCOL_NUM_OPTS 0x3c

#define ICMPV6_OPTION_SOURCE_LINK_LAYER_ADDRESS 1
#define ICMPV6_OPTION_TARGET_LINK_LAYER_ADDRESS 2

struct patricia_node;

extern patricia_node *ipv6_fib;

struct ipv6_device {
    in6_addr address; // IPv6アドレス
    uint32_t prefix_len; // プレフィックス長(0~128)
    uint8_t scope; // スコープ
    net_device *net_dev; // ネットワークデバイスへのポインタ
    ipv6_device *next; // 次のIPv6デバイス
};

enum class ipv6_route_type {
    connected, // 直接接続されているネットワークの経路　
    network
};

struct net_device;

struct ipv6_route_entry {
    ipv6_route_type type;
    union {
        net_device *dev;
        in6_addr next_hop;
    };
};

struct ipv6_header {
    uint32_t ver_tc_fl;
    uint16_t payload_len;
    uint8_t next_hdr;
    uint8_t hop_limit;
    in6_addr src_addr;
    in6_addr dst_addr;
} __attribute__((packed));

struct ipv6_pseudo_header {
    in6_addr src_addr;
    in6_addr dst_addr;
    uint32_t packet_length;
    uint16_t zero1;
    uint8_t zero2;
    uint8_t next_header;
};


int in6_addr_equals(in6_addr addr1, in6_addr addr2);

long in6_addr_sum(in6_addr addr);

void dump_ipv6_route(patricia_node *root);

void ipv6_input(net_device *input_dev, uint8_t *buffer, ssize_t len);

struct my_buf;

void ipv6_encap_dev_output(net_device *output_dev, const uint8_t *dst_mac_addr, in6_addr dst_addr, my_buf *buffer, uint8_t next_hdr_num);

void ipv6_encap_dev_mcast_output(net_device *output_dev, in6_addr dst_addr, my_buf *buffer, uint8_t next_hdr_num);

void ipv6_encap_output(in6_addr dst_addr, in6_addr src_addr, my_buf * buffer, uint8_t next_hdr_num);

#endif // CURO_IPV6_H
