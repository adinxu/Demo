#define _GNU_SOURCE

#include "realtek_adapter.h"

#include <arpa/inet.h>
#include <errno.h>
#include <linux/filter.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include "td_atomic.h"
#include <stdarg.h>
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

#include "td_logging.h"

#ifndef TD_REALTEK_RX_BUFFER_SIZE
#define TD_REALTEK_RX_BUFFER_SIZE 2048
#endif

#ifndef TD_REALTEK_DEFAULT_TX_INTERVAL_MS
#define TD_REALTEK_DEFAULT_TX_INTERVAL_MS 100U
#endif

struct vlan_header {
    uint16_t tci;
    uint16_t encapsulated_proto;
} __attribute__((packed));

struct td_adapter {
    struct td_adapter_config cfg;
    struct td_adapter_env env;
    char rx_iface[IFNAMSIZ];
    char tx_iface[IFNAMSIZ];

    atomic_bool running;
    int rx_fd;
    int tx_fd;
    int rx_kernel_ifindex;
    int tx_kernel_ifindex;
    pthread_t rx_thread;
    bool rx_thread_started;

    struct td_adapter_packet_subscription packet_sub;
    bool packet_subscribed;

    pthread_mutex_t state_lock;
    pthread_mutex_t send_lock;
    struct timespec last_send;

    uint8_t tx_mac[ETH_ALEN];
    struct in_addr tx_ipv4;
};

static int normalize_vlan_id(int raw_vlan) {
    if (raw_vlan >= 1 && raw_vlan <= 4094) {
        return raw_vlan;
    }
    return -1;
}

static void realtek_logf(struct td_adapter *adapter,
                         td_log_level_t level,
                         const char *fmt,
                         ...)
    __attribute__((format(printf, 3, 4)));

static void realtek_logf(struct td_adapter *adapter,
                         td_log_level_t level,
                         const char *fmt,
                         ...) {
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (adapter && adapter->env.log_fn) {
        adapter->env.log_fn(adapter->env.log_user_data, level, "realtek", buffer);
    } else {
        td_log_writef(level, "realtek", "%s", buffer);
    }
}

static int attach_arp_filter(int fd) {
    struct sock_filter code[] = {
        {BPF_LD | BPF_B | BPF_ABS, 0, 0, SKF_AD_OFF + SKF_AD_PKTTYPE},
        {BPF_JMP | BPF_JEQ | BPF_K, 3, 0, PACKET_HOST},
        {BPF_JMP | BPF_JEQ | BPF_K, 2, 0, PACKET_BROADCAST},
        {BPF_JMP | BPF_JEQ | BPF_K, 1, 0, PACKET_MULTICAST},
        {BPF_RET | BPF_K, 0, 0, 0},
        {BPF_LD | BPF_H | BPF_ABS, 0, 0, 12},
        {BPF_JMP | BPF_JEQ | BPF_K, 6, 0, htons(ETH_P_ARP)},
        {BPF_JMP | BPF_JEQ | BPF_K, 2, 0, htons(ETH_P_8021Q)},
        {BPF_JMP | BPF_JEQ | BPF_K, 1, 0, htons(ETH_P_8021AD)},
        {BPF_RET | BPF_K, 0, 0, 0},
        {BPF_LD | BPF_H | BPF_ABS, 0, 0, 16},
        {BPF_JMP | BPF_JEQ | BPF_K, 1, 0, htons(ETH_P_ARP)},
        {BPF_RET | BPF_K, 0, 0, 0},
        {BPF_RET | BPF_K, 0, 0, 0xFFFF},
    };

    struct sock_fprog program = {
        .len = (unsigned short)(sizeof(code) / sizeof(code[0])),
        .filter = code,
    };

    if (setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, &program, sizeof(program)) < 0) {
        return -1;
    }
    return 0;
}

