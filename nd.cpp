#include "nd.h"

#include "net.h"
#include "utils.h"

// TODO タイマーを追加

/**
 * NDテーブル
 * グローバル変数にテーブルを保持
 */
nd_table_entry nd_table[ND_TABLE_SIZE];


void init_nd_table(){
    for (int i = 0; i < ND_TABLE_SIZE; i++) {
        memset(&nd_table[i], 0x00, sizeof(nd_table[i]));
    }
}

/**
 * NDテーブルにエントリの追加と更新
 * @param dev
 * @param mac_addr
 * @param v6_addr
 */
void add_nd_table_entry(net_device *dev, uint8_t *mac_addr, in6_addr v6_addr) {

    // 候補の場所は、HashテーブルのIPアドレスのハッシュがindexのもの
    const uint32_t index = in6_addr_sum(v6_addr) % ND_TABLE_SIZE;
    nd_table_entry *candidate = &nd_table[index];
    printf("FF2\n");

    nd_table_entry candidate2 = nd_table[index];

    //printf("%p %p %d\n", candidate2, candidate2.v6addr, index);

    // テーブルに入れられるか確認
    if (in6_addr_sum(candidate->v6addr) == 0 or in6_addr_equals(candidate->v6addr, v6_addr)) { // 初めの候補の場所に入れられるとき
        // エントリをセット
        memcpy(candidate->mac_addr, mac_addr, 6);
        candidate->v6addr = v6_addr;
        candidate->dev = dev;
        return;
    }
    printf("000\n");

    // 入れられなかった場合は、その候補にあるエントリに連結する
    while (candidate->next != nullptr) { // 連結リストの末尾までたどる
        candidate = candidate->next;
        // 途中で同じIPアドレスのエントリがあったら、そのエントリを更新する
        if (in6_addr_equals(candidate->v6addr, v6_addr)) {
            memcpy(candidate->mac_addr, mac_addr, 6);
            candidate->v6addr = v6_addr;
            candidate->dev = dev;
            return;
        }
    }
    printf("000\n");

    // 連結リストの末尾に新しくエントリを作成
    candidate->next = (nd_table_entry *)calloc(1, sizeof(nd_table_entry));
    memcpy(candidate->next->mac_addr, mac_addr, 6);
    candidate->next->v6addr = v6_addr;
    candidate->next->dev = dev;
}

/**
 * NDテーブルの検索
 * @param ip_addr
 */
nd_table_entry *search_nd_table_entry(in6_addr v6_addr) {
    // 初めの候補の場所は、HashテーブルのIPアドレスのハッシュがindexのもの
    nd_table_entry *candidate = &nd_table[in6_addr_sum(v6_addr) % ND_TABLE_SIZE];

    if (in6_addr_equals(candidate->v6addr, v6_addr)) { // 候補のエントリが検索しているIPアドレスの物だったら
        return candidate;
    } else if (in6_addr_sum(candidate->v6addr) == 0) { // 候補のエントリが登録されていなかったら
        return nullptr;
    }
    // 候補のエントリが検索しているIPアドレスの物でなかった場合、そのエントリの連結リストを調べる
    while (candidate->next != nullptr) {
        candidate = candidate->next;
        if (in6_addr_equals(candidate->v6addr, v6_addr)) { // 連結リストの中に検索しているIPアドレスの物があったら
            return candidate;
        }
    }

    // 連結リストの中に見つからなかったら
    return nullptr;
}

/**
 * NDテーブルの出力
 */
void dump_nd_table_entry() {
    printf("|--------------IPv6 ADDRESS---------------|----MAC "
           "ADDRESS----|----DEVICE-----|-INDEX-|\n");
    for (int i = 0; i < ND_TABLE_SIZE; ++i) {
        if (in6_addr_sum(nd_table[i].v6addr) == 0) {
            continue;
        }
        // エントリの連結リストを順に出力する
        for (nd_table_entry *entry = &nd_table[i]; entry;
             entry = entry->next) {

            char addr_str[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &entry->v6addr, addr_str, INET6_ADDRSTRLEN);

            printf("| %37s | %14s | %13s |  %04d |\n", addr_str, mac_addr_toa(entry->mac_addr), entry->dev->name, i);
        }
    }
    printf("|-----------------------------------------|-------------------|---------------|-------|"
           "\n");
}