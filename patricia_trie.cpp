#include <bit>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>

#include "patricia_trie.h"

// ビットは0から127
// 指定したビットを取得する
int in6_addr_get_bit(in6_addr address, int bit) {

  assert(bit < 128);
  assert(bit >= 0);
  int byte_index = bit / 8;
  int bit_index = 7 - (bit % 8);
  return (address.s6_addr[byte_index] >> bit_index) & 0b01;
}

// 指定したビットを0にする
int in6_addr_clear_bit(in6_addr *address, int bit) {

  assert(bit < 128);
  assert(bit >= 0);
  int byte_index = bit / 8;
  int bit_index = 7 - (bit % 8);
  address->s6_addr[byte_index] &= ~(1 << bit_index);
}

// 2つのアドレスを比べ、ビット列のマッチしてる長さを返す
int in6_addr_get_match_bits_len(in6_addr addr1, in6_addr addr2, int end_bit) {

  int start_bit = 0;
  assert(start_bit <= end_bit);
  assert(end_bit < 128);
  assert(start_bit >= 0);

  int count = 0;
  for (int i = start_bit; i <= end_bit; i++) {
    if (in6_addr_get_bit(addr1, i) != in6_addr_get_bit(addr2, i))
      return count;
    count++;
  }

  return count;
}

// IPアドレスを、プレフックス長でクリアする
in6_addr in6_addr_clear_prefix(in6_addr addr, int prefix_len) {

  for (int i = prefix_len; i < 128; i++) {
    in6_addr_clear_bit(&addr, i);
  }
  return addr;
}

// ノードを作成
patricia_node *create_patricia_node(in6_addr address, int bits_len, int is_prefix, patricia_node *parent) {

  patricia_node *node = (patricia_node *)malloc(sizeof(patricia_node));
  if (node == nullptr) {
    fprintf(stderr, "failed to malloc for patricia node\n");
    return nullptr;
  }

  node->parent = parent;
  node->left = node->right = nullptr;
  node->address = address;
  node->bits_len = bits_len;
  node->is_prefix = is_prefix;

  char addr_str[INET6_ADDRSTRLEN];
  inet_ntop(AF_INET6, &address, addr_str, sizeof(addr_str));

  LOG_TRIE("Created patricia node %s (%d)\n", addr_str, bits_len);

  return node;
}

// トライ木からIPアドレスを検索する
patricia_node *patricia_trie_search(patricia_node *root, in6_addr address) {

  LOG_TRIE("Entering patricia_trie_search\n");

  int current_bits_len = 0;
  patricia_node *current_node = root;
  patricia_node *next_node = nullptr;
  patricia_node *last_matched = nullptr;

  while (current_bits_len < 128) { // 最後までたどり着いてない間は進める

    next_node = (in6_addr_get_bit(address, current_bits_len) == 0) ? current_node->left : current_node->right; // 進めるノードの選択

    if (next_node == nullptr) {
      break;
    }

    int match_len = in6_addr_get_match_bits_len(address, next_node->address, current_bits_len + next_node->bits_len - 1);

    LOG_TRIE("Compare1: %s\n", in6_addr_to_bits_string(address, 0, 127));
    LOG_TRIE("Compare2: %s\n", in6_addr_to_bits_string(next_node->address, 0, 127));

    LOG_TRIE("Match %d / %d current: %d\n", match_len, current_bits_len + next_node->bits_len, current_bits_len);

    if (next_node->is_prefix) {
      last_matched = next_node;
    }

    if (match_len != current_bits_len + next_node->bits_len) {
      break;
    }

    current_node = next_node;
    current_bits_len += next_node->bits_len;
  }

  LOG_TRIE("Exited patricia_trie_search\n");

  return last_matched;
}

