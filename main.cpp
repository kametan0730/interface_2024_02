#include <cstdint>
#include <fcntl.h>
#include <ifaddrs.h>
#include <linux/if_ether.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>
#include <sys/epoll.h>

#include "patricia_trie.h"
#include "config.h"
#include "ethernet.h"
#include "ipv6.h"
#include "log.h"
#include "nd.h"
#include "net.h"
#include "utils.h"

/*  無視するネットワークインターフェースたち  */
#define IGNORE_INTERFACES  { "lo", "bond0", "dummy0", "tunl0", "sit0" }

/* 無視するデバイスかどうかを返す */
bool is_ignore_interface(const char *ifname) {
    char ignore_interfaces[][IF_NAMESIZE] = IGNORE_INTERFACES;
    for (int i = 0; i < sizeof(ignore_interfaces) / IF_NAMESIZE; i++) {
        if (strcmp(ignore_interfaces[i], ifname) == 0) {
            return true;
        }
    }
    return false;
}

/* インターフェース名からデバイスを探す */
net_device *get_net_device_by_name(const char *name) {
    net_device *dev;
    for (dev = net_dev_list; dev; dev = dev->next) {
        if (strcmp(dev->name, name) == 0) {
            return dev;
        }
    }
    return nullptr;
}

/* 設定する */
void configure() {

    in6_addr addr6_to_host1;
    inet_pton(AF_INET6, "2001:db8:0:1001::1", &addr6_to_host1);

    configure_ipv6_address(get_net_device_by_name("router1-host1"), addr6_to_host1, 64);

    in6_addr addr6_to_router2;
    inet_pton(AF_INET6, "2001:db8:0:1000::1", &addr6_to_router2);

    configure_ipv6_address(get_net_device_by_name("router1-router2"), addr6_to_router2, 64);

    in6_addr addr6_net;
    inet_pton(AF_INET6, "2001:db8:0:1002::", &addr6_net);

    in6_addr addr6_nh;
    inet_pton(AF_INET6, "2001:db8:0:1000::2", &addr6_nh);

    configure_ipv6_net_route(addr6_net, 64, addr6_nh);

    uint8_t mac_addr_host1[6] = {0x96, 0xe0, 0x07, 0xc6, 0x7f , 0xe1};
    in6_addr addr6_host1;
    inet_pton(AF_INET6, "2001:db8:0:1001::2", &addr6_host1);
    // ip -6 neigh add 2001:db8:0:1001::1 lladdr 9e:b7:96:aa:4a:8a dev host1-router1

    add_nd_table_entry(get_net_device_by_name("router1-host1"), mac_addr_host1, addr6_host1);
}

/* 宣言のみ */
int net_device_transmit(struct net_device *dev, uint8_t *buffer, size_t len);
int net_device_poll(net_device *dev);

/* デバイスのプラットフォーム依存のデータ */
struct net_device_data {
    int fd; // socketのfile descriptor
};

