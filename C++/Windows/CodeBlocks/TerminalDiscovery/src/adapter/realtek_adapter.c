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
#include <inttypes.h>
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
#include "td_time_utils.h"
#include "td_switch_mac_bridge.h"

#ifndef TD_REALTEK_RX_BUFFER_SIZE
#define TD_REALTEK_RX_BUFFER_SIZE 2048
#endif

#ifndef TD_REALTEK_DEFAULT_TX_INTERVAL_MS
#define TD_REALTEK_DEFAULT_TX_INTERVAL_MS 100U
#endif

#ifndef TD_REALTEK_MAC_CACHE_TTL_MS
#define TD_REALTEK_MAC_CACHE_TTL_MS 30000U
#endif

#ifndef TD_REALTEK_MAC_BUCKET_COUNT
#define TD_REALTEK_MAC_BUCKET_COUNT 256U
#endif

struct mac_bucket_entry {
    uint8_t mac[ETH_ALEN];
    uint16_t vlan;
    uint32_t ifindex;
    struct mac_bucket_entry *next;
};

struct realtek_mac_cache {
    pthread_rwlock_t map_lock;
    struct mac_bucket_entry *buckets[TD_REALTEK_MAC_BUCKET_COUNT];
    SwUcMacEntry *entries;
    uint32_t capacity;
    uint64_t version;
    struct timespec last_refresh;
    uint32_t ttl_ms;
    pthread_mutex_t worker_lock;
    pthread_cond_t worker_cond;
    pthread_t worker_thread;
    bool worker_started;
    bool worker_stop;
    bool refresh_requested;
    td_adapter_mac_locator_refresh_cb refresh_cb;
    void *refresh_ctx;
};

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

    struct realtek_mac_cache mac_cache;
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

static void mac_cache_init(struct realtek_mac_cache *cache) {
    if (!cache) {
        return;
    }
    memset(cache, 0, sizeof(*cache));
    cache->ttl_ms = TD_REALTEK_MAC_CACHE_TTL_MS;
    pthread_rwlock_init(&cache->map_lock, NULL);
    pthread_mutex_init(&cache->worker_lock, NULL);
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&cache->worker_cond, &attr);
    pthread_condattr_destroy(&attr);
}

static void *mac_cache_worker_main(void *arg);

static bool mac_cache_start_worker(struct td_adapter *adapter) {
    if (!adapter) {
        return false;
    }

    struct realtek_mac_cache *cache = &adapter->mac_cache;
    pthread_mutex_lock(&cache->worker_lock);
    if (cache->worker_started) {
        pthread_mutex_unlock(&cache->worker_lock);
        return true;
    }

    cache->worker_stop = false;
    cache->refresh_requested = true;
    int rc = pthread_create(&cache->worker_thread, NULL, mac_cache_worker_main, adapter);
    if (rc != 0) {
        cache->refresh_requested = false;
        pthread_mutex_unlock(&cache->worker_lock);
        realtek_logf(adapter, TD_LOG_ERROR, "pthread_create for MAC cache worker failed: %s", strerror(rc));
        return false;
    }

    cache->worker_started = true;
    pthread_mutex_unlock(&cache->worker_lock);
    return true;
}

static void mac_cache_stop_worker(struct td_adapter *adapter) {
    if (!adapter) {
        return;
    }

    struct realtek_mac_cache *cache = &adapter->mac_cache;
    pthread_mutex_lock(&cache->worker_lock);
    if (!cache->worker_started) {
        pthread_mutex_unlock(&cache->worker_lock);
        return;
    }

    cache->worker_stop = true;
    pthread_cond_signal(&cache->worker_cond);
    pthread_t thread = cache->worker_thread;
    pthread_mutex_unlock(&cache->worker_lock);

    int rc = pthread_join(thread, NULL);
    if (rc != 0) {
        realtek_logf(adapter, TD_LOG_WARN, "pthread_join for MAC cache worker failed: %s", strerror(rc));
    }

    pthread_mutex_lock(&cache->worker_lock);
    cache->worker_started = false;
    cache->worker_stop = false;
    cache->refresh_requested = false;
    cache->worker_thread = (pthread_t)0;
    pthread_mutex_unlock(&cache->worker_lock);
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

static uint32_t mac_hash(const uint8_t mac[ETH_ALEN], uint16_t vlan) {
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < ETH_ALEN; ++i) {
        hash ^= mac[i];
        hash *= 1099511628211ULL;
    }
    hash ^= (uint64_t)vlan & 0x0FFFU;
    hash *= 1099511628211ULL;
    return (uint32_t)(hash % TD_REALTEK_MAC_BUCKET_COUNT);
}

