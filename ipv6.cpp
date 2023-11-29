#include "ipv6.h"

#include "config.h"
#include "ethernet.h"
#include "icmpv6.h"
#include "log.h"
#include "my_buf.h"
#include "nd.h"
#include "patricia_trie.h"
#include "utils.h"

/**
 * IPv6ルーティングテーブルのルートノード
 */
patricia_node *ipv6_fib;

int in6_addr_equals(in6_addr addr1, in6_addr addr2) {
  for (int i = 0; i < 7; i++) {
    if (addr1.s6_addr32[i] != addr2.s6_addr32[i])
      return 0;
  }
  return 1;
}

long in6_addr_sum(in6_addr addr) { return (addr.s6_addr32[0] + addr.s6_addr32[1] + addr.s6_addr32[2] + addr.s6_addr32[3]); }

// テキストでエントリを出力する
void dump_ipv6_route(patricia_node *root) {

  patricia_node *current_node;
  std::queue<patricia_node *> node_queue;
  node_queue.push(root);

  while (!node_queue.empty()) {
    current_node = node_queue.front();
    node_queue.pop();

    char ipv6_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &(current_node->address), ipv6_str, INET6_ADDRSTRLEN);

    if (current_node->is_prefix) {
      if (current_node->data != nullptr) {

        ipv6_route_entry *entry = (ipv6_route_entry *)current_node->data;

        if (entry->type == ipv6_route_type::connected) {
          LOG_IPV6("%s/%d via %s\n", ipv6_str, patricia_trie_get_prefix_len(current_node), entry->dev->name);
        } else if (entry->type == ipv6_route_type::network) {
          char ipv6_nh_str[INET6_ADDRSTRLEN];
          inet_ntop(AF_INET6, &(entry->next_hop), ipv6_nh_str, INET6_ADDRSTRLEN);

          LOG_IPV6("%s/%d next hop %s\n", ipv6_str, patricia_trie_get_prefix_len(current_node), ipv6_nh_str);
        }
      }
    }

    if (current_node->left != nullptr) {
      node_queue.push(current_node->left);
    }
    if (current_node->right != nullptr) {
      node_queue.push(current_node->right);
    }
  }
}

/* 自分宛のIPパケットの処理 */
void ipv6_input_to_ours(net_device *input_dev, ipv6_header *packet, size_t len) {

  switch (packet->next_hdr) {
  case IPV6_PROTOCOL_NUM_ICMP:
    return icmpv6_input(input_dev->ipv6_dev, packet->src_addr, packet->dst_addr, ((uint8_t *)packet) + sizeof(ipv6_header), len - sizeof(ipv6_header));
  default:
    break;
  }
}

void ipv6_output_to_host(net_device *dev, in6_addr dst_addr, in6_addr src_addr, my_buf *buffer);
void ipv6_output_to_next_hop(in6_addr dst_addr, my_buf *buffer);