/* エントリポイント */
int main() {
    struct ifreq ifr {};
    struct ifaddrs *addrs;

    // ネットワークインターフェースを情報を取得
    getifaddrs(&addrs);

    for (ifaddrs *tmp = addrs; tmp; tmp = tmp->ifa_next) {
        if (tmp->ifa_addr && tmp->ifa_addr->sa_family == AF_PACKET) {
            // ioctlでコントロールするインターフェースを設定
            memset(&ifr, 0, sizeof(ifr));
            strcpy(ifr.ifr_name, tmp->ifa_name);

            // 無視するインターフェースか確認
            if (is_ignore_interface(tmp->ifa_name)) {
                LOG_INFO("skipped to enable interface %s\n", tmp->ifa_name);
                continue;
            }

            // Socketをオープン
            int sock = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
            if (sock == -1) {
                LOG_ERROR("failed open socket: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }

            // インターフェースのインデックスを取得
            if (ioctl(sock, SIOCGIFINDEX, &ifr) == -1) {
                LOG_ERROR("failed to ioctl SIOCGIFINDEX: %s\n", strerror(errno));
                close(sock);
                exit(EXIT_FAILURE);
            }

            // socketにインターフェースをbindする
            sockaddr_ll addr{};
            memset(&addr, 0x00, sizeof(addr));
            addr.sll_family = AF_PACKET;
            addr.sll_protocol = htons(ETH_P_ALL);
            addr.sll_ifindex = ifr.ifr_ifindex;
            if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
                LOG_ERROR("failed to bind: %s\n", strerror(errno));
                close(sock);
                exit(EXIT_FAILURE);
            }

            // インターフェースのMACアドレスを取得
            if (ioctl(sock, SIOCGIFHWADDR, &ifr) != 0) {
                LOG_ERROR("failed to ioctl SIOCGIFHWADDR %s\n", strerror(errno));
                close(sock);
                continue;
            }

            /* net_device構造体を作成 */

            // net_deviceの領域と、net_device_dataの領域を確保する
            auto *dev = (net_device *)calloc(1, sizeof(net_device) + sizeof(net_device_data));
            dev->ops.transmit = net_device_transmit; // 送信用の関数を設定
            dev->ops.poll = net_device_poll; // 受信用の関数を設定

            // net_deviceにインターフェース名をセット
            strcpy(dev->name, tmp->ifa_name);
            // net_deviceにMACアドレスをセット
            memcpy(dev->mac_addr, &ifr.ifr_hwaddr.sa_data[0], 6);
            ((net_device_data *)dev->data)->fd = sock;

            LOG_INFO("created device %s socket %d address %s \n", dev->name, sock, mac_addr_toa(dev->mac_addr));

            // net_deviceの連結リストに連結させる
            net_device *next;
            next = net_dev_list;
            net_dev_list = dev;
            dev->next = next;

            /* ノンブロッキングに設定 */
            // File descriptorのflagを取得
            int val = fcntl(sock, F_GETFL, 0);
            // Non blockingのビットをセット
            fcntl(sock, F_SETFL, val | O_NONBLOCK);
        }
    }
    // 確保されていたメモリを解放
    freeifaddrs(addrs);

    // 1つも有効化されたインターフェースをが無かったら終了
    if (net_dev_list == nullptr) {
        LOG_ERROR("No interface is enabled!\n");
        exit(EXIT_FAILURE);
    }

    in6_addr root_addr;
    memset(&root_addr, 0x00, sizeof(root_addr));
    ipv6_fib = create_patricia_node(root_addr, 0, false, nullptr);

    // ネットワーク設定の投入
    configure();

#ifdef ENABLE_COMMAND
    // 入力時にバッファリングせずにすぐ受け取る設定
    termios attr{};
    tcgetattr(0, &attr);
    attr.c_lflag &= ~ICANON;
    attr.c_cc[VTIME] = 0;
    attr.c_cc[VMIN] = 1;
    tcsetattr(0, TCSANOW, &attr);
    fcntl(0, F_SETFL, O_NONBLOCK); // 標準入力にノンブロッキングの設定
#endif

    int epoll_fd;
    epoll_event ev, ev_ret[MAX_INTERFACES+1];

    // epollの初期化
    epoll_fd = epoll_create(MAX_INTERFACES+1);
    if (epoll_fd < 0) {
        perror("failed to epoll_create");
        return 1;
    }

    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = STDIN_FILENO;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) != 0) {
        perror("failed to epoll_ctl");
        return 1;
    }

    for (net_device *dev = net_dev_list; dev; dev = dev->next) {
        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN;
        ev.data.fd = ((net_device_data *)dev->data)->fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ((net_device_data *)dev->data)->fd, &ev) != 0) {
            perror("failed to epoll_ctl");
            return 1;
        }
        dev->ops.poll(dev);
    }

    // デバイスから通信を受信
    int nfds;
    while(true){
        nfds = epoll_wait(epoll_fd, ev_ret, MAX_INTERFACES+1, -1);
        if (nfds <= 0) {
            perror("failed to epoll_wait");
            return 1;
        }

        for (int i = 0; i < nfds; i++) {
            if (ev_ret[i].data.fd == STDIN_FILENO) {
                int input = getchar(); // 入力を受け取る
                if (input != -1) {     // 入力があったら
                    printf("\n");
                    if (input == 'a') {
                        dump_nd_table_entry();
                    } else if (input == 'r')
                        dump_ipv6_route(ipv6_fib);
                    else if (input == 'q')
                        goto exit_loop;
                }
            }

            for (net_device *dev = net_dev_list; dev; dev = dev->next) {
                if (ev_ret[i].data.fd == ((net_device_data *)dev->data)->fd) {
                    dev->ops.poll(dev);
                }
            }
        }
    }

exit_loop:

    printf("Goodbye!\n");
    return 0;
}

/**
 * ネットデバイスの送信処理
 * @param dev 送信に使用するデバイス
 * @param buffer 送信するバッファ
 * @param len バッファの長さ
 */
int net_device_transmit(struct net_device *dev, uint8_t *buffer, size_t len) {
    // Socketを通して送信
    send(((net_device_data *)dev->data)->fd, buffer, len, 0);
    return 0;
}

/**
 * ネットワークデバイスの受信処理
 * @param dev 受信を試みるデバイス
 */
int net_device_poll(net_device *dev) {
    uint8_t buffer[1550];
    // Socketから受信
    ssize_t n = recv(((net_device_data *)dev->data)->fd, buffer, sizeof(buffer), 0);
    if (n == -1) {
        if (errno == EAGAIN) { // 受け取るデータが無い場合
            return 0;
        } else {
            return -1; // 他のエラーなら
        }
    }
    // 受信したデータをイーサネットに送る
    ethernet_input(dev, buffer, n);

    return 0;
}
