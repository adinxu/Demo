#define _GNU_SOURCE
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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_RX_IFACE "eth0"
#define DEFAULT_INTERVAL_MS 100
#define BUFFER_SIZE 2048
#define HEX_DUMP_BYTES 64

static volatile sig_atomic_t g_running = 0;

struct options {
    char rx_iface[IFNAMSIZ];
    char tx_iface[IFNAMSIZ];
    uint8_t src_mac[ETH_ALEN];
    bool src_mac_set;
    struct in_addr src_ip;
    bool src_ip_set;
    uint8_t dst_mac[ETH_ALEN];
    struct in_addr dst_ip;
    bool dst_ip_set;
    int interval_ms;
    unsigned long long count;
    int timeout_sec;
    bool verbose;
    bool rx_enabled;
    bool tx_enabled;
};

struct receiver_args {
    int fd;
    int timeout_sec;
    bool verbose;
};

struct sender_args {
    char iface[IFNAMSIZ];
    uint8_t src_mac[ETH_ALEN];
    bool src_mac_set;
    struct in_addr src_ip;
    bool src_ip_set;
    uint8_t dst_mac[ETH_ALEN];
    struct in_addr dst_ip;
    int interval_ms;
    unsigned long long count;
};

struct send_context {
    int fd;
    int ifindex;
    uint8_t mac[ETH_ALEN];
    struct in_addr ipv4;
};

struct vlan_header {
    uint16_t tci;
    uint16_t encapsulated_proto;
} __attribute__((packed));

static void handle_signal(int signo) {
    (void)signo;
    g_running = 0;
}

