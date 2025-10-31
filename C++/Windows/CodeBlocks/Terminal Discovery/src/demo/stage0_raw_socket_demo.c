#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <linux/filter.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_INTERVAL_SEC 120
#define BUFFER_SIZE 2048

static atomic_bool g_running;

struct receiver_args {
    int fd;
};

struct sender_args {
    char iface[IFNAMSIZ];
    struct in_addr target_ip;
    int interval_sec;
};

struct send_context {
    int fd;
    int ifindex;
    uint8_t mac[ETH_ALEN];
    struct in_addr ipv4;
};

static void handle_signal(int signo) {
    (void)signo;
    atomic_store_explicit(&g_running, false, memory_order_relaxed);
}

static void format_mac(const uint8_t *mac, char *buf, size_t buf_len) {
    if (!mac || !buf || buf_len < 18) {
        return;
    }
    snprintf(buf, buf_len, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void format_ip(const uint8_t *ip, char *buf, size_t buf_len) {
    if (!ip || !buf) {
        return;
    }
    if (!inet_ntop(AF_INET, ip, buf, (socklen_t)buf_len)) {
        snprintf(buf, buf_len, "0.0.0.0");
    }
}

static int attach_arp_filter(int fd) {
    struct sock_filter code[] = {
        {BPF_LD | BPF_H | BPF_ABS, 0, 0, 12},
        {BPF_JMP | BPF_JEQ | BPF_K, 0, 1, htons(ETH_P_ARP)},
        {BPF_RET | BPF_K, 0, 0, 0xFFFF},
        {BPF_RET | BPF_K, 0, 0, 0},
    };
    struct sock_fprog program = {
        .len = (unsigned short)(sizeof(code) / sizeof(code[0])),
        .filter = code,
    };
    if (setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, &program, sizeof(program)) < 0) {
        perror("setsockopt(SO_ATTACH_FILTER)");
        return -1;
    }
    return 0;
}

static int create_recv_socket(const char *iface) {
    int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ARP));
    if (fd < 0) {
        perror("socket(AF_PACKET)");
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);

    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl(SIOCGIFINDEX)");
        close(fd);
        return -1;
    }

    struct sockaddr_ll addr;
    memset(&addr, 0, sizeof(addr));
    addr.sll_family = AF_PACKET;
    addr.sll_protocol = htons(ETH_P_ARP);
    addr.sll_ifindex = ifr.ifr_ifindex;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind(AF_PACKET)");
        close(fd);
        return -1;
    }

    if (attach_arp_filter(fd) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static bool init_send_context(const char *iface, struct send_context *ctx) {
    int raw_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ARP));
    if (raw_fd < 0) {
        perror("socket(AF_PACKET)");
        return false;
    }

    if (setsockopt(raw_fd, SOL_SOCKET, SO_BINDTODEVICE, iface, strlen(iface)) < 0) {
        perror("setsockopt(SO_BINDTODEVICE)");
    }

    int ioctl_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ioctl_fd < 0) {
        perror("socket(AF_INET)");
        close(raw_fd);
        return false;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);

    if (ioctl(ioctl_fd, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl(SIOCGIFINDEX)");
        close(ioctl_fd);
        close(raw_fd);
        return false;
    }
    ctx->ifindex = ifr.ifr_ifindex;

    if (ioctl(ioctl_fd, SIOCGIFHWADDR, &ifr) < 0) {
        perror("ioctl(SIOCGIFHWADDR)");
        close(ioctl_fd);
        close(raw_fd);
        return false;
    }
    memcpy(ctx->mac, ifr.ifr_hwaddr.sa_data, ETH_ALEN);

    if (ioctl(ioctl_fd, SIOCGIFADDR, &ifr) == 0) {
        struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
        ctx->ipv4 = sin->sin_addr;
    } else {
        perror("ioctl(SIOCGIFADDR)");
        ctx->ipv4.s_addr = 0;
    }

    close(ioctl_fd);
    ctx->fd = raw_fd;
    return true;
}