void ipv6_input(net_device *input_dev, uint8_t *buffer, ssize_t len) {

  if (input_dev->ipv6_dev == nullptr) {
    LOG_IPV6("received ipv6 packet from non ipv6 device %s\n", input_dev->name);
    return;
  }

  if (len < sizeof(in6_addr)) {
    LOG_IPV6("received ipv6 packet too short from %s\n", input_dev->name);
    return;
  }

  // 送られてきたバッファをキャストして扱う
  ipv6_header *packet = (ipv6_header *)buffer;

  if (packet->ver_tc_fl & 0b1111 != 6) {
    return;
  }

  char src_addr_str[INET6_ADDRSTRLEN];
  inet_ntop(AF_INET6, &packet->src_addr, src_addr_str, INET6_ADDRSTRLEN);

  char dst_addr_str[INET6_ADDRSTRLEN];
  inet_ntop(AF_INET6, &packet->dst_addr, dst_addr_str, INET6_ADDRSTRLEN);

  LOG_IPV6("received ipv6 packet next-header 0x%02x %s =>> %s\n", packet->next_hdr, src_addr_str, dst_addr_str);

  // マルチキャストアドレスの判定
  if (packet->dst_addr.s6_addr[0] == 0xff) { // ff00::/8の範囲だったら

    if (memcmp(&input_dev->ipv6_dev->address.s6_addr[13], &packet->dst_addr.s6_addr[13], 3) == 0) {
      LOG_IPV6("packet to multicast address (solicited-node multicast address)\n");
      return ipv6_input_to_ours(input_dev, packet, len); // 自分宛の通信として処理
    }
  }

  // 宛先IPアドレスをルータが持ってるか調べる
  for (net_device *dev = net_dev_list; dev; dev = dev->next) {
    if (dev->ipv6_dev != nullptr) {
      // 宛先IPアドレスがルータの持っているIPアドレスの処理
      if (memcmp(&dev->ipv6_dev->address, &packet->dst_addr, 16) == 0) {
        return ipv6_input_to_ours(dev, packet, len); // 自分宛の通信として処理
      }
    }
  }

  // 自分宛てのパケット出ない場合フォワーディングテーブルの検索

  // 宛先IPアドレスがルータの持っているIPアドレスでない場合はフォワーディングを行う
  patricia_node *res_node = patricia_trie_search(ipv6_fib, packet->dst_addr); // ルーティングテーブルをルックアップ

  if (res_node == nullptr or res_node->data == nullptr) { // 宛先までの経路がなかったらパケットを破棄

    LOG_IPV6("No route to %s\n", dst_addr_str);
    // Drop packet
    return;
  }

  ipv6_route_entry *route = (ipv6_route_entry *)res_node->data;

  packet->hop_limit--; // Hop Limitをデクリメント

  my_buf *ipv6_fwd_mybuf = my_buf::create(len);
  memcpy(ipv6_fwd_mybuf->buffer, buffer, len);
  ipv6_fwd_mybuf->len = len;

  if (route->type == ipv6_route_type::connected) { // 直接接続ネットワークの経路なら
    LOG_IPV6("forwarding ipv6 packet to host\n");
    ipv6_output_to_host(route->dev, packet->dst_addr, packet->src_addr, ipv6_fwd_mybuf); // hostに直接送信
    return;
  } else if (route->type == ipv6_route_type::network) { // 直接接続ネットワークの経路ではなかったら
    LOG_IPV6("forwarding ipv6 packet to network\n");
    ipv6_output_to_next_hop(route->next_hop, ipv6_fwd_mybuf); // next hopに送信
    return;
  }

  // ルーティングのための処理
}

void ipv6_encap_dev_output(net_device *output_dev, const uint8_t *dst_mac_addr, in6_addr dst_addr, my_buf *buffer, uint8_t next_hdr_num) {

  // 連結リストをたどってIPヘッダで必要なIPパケットの全長を算出する
  uint16_t payload_len = 0;
  my_buf *current = buffer;
  while (current != nullptr) {
    payload_len += current->len;
    current = current->next;
  }

  // IPv6ヘッダ用のバッファを確保する
  my_buf *v6h_mybuf = my_buf::create(sizeof(ipv6_header));
  buffer->add_header(v6h_mybuf); // 包んで送るデータにヘッダとして連結する

  // IPヘッダの各項目を設定
  ipv6_header *v6h_buf = (ipv6_header *)v6h_mybuf->buffer;
  v6h_buf->ver_tc_fl = 0x60;
  v6h_buf->payload_len = htons(payload_len);
  v6h_buf->next_hdr = next_hdr_num;
  v6h_buf->hop_limit = 0xff;
  v6h_buf->src_addr = output_dev->ipv6_dev->address;
  v6h_buf->dst_addr = dst_addr;

  ethernet_encapsulate_output(output_dev, dst_mac_addr, v6h_mybuf, ETHER_TYPE_IPV6);
}

