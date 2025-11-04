#define _GNU_SOURCE

#include "terminal_manager.h"

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "td_logging.h"

#ifndef TERMINAL_BUCKET_COUNT
#define TERMINAL_BUCKET_COUNT 256
#endif

#ifndef TERMINAL_SCAN_INTERVAL_DEFAULT_MS
#define TERMINAL_SCAN_INTERVAL_DEFAULT_MS 1000U
#endif

#ifndef TERMINAL_KEEPALIVE_INTERVAL_DEFAULT_SEC
#define TERMINAL_KEEPALIVE_INTERVAL_DEFAULT_SEC 120U
#endif

#ifndef TERMINAL_KEEPALIVE_MISS_DEFAULT
#define TERMINAL_KEEPALIVE_MISS_DEFAULT 3U
#endif

#ifndef TERMINAL_IFACE_INVALID_HOLDOFF_DEFAULT_SEC
#define TERMINAL_IFACE_INVALID_HOLDOFF_DEFAULT_SEC 1800U
#endif

#ifndef TERMINAL_DEFAULT_VLAN_IFACE_FORMAT
#define TERMINAL_DEFAULT_VLAN_IFACE_FORMAT "vlan%u"
#endif

#ifndef TERMINAL_DEFAULT_MAX_TERMINALS
#define TERMINAL_DEFAULT_MAX_TERMINALS 1000U
#endif

struct terminal_event_node {
    terminal_event_record_t record;
    struct terminal_event_node *next;
};

struct terminal_event_queue {
    struct terminal_event_node *head;
    struct terminal_event_node *tail;
    size_t size;
};

struct probe_task {
    terminal_probe_request_t request;
    struct probe_task *next;
};

static pthread_mutex_t g_active_manager_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct terminal_manager *g_active_manager = NULL;

static void bind_active_manager(struct terminal_manager *mgr);
static void unbind_active_manager(struct terminal_manager *mgr);

struct terminal_manager *terminal_manager_get_active(void) {
    pthread_mutex_lock(&g_active_manager_mutex);
    struct terminal_manager *mgr = g_active_manager;
    pthread_mutex_unlock(&g_active_manager_mutex);
    return mgr;
}

struct terminal_manager {
    struct terminal_manager_config cfg;
    td_adapter_t *adapter;
    terminal_probe_fn probe_cb;
    void *probe_ctx;

    pthread_mutex_t lock;
    struct terminal_entry *table[TERMINAL_BUCKET_COUNT];

    pthread_mutex_t worker_lock;
    pthread_cond_t worker_cond;
    bool worker_stop;
    bool worker_started;
    pthread_t worker_thread;

    terminal_event_callback_fn event_cb;
    void *event_cb_ctx;
    struct terminal_event_queue events;
    size_t terminal_count;
    size_t max_terminals;
    struct terminal_manager_stats stats;
};

static bool is_iface_available(const struct terminal_entry *entry);
static void snapshot_from_entry(const struct terminal_entry *entry, terminal_snapshot_t *snapshot);
static uint32_t snapshot_port(const terminal_snapshot_t *snapshot);
static uint32_t entry_port(const struct terminal_entry *entry);
static void event_queue_push(struct terminal_event_queue *queue, struct terminal_event_node *node);
static void queue_event(struct terminal_manager *mgr,
                        terminal_event_tag_t tag,
                        const struct terminal_key *key,
                        uint32_t port);
static void queue_add_event(struct terminal_manager *mgr,
                            const struct terminal_entry *entry);
static void queue_remove_event(struct terminal_manager *mgr,
                               const terminal_snapshot_t *snapshot);
static void queue_modify_event_if_port_changed(struct terminal_manager *mgr,
                                               const terminal_snapshot_t *before,
                                               const struct terminal_entry *entry);
static void free_event_queue(struct terminal_event_queue *queue);
static void terminal_manager_maybe_dispatch_events(struct terminal_manager *mgr);

static void monotonic_now(struct timespec *ts) {
    if (!ts) {
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, ts);
}

static uint64_t timespec_diff_ms(const struct timespec *start,
                                 const struct timespec *end) {
    if (!start || !end) {
        return 0ULL;
    }

    time_t sec = end->tv_sec - start->tv_sec;
    long nsec = end->tv_nsec - start->tv_nsec;

    if (sec < 0) {
        return 0ULL;
    }

    if (nsec < 0) {
        if (sec == 0) {
            return 0ULL;
        }
        sec -= 1;
        nsec += 1000000000L;
    }

    uint64_t millis = (uint64_t)sec * 1000ULL;
    millis += (uint64_t)(nsec / 1000000L);
    return millis;
}