static void format_mac(const uint8_t *mac, char *buf, size_t len) {
    if (!mac || !buf || len < 18) {
        return;
    }
    snprintf(buf, len, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static bool parse_mac(const char *str, uint8_t out[ETH_ALEN]) {
    unsigned int values[ETH_ALEN];
    if (sscanf(str, "%x:%x:%x:%x:%x:%x",
               &values[0], &values[1], &values[2],
               &values[3], &values[4], &values[5]) != 6) {
        return false;
    }
    for (size_t i = 0; i < ETH_ALEN; ++i) {
        if (values[i] > 0xFF) {
            return false;
        }
        out[i] = (uint8_t)values[i];
    }
    return true;
}

static void format_ip(const uint8_t *ip, char *buf, size_t len) {
    if (!inet_ntop(AF_INET, ip, buf, (socklen_t)len)) {
        snprintf(buf, len, "0.0.0.0");
    }
}

static void hex_dump(const uint8_t *data, size_t len) {
    size_t count = len < HEX_DUMP_BYTES ? len : HEX_DUMP_BYTES;
    for (size_t i = 0; i < count; ++i) {
        printf("%02x%s", data[i], (i + 1) % 16 == 0 ? "\n" : " ");
    }
    if (count % 16 != 0) {
        printf("\n");
    }
}

static int attach_arp_filter(int fd) {
    /* Accept inbound ARP and single-tag VLAN ARP frames. */
    struct sock_filter code[] = {
        {BPF_LD | BPF_B | BPF_ABS, 0, 0, SKF_AD_OFF + SKF_AD_PKTTYPE},        /* packet direction */
        {BPF_JMP | BPF_JEQ | BPF_K, 3, 0, PACKET_HOST},                       /* accept host */
        {BPF_JMP | BPF_JEQ | BPF_K, 2, 0, PACKET_BROADCAST},                  /* accept broadcast */
        {BPF_JMP | BPF_JEQ | BPF_K, 1, 0, PACKET_MULTICAST},                  /* accept multicast */
        {BPF_RET | BPF_K, 0, 0, 0},                                           /* reject others */
        {BPF_LD | BPF_H | BPF_ABS, 0, 0, 12},                        /* EtherType */
        {BPF_JMP | BPF_JEQ | BPF_K, 6, 0, htons(ETH_P_ARP)},         /* direct ARP */
        {BPF_JMP | BPF_JEQ | BPF_K, 2, 0, htons(ETH_P_8021Q)},       /* 802.1Q */
        {BPF_JMP | BPF_JEQ | BPF_K, 1, 0, htons(ETH_P_8021AD)},      /* 802.1ad */
        {BPF_RET | BPF_K, 0, 0, 0},                                  /* reject */
        {BPF_LD | BPF_H | BPF_ABS, 0, 0, 16},                        /* inner EtherType */
        {BPF_JMP | BPF_JEQ | BPF_K, 1, 0, htons(ETH_P_ARP)},         /* inner ARP */
        {BPF_RET | BPF_K, 0, 0, 0},                                  /* reject */
        {BPF_RET | BPF_K, 0, 0, 0xFFFF},                             /* accept */
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
    int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd < 0) {
        perror("socket(AF_PACKET)");
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", iface);

    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl(SIOCGIFINDEX)");
        close(fd);
        return -1;
    }

    struct sockaddr_ll addr;
    memset(&addr, 0, sizeof(addr));
    addr.sll_family = AF_PACKET;
    addr.sll_protocol = htons(ETH_P_ALL);
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

    int enable_aux = 1;
    if (setsockopt(fd, SOL_PACKET, PACKET_AUXDATA, &enable_aux, sizeof(enable_aux)) < 0) {
        perror("setsockopt(PACKET_AUXDATA)");
        close(fd);
        return -1;
    }

    return fd;
}

static bool init_send_context(const char *iface,
                              const uint8_t *override_mac,
                              bool mac_set,
                              const struct in_addr *override_ip,
                              bool ip_set,
                              struct send_context *ctx) {
    int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ARP));
    if (fd < 0) {
        perror("socket(AF_PACKET)");
        return false;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, iface, strlen(iface)) < 0) {
        perror("setsockopt(SO_BINDTODEVICE)");
        /* continue even if binding fails so long as the interface index resolves */
    }

    int ioctl_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ioctl_fd < 0) {
        perror("socket(AF_INET)");
        close(fd);
        return false;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", iface);

    if (ioctl(ioctl_fd, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl(SIOCGIFINDEX)");
        close(ioctl_fd);
        close(fd);
        return false;
    }
    ctx->ifindex = ifr.ifr_ifindex;

    if (mac_set) {
        memcpy(ctx->mac, override_mac, ETH_ALEN);
    } else if (ioctl(ioctl_fd, SIOCGIFHWADDR, &ifr) == 0) {
        memcpy(ctx->mac, (uint8_t *)ifr.ifr_hwaddr.sa_data, ETH_ALEN);
    } else {
        perror("ioctl(SIOCGIFHWADDR)");
        close(ioctl_fd);
        close(fd);
        return false;
    }

    if (ip_set) {
        ctx->ipv4 = *override_ip;
    } else if (ioctl(ioctl_fd, SIOCGIFADDR, &ifr) == 0) {
        ctx->ipv4 = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;
    } else {
        perror("ioctl(SIOCGIFADDR)");
        ctx->ipv4.s_addr = 0;
    }

    close(ioctl_fd);
    ctx->fd = fd;
    return true;
}

static bool send_arp_request(const struct send_context *ctx,
                             const uint8_t dst_mac[ETH_ALEN],
                             const struct in_addr *target_ip) {
    if (ctx->fd < 0 || ctx->ifindex <= 0) {
        fprintf(stderr, "[ERROR] invalid send context\n");
        return false;
    }

    if (ctx->ipv4.s_addr == 0) {
        fprintf(stderr, "[WARN] interface has no IPv4 address, skipping ARP request\n");
        return false;
    }

    uint8_t frame[sizeof(struct ethhdr) + sizeof(struct ether_arp)] = {0};
    struct ethhdr *eth = (struct ethhdr *)frame;
    struct ether_arp *arp = (struct ether_arp *)(frame + sizeof(struct ethhdr));

    memcpy(eth->h_dest, dst_mac, ETH_ALEN);
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
    memcpy(addr.sll_addr, dst_mac, ETH_ALEN);

    ssize_t sent = sendto(ctx->fd, frame, sizeof(frame), 0,
                          (struct sockaddr *)&addr, sizeof(addr));
    if (sent < 0) {
        perror("sendto(ARP)");
        return false;
    }

    char ip_str[INET_ADDRSTRLEN];
    format_ip((const uint8_t *)&target_ip->s_addr, ip_str, sizeof(ip_str));
    fprintf(stderr, "[INFO] ARP probe sent to %s\n", ip_str);
    return true;
}

static void sleep_ms(int millis) {
    if (millis <= 0) {
        return;
    }
    struct timespec req = {
        .tv_sec = millis / 1000,
        .tv_nsec = (millis % 1000) * 1000000L,
    };
    while (nanosleep(&req, &req) < 0 && errno == EINTR) {
    }
}

static void *receiver_thread(void *arg) {
    struct receiver_args *args = (struct receiver_args *)arg;
    uint8_t buffer[BUFFER_SIZE];
    time_t last_activity = time(NULL);

    while (g_running) {
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

        if (ready == 0) {
            if (args->timeout_sec > 0) {
                time_t now = time(NULL);
                if (now - last_activity >= args->timeout_sec) {
                    fprintf(stderr, "[INFO] receiver idle timeout reached\n");
                    break;
                }
            }
            continue;
        }

        if (!(pfd.revents & POLLIN)) {
            continue;
        }

        struct sockaddr_ll addr;
        uint8_t control[CMSG_SPACE(sizeof(struct tpacket_auxdata))];
        struct iovec iov = {
            .iov_base = buffer,
            .iov_len = sizeof(buffer),
        };
        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_name = &addr;
        msg.msg_namelen = sizeof(addr);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);

        ssize_t received = recvmsg(args->fd, &msg, 0);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("recvmsg");
            continue;
        }
        if (received < (ssize_t)(sizeof(struct ethhdr))) {
            continue;
        }

        const uint8_t *payload = buffer;
        size_t offset = sizeof(struct ethhdr);
        struct ethhdr eth_local;
        memcpy(&eth_local, buffer, sizeof(eth_local));
        uint16_t ether_type = ntohs(eth_local.h_proto);
        int vlan_id = -1;

        if (msg.msg_controllen >= sizeof(struct cmsghdr)) {
            for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
                 cmsg != NULL;
                 cmsg = CMSG_NXTHDR(&msg, cmsg)) {
                if (cmsg->cmsg_level == SOL_PACKET && cmsg->cmsg_type == PACKET_AUXDATA) {
                    const struct tpacket_auxdata *aux = (const struct tpacket_auxdata *)CMSG_DATA(cmsg);
#ifdef TP_STATUS_VLAN_VALID
                    if (aux->tp_status & TP_STATUS_VLAN_VALID) {
                        vlan_id = aux->tp_vlan_tci & 0x0FFF;
                        break;
                    }
#else
                    if (aux->tp_vlan_tci != 0 || aux->tp_vlan_tpid != 0) {
                        vlan_id = aux->tp_vlan_tci & 0x0FFF;
                        break;
                    }
#endif
                }
            }
        }

        if (ether_type == ETH_P_8021Q || ether_type == ETH_P_8021AD) {
            if (received < (ssize_t)(sizeof(struct ethhdr) + sizeof(struct vlan_header))) {
                continue;
            }
            struct vlan_header vlan_local;
            memcpy(&vlan_local, buffer + sizeof(struct ethhdr), sizeof(vlan_local));
            vlan_id = ntohs(vlan_local.tci) & 0x0FFF;
            ether_type = ntohs(vlan_local.encapsulated_proto);
            offset += sizeof(struct vlan_header);
        }

        if (ether_type != ETH_P_ARP) {
            continue;
        }

        if (received < (ssize_t)(offset + sizeof(struct ether_arp))) {
            continue;
        }

    struct ether_arp arp_local;
    memcpy(&arp_local, buffer + offset, sizeof(arp_local));
    uint16_t opcode = ntohs(arp_local.ea_hdr.ar_op);
        char sha[18];
        char spa[INET_ADDRSTRLEN];
        char tpa[INET_ADDRSTRLEN];
    format_mac(arp_local.arp_sha, sha, sizeof(sha));
    format_ip(arp_local.arp_spa, spa, sizeof(spa));
    format_ip(arp_local.arp_tpa, tpa, sizeof(tpa));

        if (vlan_id >= 0) {
            printf("[ARP][vlan=%d] opcode=%s sender=%s/%s target=%s\n",
                   vlan_id,
                   opcode == ARPOP_REQUEST ? "REQUEST" :
                   (opcode == ARPOP_REPLY ? "REPLY" : "OTHER"),
                   sha,
                   spa,
                   tpa);
        } else {
            printf("[ARP] opcode=%s sender=%s/%s target=%s\n",
                   opcode == ARPOP_REQUEST ? "REQUEST" :
                   (opcode == ARPOP_REPLY ? "REPLY" : "OTHER"),
                   sha,
                   spa,
                   tpa);
        }

        if (args->verbose) {
            hex_dump(payload, (size_t)received);
        }

        last_activity = time(NULL);
    }

    close(args->fd);
    return NULL;
}

