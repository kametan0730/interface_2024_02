#include "ethernet.h"

#include "config.h"
#include "ipv6.h"
#include "log.h"
#include "my_buf.h"
#include "utils.h"
#include <cstring>

/* イーサネットの受信処理 */
void ethernet_input(net_device *dev, uint8_t *buffer, ssize_t len) {
  // 送られてきた通信をイーサネットのフレームとして解釈する
  ethernet_header *header = (ethernet_header *)buffer;

  // イーサタイプを抜き出し、ホストバイトオーダーに変換
  uint16_t ether_type = ntohs(header->type);

  // 自分のMACアドレス宛てかブロードキャスト/マルチキャストの通信かを確認する
  if (memcmp(header->dst_addr, dev->mac_addr, 6) != 0 and memcmp(header->dst_addr, ETHER_ADDR_BCAST, 6) != 0 and memcmp(header->dst_addr, ETHER_ADDR_IPV6_MCAST_PREFIX, 2) != 0) {
    return;
  }

  LOG_ETHERNET("received ethernet frame type %04x from %s to %s\n", ether_type, mac_addr_toa(header->src_addr), mac_addr_toa(header->dst_addr));

  // イーサタイプの値から上位プロトコルを特定する
  switch (ether_type) {

  case ETHER_TYPE_IPV6: // イーサタイプがIPのものだったら
    // Ethernetヘッダを外してIP処理へ
    return ipv6_input(dev, buffer + ETHERNET_HEADER_SIZE, len - ETHERNET_HEADER_SIZE);

  default: // 知らないイーサタイプだったら
    LOG_ETHERNET("received unhandled ether type %04x\n", ether_type);
    return;
  }
}

/* イーサネットにカプセル化して送信 */
void ethernet_encapsulate_output(net_device *dev, const uint8_t *dst_addr, my_buf *payload_mybuf, uint16_t ether_type) {
  LOG_ETHERNET("sending ethernet frame type %04x from %s to %s\n", ether_type, mac_addr_toa(dev->mac_addr), mac_addr_toa(dst_addr));

  my_buf *header_mybuf = my_buf::create(ETHERNET_HEADER_SIZE); // イーサネットヘッダ長分のバッファを確保
  ethernet_header *header = (ethernet_header *)header_mybuf->buffer;

  // イーサネットヘッダの設定
  memcpy(header->src_addr, dev->mac_addr,
         6);                             // 送信元アドレスにはデバイスのアドレスを設定
  memcpy(header->dst_addr, dst_addr, 6); // `宛先アドレスの設定
  header->type = htons(ether_type);      // イーサタイプの設定

  payload_mybuf->add_header(header_mybuf); // 上位プロトコルから受け取ったバッファにヘッダをつける

#ifdef DEBUG_ETHERNET
#if DEBUG_ETHERNET > 1
  printf("[ETHER] sending buffer: ");
  for (int i = 0; i < header_mybuf->len; ++i) {
    printf("%02x", header_mybuf->buffer[i]);
  }
  printf("\n");
#endif
#endif

  uint8_t send_buffer[1550];
  // 全長を計算しながらメモリにバッファを展開する
  size_t total_len = 0;
  my_buf *current = header_mybuf;
  while (current != nullptr) {
    if (total_len + current->len > sizeof(send_buffer)) { // Overflowする場合
      LOG_ETHERNET("frame is too big!\n");
      return;
    }

#ifdef ENABLE_MYBUF_NON_COPY_MODE
    if (current->buf_ptr != nullptr) {
      memcpy(&send_buffer[total_len], current->buf_ptr, current->len);
    } else {
#endif
      memcpy(&send_buffer[total_len], current->buffer, current->len);
#ifdef ENABLE_MYBUF_NON_COPY_MODE
    }
#endif
    total_len += current->len;
    current = current->next;
  }

  // ネットワークデバイスに送信する
  dev->ops.transmit(dev, send_buffer, total_len);

  my_buf::my_buf_free(header_mybuf, true); // メモリ開放
}