static void bind_active_manager(struct terminal_manager *mgr) {
    pthread_mutex_lock(&g_active_manager_mutex);
    if (g_active_manager && g_active_manager != mgr) {
        td_log_writef(TD_LOG_WARN,
                      "terminal_manager",
                      "replacing active manager instance");
    }
    g_active_manager = mgr;
    pthread_mutex_unlock(&g_active_manager_mutex);
}

static void unbind_active_manager(struct terminal_manager *mgr) {
    pthread_mutex_lock(&g_active_manager_mutex);
    if (g_active_manager == mgr) {
        g_active_manager = NULL;
    }
    pthread_mutex_unlock(&g_active_manager_mutex);
}

static size_t hash_key(const struct terminal_key *key) {
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < ETH_ALEN; ++i) {
        hash ^= key->mac[i];
        hash *= 1099511628211ULL;
    }
    const uint8_t *ip_bytes = (const uint8_t *)&key->ip.s_addr;
    for (size_t i = 0; i < sizeof(key->ip.s_addr); ++i) {
        hash ^= ip_bytes[i];
        hash *= 1099511628211ULL;
    }
    return (size_t)hash;
}

static void format_terminal_identity(const struct terminal_key *key,
                                     char mac_buf[18],
                                     char ip_buf[INET_ADDRSTRLEN]) {
    if (!key) {
        if (mac_buf) {
            snprintf(mac_buf, 18, "<invalid>");
        }
        if (ip_buf) {
            snprintf(ip_buf, INET_ADDRSTRLEN, "<invalid>");
        }
        return;
    }

    if (mac_buf) {
        snprintf(mac_buf,
                 18,
                 "%02x:%02x:%02x:%02x:%02x:%02x",
                 key->mac[0],
                 key->mac[1],
                 key->mac[2],
                 key->mac[3],
                 key->mac[4],
                 key->mac[5]);
    }

    if (ip_buf) {
        inet_ntop(AF_INET, &key->ip, ip_buf, INET_ADDRSTRLEN);
    }
}

static void snapshot_from_entry(const struct terminal_entry *entry, terminal_snapshot_t *snapshot) {
    if (!entry || !snapshot) {
        return;
    }

    snapshot->key = entry->key;
    snapshot->meta = entry->meta;
    snapshot->state = entry->state;
    snprintf(snapshot->tx_iface, sizeof(snapshot->tx_iface), "%s", entry->tx_iface);
    snapshot->tx_ifindex = entry->tx_ifindex;
    snapshot->last_seen = entry->last_seen;
    snapshot->last_probe = entry->last_probe;
    snapshot->failed_probes = entry->failed_probes;
}

static uint32_t snapshot_port(const terminal_snapshot_t *snapshot) {
    if (!snapshot) {
        return 0U;
    }
    if (snapshot->meta.lport > 0U) {
        return snapshot->meta.lport;
    }
    if (snapshot->tx_ifindex > 0) {
        return (uint32_t)snapshot->tx_ifindex;
    }
    return 0U;
}

static uint32_t entry_port(const struct terminal_entry *entry) {
    if (!entry) {
        return 0U;
    }
    if (entry->meta.lport > 0U) {
        return entry->meta.lport;
    }
    if (entry->tx_ifindex > 0) {
        return (uint32_t)entry->tx_ifindex;
    }
    return 0U;
}

static void event_queue_push(struct terminal_event_queue *queue, struct terminal_event_node *node) {
    if (!queue || !node) {
        return;
    }
    node->next = NULL;
    if (!queue->head) {
        queue->head = node;
        queue->tail = node;
    } else {
        queue->tail->next = node;
        queue->tail = node;
    }
    queue->size += 1;
}

static void free_event_queue(struct terminal_event_queue *queue) {
    if (!queue) {
        return;
    }
    struct terminal_event_node *node = queue->head;
    while (node) {
        struct terminal_event_node *next = node->next;
        free(node);
        node = next;
    }
    queue->head = NULL;
    queue->tail = NULL;
    queue->size = 0;
}