static bool send_arp_request(const struct send_context *ctx, const struct in_addr *target_ip) {
    if (ctx->fd < 0 || ctx->ifindex == 0) {
        fprintf(stderr, "[ERROR] 无效的发送上下文\n");
        return false;
    }
    if (ctx->ipv4.s_addr == 0) {
        fprintf(stderr, "[WARN] 接口缺少 IPv4 地址，跳过 ARP 请求\n");
        return false;
    }

    uint8_t frame[sizeof(struct ethhdr) + sizeof(struct ether_arp)] = {0};
    struct ethhdr *eth = (struct ethhdr *)frame;
    struct ether_arp *arp = (struct ether_arp *)(frame + sizeof(struct ethhdr));

    memset(eth->h_dest, 0xFF, ETH_ALEN);
    memcpy(eth->h_source, ctx->mac, ETH_ALEN);
    eth->h_proto = htons(ETH_P_ARP);

    arp->ea_hdr.ar_hrd = htons(ARPHRD_ETHER);
    arp->ea_hdr.ar_pro = htons(ETH_P_IP);
    arp->ea_hdr.ar_hln = ETH_ALEN;
    arp->ea_hdr.ar_pln = 4;
    arp->ea_hdr.ar_op = htons(ARPOP_REQUEST);

    memcpy(arp->arp_sha, ctx->mac, ETH_ALEN);
    memcpy(arp->arp_spa, &ctx->ipv4.s_addr, sizeof(arp->arp_spa));
    memset(arp->arp_tha, 0, ETH_ALEN);
    memcpy(arp->arp_tpa, &target_ip->s_addr, sizeof(arp->arp_tpa));

    struct sockaddr_ll addr;
    memset(&addr, 0, sizeof(addr));
    addr.sll_family = AF_PACKET;
    addr.sll_protocol = htons(ETH_P_ARP);
    addr.sll_ifindex = ctx->ifindex;
    addr.sll_halen = ETH_ALEN;
    memset(addr.sll_addr, 0xFF, ETH_ALEN);

    ssize_t sent = sendto(ctx->fd, frame, sizeof(frame), 0, (struct sockaddr *)&addr, sizeof(addr));
    if (sent < 0) {
        perror("sendto(ARP)");
        return false;
    }

    char target_str[INET_ADDRSTRLEN];
    format_ip((const uint8_t *)&target_ip->s_addr, target_str, sizeof(target_str));
    fprintf(stderr, "[INFO] 已发送 ARP 探测目标 %s\n", target_str);
    return true;
}

static void *receiver_thread(void *arg) {
    struct receiver_args *args = (struct receiver_args *)arg;
    uint8_t buffer[BUFFER_SIZE];

    while (atomic_load_explicit(&g_running, memory_order_relaxed)) {
        struct pollfd pfd = {
            .fd = args->fd,
            .events = POLLIN,
            .revents = 0,
        };

        int ready = poll(&pfd, 1, 1000);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            break;
        }
        if (ready == 0 || !(pfd.revents & POLLIN)) {
            continue;
        }

        struct sockaddr_ll addr;
        socklen_t addr_len = sizeof(addr);
        ssize_t received = recvfrom(args->fd, buffer, sizeof(buffer), 0,
                                    (struct sockaddr *)&addr, &addr_len);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("recvfrom");
            continue;
        }
        if (received < (ssize_t)(sizeof(struct ethhdr) + sizeof(struct ether_arp))) {
            continue;
        }

        struct ethhdr *eth = (struct ethhdr *)buffer;
        if (ntohs(eth->h_proto) != ETH_P_ARP) {
            continue;
        }

        struct ether_arp *arp = (struct ether_arp *)(buffer + sizeof(struct ethhdr));
        uint16_t opcode = ntohs(arp->ea_hdr.ar_op);
        char mac_str[18];
        char sender_ip[INET_ADDRSTRLEN];
        char target_ip[INET_ADDRSTRLEN];
        format_mac(arp->arp_sha, mac_str, sizeof(mac_str));
        format_ip(arp->arp_spa, sender_ip, sizeof(sender_ip));
        format_ip(arp->arp_tpa, target_ip, sizeof(target_ip));

        printf("[ARP] opcode=%s sender=%s/%s target=%s\n",
               opcode == ARPOP_REQUEST ? "REQUEST" :
               (opcode == ARPOP_REPLY ? "REPLY" : "OTHER"),
               mac_str,
               sender_ip,
               target_ip);
    }

    close(args->fd);
    return NULL;
}