static void mac_cache_clear_buckets(struct realtek_mac_cache *cache) {
    if (!cache) {
        return;
    }
    for (size_t i = 0; i < TD_REALTEK_MAC_BUCKET_COUNT; ++i) {
        struct mac_bucket_entry *node = cache->buckets[i];
        while (node) {
            struct mac_bucket_entry *next = node->next;
            free(node);
            node = next;
        }
        cache->buckets[i] = NULL;
    }
}

static void mac_cache_destroy(struct td_adapter *adapter) {
    if (!adapter) {
        return;
    }

    struct realtek_mac_cache *cache = &adapter->mac_cache;
    mac_cache_stop_worker(adapter);

    pthread_rwlock_wrlock(&cache->map_lock);
    mac_cache_clear_buckets(cache);
    SwUcMacEntry *entries = cache->entries;
    cache->entries = NULL;
    cache->capacity = 0;
    cache->version = 0ULL;
    cache->last_refresh.tv_sec = 0;
    cache->last_refresh.tv_nsec = 0;
    pthread_rwlock_unlock(&cache->map_lock);

    free(entries);

    pthread_rwlock_destroy(&cache->map_lock);
    pthread_mutex_destroy(&cache->worker_lock);
    pthread_cond_destroy(&cache->worker_cond);
    cache->refresh_cb = NULL;
    cache->refresh_ctx = NULL;
}

static void mac_cache_request_refresh(struct td_adapter *adapter) {
    if (!adapter) {
        return;
    }
    struct realtek_mac_cache *cache = &adapter->mac_cache;
    pthread_mutex_lock(&cache->worker_lock);
    cache->refresh_requested = true;
    pthread_cond_signal(&cache->worker_cond);
    pthread_mutex_unlock(&cache->worker_lock);
}

static bool mac_cache_should_refresh(struct realtek_mac_cache *cache, const struct timespec *now) {
    if (!cache) {
        return false;
    }
    if (cache->version == 0ULL) {
        return true;
    }
    if (!now) {
        return false;
    }
    uint64_t elapsed = timespec_diff_ms(&cache->last_refresh, now);
    return elapsed >= cache->ttl_ms;
}

static bool mac_cache_refresh(struct td_adapter *adapter, bool force);

static bool mac_cache_ensure_capacity(struct td_adapter *adapter) {
    if (!adapter) {
        return false;
    }

    struct realtek_mac_cache *cache = &adapter->mac_cache;
    if (cache->capacity > 0 && cache->entries) {
        return true;
    }

    uint32_t capacity = 0;
    int rc = td_switch_mac_get_capacity(&capacity);
    if (rc != 0 || capacity == 0) {
        realtek_logf(adapter, TD_LOG_WARN, "td_switch_mac_get_capacity failed: %d", rc);
        return false;
    }

    SwUcMacEntry *entries = calloc(capacity, sizeof(SwUcMacEntry));
    if (!entries) {
        realtek_logf(adapter, TD_LOG_ERROR, "failed to allocate MAC cache buffer for %u entries", capacity);
        return false;
    }

    cache->entries = entries;
    cache->capacity = capacity;
    cache->version = 0ULL;
    cache->last_refresh.tv_sec = 0;
    cache->last_refresh.tv_nsec = 0;
    return true;
}