static void queue_event(struct terminal_manager *mgr,
                        terminal_event_tag_t tag,
                        const struct terminal_key *key,
                        uint32_t port) {
    if (!mgr || !mgr->event_cb || !key) {
        return;
    }
    struct terminal_event_node *node = calloc(1, sizeof(*node));
    if (!node) {
        td_log_writef(TD_LOG_WARN, "terminal_manager", "failed to allocate event node");
        return;
    }
    memcpy(node->record.key.mac, key->mac, ETH_ALEN);
    node->record.key.ip = key->ip;
    node->record.port = port;
    node->record.tag = tag;
    event_queue_push(&mgr->events, node);
}

static void queue_add_event(struct terminal_manager *mgr,
                            const struct terminal_entry *entry) {
    if (!mgr || !entry) {
        return;
    }
    queue_event(mgr, TERMINAL_EVENT_TAG_ADD, &entry->key, entry_port(entry));
}

static void queue_remove_event(struct terminal_manager *mgr,
                               const terminal_snapshot_t *snapshot) {
    if (!mgr || !snapshot) {
        return;
    }
    queue_event(mgr, TERMINAL_EVENT_TAG_DEL, &snapshot->key, snapshot_port(snapshot));
}

static void queue_modify_event_if_port_changed(struct terminal_manager *mgr,
                                               const terminal_snapshot_t *before,
                                               const struct terminal_entry *entry) {
    if (!mgr || !before || !entry || !mgr->event_cb) {
        return;
    }
    uint32_t before_port = snapshot_port(before);
    uint32_t after_port = entry_port(entry);
    if (before_port == after_port) {
        return;
    }
    queue_event(mgr, TERMINAL_EVENT_TAG_MOD, &entry->key, after_port);
}

static void terminal_manager_maybe_dispatch_events(struct terminal_manager *mgr) {
    if (!mgr) {
        return;
    }

    struct terminal_event_node *head = NULL;
    size_t count = 0;

    pthread_mutex_lock(&mgr->lock);

    if (!mgr->event_cb) {
        free_event_queue(&mgr->events);
        pthread_mutex_unlock(&mgr->lock);
        return;
    }

    if (!mgr->events.head) {
        pthread_mutex_unlock(&mgr->lock);
        return;
    }

    head = mgr->events.head;
    count = mgr->events.size;
    mgr->events.head = NULL;
    mgr->events.tail = NULL;
    mgr->events.size = 0;
    terminal_event_callback_fn callback = mgr->event_cb;
    void *callback_ctx = mgr->event_cb_ctx;

    pthread_mutex_unlock(&mgr->lock);

    terminal_event_record_t *records = NULL;
    if (count > 0) {
        records = calloc(count, sizeof(*records));
        if (!records) {
            td_log_writef(TD_LOG_WARN, "terminal_manager", "failed to allocate event batch (%zu)", count);
        }
    }

    size_t idx = 0;
    struct terminal_event_node *node = head;
    while (node) {
        struct terminal_event_node *next = node->next;
        if (records && idx < count) {
            records[idx++] = node->record;
        }
        free(node);
        node = next;
    }

    bool dispatched = false;
    if (callback && records && count > 0) {
        callback(records, count, callback_ctx);
        dispatched = true;
    }

    free(records);

    if (count > 0) {
        pthread_mutex_lock(&mgr->lock);
        if (dispatched) {
            mgr->stats.events_dispatched += count;
        } else {
            mgr->stats.event_dispatch_failures += 1;
        }
        pthread_mutex_unlock(&mgr->lock);
    }
}

static struct timespec timespec_add_ms(const struct timespec *base, unsigned int ms) {
    struct timespec result = *base;
    result.tv_sec += ms / 1000U;
    result.tv_nsec += (long)(ms % 1000U) * 1000000L;
    if (result.tv_nsec >= 1000000000L) {
        result.tv_sec += 1;
        result.tv_nsec -= 1000000000L;
    }
    return result;
}

static void *terminal_manager_worker(void *arg) {
    struct terminal_manager *mgr = (struct terminal_manager *)arg;

    pthread_mutex_lock(&mgr->worker_lock);
    while (!mgr->worker_stop) {
    struct timespec now;
    monotonic_now(&now);
        struct timespec wake = timespec_add_ms(&now, mgr->cfg.scan_interval_ms);

        int rc = 0;
        while (!mgr->worker_stop && rc != ETIMEDOUT) {
            rc = pthread_cond_timedwait(&mgr->worker_cond, &mgr->worker_lock, &wake);
        }

        if (mgr->worker_stop) {
            break;
        }

        pthread_mutex_unlock(&mgr->worker_lock);
        terminal_manager_on_timer(mgr);
        pthread_mutex_lock(&mgr->worker_lock);
    }
    pthread_mutex_unlock(&mgr->worker_lock);
    return NULL;
}

