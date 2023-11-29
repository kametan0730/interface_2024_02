#ifndef CURO_CONFIG_H
#define CURO_CONFIG_H

#include <cstdint>
#include <cstdio>

#define MAX_INTERFACES 8

/*
 * 各プロトコルについてデバッグレベルを設定できます
 *
 * 0 No debug
 * 1 Print debug message
 */

#define DEBUG_ETHERNET 1
#define DEBUG_IPV6 1
#define DEBUG_INFO 1
#define DEBUG_ICMPV6 1

// #define ENABLE_MYBUF_NON_COPY_MODE // パケット転送時にバッファのコピーを削減するか
#define ENABLE_NAT        // NATを有効にするか
#define ENABLE_ICMP_ERROR // ICMPエラーを送信するか
#define ENABLE_COMMAND    // 対話的なコマンドを有効化するか
#define ENABLE_IPV6       // IPv6を有効にするか

struct net_device;
struct in6_addr;

void configure_ipv6_net_route(in6_addr prefix, uint32_t prefix_len, in6_addr next_hop);
void configure_ipv6_address(net_device *dev, in6_addr address, uint32_t prefix_len);

#endif // CURO_CONFIG_H