static void *sender_thread(void *arg) {
    struct sender_args *args = (struct sender_args *)arg;
    struct send_context ctx;
    memset(&ctx, 0, sizeof(ctx));

    if (!init_send_context(args->iface,
                           args->src_mac,
                           args->src_mac_set,
                           &args->src_ip,
                           args->src_ip_set,
                           &ctx)) {
        fprintf(stderr, "[ERROR] failed to initialize TX context\n");
        return NULL;
    }

    unsigned long long remaining = args->count;
    while (g_running) {
        send_arp_request(&ctx, args->dst_mac, &args->dst_ip);
        if (args->interval_ms > 0) {
            sleep_ms(args->interval_ms);
        }
        if (args->count > 0) {
            if (--remaining == 0) {
                break;
            }
        }
    }

    close(ctx.fd);
    return NULL;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [options]\n"
            "\n"
            "Receive options:\n"
            "  --rx-iface <ifname>     Interface to capture ARP frames (default eth0)\n"
            "  --timeout <sec>        Stop receiver after idle seconds (0 disables)\n"
            "  --verbose              Hex dump captured frames (first 64 bytes)\n"
            "\n"
            "Transmit options:\n"
            "  --tx-iface <ifname>    Interface to send ARP probes (disabled if absent)\n"
            "  --src-mac <mac>        Override source MAC address\n"
            "  --src-ip <ipv4>        Override source IPv4 address\n"
            "  --dst-mac <mac>        Destination MAC (default ff:ff:ff:ff:ff:ff)\n"
            "  --dst-ip <ipv4>        Destination IPv4 (default 192.168.1.100)\n"
            "  --interval-ms <ms>     Probe interval in milliseconds (default 100)\n"
            "  --count <n>            Number of probes to send (0 runs indefinitely)\n"
            "\n"
            "General:\n"
            "  --help                 Show this message\n",
            prog);
}