static struct terminal_entry *find_entry(struct terminal_manager *mgr,
                                         const struct terminal_key *key,
                                         size_t bucket,
                                         struct terminal_entry ***prev_next_out) {
    struct terminal_entry **prev_next = &mgr->table[bucket];
    struct terminal_entry *node = mgr->table[bucket];
    while (node) {
        if (memcmp(node->key.mac, key->mac, ETH_ALEN) == 0 &&
            node->key.ip.s_addr == key->ip.s_addr) {
                if (prev_next_out) {
                    *prev_next_out = prev_next;
                }
                return node;
            }
        prev_next = &node->next;
        node = node->next;
    }
    if (prev_next_out) {
        *prev_next_out = prev_next;
    }
    return NULL;
}

static const char *state_to_string(terminal_state_t state) {
    switch (state) {
    case TERMINAL_STATE_ACTIVE:
        return "ACTIVE";
    case TERMINAL_STATE_PROBING:
        return "PROBING";
    case TERMINAL_STATE_IFACE_INVALID:
        return "IFACE_INVALID";
    default:
        return "UNKNOWN";
    }
}

static void resolve_tx_interface(struct terminal_manager *mgr, struct terminal_entry *entry) {
    if (!entry) {
        return;
    }
    char candidate[IFNAMSIZ] = {0};
    int candidate_ifindex = -1;
    bool resolved = false;

    if (mgr->cfg.iface_selector) {
        resolved = mgr->cfg.iface_selector(&entry->meta, candidate, &candidate_ifindex, mgr->cfg.iface_selector_ctx);
    }

    if (!resolved && mgr->cfg.vlan_iface_format && entry->meta.vlan_id >= 0) {
        snprintf(candidate, sizeof(candidate), mgr->cfg.vlan_iface_format, (unsigned int)entry->meta.vlan_id);
        candidate_ifindex = (int)if_nametoindex(candidate);
        if (candidate_ifindex <= 0) {
            candidate_ifindex = -1;
        }
        resolved = true;
    }

    if (resolved) {
        snprintf(entry->tx_iface, sizeof(entry->tx_iface), "%s", candidate);
        entry->tx_ifindex = candidate_ifindex > 0 ? candidate_ifindex : -1;
    } else if (entry->tx_iface[0] == '\0') {
        entry->tx_ifindex = -1;
    }
}

static void apply_packet_binding(struct terminal_manager *mgr,
                                 struct terminal_entry *entry,
                                 const struct td_adapter_packet_view *packet) {
    if (!packet || !entry) {
        return;
    }

    entry->meta.vlan_id = packet->vlan_id;
    if (packet->lport > 0U) {
        entry->meta.lport = packet->lport;
    }

    resolve_tx_interface(mgr, entry);
}

static struct terminal_entry *create_entry(const struct terminal_key *key,
                                           struct terminal_manager *mgr,
                                           const struct td_adapter_packet_view *packet) {
    struct terminal_entry *entry = calloc(1, sizeof(*entry));
    if (!entry) {
        return NULL;
    }

    entry->key = *key;
    entry->state = TERMINAL_STATE_ACTIVE;
    monotonic_now(&entry->last_seen);
    entry->last_probe.tv_sec = 0;
    entry->last_probe.tv_nsec = 0;
    entry->failed_probes = 0;
    memset(&entry->meta, 0, sizeof(entry->meta));
    entry->meta.vlan_id = -1;
    entry->meta.lport = 0U;
    entry->tx_iface[0] = '\0';
    entry->tx_ifindex = -1;
    entry->next = NULL;

    if (packet) {
        apply_packet_binding(mgr, entry, packet);
    }

    return entry;
}