void ipv6_encap_dev_mcast_output(net_device *output_dev, in6_addr dst_addr, my_buf *buffer, uint8_t next_hdr_num) {

  // 連結リストをたどってIPヘッダで必要なIPパケットの全長を算出する
  uint16_t payload_len = 0;
  my_buf *current = buffer;
  while (current != nullptr) {
    payload_len += current->len;
    current = current->next;
  }

  // IPv6ヘッダ用のバッファを確保する
  my_buf *v6h_mybuf = my_buf::create(sizeof(ipv6_header));
  buffer->add_header(v6h_mybuf); // 包んで送るデータにヘッダとして連結する

  // IPヘッダの各項目を設定
  ipv6_header *v6h_buf = (ipv6_header *)v6h_mybuf->buffer;
  v6h_buf->ver_tc_fl = 0x60;
  v6h_buf->payload_len = htons(payload_len);
  v6h_buf->next_hdr = next_hdr_num;
  v6h_buf->hop_limit = 0xff;
  v6h_buf->src_addr = output_dev->ipv6_dev->address;
  v6h_buf->dst_addr = dst_addr;

  uint8_t dst_mac_addr[6];
  dst_mac_addr[0] = 0x33;
  dst_mac_addr[1] = 0x33;

  memcpy(&dst_mac_addr[2], &dst_addr.s6_addr[12], 4);

  ethernet_encapsulate_output(output_dev, dst_mac_addr, v6h_mybuf, ETHER_TYPE_IPV6);
}

void ipv6_output_to_host(net_device *dev, in6_addr dst_addr, in6_addr src_addr, my_buf *buffer) { // devいらない?

  nd_table_entry *nde = search_nd_table_entry(dst_addr);
  if (!nde) { // ARPエントリが無かったら

    char dst_addr_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &dst_addr, dst_addr_str, INET6_ADDRSTRLEN);

    LOG_IPV6("Trying ipv6 output to host, but no nd record to %s\n", dst_addr_str);
    send_ns_packet(dev, dst_addr);     // ARPリクエストの送信
    my_buf::my_buf_free(buffer, true); // Drop packet
    return;
  } else {
    ethernet_encapsulate_output(nde->dev, nde->mac_addr, buffer,
                                ETHER_TYPE_IPV6); // イーサネットでカプセル化して送信
  }
}

void ipv6_output_to_next_hop(in6_addr dst_addr, my_buf *buffer) {
  nd_table_entry *entry = search_nd_table_entry(dst_addr);

  if (entry == nullptr) {

    patricia_node *res = patricia_trie_search(ipv6_fib, dst_addr);
    if (res != nullptr and res->data != nullptr) {
      ipv6_route_entry *route_entry = (ipv6_route_entry *)res->data;

      if (route_entry != nullptr and route_entry->type == ipv6_route_type::connected) {
        send_ns_packet(route_entry->dev, dst_addr);
        return;
      }
    }

    char dst_addr_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &dst_addr, dst_addr_str, INET6_ADDRSTRLEN);
    LOG_IPV6("next hop unreachable %s\n", dst_addr_str);

  } else {

    LOG_IPV6("found nd entry to next hop!\n");
    ethernet_encapsulate_output(entry->dev, entry->mac_addr, buffer, ETHER_TYPE_IPV6);
  }
}

void ipv6_encap_output(in6_addr dst_addr, in6_addr src_addr, my_buf *buffer, uint8_t next_hdr_num) {

  // 連結リストをたどってIPヘッダで必要なIPパケットの全長を算出する
  uint16_t payload_len = 0;
  my_buf *current = buffer;
  while (current != nullptr) {
    payload_len += current->len;
    current = current->next;
  }

  // IPv6ヘッダ用のバッファを確保する
  my_buf *v6h_mybuf = my_buf::create(sizeof(ipv6_header));
  buffer->add_header(v6h_mybuf); // 包んで送るデータにヘッダとして連結する

  // IPヘッダの各項目を設定
  ipv6_header *v6h_buf = (ipv6_header *)v6h_mybuf->buffer;
  v6h_buf->ver_tc_fl = 0x60;
  v6h_buf->payload_len = htons(payload_len);
  v6h_buf->next_hdr = next_hdr_num;
  v6h_buf->hop_limit = 0xff;
  v6h_buf->src_addr = src_addr;
  v6h_buf->dst_addr = dst_addr;

  ipv6_output_to_next_hop(dst_addr, v6h_mybuf);
}