static void *sender_thread(void *arg) {
    struct sender_args *args = (struct sender_args *)arg;
    struct send_context ctx;
    memset(&ctx, 0, sizeof(ctx));

    if (!init_send_context(args->iface, &ctx)) {
        fprintf(stderr, "[ERROR] 初始化发送上下文失败\n");
        return NULL;
    }

    int interval = args->interval_sec > 0 ? args->interval_sec : DEFAULT_INTERVAL_SEC;

    while (atomic_load_explicit(&g_running, memory_order_relaxed)) {
        send_arp_request(&ctx, &args->target_ip);
        for (int i = 0; i < interval && atomic_load_explicit(&g_running, memory_order_relaxed); ++i) {
            sleep(1);
        }
    }

    close(ctx.fd);
    return NULL;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
            "用法: sudo %s [--rx-iface eth0] [--tx-iface vlan1]"
            " [--target-ip 192.168.1.1] [--interval 120]\n",
            prog);
}

int main(int argc, char *argv[]) {
    char rx_iface[IFNAMSIZ] = "eth0";
    char tx_iface[IFNAMSIZ] = "vlan1";
    char target_ip_str[INET_ADDRSTRLEN] = "192.168.1.1";
    int interval_sec = DEFAULT_INTERVAL_SEC;

    const struct option long_opts[] = {
        {"rx-iface", required_argument, NULL, 'r'},
        {"tx-iface", required_argument, NULL, 't'},
        {"target-ip", required_argument, NULL, 'd'},
        {"interval", required_argument, NULL, 'i'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "r:t:d:i:h", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'r':
            strncpy(rx_iface, optarg, IFNAMSIZ - 1);
            rx_iface[IFNAMSIZ - 1] = '\0';
            break;
        case 't':
            strncpy(tx_iface, optarg, IFNAMSIZ - 1);
            tx_iface[IFNAMSIZ - 1] = '\0';
            break;
        case 'd':
            strncpy(target_ip_str, optarg, sizeof(target_ip_str) - 1);
            target_ip_str[sizeof(target_ip_str) - 1] = '\0';
            break;
        case 'i':
            interval_sec = atoi(optarg);
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    struct in_addr target_ip;
    if (inet_pton(AF_INET, target_ip_str, &target_ip) != 1) {
        fprintf(stderr, "[ERROR] 无效的目标 IP: %s\n", target_ip_str);
        return 1;
    }

    int recv_fd = create_recv_socket(rx_iface);
    if (recv_fd < 0) {
        return 1;
    }

    atomic_init(&g_running, true);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    struct receiver_args rx_args = {
        .fd = recv_fd,
    };

    struct sender_args tx_args;
    memset(&tx_args, 0, sizeof(tx_args));
    strncpy(tx_args.iface, tx_iface, IFNAMSIZ - 1);
    tx_args.target_ip = target_ip;
    tx_args.interval_sec = interval_sec;

    pthread_t rx_thread;
    pthread_t tx_thread;

    if (pthread_create(&rx_thread, NULL, receiver_thread, &rx_args) != 0) {
        perror("pthread_create(receiver)");
        close(recv_fd);
        return 1;
    }

    if (pthread_create(&tx_thread, NULL, sender_thread, &tx_args) != 0) {
        perror("pthread_create(sender)");
        atomic_store_explicit(&g_running, false, memory_order_relaxed);
        pthread_join(rx_thread, NULL);
        return 1;
    }

    pthread_join(rx_thread, NULL);
    atomic_store_explicit(&g_running, false, memory_order_relaxed);
    pthread_join(tx_thread, NULL);

    printf("程序已退出\n");
    return 0;
}