struct terminal_manager *terminal_manager_create(const struct terminal_manager_config *cfg,
                                                  td_adapter_t *adapter,
                                                  terminal_probe_fn probe_cb,
                                                  void *probe_ctx) {
    if (!cfg || !adapter) {
        return NULL;
    }

    struct terminal_manager *mgr = calloc(1, sizeof(*mgr));
    if (!mgr) {
        return NULL;
    }

    mgr->cfg = *cfg;
    if (mgr->cfg.keepalive_interval_sec == 0) {
        mgr->cfg.keepalive_interval_sec = TERMINAL_KEEPALIVE_INTERVAL_DEFAULT_SEC;
    }
    if (mgr->cfg.keepalive_miss_threshold == 0) {
        mgr->cfg.keepalive_miss_threshold = TERMINAL_KEEPALIVE_MISS_DEFAULT;
    }
    if (mgr->cfg.iface_invalid_holdoff_sec == 0) {
        mgr->cfg.iface_invalid_holdoff_sec = TERMINAL_IFACE_INVALID_HOLDOFF_DEFAULT_SEC;
    }
    if (mgr->cfg.scan_interval_ms == 0) {
        mgr->cfg.scan_interval_ms = TERMINAL_SCAN_INTERVAL_DEFAULT_MS;
    }
    if (!mgr->cfg.vlan_iface_format) {
        mgr->cfg.vlan_iface_format = TERMINAL_DEFAULT_VLAN_IFACE_FORMAT;
    }
    if (mgr->cfg.max_terminals == 0) {
        mgr->cfg.max_terminals = TERMINAL_DEFAULT_MAX_TERMINALS;
    }
    mgr->adapter = adapter;
    mgr->probe_cb = probe_cb;
    mgr->probe_ctx = probe_ctx;
    pthread_mutex_init(&mgr->lock, NULL);
    pthread_mutex_init(&mgr->worker_lock, NULL);

    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
    pthread_cond_init(&mgr->worker_cond, &cond_attr);
    pthread_condattr_destroy(&cond_attr);
    mgr->worker_stop = false;
    mgr->worker_started = false;
    mgr->event_cb = NULL;
    mgr->event_cb_ctx = NULL;
    mgr->events.head = NULL;
    mgr->events.tail = NULL;
    mgr->events.size = 0;
    mgr->terminal_count = 0;
    mgr->max_terminals = mgr->cfg.max_terminals;
    memset(&mgr->stats, 0, sizeof(mgr->stats));

    for (size_t i = 0; i < TERMINAL_BUCKET_COUNT; ++i) {
        mgr->table[i] = NULL;
    }

    if (pthread_create(&mgr->worker_thread, NULL, terminal_manager_worker, mgr) == 0) {
        mgr->worker_started = true;
    } else {
        td_log_writef(TD_LOG_ERROR, "terminal_manager", "failed to start timer worker thread");
    }

    bind_active_manager(mgr);

    return mgr;
}

void terminal_manager_destroy(struct terminal_manager *mgr) {
    if (!mgr) {
        return;
    }

    unbind_active_manager(mgr);

    pthread_mutex_lock(&mgr->worker_lock);
    mgr->worker_stop = true;
    pthread_cond_broadcast(&mgr->worker_cond);
    pthread_mutex_unlock(&mgr->worker_lock);

    if (mgr->worker_started) {
        pthread_join(mgr->worker_thread, NULL);
        mgr->worker_started = false;
    }

    pthread_mutex_lock(&mgr->lock);
    for (size_t i = 0; i < TERMINAL_BUCKET_COUNT; ++i) {
        struct terminal_entry *node = mgr->table[i];
        while (node) {
            struct terminal_entry *next = node->next;
            free(node);
            node = next;
        }
        mgr->table[i] = NULL;
    }
    free_event_queue(&mgr->events);
    pthread_mutex_unlock(&mgr->lock);

    pthread_mutex_destroy(&mgr->lock);
    pthread_mutex_destroy(&mgr->worker_lock);
    pthread_cond_destroy(&mgr->worker_cond);
    free(mgr);
}

static void log_transition(const struct terminal_entry *entry, terminal_state_t new_state) {
    char mac_buf[18];
    snprintf(mac_buf, sizeof(mac_buf), "%02x:%02x:%02x:%02x:%02x:%02x",
             entry->key.mac[0], entry->key.mac[1], entry->key.mac[2],
             entry->key.mac[3], entry->key.mac[4], entry->key.mac[5]);

    char ip_buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &entry->key.ip, ip_buf, sizeof(ip_buf));

    td_log_writef(TD_LOG_DEBUG,
                  "terminal_manager",
                  "terminal %s/%s state %s -> %s (iface=%s vlan=%d)",
                  mac_buf,
                  ip_buf,
                  state_to_string(entry->state),
                  state_to_string(new_state),
                  entry->tx_iface,
                  entry->meta.vlan_id);
}