static int configure_rx_socket(struct td_adapter *adapter) {
    int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd < 0) {
        realtek_logf(adapter, TD_LOG_ERROR, "socket(AF_PACKET) failed: %s", strerror(errno));
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", adapter->rx_iface);

    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        realtek_logf(adapter, TD_LOG_ERROR, "ioctl(SIOCGIFINDEX,%s) failed: %s", adapter->rx_iface, strerror(errno));
        close(fd);
        return -1;
    }
    adapter->rx_kernel_ifindex = ifr.ifr_ifindex;

    struct sockaddr_ll addr;
    memset(&addr, 0, sizeof(addr));
    addr.sll_family = AF_PACKET;
    addr.sll_protocol = htons(ETH_P_ALL);
    addr.sll_ifindex = adapter->rx_kernel_ifindex;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        realtek_logf(adapter, TD_LOG_ERROR, "bind(%s) failed: %s", adapter->rx_iface, strerror(errno));
        close(fd);
        return -1;
    }

    if (attach_arp_filter(fd) < 0) {
        realtek_logf(adapter, TD_LOG_ERROR, "failed to attach ARP filter: %s", strerror(errno));
        close(fd);
        return -1;
    }

    int enable_aux = 1;
    if (setsockopt(fd, SOL_PACKET, PACKET_AUXDATA, &enable_aux, sizeof(enable_aux)) < 0) {
        realtek_logf(adapter, TD_LOG_ERROR, "setsockopt(PACKET_AUXDATA) failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    if (adapter->cfg.rx_ring_size > 0) {
        int rcvbuf = (int)adapter->cfg.rx_ring_size;
        if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) < 0) {
            realtek_logf(adapter, TD_LOG_WARN, "setsockopt(SO_RCVBUF) failed: %s", strerror(errno));
        }
    }

    return fd;
}

