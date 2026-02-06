#define _DEFAULT_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/if_ether.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "netforward_sidecar.h"

struct sockaddr_vlan {
    uint8_t dest_mac[ETH_ALEN];
    uint8_t src_mac[ETH_ALEN];
    uint32_t port;
    uint16_t vlanid;
    uint16_t svlanid;
    uint32_t length;
    uint16_t eth_type;
};

struct stub_opts {
    uint32_t port;
    uint16_t vlan;
    int count;
    int interval_ms;
    bool gratuitous;
    bool idle;
};

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int signo) {
    (void)signo;
    g_stop = 1;
}

static void write_mac(uint8_t mac[ETH_ALEN], uint8_t base, uint8_t step) {
    mac[0] = 0x02;
    mac[1] = 0x00;
    mac[2] = 0x00;
    mac[3] = 0x00;
    mac[4] = base;
    mac[5] = step;
}

static size_t build_arp_payload(uint8_t *buf, size_t cap, uint32_t sender_host, uint32_t target_host, bool gratuitous, uint8_t src_mac[ETH_ALEN]) {
    if (cap < sizeof(struct ether_arp)) {
        return 0;
    }

    struct ether_arp *arp = (struct ether_arp *)buf;
    memset(arp, 0, sizeof(*arp));
    arp->ea_hdr.ar_hrd = htons(ARPHRD_ETHER);
    arp->ea_hdr.ar_pro = htons(ETH_P_IP);
    arp->ea_hdr.ar_hln = ETH_ALEN;
    arp->ea_hdr.ar_pln = 4;
    arp->ea_hdr.ar_op = htons(gratuitous ? ARPOP_REPLY : ARPOP_REQUEST);

    uint32_t sender_ip = htonl(sender_host);
    uint32_t target_ip = htonl(target_host);
    memcpy(&arp->arp_sha, src_mac, ETH_ALEN);
    memcpy(&arp->arp_spa, &sender_ip, sizeof(sender_ip));
    memcpy(&arp->arp_tha, src_mac, ETH_ALEN);
    memcpy(&arp->arp_tpa, gratuitous ? &sender_ip : &target_ip, sizeof(target_ip));

    return sizeof(struct ether_arp);
}

static void parse_args(int argc, char **argv, struct stub_opts *opts) {
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->port = 3;
    opts->vlan = 1;
    opts->count = 10;
    opts->interval_ms = 500;
    opts->gratuitous = false;
    opts->idle = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            opts->count = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--interval-ms") == 0 && i + 1 < argc) {
            opts->interval_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--vlan") == 0 && i + 1 < argc) {
            opts->vlan = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--ifindex") == 0 && i + 1 < argc) {
            opts->port = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--gratuitous") == 0) {
            opts->gratuitous = true;
        } else if (strcmp(argv[i], "--idle") == 0) {
            opts->idle = true;
        }
    }
}

int main(int argc, char **argv) {
    struct stub_opts opts;
    parse_args(argc, argv, &opts);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    printf("[netforward-sidecar-stub] listening (path=auto env TD_NETFORWARD_SIDECAR_SOCK or default) vlan=%u ifindex=%u count=%d interval_ms=%d gratuitous=%d idle=%d\n",
           opts.vlan,
           opts.port,
           opts.count,
           opts.interval_ms,
           opts.gratuitous ? 1 : 0,
           opts.idle ? 1 : 0);
    fflush(stdout);

    if (netforward_sidecar_start() != 0) {
        perror("netforward_sidecar_start");
        return EXIT_FAILURE;
    }

    if (opts.idle) {
        pause();
        netforward_sidecar_stop();
        return EXIT_SUCCESS;
    }

    uint8_t payload[sizeof(struct ether_arp)];
    uint8_t src_mac[ETH_ALEN];

    int to_send = opts.count <= 0 ? 1 : opts.count;
    for (int i = 0; (opts.count <= 0) || i < to_send; ++i) {
        if (g_stop) {
            break;
        }

        write_mac(src_mac, 0x10, (uint8_t)i);
        size_t payload_len = build_arp_payload(payload, sizeof(payload), 0x0A000000 | (uint32_t)(10 + i), 0x0A000064, opts.gratuitous, src_mac);
        if (payload_len == 0) {
            fprintf(stderr, "failed to build ARP payload\n");
            break;
        }

        struct sockaddr_vlan header;
        memset(&header, 0, sizeof(header));
        memset(header.dest_mac, 0xFF, ETH_ALEN);
        memcpy(header.src_mac, src_mac, ETH_ALEN);
        header.port = opts.port;
        header.vlanid = opts.vlan;
        header.length = (uint32_t)(sizeof(header) + payload_len);
        header.eth_type = ETH_P_ARP;

        uint8_t packet[sizeof(header) + sizeof(payload)];
        size_t packet_len = sizeof(header) + payload_len;

        memcpy(packet, &header, sizeof(header));
        memcpy(packet + sizeof(header), payload, payload_len);

        if (netforward_sidecar_forward(packet, (int)packet_len) != 0) {
            perror("netforward_sidecar_forward");
            break;
        }

        if (opts.interval_ms > 0) {
            struct timespec ts = {
                .tv_sec = opts.interval_ms / 1000,
                .tv_nsec = (opts.interval_ms % 1000) * 1000000L,
            };
            nanosleep(&ts, NULL);
        }

        if (opts.count <= 0) {
            continue;
        }
    }

    netforward_sidecar_stop();
    printf("[netforward-sidecar-stub] completed\n");
    return EXIT_SUCCESS;
}