static void set_state(struct terminal_entry *entry, terminal_state_t new_state) {
    if (entry->state == new_state) {
        return;
    }
    log_transition(entry, new_state);
    entry->state = new_state;
}

void terminal_manager_on_packet(struct terminal_manager *mgr,
                                const struct td_adapter_packet_view *packet) {
    if (!mgr || !packet) {
        return;
    }

    if (packet->payload_len < sizeof(struct ether_arp)) {
        return;
    }

    const struct ether_arp *arp = (const struct ether_arp *)packet->payload;
    struct terminal_key key;
    memcpy(key.mac, arp->arp_sha, ETH_ALEN);
    memcpy(&key.ip.s_addr, arp->arp_spa, sizeof(key.ip.s_addr));

    size_t bucket = hash_key(&key) % TERMINAL_BUCKET_COUNT;

    pthread_mutex_lock(&mgr->lock);

    bool track_events = mgr->event_cb != NULL;
    bool newly_created = false;
    terminal_snapshot_t before_snapshot;
    bool have_before_snapshot = false;

    struct terminal_entry **prev_next = NULL;
    struct terminal_entry *entry = find_entry(mgr, &key, bucket, &prev_next);
    if (!entry) {
        if (mgr->terminal_count >= mgr->max_terminals) {
            char mac_buf[18];
            char ip_buf[INET_ADDRSTRLEN];
            format_terminal_identity(&key, mac_buf, ip_buf);
            td_log_writef(TD_LOG_WARN,
                          "terminal_manager",
                          "terminal capacity reached (%zu/%zu); dropping %s/%s",
                          mgr->terminal_count,
                          mgr->max_terminals,
                          mac_buf,
                          ip_buf);
            mgr->stats.capacity_drops += 1;
            pthread_mutex_unlock(&mgr->lock);
            return;
        }
        entry = create_entry(&key, mgr, packet);
        if (!entry) {
            char mac_buf[18];
            char ip_buf[INET_ADDRSTRLEN];
            format_terminal_identity(&key, mac_buf, ip_buf);
            td_log_writef(TD_LOG_ERROR,
                          "terminal_manager",
                          "failed to allocate terminal entry for %s/%s",
                          mac_buf,
                          ip_buf);
            pthread_mutex_unlock(&mgr->lock);
            return;
        }
        entry->next = mgr->table[bucket];
        mgr->table[bucket] = entry;
        td_log_writef(TD_LOG_INFO,
                      "terminal_manager",
                      "new terminal discovered on %s vlan=%d",
                      entry->tx_iface[0] ? entry->tx_iface : "<unresolved>",
                      entry->meta.vlan_id);
        newly_created = true;
        mgr->terminal_count += 1;
        mgr->stats.terminals_discovered += 1;
        mgr->stats.current_terminals = mgr->terminal_count;
    } else {
        if (track_events) {
            snapshot_from_entry(entry, &before_snapshot);
            have_before_snapshot = true;
        }
    monotonic_now(&entry->last_seen);
        apply_packet_binding(mgr, entry, packet);
    }

    entry->failed_probes = 0;
    monotonic_now(&entry->last_seen);

    if (!is_iface_available(entry)) {
        set_state(entry, TERMINAL_STATE_IFACE_INVALID);
    } else if (entry->state == TERMINAL_STATE_IFACE_INVALID) {
        set_state(entry, TERMINAL_STATE_PROBING);
    } else {
        set_state(entry, TERMINAL_STATE_ACTIVE);
    }

    if (newly_created) {
        queue_add_event(mgr, entry);
    } else if (have_before_snapshot) {
        queue_modify_event_if_port_changed(mgr, &before_snapshot, entry);
    }

    pthread_mutex_unlock(&mgr->lock);

    terminal_manager_maybe_dispatch_events(mgr);
}

static bool is_iface_available(const struct terminal_entry *entry) {
    return entry->tx_iface[0] != '\0';
}

static bool has_expired(const struct terminal_manager *mgr,
                        const struct terminal_entry *entry,
                        const struct timespec *now) {
    (void)mgr;
    if (entry->state != TERMINAL_STATE_IFACE_INVALID) {
        return false;
    }

    uint64_t elapsed_ms = timespec_diff_ms(&entry->last_seen, now);
    uint64_t holdoff_ms = (uint64_t)mgr->cfg.iface_invalid_holdoff_sec * 1000ULL;
    return elapsed_ms >= holdoff_ms;
}