static bool parse_ip_address(const char *str, struct in_addr *addr) {
    return inet_pton(AF_INET, str, addr) == 1;
}

static void init_default_options(struct options *opts) {
    memset(opts, 0, sizeof(*opts));
    snprintf(opts->rx_iface, IFNAMSIZ, "%s", DEFAULT_RX_IFACE);
    opts->interval_ms = DEFAULT_INTERVAL_MS;
    opts->timeout_sec = 0;
    opts->rx_enabled = true;
    memset(opts->dst_mac, 0xFF, ETH_ALEN);
    parse_ip_address("192.168.1.100", &opts->dst_ip);
    opts->dst_ip_set = true;
}

int main(int argc, char *argv[]) {
    struct options opts;
    init_default_options(&opts);

    const struct option long_opts[] = {
        {"rx-iface", required_argument, NULL, 'r'},
        {"tx-iface", required_argument, NULL, 't'},
        {"src-mac", required_argument, NULL, 'm'},
        {"src-ip", required_argument, NULL, 's'},
        {"dst-mac", required_argument, NULL, 'M'},
        {"dst-ip", required_argument, NULL, 'd'},
        {"interval-ms", required_argument, NULL, 'i'},
        {"count", required_argument, NULL, 'c'},
        {"timeout", required_argument, NULL, 'T'},
        {"verbose", no_argument, NULL, 'v'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "r:t:m:s:M:d:i:c:T:vh", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'r':
            snprintf(opts.rx_iface, IFNAMSIZ, "%s", optarg);
            opts.rx_enabled = true;
            break;
        case 't':
            snprintf(opts.tx_iface, IFNAMSIZ, "%s", optarg);
            opts.tx_enabled = true;
            break;
        case 'm':
            if (!parse_mac(optarg, opts.src_mac)) {
                fprintf(stderr, "[ERROR] invalid source MAC: %s\n", optarg);
                return 1;
            }
            opts.src_mac_set = true;
            break;
        case 's':
            if (!parse_ip_address(optarg, &opts.src_ip)) {
                fprintf(stderr, "[ERROR] invalid source IP: %s\n", optarg);
                return 1;
            }
            opts.src_ip_set = true;
            break;
        case 'M':
            if (!parse_mac(optarg, opts.dst_mac)) {
                fprintf(stderr, "[ERROR] invalid destination MAC: %s\n", optarg);
                return 1;
            }
            break;
        case 'd':
            if (!parse_ip_address(optarg, &opts.dst_ip)) {
                fprintf(stderr, "[ERROR] invalid destination IP: %s\n", optarg);
                return 1;
            }
            opts.dst_ip_set = true;
            break;
        case 'i':
            opts.interval_ms = atoi(optarg);
            if (opts.interval_ms < 0) {
                fprintf(stderr, "[ERROR] interval-ms must be non-negative\n");
                return 1;
            }
            break;
        case 'c':
            opts.count = strtoull(optarg, NULL, 10);
            break;
        case 'T':
            opts.timeout_sec = atoi(optarg);
            if (opts.timeout_sec < 0) {
                fprintf(stderr, "[ERROR] timeout must be non-negative\n");
                return 1;
            }
            break;
        case 'v':
            opts.verbose = true;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!opts.rx_enabled && !opts.tx_enabled) {
        fprintf(stderr, "[ERROR] at least one of receive or transmit mode must be enabled\n");
        print_usage(argv[0]);
        return 1;
    }

    g_running = 1;
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    pthread_t rx_thread;
    pthread_t tx_thread;
    bool rx_started = false;
    bool tx_started = false;

    struct receiver_args rx_args;
    memset(&rx_args, 0, sizeof(rx_args));

    if (opts.rx_enabled) {
        int recv_fd = create_recv_socket(opts.rx_iface);
        if (recv_fd < 0) {
            if (!opts.tx_enabled) {
                return 1;
            }
            fprintf(stderr, "[WARN] receiver disabled due to initialization failure\n");
        } else {
            rx_args.fd = recv_fd;
            rx_args.timeout_sec = opts.timeout_sec;
            rx_args.verbose = opts.verbose;
            if (pthread_create(&rx_thread, NULL, receiver_thread, &rx_args) != 0) {
                perror("pthread_create(receiver)");
                close(recv_fd);
                if (!opts.tx_enabled) {
                    return 1;
                }
            } else {
                rx_started = true;
            }
        }
    }

    struct sender_args tx_args;
    memset(&tx_args, 0, sizeof(tx_args));

    if (opts.tx_enabled) {
    snprintf(tx_args.iface, IFNAMSIZ, "%s", opts.tx_iface);
        if (opts.src_mac_set) {
            memcpy(tx_args.src_mac, opts.src_mac, ETH_ALEN);
            tx_args.src_mac_set = true;
        }
        if (opts.src_ip_set) {
            tx_args.src_ip = opts.src_ip;
            tx_args.src_ip_set = true;
        }
        memcpy(tx_args.dst_mac, opts.dst_mac, ETH_ALEN);
        tx_args.dst_ip = opts.dst_ip;
        tx_args.interval_ms = opts.interval_ms;
        tx_args.count = opts.count;

        if (pthread_create(&tx_thread, NULL, sender_thread, &tx_args) != 0) {
            perror("pthread_create(sender)");
            if (!rx_started) {
                return 1;
            }
        } else {
            tx_started = true;
        }
    }

    if (rx_started) {
        pthread_join(rx_thread, NULL);
    g_running = 0;
    }
    if (tx_started) {
        pthread_join(tx_thread, NULL);
    }

    printf("Program exiting\n");
    return 0;
}