static bool mac_cache_refresh(struct td_adapter *adapter, bool force) {
    if (!adapter) {
        return false;
    }

    if (!mac_cache_ensure_capacity(adapter)) {
        return false;
    }

    struct realtek_mac_cache *cache = &adapter->mac_cache;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    pthread_rwlock_wrlock(&cache->map_lock);

    if (!force && cache->version != 0ULL && !mac_cache_should_refresh(cache, &start)) {
        pthread_rwlock_unlock(&cache->map_lock);
        return true;
    }

    uint32_t count = 0;
    int rc = td_switch_mac_snapshot(cache->entries, &count);
    if (rc != 0) {
        pthread_rwlock_unlock(&cache->map_lock);
        realtek_logf(adapter, TD_LOG_WARN, "td_switch_mac_snapshot failed: %d", rc);
        pthread_mutex_lock(&cache->worker_lock);
        td_adapter_mac_locator_refresh_cb cb = cache->refresh_cb;
        void *ctx = cache->refresh_ctx;
        pthread_mutex_unlock(&cache->worker_lock);
        if (cb) {
            cb(0ULL, ctx);
        }
        return false;
    }

    uint32_t snapshot_count = count;
    if (snapshot_count > cache->capacity) {
        realtek_logf(adapter, TD_LOG_WARN, "snapshot count %u exceeds capacity %u, truncating", snapshot_count, cache->capacity);
        count = cache->capacity;
    }

    mac_cache_clear_buckets(cache);

    uint32_t inserted = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const SwUcMacEntry *entry = &cache->entries[i];
        struct mac_bucket_entry *node = calloc(1, sizeof(*node));
        if (!node) {
            realtek_logf(adapter, TD_LOG_ERROR, "failed to allocate mac bucket entry");
            continue;
        }
        memcpy(node->mac, entry->mac, ETH_ALEN);
        node->vlan = entry->vlan;
        node->ifindex = entry->ifindex;
        uint32_t bucket = mac_hash(node->mac, node->vlan);
        node->next = cache->buckets[bucket];
        cache->buckets[bucket] = node;
        inserted += 1U;
    }

    cache->version += 1ULL;

    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    cache->last_refresh = end;

    uint64_t elapsed_ms = timespec_diff_ms(&start, &end);
    uint64_t version = cache->version;
    bool truncated = inserted < snapshot_count;

    pthread_rwlock_unlock(&cache->map_lock);

    pthread_mutex_lock(&cache->worker_lock);
    td_adapter_mac_locator_refresh_cb cb = cache->refresh_cb;
    void *ctx = cache->refresh_ctx;
    pthread_mutex_unlock(&cache->worker_lock);

    if (truncated) {
        realtek_logf(adapter, TD_LOG_WARN, "MAC cache truncated: inserted=%u snapshot=%u", inserted, snapshot_count);
    }

    realtek_logf(adapter, TD_LOG_DEBUG, "MAC cache refresh done: version=%" PRIu64 " entries=%u elapsed=%" PRIu64 "ms",
                 version,
                 inserted,
                 elapsed_ms);

    if (cb) {
        cb(version, ctx);
    }

    return true;
}

static void *mac_cache_worker_main(void *arg) {
    struct td_adapter *adapter = (struct td_adapter *)arg;
    if (!adapter) {
        return NULL;
    }

    struct realtek_mac_cache *cache = &adapter->mac_cache;
    pthread_mutex_lock(&cache->worker_lock);
    while (!cache->worker_stop) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        bool stale = false;
        pthread_rwlock_rdlock(&cache->map_lock);
        stale = mac_cache_should_refresh(cache, &now);
        pthread_rwlock_unlock(&cache->map_lock);

        bool should_refresh = cache->refresh_requested || stale;
        if (!should_refresh) {
            struct timespec wake = timespec_add_ms(&now, cache->ttl_ms);
            (void)pthread_cond_timedwait(&cache->worker_cond, &cache->worker_lock, &wake);
            continue;
        }

        cache->refresh_requested = false;
        pthread_mutex_unlock(&cache->worker_lock);

        (void)mac_cache_refresh(adapter, true);

        pthread_mutex_lock(&cache->worker_lock);
    }
    pthread_mutex_unlock(&cache->worker_lock);
    return NULL;
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

    mac_cache_init(&adapter->mac_cache);

    if (!mac_cache_ensure_capacity(adapter)) {
        mac_cache_destroy(adapter);
        pthread_mutex_destroy(&adapter->state_lock);
        pthread_mutex_destroy(&adapter->send_lock);
        free(adapter);
        return TD_ADAPTER_ERR_NOT_READY;
    }

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

    mac_cache_destroy(adapter);

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

    if (!mac_cache_start_worker(adapter)) {
        atomic_store(&adapter->running, false);
        if (adapter->rx_fd >= 0) {
            close(adapter->rx_fd);
            adapter->rx_fd = -1;
        }
        if (adapter->tx_fd >= 0) {
            close(adapter->tx_fd);
            adapter->tx_fd = -1;
        }
        return TD_ADAPTER_ERR_SYS;
    }
    mac_cache_request_refresh(adapter);

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

    mac_cache_stop_worker(adapter);

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