void terminal_manager_on_timer(struct terminal_manager *mgr) {
    if (!mgr) {
        return;
    }

    struct timespec now;
    monotonic_now(&now);

    pthread_mutex_lock(&mgr->lock);

    struct probe_task *tasks_head = NULL;
    struct probe_task *tasks_tail = NULL;
    bool track_events = mgr->event_cb != NULL;

    for (size_t i = 0; i < TERMINAL_BUCKET_COUNT; ++i) {
        struct terminal_entry **prev_next = &mgr->table[i];
        struct terminal_entry *entry = mgr->table[i];
        while (entry) {
            bool remove = false;
            bool removed_due_to_probe_failure = false;
            terminal_snapshot_t before_snapshot;
            bool have_before_snapshot = false;

            if (track_events) {
                snapshot_from_entry(entry, &before_snapshot);
                have_before_snapshot = true;
            }

            if (has_expired(mgr, entry, &now)) {
                td_log_writef(TD_LOG_INFO,
                              "terminal_manager",
                              "terminal expired after iface invalid holdoff: state=%s", state_to_string(entry->state));
                remove = true;
            } else {
                time_t since_last_seen = now.tv_sec - entry->last_seen.tv_sec;
                time_t since_last_probe = entry->last_probe.tv_sec ? (now.tv_sec - entry->last_probe.tv_sec) : (time_t)mgr->cfg.keepalive_interval_sec;

                bool need_probe = since_last_seen >= (time_t)mgr->cfg.keepalive_interval_sec &&
                                   since_last_probe >= (time_t)mgr->cfg.keepalive_interval_sec;

                if (need_probe) {
                    if (!is_iface_available(entry)) {
                        set_state(entry, TERMINAL_STATE_IFACE_INVALID);
                    } else {
                        set_state(entry, TERMINAL_STATE_PROBING);
                        entry->last_probe = now;
                        entry->failed_probes += 1;

                        if (mgr->probe_cb) {
                            struct probe_task *task = calloc(1, sizeof(*task));
                            if (task) {
                                task->request.key = entry->key;
                                snprintf(task->request.tx_iface, sizeof(task->request.tx_iface), "%s", entry->tx_iface);
                                task->request.tx_ifindex = entry->tx_ifindex;
                                task->request.state_before_probe = entry->state;
                                if (!tasks_head) {
                                    tasks_head = task;
                                    tasks_tail = task;
                                } else {
                                    tasks_tail->next = task;
                                    tasks_tail = task;
                                }
                                mgr->stats.probes_scheduled += 1;
                            } else {
                                td_log_writef(TD_LOG_WARN,
                                              "terminal_manager",
                                              "failed to allocate probe task for terminal");
                            }
                        }

                        if (entry->failed_probes >= mgr->cfg.keepalive_miss_threshold) {
                            td_log_writef(TD_LOG_INFO,
                                          "terminal_manager",
                                          "terminal exceeded probe failure threshold (iface=%s)",
                                          entry->tx_iface);
                            remove = true;
                            removed_due_to_probe_failure = true;
                        }
                    }
                }
            }

            if (remove) {
                struct terminal_entry *to_free = entry;
                if (track_events) {
                    terminal_snapshot_t remove_snapshot;
                    snapshot_from_entry(entry, &remove_snapshot);
                    queue_remove_event(mgr, &remove_snapshot);
                }
                *prev_next = entry->next;
                entry = entry->next;
                if (mgr->terminal_count > 0) {
                    mgr->terminal_count -= 1;
                }
                mgr->stats.terminals_removed += 1;
                mgr->stats.current_terminals = mgr->terminal_count;
                if (removed_due_to_probe_failure) {
                    mgr->stats.probe_failures += 1;
                }
                free(to_free);
            } else {
                if (have_before_snapshot) {
                    queue_modify_event_if_port_changed(mgr, &before_snapshot, entry);
                }
                prev_next = &entry->next;
                entry = entry->next;
            }
        }
    }

    pthread_mutex_unlock(&mgr->lock);

    /* Execute probe callbacks outside the manager lock to avoid deadlocks. */
    struct probe_task *task = tasks_head;
    while (task) {
        if (mgr->probe_cb) {
            mgr->probe_cb(&task->request, mgr->probe_ctx);
        }
        struct probe_task *next = task->next;
        free(task);
        task = next;
    }

    terminal_manager_maybe_dispatch_events(mgr);
}