// トライ木にエントリを追加する
patricia_node *patricia_trie_insert(patricia_node *root, in6_addr address, int prefix_len, void *data_ptr) {

  LOG_TRIE("Entering patricia_trie_insert\n");

  int current_bits_len = 0;
  patricia_node *current_node = root; // ループ内で注目するノード
  patricia_node *next_node;           // ビット列と比較して次に移動するノード

  // 引数で渡されたプレフィックスをきれいにする
  address = in6_addr_clear_prefix(address, prefix_len);

  char addr_str[INET6_ADDRSTRLEN];
  inet_ntop(AF_INET6, &address, addr_str, sizeof(addr_str));

  LOG_TRIE("Inserting %s/%d\n", addr_str, prefix_len);

  // 枝を辿る
  while (true) { // ループ内では次に進むノードを決定する

    if (in6_addr_get_bit(address, current_bits_len) == 0) { // 現在のノードから次に進むノードを決める
      next_node = current_node->left;
      if (next_node == nullptr) { // ノードを作成
        current_node->left = create_patricia_node(address, prefix_len - current_bits_len, true, current_node);
        current_node->left->data = data_ptr;
        break;
      }
    } else {
      next_node = current_node->right;
      if (next_node == nullptr) { // ノードを作成
        current_node->right = create_patricia_node(address, prefix_len - current_bits_len, true, current_node);
        current_node->right->data = data_ptr;
        break;
      }
    }

    int match_len = in6_addr_get_match_bits_len(address, next_node->address, current_bits_len + next_node->bits_len - 1);

    LOG_TRIE("Match len = %d & Next bits prefix = %d (%d + %d)\n", match_len, current_bits_len + next_node->bits_len, current_bits_len, next_node->bits_len);

    if (match_len == current_bits_len + next_node->bits_len) { // 次のノードと全マッチ
      current_bits_len += next_node->bits_len;                 // next? current?
      current_node = next_node;

      // ここで、目標のノードかの判定がいる?
      if (current_bits_len == prefix_len) { // 目標だった時
        LOG_TRIE("TODO: implementation\n");
        next_node->is_prefix = true;
        next_node->data = data_ptr;
        // TODO: Set data
        break;
      }

    } else { // 次のノードと途中までマッチ

      // Current-Intermediate-Nextに分割
      int im_node_bits_len = match_len - current_bits_len;
      LOG_TRIE("Intermediate node bits len = %d\n", match_len);
      patricia_node *im_node = create_patricia_node(in6_addr_clear_prefix(address, match_len), im_node_bits_len, false, current_node); // 新しく作る

      if (current_node->left == next_node) { // Current-Intermediateをつなぎなおす
        current_node->left = im_node;
      } else {
        current_node->right = im_node;
      }

      next_node->bits_len -= im_node_bits_len;
      next_node->parent = im_node;

      LOG_TRIE("Separated %d & %d\n", im_node_bits_len, next_node->bits_len);

      if (prefix_len == current_bits_len + match_len) {
        LOG_TRIE("TODO: Implementation 2\n");
        next_node->is_prefix = true;
        next_node->data = data_ptr;
        // TODO: Set data
        break;
      }

      if (in6_addr_get_bit(address, match_len) == 0) { // Intermediate-Nextをつなぎなおす&目的のノードを作る
        im_node->right = next_node;
        im_node->left = create_patricia_node(address, prefix_len - match_len, true, im_node);
        im_node->left->data = data_ptr;
      } else {
        im_node->left = next_node;
        im_node->right = create_patricia_node(address, prefix_len - match_len, true, im_node);
        im_node->right->data = data_ptr;
      }

      break;
    }
  }

  LOG_TRIE("Exited patricia_trie_insert\n");

  return root;
}

int patricia_trie_get_prefix_len(patricia_node *node) {
  int sum = 0;
  patricia_node *current = node;
  while (true) {
    sum += current->bits_len;
    if (current->parent == nullptr) {
      break;
    }
    current = current->parent;
  }
  return sum;
}

int patricia_trie_get_distance_from_root(patricia_node *node) {
  int sum = 0;
  patricia_node *current = node;
  while (true) {
    if (current->parent == nullptr) {
      break;
    }
    sum += 1;
    current = current->parent;
  }
  return sum;
}

char in6_addr_bits_string[130];
// IPv6アドレスをビット列の文字列に変換する
char *in6_addr_to_bits_string(in6_addr addr, int start_bit, int end_bit) {

  assert(start_bit >= 0);
  assert(end_bit < 128);
  assert(start_bit <= end_bit);

  int i;
  for (i = 0; i <= end_bit - start_bit; i++) {
    in6_addr_bits_string[i] = in6_addr_get_bit(addr, start_bit + i) == 0 ? '0' : '1';
  }
  in6_addr_bits_string[i] = '\0';
  return in6_addr_bits_string;
}

