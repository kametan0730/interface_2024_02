#ifndef CURO_PATRICIA_TRIE_H
#define CURO_PATRICIA_TRIE_H

#include <arpa/inet.h>

#define DEBUG_TRIE 0

#if DEBUG_TRIE > 0
#define LOG_TRIE(...)                                                                                                                                                                                  \
    printf("[TRIE] ");                                                                                                                                                                                 \
    printf(__VA_ARGS__)
#else
#define LOG_TRIE(...)
#endif

struct patricia_node {
    patricia_node *left, *right, *parent;
    in6_addr address; // IPv6アドレス
    int bits_len;     // このノードで比較するビットの位置
    int is_prefix;    // このノードがプレフィックスを表すかどうか
    void *data;
};

int in6_addr_get_bit(in6_addr address, int bit);
int in6_addr_get_match_bits_len(in6_addr addr1, in6_addr addr2, int end_bit);

char *in6_addr_to_bits_string(in6_addr addr, int start_bit, int end_bit);

in6_addr in6_addr_clear_prefix(in6_addr addr, int prefix_len);

int patricia_trie_get_prefix_len(patricia_node *node);

patricia_node *create_patricia_node(in6_addr address, int bits_len, int is_prefix, patricia_node *parent);
patricia_node *patricia_trie_search(patricia_node *root, in6_addr address);
patricia_node *patricia_trie_insert(patricia_node *root, in6_addr address, int prefix_len, void *data_ptr);
void dump_patricia_trie_dot(patricia_node *root);
void dump_patricia_trie_text(patricia_node *root);

#endif