#include "config.h"

#include "ipv6.h"
#include "log.h"
#include "net.h"
#include "patricia_trie.h"
#include "utils.h"

/* IPv6ネットワークへの経路を設定 */
void configure_ipv6_net_route(in6_addr prefix, uint32_t prefix_len, in6_addr next_hop) {

  // 経路エントリの生成
  ipv6_route_entry *entry;
  entry = (ipv6_route_entry *)(calloc(1, sizeof(ipv6_route_entry)));
  entry->type = ipv6_route_type::network;
  entry->next_hop = next_hop;

  // 経路の登録
  patricia_trie_insert(ipv6_fib, prefix, prefix_len, entry);

  char addr_str[INET6_ADDRSTRLEN];
  inet_ntop(AF_INET6, &prefix, addr_str, INET6_ADDRSTRLEN);

  char addr_nh[INET6_ADDRSTRLEN];
  inet_ntop(AF_INET6, &next_hop, addr_nh, INET6_ADDRSTRLEN);

  LOG_INFO("configure route to %s/%d via %s\n", addr_str, prefix_len, addr_nh);
}

/* デバイスにIPv6アドレスを設定 */
void configure_ipv6_address(net_device *dev, in6_addr address, uint32_t prefix_len) {
  if (dev == nullptr) {
    LOG_ERROR("net device to configure not found\n");
    exit(EXIT_FAILURE);
  }

  // IPアドレスの登録
  dev->ipv6_dev = (ipv6_device *)calloc(1, sizeof(ipv6_device));
  dev->ipv6_dev->address = address;
  dev->ipv6_dev->prefix_len = prefix_len;
  dev->ipv6_dev->net_dev = dev;

  char addr_str[INET6_ADDRSTRLEN];
  inet_ntop(AF_INET6, &address, addr_str, INET6_ADDRSTRLEN);

  LOG_INFO("configure ipv6 address to %s\n", addr_str);

  // IPアドレスを設定すると同時に直接接続ルートを設定する
  ipv6_route_entry *entry;
  entry = (ipv6_route_entry *)calloc(1, sizeof(ipv6_route_entry));
  entry->type = ipv6_route_type::connected;
  entry->dev = dev;

  patricia_trie_insert(ipv6_fib, address, prefix_len, entry);

  address = in6_addr_clear_prefix(address, prefix_len);

  inet_ntop(AF_INET6, &address, addr_str, INET6_ADDRSTRLEN);

  LOG_INFO("configure directly connected route %s/%d device %s\n", addr_str, prefix_len, dev->name);
}