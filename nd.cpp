#include "nd.h"

#include "net.h"
#include "utils.h"

// TODO タイマーを追加

/* グローバル変数にテーブルを保持  */
nd_table_entry *nd_table[ND_TABLE_SIZE];

/* NDテーブルの初期化 */
void init_nd_table() {
  for (int i = 0; i < ND_TABLE_SIZE; i++) {
    nd_table[i] = nullptr;
  }
}

/* NDテーブルにエントリの追加と更新 */
void update_nd_table_entry(net_device *dev, uint8_t *mac_addr, in6_addr v6_addr) {

  // 候補の場所は、HashテーブルのIPアドレスのハッシュがindexのもの
  const uint32_t index = in6_addr_sum(v6_addr) % ND_TABLE_SIZE;
  nd_table_entry **candidate = &nd_table[index];

  while (*candidate != nullptr) { // 連結リストの末尾までたどる
    // 途中で同じIPアドレスのエントリがあったら、そのエントリを更新する
    if (in6_addr_equals((*candidate)->v6_addr, v6_addr)) {
      memcpy((*candidate)->mac_addr, mac_addr, 6);
      (*candidate)->v6_addr = v6_addr;
      (*candidate)->dev = dev;
      return;
    }
    *candidate = (*candidate)->next;
  }

  // 連結リストの末尾に新しくエントリを作成
  *candidate = (nd_table_entry *)calloc(1, sizeof(nd_table_entry));
  memcpy((*candidate)->mac_addr, mac_addr, 6);
  (*candidate)->v6_addr = v6_addr;
  (*candidate)->dev = dev;
}

/* NDテーブルの検索 */
nd_table_entry *search_nd_table_entry(in6_addr v6_addr) {
  // 初めの候補の場所は、HashテーブルのIPアドレスのハッシュがindexのもの
  nd_table_entry *candidate = nd_table[in6_addr_sum(v6_addr) % ND_TABLE_SIZE];

  // 候補のエントリが検索しているIPアドレスの物でなかった場合、そのエントリの連結リストを調べる
  while (candidate != nullptr) {
    if (in6_addr_equals(candidate->v6_addr, v6_addr)) { // 連結リストの中に検索しているIPアドレスの物があったら
      return candidate;
    }
    candidate = candidate->next;
  }

  // 連結リストの中に見つからなかったら
  return nullptr;
}

/* NDテーブルの出力 */
void dump_nd_table_entry() {
  printf("|--------------IPv6 ADDRESS---------------|----MAC "
         "ADDRESS----|-----DEVICE------|-INDEX-|\n");

  nd_table_entry *entry_ptr;
  for (int i = 0; i < ND_TABLE_SIZE; ++i) {
    entry_ptr = nd_table[i];
    while (entry_ptr != nullptr) {
      char addr_str[INET6_ADDRSTRLEN];
      inet_ntop(AF_INET6, &entry_ptr->v6_addr, addr_str, INET6_ADDRSTRLEN);
      printf("| %39s | %14s | %15s |  %04d |\n", addr_str, mac_addr_toa(entry_ptr->mac_addr), entry_ptr->dev->name, i);
      entry_ptr = entry_ptr->next;
    }
  }
  printf("|-----------------------------------------|-------------------|-----------------|-------|\n");
}