void terminal_manager_on_iface_event(struct terminal_manager *mgr,
                                     const struct td_adapter_iface_event *event) {
    if (!mgr || !event) {
        return;
    }

    pthread_mutex_lock(&mgr->lock);
    bool now_up_event = (event->flags_after & IFF_UP) != 0;
    if (now_up_event) {
        mgr->stats.iface_up_events += 1;
    } else {
        mgr->stats.iface_down_events += 1;
    }

    for (size_t i = 0; i < TERMINAL_BUCKET_COUNT; ++i) {
        for (struct terminal_entry *entry = mgr->table[i]; entry; entry = entry->next) {
            if (strncmp(entry->tx_iface, event->ifname, sizeof(entry->tx_iface)) == 0) {
                if (!now_up_event) {
                    set_state(entry, TERMINAL_STATE_IFACE_INVALID);
                    monotonic_now(&entry->last_seen);
                    entry->tx_ifindex = -1;
                } else {
                    set_state(entry, TERMINAL_STATE_PROBING);
                    entry->failed_probes = 0;
                    entry->last_probe.tv_sec = 0;
                    entry->last_probe.tv_nsec = 0;
                    int ifindex = (int)if_nametoindex(event->ifname);
                    if (ifindex > 0) {
                        entry->tx_ifindex = ifindex;
                    }
                }
            }
        }
    }

    pthread_mutex_unlock(&mgr->lock);

    terminal_manager_maybe_dispatch_events(mgr);
}

int terminal_manager_set_event_sink(struct terminal_manager *mgr,
                                    terminal_event_callback_fn callback,
                                    void *callback_ctx) {
    if (!mgr) {
        return -1;
    }

    pthread_mutex_lock(&mgr->lock);
    mgr->event_cb = callback;
    mgr->event_cb_ctx = callback_ctx;
    if (!callback) {
        free_event_queue(&mgr->events);
    }
    pthread_mutex_unlock(&mgr->lock);

    if (callback) {
        terminal_manager_maybe_dispatch_events(mgr);
    }

    return 0;
}

int terminal_manager_query_all(struct terminal_manager *mgr,
                               terminal_query_callback_fn callback,
                               void *callback_ctx) {
    if (!mgr || !callback) {
        return -1;
    }

    pthread_mutex_lock(&mgr->lock);

    size_t count = 0;
    for (size_t i = 0; i < TERMINAL_BUCKET_COUNT; ++i) {
        for (struct terminal_entry *entry = mgr->table[i]; entry; entry = entry->next) {
            ++count;
        }
    }

    terminal_event_record_t *records = NULL;
    if (count > 0) {
        records = calloc(count, sizeof(*records));
        if (!records) {
            pthread_mutex_unlock(&mgr->lock);
            return -1;
        }
    }

    size_t idx = 0;
    for (size_t i = 0; i < TERMINAL_BUCKET_COUNT; ++i) {
        for (struct terminal_entry *entry = mgr->table[i]; entry; entry = entry->next) {
            if (records && idx < count) {
                memcpy(records[idx].key.mac, entry->key.mac, ETH_ALEN);
                records[idx].key.ip = entry->key.ip;
                records[idx].port = entry_port(entry);
                records[idx].tag = TERMINAL_EVENT_TAG_ADD;
            }
            ++idx;
        }
    }

    pthread_mutex_unlock(&mgr->lock);

    if (records) {
        for (size_t i = 0; i < count; ++i) {
            if (!callback(&records[i], callback_ctx)) {
                break;
            }
        }
    }

    free(records);
    return 0;
}

void terminal_manager_flush_events(struct terminal_manager *mgr) {
    if (!mgr) {
        return;
    }
    terminal_manager_maybe_dispatch_events(mgr);
}

void terminal_manager_get_stats(struct terminal_manager *mgr,
                                struct terminal_manager_stats *out) {
    if (!mgr || !out) {
        return;
    }

    pthread_mutex_lock(&mgr->lock);
    mgr->stats.current_terminals = mgr->terminal_count;
    *out = mgr->stats;
    pthread_mutex_unlock(&mgr->lock);
}