static int configure_tx_socket(struct td_adapter *adapter) {
    int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ARP));
    if (fd < 0) {
        realtek_logf(adapter, TD_LOG_ERROR, "socket(AF_PACKET,ARP) failed: %s", strerror(errno));
        return -1;
    }

    int ioctl_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ioctl_fd < 0) {
        realtek_logf(adapter, TD_LOG_ERROR, "socket(AF_INET) failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", adapter->tx_iface);

    if (ioctl(ioctl_fd, SIOCGIFINDEX, &ifr) < 0) {
        realtek_logf(adapter, TD_LOG_ERROR, "ioctl(SIOCGIFINDEX,%s) failed: %s", adapter->tx_iface, strerror(errno));
        close(ioctl_fd);
        close(fd);
        return -1;
    }
    adapter->tx_kernel_ifindex = ifr.ifr_ifindex;

    if (ioctl(ioctl_fd, SIOCGIFHWADDR, &ifr) == 0) {
        memcpy(adapter->tx_mac, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
    } else {
        memset(adapter->tx_mac, 0, ETH_ALEN);
        realtek_logf(adapter, TD_LOG_WARN, "ioctl(SIOCGIFHWADDR,%s) failed: %s", adapter->tx_iface, strerror(errno));
    }

    if (ioctl(ioctl_fd, SIOCGIFADDR, &ifr) == 0) {
        adapter->tx_ipv4 = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;
    } else {
        adapter->tx_ipv4.s_addr = 0;
        realtek_logf(adapter, TD_LOG_WARN, "ioctl(SIOCGIFADDR,%s) failed: %s", adapter->tx_iface, strerror(errno));
    }

    close(ioctl_fd);
    return fd;
}

static bool all_zero_mac(const uint8_t mac[ETH_ALEN]) {
    for (size_t i = 0; i < ETH_ALEN; ++i) {
        if (mac[i] != 0) {
            return false;
        }
    }
    return true;
}

static bool query_iface_details(const char *iface,
                                int *kernel_ifindex_out,
                                uint8_t mac_out[ETH_ALEN],
                                struct in_addr *ip_out) {
    if (!iface || !iface[0]) {
        return false;
    }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", iface);

    bool ok = true;

    if (ioctl(fd, SIOCGIFINDEX, &ifr) == 0) {
        if (kernel_ifindex_out) {
            *kernel_ifindex_out = ifr.ifr_ifindex;
        }
    } else {
        ok = false;
    }

    if (ok && ioctl(fd, SIOCGIFHWADDR, &ifr) == 0) {
        if (mac_out) {
            memcpy(mac_out, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
        }
    } else if (mac_out) {
        memset(mac_out, 0, ETH_ALEN);
    }

    if (ok && ioctl(fd, SIOCGIFADDR, &ifr) == 0) {
        if (ip_out) {
            *ip_out = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;
        }
    } else if (ip_out) {
        ip_out->s_addr = 0;
    }

    close(fd);
    return ok;
}

static void bind_socket_to_iface(struct td_adapter *adapter, const char *iface) {
    if (!iface || !iface[0]) {
        return;
    }

    size_t len = strlen(iface) + 1;
    if (setsockopt(adapter->tx_fd, SOL_SOCKET, SO_BINDTODEVICE, iface, len) < 0) {
        realtek_logf(adapter, TD_LOG_WARN, "SO_BINDTODEVICE(%s) failed: %s", iface, strerror(errno));
    }
}

static void *rx_thread_main(void *arg) {
    struct td_adapter *adapter = (struct td_adapter *)arg;
    uint8_t buffer[TD_REALTEK_RX_BUFFER_SIZE];

    realtek_logf(adapter, TD_LOG_INFO, "RX thread started on %s", adapter->rx_iface);

    while (atomic_load(&adapter->running)) {
        struct pollfd pfd = {
            .fd = adapter->rx_fd,
            .events = POLLIN,
            .revents = 0,
        };

        int ready = poll(&pfd, 1, 1000);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            realtek_logf(adapter, TD_LOG_ERROR, "poll failed: %s", strerror(errno));
            break;
        }

        if (ready == 0) {
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

        ssize_t received = recvmsg(adapter->rx_fd, &msg, 0);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            realtek_logf(adapter, TD_LOG_ERROR, "recvmsg failed: %s", strerror(errno));
            continue;
        }
        if (received < (ssize_t)sizeof(struct ethhdr)) {
            continue;
        }

        struct ethhdr eth_local;
        memcpy(&eth_local, buffer, sizeof(eth_local));

        uint16_t ether_type = ntohs(eth_local.h_proto);
        size_t offset = sizeof(struct ethhdr);
        int vlan_id = -1;

        if (msg.msg_controllen >= sizeof(struct cmsghdr)) {
            for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
                if (cmsg->cmsg_level == SOL_PACKET && cmsg->cmsg_type == PACKET_AUXDATA) {
                    const struct tpacket_auxdata *aux = (const struct tpacket_auxdata *)CMSG_DATA(cmsg);
#ifdef TP_STATUS_VLAN_VALID
                    if (aux->tp_status & TP_STATUS_VLAN_VALID) {
                        vlan_id = normalize_vlan_id((int)(aux->tp_vlan_tci & 0x0FFF));
                        break;
                    }
#else
                    if (aux->tp_vlan_tci != 0 || aux->tp_vlan_tpid != 0) {
                        vlan_id = normalize_vlan_id((int)(aux->tp_vlan_tci & 0x0FFF));
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
            vlan_id = normalize_vlan_id((int)(ntohs(vlan_local.tci) & 0x0FFF));
            ether_type = ntohs(vlan_local.encapsulated_proto);
            offset += sizeof(vlan_local);
        }

        vlan_id = normalize_vlan_id(vlan_id);

        if (ether_type != ETH_P_ARP) {
            continue;
        }

        size_t payload_len = 0;
        if ((size_t)received > offset) {
            payload_len = (size_t)received - offset;
        }

        struct td_adapter_packet_subscription sub;
        bool subscribed = false;
        pthread_mutex_lock(&adapter->state_lock);
        if (adapter->packet_subscribed) {
            sub = adapter->packet_sub;
            subscribed = true;
        }
        pthread_mutex_unlock(&adapter->state_lock);

        if (!subscribed || !sub.callback) {
            continue;
        }

        struct td_adapter_packet_view view;
        memset(&view, 0, sizeof(view));
        view.frame = buffer;
        view.frame_len = (size_t)received;
        view.payload = buffer + offset;
        view.payload_len = payload_len;
        view.ether_type = ether_type;
    view.vlan_id = vlan_id;
        clock_gettime(CLOCK_REALTIME, &view.ts);
    view.ifindex = 0U;
        memcpy(view.src_mac, eth_local.h_source, ETH_ALEN);
        memcpy(view.dst_mac, eth_local.h_dest, ETH_ALEN);

        sub.callback(&view, sub.user_ctx);
    }

    realtek_logf(adapter, TD_LOG_INFO, "RX thread stopping on %s", adapter->rx_iface);
    return NULL;
}

static td_adapter_result_t ensure_rx_thread(struct td_adapter *adapter) {
    if (!atomic_load(&adapter->running)) {
        return TD_ADAPTER_ERR_NOT_READY;
    }
    if (adapter->rx_thread_started) {
        return TD_ADAPTER_OK;
    }

    int rc = pthread_create(&adapter->rx_thread, NULL, rx_thread_main, adapter);
    if (rc != 0) {
        realtek_logf(adapter, TD_LOG_ERROR, "pthread_create failed: %s", strerror(rc));
        return TD_ADAPTER_ERR_SYS;
    }
    adapter->rx_thread_started = true;
    return TD_ADAPTER_OK;
}

static td_adapter_result_t realtek_init(const struct td_adapter_config *cfg,
                                        const struct td_adapter_env *env,
                                        td_adapter_t **handle_out) {
    if (!handle_out) {
        return TD_ADAPTER_ERR_INVALID_ARG;
    }

    struct td_adapter *adapter = calloc(1, sizeof(*adapter));
    if (!adapter) {
        return TD_ADAPTER_ERR_NO_MEMORY;
    }

    if (cfg) {
        adapter->cfg = *cfg;
    } else {
        memset(&adapter->cfg, 0, sizeof(adapter->cfg));
    }

    if (adapter->cfg.tx_interval_ms == 0) {
        adapter->cfg.tx_interval_ms = TD_REALTEK_DEFAULT_TX_INTERVAL_MS;
    }

    const char *rx_iface_cfg = adapter->cfg.rx_iface ? adapter->cfg.rx_iface : "eth0";
    const char *tx_iface_cfg = adapter->cfg.tx_iface ? adapter->cfg.tx_iface : "vlan1";
    snprintf(adapter->rx_iface, sizeof(adapter->rx_iface), "%s", rx_iface_cfg);
    snprintf(adapter->tx_iface, sizeof(adapter->tx_iface), "%s", tx_iface_cfg);

    if (env) {
        adapter->env = *env;
    } else {
        memset(&adapter->env, 0, sizeof(adapter->env));
    }

    atomic_init(&adapter->running, false);
    adapter->rx_fd = -1;
    adapter->tx_fd = -1;
    adapter->rx_kernel_ifindex = -1;
    adapter->tx_kernel_ifindex = -1;
    adapter->tx_ipv4.s_addr = 0;
    adapter->packet_subscribed = false;
    adapter->rx_thread_started = false;
    adapter->last_send.tv_sec = 0;
    adapter->last_send.tv_nsec = 0;

    pthread_mutex_init(&adapter->state_lock, NULL);
    pthread_mutex_init(&adapter->send_lock, NULL);

    *handle_out = adapter;
    realtek_logf(adapter, TD_LOG_INFO, "adapter initialized (rx=%s tx=%s interval=%ums)",
                 adapter->rx_iface,
                 adapter->tx_iface,
                 adapter->cfg.tx_interval_ms);
    return TD_ADAPTER_OK;
}

static void realtek_shutdown(td_adapter_t *handle) {
    if (!handle) {
        return;
    }

    struct td_adapter *adapter = handle;

    if (atomic_load(&adapter->running)) {
        atomic_store(&adapter->running, false);
    }

    if (adapter->rx_thread_started) {
        pthread_join(adapter->rx_thread, NULL);
        adapter->rx_thread_started = false;
    }

    if (adapter->rx_fd >= 0) {
        close(adapter->rx_fd);
        adapter->rx_fd = -1;
    }
    if (adapter->tx_fd >= 0) {
        close(adapter->tx_fd);
        adapter->tx_fd = -1;
    }

    pthread_mutex_destroy(&adapter->state_lock);
    pthread_mutex_destroy(&adapter->send_lock);

    free(adapter);
}

static td_adapter_result_t realtek_start(td_adapter_t *handle) {
    if (!handle) {
        return TD_ADAPTER_ERR_INVALID_ARG;
    }

    struct td_adapter *adapter = handle;
    if (atomic_load(&adapter->running)) {
        return TD_ADAPTER_OK;
    }

    adapter->rx_fd = configure_rx_socket(adapter);
    if (adapter->rx_fd < 0) {
        return TD_ADAPTER_ERR_SYS;
    }

    adapter->tx_fd = configure_tx_socket(adapter);
    if (adapter->tx_fd < 0) {
        close(adapter->rx_fd);
        adapter->rx_fd = -1;
        return TD_ADAPTER_ERR_SYS;
    }

    atomic_store(&adapter->running, true);

    pthread_mutex_lock(&adapter->state_lock);
    bool need_thread = adapter->packet_subscribed;
    pthread_mutex_unlock(&adapter->state_lock);

    if (need_thread) {
        td_adapter_result_t rc = ensure_rx_thread(adapter);
        if (rc != TD_ADAPTER_OK) {
            atomic_store(&adapter->running, false);
            close(adapter->rx_fd);
            adapter->rx_fd = -1;
            close(adapter->tx_fd);
            adapter->tx_fd = -1;
            return rc;
        }
    }

    realtek_logf(adapter, TD_LOG_INFO, "adapter started");
    return TD_ADAPTER_OK;
}

static void realtek_stop(td_adapter_t *handle) {
    if (!handle) {
        return;
    }

    struct td_adapter *adapter = handle;
    if (!atomic_load(&adapter->running)) {
        return;
    }

    atomic_store(&adapter->running, false);

    if (adapter->rx_thread_started) {
        pthread_join(adapter->rx_thread, NULL);
        adapter->rx_thread_started = false;
    }

    if (adapter->rx_fd >= 0) {
        close(adapter->rx_fd);
        adapter->rx_fd = -1;
    }
    if (adapter->tx_fd >= 0) {
        close(adapter->tx_fd);
        adapter->tx_fd = -1;
    }

    realtek_logf(adapter, TD_LOG_INFO, "adapter stopped");
}

static td_adapter_result_t realtek_register_packet_rx(td_adapter_t *handle,
                                                      const struct td_adapter_packet_subscription *sub) {
    if (!handle || !sub || !sub->callback) {
        return TD_ADAPTER_ERR_INVALID_ARG;
    }

    struct td_adapter *adapter = handle;
    pthread_mutex_lock(&adapter->state_lock);
    adapter->packet_sub = *sub;
    adapter->packet_subscribed = true;
    pthread_mutex_unlock(&adapter->state_lock);

    if (atomic_load(&adapter->running)) {
        return ensure_rx_thread(adapter);
    }

    return TD_ADAPTER_OK;
}

static td_adapter_result_t realtek_send_arp(td_adapter_t *handle,
                                            const struct td_adapter_arp_request *req) {
    if (!handle || !req) {
        return TD_ADAPTER_ERR_INVALID_ARG;
    }

    struct td_adapter *adapter = handle;
    if (adapter->tx_fd < 0 || adapter->tx_kernel_ifindex <= 0) {
        return TD_ADAPTER_ERR_NOT_READY;
    }

    pthread_mutex_lock(&adapter->send_lock);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    if (adapter->cfg.tx_interval_ms > 0 && adapter->last_send.tv_sec != 0) {
        long diff_ms = (now.tv_sec - adapter->last_send.tv_sec) * 1000L;
        diff_ms += (now.tv_nsec - adapter->last_send.tv_nsec) / 1000000L;
        if (diff_ms < (long)adapter->cfg.tx_interval_ms) {
            long sleep_ms = (long)adapter->cfg.tx_interval_ms - diff_ms;
            if (sleep_ms > 0) {
                struct timespec req_sleep = {
                    .tv_sec = sleep_ms / 1000,
                    .tv_nsec = (sleep_ms % 1000) * 1000000L,
                };
                nanosleep(&req_sleep, NULL);
                clock_gettime(CLOCK_MONOTONIC, &now);
            }
        }
    }

    const char *tx_iface = adapter->tx_iface;
    int tx_kernel_ifindex = adapter->tx_kernel_ifindex;
    struct in_addr iface_ip = adapter->tx_ipv4;
    uint8_t iface_mac[ETH_ALEN];
    memcpy(iface_mac, adapter->tx_mac, ETH_ALEN);

    if (req->tx_iface_valid && req->tx_iface[0]) {
        tx_iface = req->tx_iface;
    tx_kernel_ifindex = req->tx_kernel_ifindex;
    if (!query_iface_details(tx_iface, &tx_kernel_ifindex, iface_mac, &iface_ip)) {
            pthread_mutex_unlock(&adapter->send_lock);
            realtek_logf(adapter, TD_LOG_ERROR, "failed to resolve interface %s for ARP send", tx_iface);
            return TD_ADAPTER_ERR_INVALID_ARG;
        }
    } else if (!tx_iface || !tx_iface[0]) {
        pthread_mutex_unlock(&adapter->send_lock);
        realtek_logf(adapter, TD_LOG_ERROR, "no transmit interface configured for ARP send");
        return TD_ADAPTER_ERR_INVALID_ARG;
    }

    if (tx_kernel_ifindex <= 0) {
        if (!query_iface_details(tx_iface, &tx_kernel_ifindex, iface_mac, &iface_ip)) {
            pthread_mutex_unlock(&adapter->send_lock);
            realtek_logf(adapter, TD_LOG_ERROR, "failed to resolve interface %s for ARP send", tx_iface);
            return TD_ADAPTER_ERR_INVALID_ARG;
        }
    }

    bind_socket_to_iface(adapter, tx_iface);

    uint8_t sender_mac[ETH_ALEN];
    if (all_zero_mac(req->sender_mac)) {
        memcpy(sender_mac, iface_mac, ETH_ALEN);
    } else {
        memcpy(sender_mac, req->sender_mac, ETH_ALEN);
    }

    struct in_addr sender_ip = req->sender_ip;
    if (sender_ip.s_addr == 0) {
        sender_ip = iface_ip;
    }

    if (sender_ip.s_addr == 0) {
        pthread_mutex_unlock(&adapter->send_lock);
        realtek_logf(adapter, TD_LOG_WARN, "interface %s has no IPv4, skipping ARP send", tx_iface);
        return TD_ADAPTER_ERR_NOT_READY;
    }

    uint8_t target_mac[ETH_ALEN];
    if (all_zero_mac(req->target_mac)) {
        memset(target_mac, 0xFF, ETH_ALEN);
    } else {
        memcpy(target_mac, req->target_mac, ETH_ALEN);
    }

    int effective_vlan = (req->vlan_id >= 1 && req->vlan_id <= 4094) ? req->vlan_id : -1;
    bool vlan_tagging = effective_vlan > 0;

    uint8_t frame[sizeof(struct ethhdr) + sizeof(struct vlan_header) + sizeof(struct ether_arp)];
    memset(frame, 0, sizeof(frame));

    struct ethhdr *eth = (struct ethhdr *)frame;
    memcpy(eth->h_dest, target_mac, ETH_ALEN);
    memcpy(eth->h_source, sender_mac, ETH_ALEN);

    size_t offset = sizeof(struct ethhdr);
    if (vlan_tagging) {
        eth->h_proto = htons(ETH_P_8021Q);
    struct vlan_header *vlan = (struct vlan_header *)(frame + sizeof(struct ethhdr));
    vlan->tci = htons((uint16_t)(effective_vlan & 0x0FFF));
        vlan->encapsulated_proto = htons(ETH_P_ARP);
        offset += sizeof(struct vlan_header);
    } else {
        eth->h_proto = htons(ETH_P_ARP);
    }

    struct ether_arp *arp = (struct ether_arp *)(frame + offset);
    arp->ea_hdr.ar_hrd = htons(ARPHRD_ETHER);
    arp->ea_hdr.ar_pro = htons(ETH_P_IP);
    arp->ea_hdr.ar_hln = ETH_ALEN;
    arp->ea_hdr.ar_pln = 4;
    arp->ea_hdr.ar_op = htons(ARPOP_REQUEST);
    memcpy(arp->arp_sha, sender_mac, ETH_ALEN);
    memcpy(arp->arp_spa, &sender_ip.s_addr, sizeof(arp->arp_spa));
    memcpy(arp->arp_tha, target_mac, ETH_ALEN);
    memcpy(arp->arp_tpa, &req->target_ip.s_addr, sizeof(arp->arp_tpa));

    struct sockaddr_ll addr;
    memset(&addr, 0, sizeof(addr));
    addr.sll_family = AF_PACKET;
    addr.sll_protocol = htons(vlan_tagging ? ETH_P_8021Q : ETH_P_ARP);
    addr.sll_ifindex = tx_kernel_ifindex;
    addr.sll_halen = ETH_ALEN;
    memcpy(addr.sll_addr, target_mac, ETH_ALEN);

    size_t frame_len = offset + sizeof(struct ether_arp);

    ssize_t sent = sendto(adapter->tx_fd, frame, frame_len, 0,
                          (struct sockaddr *)&addr, sizeof(addr));
    if (sent < 0) {
        int err = errno;
        pthread_mutex_unlock(&adapter->send_lock);
        realtek_logf(adapter, TD_LOG_ERROR, "sendto failed: %s", strerror(err));
        return TD_ADAPTER_ERR_SYS;
    }

    adapter->last_send = now;
    pthread_mutex_unlock(&adapter->send_lock);

    char ip_buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &req->target_ip, ip_buf, sizeof(ip_buf));
    if (vlan_tagging) {
        realtek_logf(adapter, TD_LOG_DEBUG, "ARP probe sent to %s via %s vlan=%d", ip_buf, tx_iface, effective_vlan);
    } else {
        realtek_logf(adapter, TD_LOG_DEBUG, "ARP probe sent to %s via %s", ip_buf, tx_iface);
    }

    return TD_ADAPTER_OK;
}

static td_adapter_result_t realtek_query_iface(td_adapter_t *handle,
                                               const char *ifname,
                                               struct td_adapter_iface_info *info_out) {
    if (!handle || !ifname || !info_out) {
        return TD_ADAPTER_ERR_INVALID_ARG;
    }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return TD_ADAPTER_ERR_SYS;
    }

    memset(info_out, 0, sizeof(*info_out));
    snprintf(info_out->ifname, sizeof(info_out->ifname), "%s", ifname);

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname);

    if (ioctl(fd, SIOCGIFHWADDR, &ifr) == 0) {
        memcpy(info_out->mac, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
    }

    if (ioctl(fd, SIOCGIFADDR, &ifr) == 0) {
        info_out->ipv4 = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;
    }

    if (ioctl(fd, SIOCGIFFLAGS, &ifr) == 0) {
        info_out->flags = (uint32_t)ifr.ifr_flags;
    }

    close(fd);
    return TD_ADAPTER_OK;
}

static void realtek_log_write(td_adapter_t *handle,
                              td_log_level_t level,
                              const char *component,
                              const char *message) {
    struct td_adapter *adapter = handle;
    if (!adapter) {
        td_log_writef(level, component ? component : "realtek", "%s", message ? message : "");
        return;
    }
    if (adapter->env.log_fn) {
        adapter->env.log_fn(adapter->env.log_user_data, level, component ? component : "realtek", message ? message : "");
    } else {
        td_log_writef(level, component ? component : "realtek", "%s", message ? message : "");
    }
}

static const struct td_adapter_ops g_realtek_ops = {
    .init = realtek_init,
    .shutdown = realtek_shutdown,
    .start = realtek_start,
    .stop = realtek_stop,
    .register_packet_rx = realtek_register_packet_rx,
    .send_arp = realtek_send_arp,
    .query_iface = realtek_query_iface,
    .log_write = realtek_log_write,
};

const struct td_adapter_descriptor *td_realtek_adapter_descriptor(void) {
    static const struct td_adapter_descriptor descriptor = {
        .name = "realtek",
        .ops = &g_realtek_ops,
    };
    return &descriptor;
}