static td_adapter_result_t realtek_mac_locator_lookup(td_adapter_t *handle,
                                                      const uint8_t mac[ETH_ALEN],
                                                      uint16_t vlan_id,
                                                      uint32_t *ifindex_out,
                                                      uint64_t *version_out) {
    if (!handle || !mac) {
        return TD_ADAPTER_ERR_INVALID_ARG;
    }
    if (vlan_id > 4094U) {
        return TD_ADAPTER_ERR_INVALID_ARG;
    }

    struct td_adapter *adapter = handle;
    struct realtek_mac_cache *cache = &adapter->mac_cache;

    for (int attempt = 0; attempt < 2; ++attempt) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        pthread_rwlock_rdlock(&cache->map_lock);
        uint64_t version = cache->version;
        bool need_refresh = (version == 0ULL) || mac_cache_should_refresh(cache, &now);
        if (need_refresh) {
            pthread_rwlock_unlock(&cache->map_lock);
            if (attempt == 0) {
                if (!mac_cache_refresh(adapter, true)) {
                    return TD_ADAPTER_ERR_NOT_READY;
                }
                continue;
            }
            return TD_ADAPTER_ERR_NOT_READY;
        }

        uint32_t bucket = mac_hash(mac, vlan_id);
        for (struct mac_bucket_entry *node = cache->buckets[bucket]; node; node = node->next) {
            if (node->vlan == vlan_id && memcmp(node->mac, mac, ETH_ALEN) == 0) {
                if (ifindex_out) {
                    *ifindex_out = node->ifindex;
                }
                if (version_out) {
                    *version_out = version;
                }
                pthread_rwlock_unlock(&cache->map_lock);
                return TD_ADAPTER_OK;
            }
        }

        if (ifindex_out) {
            *ifindex_out = 0U;
        }
        if (version_out) {
            *version_out = version;
        }
        pthread_rwlock_unlock(&cache->map_lock);
        return TD_ADAPTER_ERR_NOT_READY;
    }

    return TD_ADAPTER_ERR_NOT_READY;
}

static td_adapter_result_t realtek_mac_locator_subscribe(td_adapter_t *handle,
                                                         td_adapter_mac_locator_refresh_cb cb,
                                                         void *ctx) {
    if (!handle || !cb) {
        return TD_ADAPTER_ERR_INVALID_ARG;
    }

    struct td_adapter *adapter = handle;
    struct realtek_mac_cache *cache = &adapter->mac_cache;

    pthread_mutex_lock(&cache->worker_lock);
    if (cache->refresh_cb) {
        pthread_mutex_unlock(&cache->worker_lock);
        return TD_ADAPTER_ERR_ALREADY;
    }
    cache->refresh_cb = cb;
    cache->refresh_ctx = ctx;
    pthread_mutex_unlock(&cache->worker_lock);

    if (!mac_cache_start_worker(adapter)) {
        pthread_mutex_lock(&cache->worker_lock);
        cache->refresh_cb = NULL;
        cache->refresh_ctx = NULL;
        pthread_mutex_unlock(&cache->worker_lock);
        return TD_ADAPTER_ERR_SYS;
    }

    mac_cache_request_refresh(adapter);
    return TD_ADAPTER_OK;
}

static td_adapter_result_t realtek_mac_locator_get_version(td_adapter_t *handle,
                                                           uint64_t *version_out) {
    if (!handle || !version_out) {
        return TD_ADAPTER_ERR_INVALID_ARG;
    }

    struct td_adapter *adapter = handle;
    struct realtek_mac_cache *cache = &adapter->mac_cache;

    pthread_rwlock_rdlock(&cache->map_lock);
    uint64_t version = cache->version;
    pthread_rwlock_unlock(&cache->map_lock);

    *version_out = version;
    if (version == 0ULL) {
        return TD_ADAPTER_ERR_NOT_READY;
    }
    return TD_ADAPTER_OK;
}

static const struct td_adapter_mac_locator_ops g_realtek_mac_locator_ops = {
    .lookup = realtek_mac_locator_lookup,
    .subscribe = realtek_mac_locator_subscribe,
    .get_version = realtek_mac_locator_get_version,
};

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
    .mac_locator_ops = &g_realtek_mac_locator_ops,
};

const struct td_adapter_descriptor *td_realtek_adapter_descriptor(void) {
    static const struct td_adapter_descriptor descriptor = {
        .name = "realtek",
        .ops = &g_realtek_ops,
    };
    return &descriptor;
}