// DOT言語でノードの子ノードを出力する再帰関数
void output_patricia_trie_child_node_dot(patricia_node *node, FILE *output) {
  if (node == nullptr) {
    return;
  }

  char addr_str[INET6_ADDRSTRLEN];
  inet_ntop(AF_INET6, &node->address, addr_str, sizeof(addr_str));

  // ノードを出力
  fprintf(output, "  \"%p\" [label=\"%s/%d\"];\n", (void *)node, addr_str, patricia_trie_get_prefix_len(node));

  // 左の子ノードを処理
  if (node->left) {
    LOG_TRIE("%d - %d\n", patricia_trie_get_prefix_len(node->left) - node->left->bits_len, patricia_trie_get_prefix_len(node->left) - 1);

    fprintf(output, "  \"%p\" -> \"%p\" [label=\"%s (%d)\"];\n", (void *)node, (void *)(node->left),
            in6_addr_to_bits_string(node->left->address, patricia_trie_get_prefix_len(node->left) - node->left->bits_len, patricia_trie_get_prefix_len(node->left) - 1), node->left->bits_len);
    output_patricia_trie_child_node_dot(node->left, output);
  }

  // 右の子ノードを処理
  if (node->right) {
    LOG_TRIE("%d - %d\n", patricia_trie_get_prefix_len(node->right) - node->right->bits_len, patricia_trie_get_prefix_len(node->right) - 1);

    fprintf(output, "  \"%p\" -> \"%p\" [label=\"%s (%d)\"];\n", (void *)node, (void *)(node->right),
            in6_addr_to_bits_string(node->right->address, patricia_trie_get_prefix_len(node->right) - node->right->bits_len, patricia_trie_get_prefix_len(node->right) - 1), node->right->bits_len);
    output_patricia_trie_child_node_dot(node->right, output);
  }
}

void output_subgraph_strings(patricia_node *root, FILE *output) {

  uint64_t ptrs[129][100]; // 各くプレフィックス100個まで(雑実装)
  int ptrs_cnt[129];

  for (int i = 0; i < 129; i++) {
    ptrs_cnt[i] = 0;
  }

  patricia_node *current_node;
  std::queue<patricia_node *> node_queue;
  node_queue.push(root);

  while (!node_queue.empty()) {
    current_node = node_queue.front();
    node_queue.pop();

    ptrs[patricia_trie_get_prefix_len(current_node)][ptrs_cnt[patricia_trie_get_prefix_len(current_node)]++] = (uint64_t)current_node;

    if (current_node->left != nullptr) {
      node_queue.push(current_node->left);
    }
    if (current_node->right != nullptr) {
      node_queue.push(current_node->right);
    }
  }

  for (int i = 0; i < 129; i++) {

    if (ptrs_cnt[i] == 0) {
      continue;
    }

    fprintf(output, "  subgraph { rank = same; ", i);
    for (int j = 0; j < ptrs_cnt[i]; j++) {
      fprintf(output, "\"%p\"; ", ptrs[i][j]);
    }
    fprintf(output, "};\n");
  }
}

// DOT言語で木の内容を出力する
void dump_patricia_trie_dot(patricia_node *root) {

  LOG_TRIE("Generating patricia_trie.dot\n");

  FILE *output = fopen("patricia_trie.dot", "w");
  if (output == nullptr) {
    perror("[TRIE] Failed to open file");
    return;
  }

  fprintf(output, "digraph PatriciaTrie {\n");

  // output_subgraph_strings(root, output); // 同じプレフィックスのノードの高さを合わせる

  output_patricia_trie_child_node_dot(root, output);

  fprintf(output, "}\n");

  fclose(output);

  LOG_TRIE("Generated patricia_trie.dot\n");
}

// テキストでエントリを出力する
void dump_patricia_trie_text(patricia_node *root) {

  patricia_node *current_node;
  std::queue<patricia_node *> node_queue;
  node_queue.push(root);

  while (!node_queue.empty()) {
    current_node = node_queue.front();
    node_queue.pop();

    char ipv6_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &(current_node->address), ipv6_str, INET6_ADDRSTRLEN);

    LOG_TRIE("%s\n", in6_addr_to_bits_string(current_node->address, 0, 127));
    LOG_TRIE("%s/%d (%d) %d nodes - %s\n", ipv6_str, patricia_trie_get_prefix_len(current_node), current_node->bits_len, patricia_trie_get_distance_from_root(current_node),
             current_node->is_prefix ? "prefix" : "not prefix");

    if (current_node->left != nullptr) {
      node_queue.push(current_node->left);
    }
    if (current_node->right != nullptr) {
      node_queue.push(current_node->right);
    }
  }
}