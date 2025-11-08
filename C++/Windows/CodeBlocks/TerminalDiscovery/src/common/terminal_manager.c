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
#include "td_time_utils.h"

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

struct mac_lookup_task {
    struct terminal_key key;
    int vlan_id;
    bool verify;
    struct mac_lookup_task *next;
};

struct iface_prefix_entry {
    struct in_addr network;
    struct in_addr address;
    uint8_t prefix_len;
    struct iface_prefix_entry *next;
};

struct iface_binding_entry {
    struct terminal_entry *terminal;
    struct iface_binding_entry *next;
};

struct iface_record {
    int kernel_ifindex;
    struct iface_prefix_entry *prefixes;
    struct iface_binding_entry *bindings;
    struct iface_record *next;
};

static pthread_mutex_t g_active_manager_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct terminal_manager *g_active_manager = NULL;

static void bind_active_manager(struct terminal_manager *mgr);
static void unbind_active_manager(struct terminal_manager *mgr);
static void mac_locator_on_refresh(uint64_t version, void *ctx);

struct terminal_manager *terminal_manager_get_active(void) {
    pthread_mutex_lock(&g_active_manager_mutex);
    struct terminal_manager *mgr = g_active_manager;
    pthread_mutex_unlock(&g_active_manager_mutex);
    return mgr;
}

struct terminal_manager {
    struct terminal_manager_config cfg;
    td_adapter_t *adapter;
    const struct td_adapter_ops *adapter_ops;
    const struct td_adapter_mac_locator_ops *mac_locator_ops;
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
    struct iface_record *iface_records;
    struct mac_lookup_task *mac_need_refresh_head;
    struct mac_lookup_task *mac_need_refresh_tail;
    struct mac_lookup_task *mac_pending_verify_head;
    struct mac_lookup_task *mac_pending_verify_tail;
    uint64_t mac_locator_version;
    bool mac_locator_subscribed;
    bool destroying;
};

static bool is_iface_available(const struct terminal_entry *entry);
static void snapshot_from_entry(const struct terminal_entry *entry, terminal_snapshot_t *snapshot);
static void event_queue_push(struct terminal_event_queue *queue, struct terminal_event_node *node);
static void queue_event(struct terminal_manager *mgr,
                        terminal_event_tag_t tag,
                        const struct terminal_key *key,
                        const struct terminal_metadata *meta);
static void queue_add_event(struct terminal_manager *mgr,
                            const struct terminal_entry *entry);
static void queue_remove_event(struct terminal_manager *mgr,
                               const terminal_snapshot_t *snapshot);
static void queue_modify_event_if_ifindex_changed(struct terminal_manager *mgr,
                                                  const terminal_snapshot_t *before,
                                                  const struct terminal_entry *entry);
static void free_event_queue(struct terminal_event_queue *queue);
static void terminal_manager_maybe_dispatch_events(struct terminal_manager *mgr);
static void set_state(struct terminal_entry *entry, terminal_state_t new_state);
static struct iface_record **find_iface_record_slot(struct terminal_manager *mgr, int kernel_ifindex);
static struct iface_record *get_iface_record(struct terminal_manager *mgr, int kernel_ifindex);
static bool iface_record_matches_ip(const struct iface_record *record, struct in_addr ip);
static bool iface_record_select_ip(const struct iface_record *record,
                                   struct in_addr terminal_ip,
                                   struct in_addr *out_ip);
static bool iface_binding_attach(struct terminal_manager *mgr,
                                 int kernel_ifindex,
                                 struct terminal_entry *entry);
static void iface_binding_detach(struct terminal_manager *mgr,
                                 int kernel_ifindex,
                                 struct terminal_entry *entry);
static void iface_record_prune_if_empty(struct iface_record **slot_ref);
static struct in_addr prefix_network(struct in_addr address, uint8_t prefix_len);
static bool ip_matches_prefix(struct in_addr ip,
                              struct in_addr network,
                              uint8_t prefix_len);
static bool iface_prefix_add(struct terminal_manager *mgr,
                             int ifindex,
                             struct in_addr network,
                                      struct in_addr address,
                             uint8_t prefix_len);
static bool iface_prefix_remove(struct terminal_manager *mgr,
                                int ifindex,
                                struct in_addr network,
                                          struct in_addr address,
                                uint8_t prefix_len);

static void monotonic_now(struct timespec *ts) {
    if (!ts) {
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, ts);
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

static struct mac_lookup_task *mac_lookup_task_create(const struct terminal_key *key,
                                                     int vlan_id,
                                                     bool verify) {
    if (!key) {
        return NULL;
    }
    struct mac_lookup_task *task = calloc(1, sizeof(*task));
    if (!task) {
        return NULL;
    }
    task->key = *key;
    task->vlan_id = vlan_id;
    task->verify = verify;
    task->next = NULL;
    return task;
}

static void mac_lookup_task_append_node(struct mac_lookup_task **head,
                                        struct mac_lookup_task **tail,
                                        struct mac_lookup_task *node) {
    if (!head || !tail || !node) {
        return;
    }
    node->next = NULL;
    if (!*head) {
        *head = node;
        *tail = node;
    } else {
        (*tail)->next = node;
        *tail = node;
    }
}

static void mac_lookup_task_list_free(struct mac_lookup_task *head) {
    while (head) {
        struct mac_lookup_task *next = head->next;
        free(head);
        head = next;
    }
}

static void enqueue_need_refresh(struct terminal_manager *mgr, struct terminal_entry *entry) {
    if (!mgr || !entry) {
        return;
    }
    if (entry->mac_refresh_enqueued) {
        return;
    }
    if (entry->meta.vlan_id < 0) {
        return;
    }
    struct mac_lookup_task *task = mac_lookup_task_create(&entry->key, entry->meta.vlan_id, false);
    if (!task) {
        td_log_writef(TD_LOG_WARN,
                      "terminal_manager",
                      "failed to allocate mac refresh task for queue");
        return;
    }
    mac_lookup_task_append_node(&mgr->mac_need_refresh_head,
                                &mgr->mac_need_refresh_tail,
                                task);
    entry->mac_refresh_enqueued = true;
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
                        const struct terminal_metadata *meta) {
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
    if (meta) {
        node->record.ifindex = meta->ifindex;
    } else {
        node->record.ifindex = 0U;
    }
    node->record.tag = tag;
    event_queue_push(&mgr->events, node);
}

static void queue_add_event(struct terminal_manager *mgr,
                            const struct terminal_entry *entry) {
    if (!mgr || !entry) {
        return;
    }
    queue_event(mgr, TERMINAL_EVENT_TAG_ADD, &entry->key, &entry->meta);
}

static void queue_remove_event(struct terminal_manager *mgr,
                               const terminal_snapshot_t *snapshot) {
    if (!mgr || !snapshot) {
        return;
    }
    queue_event(mgr, TERMINAL_EVENT_TAG_DEL, &snapshot->key, &snapshot->meta);
}

static void queue_modify_event_if_ifindex_changed(struct terminal_manager *mgr,
                                                  const terminal_snapshot_t *before,
                                                  const struct terminal_entry *entry) {
    if (!mgr || !before || !entry || !mgr->event_cb) {
        return;
    }
    uint32_t before_ifindex = before->meta.ifindex;
    uint32_t after_ifindex = entry->meta.ifindex;
    if (before_ifindex == after_ifindex) {
        return;
    }
    queue_event(mgr, TERMINAL_EVENT_TAG_MOD, &entry->key, &entry->meta);
}

static void terminal_manager_maybe_dispatch_events(struct terminal_manager *mgr) {
    if (!mgr) {
        return;
    }

    struct terminal_event_node *head = NULL;
    size_t count = 0;

    pthread_mutex_lock(&mgr->lock);

    if (!mgr->event_cb) {
        if (mgr->events.size > 0) {
            mgr->stats.event_dispatch_failures += 1;
        }
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

static uint32_t prefix_mask_host(uint8_t prefix_len) {
    if (prefix_len == 0U) {
        return 0U;
    }
    if (prefix_len >= 32U) {
        return 0xFFFFFFFFU;
    }
    return 0xFFFFFFFFU << (32U - prefix_len);
}

static struct in_addr prefix_network(struct in_addr address, uint8_t prefix_len) {
    struct in_addr result;
    uint32_t addr_host = ntohl(address.s_addr);
    uint32_t mask_host = prefix_mask_host(prefix_len);
    result.s_addr = htonl(addr_host & mask_host);
    return result;
}

static bool ip_matches_prefix(struct in_addr ip,
                              struct in_addr network,
                              uint8_t prefix_len) {
    if (prefix_len > 32U) {
        return false;
    }
    if (prefix_len == 0U) {
        return true;
    }
    uint32_t mask_host = prefix_mask_host(prefix_len);
    uint32_t ip_host = ntohl(ip.s_addr);
    uint32_t network_host = ntohl(network.s_addr);
    return (ip_host & mask_host) == network_host;
}

static struct iface_record **find_iface_record_slot(struct terminal_manager *mgr, int kernel_ifindex) {
    struct iface_record **slot = &mgr->iface_records;
    while (*slot) {
        if ((*slot)->kernel_ifindex == kernel_ifindex) {
            break;
        }
        slot = &(*slot)->next;
    }
    return slot;
}

static struct iface_record *get_iface_record(struct terminal_manager *mgr, int kernel_ifindex) {
    struct iface_record **slot = find_iface_record_slot(mgr, kernel_ifindex);
    return slot ? *slot : NULL;
}

static bool iface_record_matches_ip(const struct iface_record *record, struct in_addr ip) {
    return iface_record_select_ip(record, ip, NULL);
}

static bool iface_record_select_ip(const struct iface_record *record,
                                   struct in_addr terminal_ip,
                                   struct in_addr *out_ip) {
    if (!record) {
        return false;
    }
    for (struct iface_prefix_entry *node = record->prefixes; node; node = node->next) {
        if (ip_matches_prefix(terminal_ip, node->network, node->prefix_len)) {
            if (node->address.s_addr == 0) {
                continue;
            }
            if (out_ip) {
                *out_ip = node->address;
            }
            return true;
        }
    }
    return false;
}

static bool iface_binding_contains(const struct iface_binding_entry *head,
                                   const struct terminal_entry *entry) {
    for (const struct iface_binding_entry *node = head; node; node = node->next) {
        if (node->terminal == entry) {
            return true;
        }
    }
    return false;
}

static bool iface_binding_attach(struct terminal_manager *mgr,
                                 int kernel_ifindex,
                                 struct terminal_entry *entry) {
    if (!mgr || kernel_ifindex <= 0 || !entry) {
        return false;
    }

    struct iface_record **slot = find_iface_record_slot(mgr, kernel_ifindex);
    struct iface_record *record = slot ? *slot : NULL;
    if (!record) {
        td_log_writef(TD_LOG_DEBUG,
                      "terminal_manager",
                      "skip binding terminal to kernel ifindex %d without address record",
                      kernel_ifindex);
        return false;
    }

    if (iface_binding_contains(record->bindings, entry)) {
        return true;
    }

    struct iface_binding_entry *node = calloc(1, sizeof(*node));
    if (!node) {
        td_log_writef(TD_LOG_WARN,
                      "terminal_manager",
                      "failed to allocate iface binding for kernel ifindex %d",
                      kernel_ifindex);
        return false;
    }
    node->terminal = entry;
    node->next = record->bindings;
    record->bindings = node;
    return true;
}

static void iface_binding_detach(struct terminal_manager *mgr,
                                 int kernel_ifindex,
                                 struct terminal_entry *entry) {
    if (!mgr || kernel_ifindex <= 0 || !entry) {
        return;
    }

    entry->tx_source_ip.s_addr = 0;

    struct iface_record **slot = find_iface_record_slot(mgr, kernel_ifindex);
    struct iface_record *record = slot ? *slot : NULL;
    if (!record) {
        return;
    }

    struct iface_binding_entry **pp = &record->bindings;
    while (*pp) {
        if ((*pp)->terminal == entry) {
            struct iface_binding_entry *node = *pp;
            *pp = node->next;
            free(node);
            break;
        }
        pp = &(*pp)->next;
    }

    iface_record_prune_if_empty(slot);
}

static void iface_record_prune_if_empty(struct iface_record **slot_ref) {
    if (!slot_ref || !*slot_ref) {
        return;
    }
    struct iface_record *record = *slot_ref;
    if (!record->prefixes && !record->bindings) {
        *slot_ref = record->next;
        free(record);
    }
}

static bool iface_prefix_add(struct terminal_manager *mgr,
                             int kernel_ifindex,
                             struct in_addr network,
                             struct in_addr address,
                             uint8_t prefix_len) {
    if (kernel_ifindex <= 0) {
        return false;
    }

    struct iface_record **slot = find_iface_record_slot(mgr, kernel_ifindex);
    struct iface_record *record = *slot;
    if (!record) {
        record = calloc(1, sizeof(*record));
        if (!record) {
            td_log_writef(TD_LOG_WARN,
                          "terminal_manager",
                          "failed to allocate iface record for kernel ifindex %d",
                          kernel_ifindex);
            return false;
        }
        record->kernel_ifindex = kernel_ifindex;
        record->next = NULL;
        record->prefixes = NULL;
        record->bindings = NULL;
        *slot = record;
    }

    for (struct iface_prefix_entry *node = record->prefixes; node; node = node->next) {
        if (node->prefix_len == prefix_len &&
            node->network.s_addr == network.s_addr &&
            node->address.s_addr == address.s_addr) {
            return true;
        }
    }

    struct iface_prefix_entry *entry = calloc(1, sizeof(*entry));
    if (!entry) {
        td_log_writef(TD_LOG_WARN,
                      "terminal_manager",
                      "failed to allocate prefix entry for kernel ifindex %d",
                      kernel_ifindex);
        return false;
    }
    entry->network = network;
    entry->address = address;
    entry->prefix_len = prefix_len;
    entry->next = record->prefixes;
    record->prefixes = entry;
    return true;
}

static bool iface_prefix_remove(struct terminal_manager *mgr,
                                int kernel_ifindex,
                                struct in_addr network,
                                struct in_addr address,
                                uint8_t prefix_len) {
    struct iface_record **slot = find_iface_record_slot(mgr, kernel_ifindex);
    struct iface_record *record = slot ? *slot : NULL;
    if (!record) {
        return true;
    }

    struct iface_prefix_entry **pp = &record->prefixes;
        while (*pp) {
            if ((*pp)->prefix_len == prefix_len &&
                (*pp)->network.s_addr == network.s_addr &&
                (*pp)->address.s_addr == address.s_addr) {
            struct iface_prefix_entry *node = *pp;
            *pp = node->next;
            free(node);
            break;
        }
        pp = &(*pp)->next;
    }

    iface_record_prune_if_empty(slot);
    return true;
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

static bool resolve_tx_interface(struct terminal_manager *mgr, struct terminal_entry *entry) {
    if (!mgr || !entry) {
        return false;
    }

    const bool had_previous_iface = entry->tx_iface[0] != '\0' && entry->tx_kernel_ifindex > 0;
    int previous_kernel_ifindex = entry->tx_kernel_ifindex;
    char previous_iface[IFNAMSIZ] = {0};
    if (had_previous_iface) {
        snprintf(previous_iface, sizeof(previous_iface), "%s", entry->tx_iface);
    }

    char candidate[IFNAMSIZ] = {0};
    int candidate_kernel_ifindex = -1;
    bool resolved = false;

    struct in_addr candidate_source_ip = {0};

    if (mgr->cfg.vlan_iface_format && entry->meta.vlan_id >= 0) {
        snprintf(candidate,
                 sizeof(candidate),
                 mgr->cfg.vlan_iface_format,
                 (unsigned int)entry->meta.vlan_id);
        candidate_kernel_ifindex = (int)if_nametoindex(candidate);
        resolved = candidate_kernel_ifindex > 0;
    }

    if (!resolved && candidate[0] != '\0') {
        candidate_kernel_ifindex = (int)if_nametoindex(candidate);
        resolved = candidate_kernel_ifindex > 0;
    }

    if (resolved) {
        struct iface_record *record = get_iface_record(mgr, candidate_kernel_ifindex);
        if (!record || !iface_record_select_ip(record, entry->key.ip, &candidate_source_ip)) {
            td_log_writef(TD_LOG_DEBUG,
                          "terminal_manager",
                          "iface %s(kernel_index=%d) lacks matching prefix for terminal",
                          candidate[0] ? candidate : "<unnamed>",
                          candidate_kernel_ifindex);
            resolved = false;
        }
    }

    if (!resolved) {
        if (had_previous_iface) {
            iface_binding_detach(mgr, previous_kernel_ifindex, entry);
        }
        entry->tx_iface[0] = '\0';
        entry->tx_kernel_ifindex = -1;
        entry->tx_source_ip.s_addr = 0;
        return false;
    }

    bool binding_changed = !had_previous_iface ||
                           previous_kernel_ifindex != candidate_kernel_ifindex ||
                           strncmp(previous_iface, candidate, sizeof(previous_iface)) != 0;

    if (binding_changed && had_previous_iface) {
        iface_binding_detach(mgr, previous_kernel_ifindex, entry);
    }

    snprintf(entry->tx_iface, sizeof(entry->tx_iface), "%s", candidate);
    entry->tx_kernel_ifindex = candidate_kernel_ifindex;
    entry->tx_source_ip = candidate_source_ip;

    if (!iface_binding_attach(mgr, candidate_kernel_ifindex, entry)) {
        entry->tx_iface[0] = '\0';
        entry->tx_kernel_ifindex = -1;
        entry->tx_source_ip.s_addr = 0;
        return false;
    }

    return true;
}

static void apply_packet_binding(struct terminal_manager *mgr,
                                 struct terminal_entry *entry,
                                 const struct td_adapter_packet_view *packet) {
    if (!packet || !entry) {
        return;
    }

    entry->meta.vlan_id = packet->vlan_id;
    if (packet->ifindex > 0U) {
        entry->meta.ifindex = packet->ifindex;
    }

    bool iface_resolved = resolve_tx_interface(mgr, entry);
    if (!iface_resolved) {
        set_state(entry, TERMINAL_STATE_IFACE_INVALID);
    }
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
    entry->meta.ifindex = 0U;
    entry->meta.mac_view_version = 0ULL;
    entry->tx_iface[0] = '\0';
    entry->tx_kernel_ifindex = -1;
    entry->tx_source_ip.s_addr = 0;
    entry->mac_refresh_enqueued = false;
    entry->mac_verify_enqueued = false;
    entry->next = NULL;

    if (packet) {
        apply_packet_binding(mgr, entry, packet);
    }

    return entry;
}

struct terminal_manager *terminal_manager_create(const struct terminal_manager_config *cfg,
                                                  td_adapter_t *adapter,
                                                  const struct td_adapter_ops *adapter_ops,
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
    mgr->adapter_ops = adapter_ops;
    mgr->mac_locator_ops = adapter_ops ? adapter_ops->mac_locator_ops : NULL;
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
    mgr->mac_need_refresh_head = NULL;
    mgr->mac_need_refresh_tail = NULL;
    mgr->mac_pending_verify_head = NULL;
    mgr->mac_pending_verify_tail = NULL;
    mgr->mac_locator_version = 0ULL;
    mgr->mac_locator_subscribed = false;
    mgr->destroying = false;

    for (size_t i = 0; i < TERMINAL_BUCKET_COUNT; ++i) {
        mgr->table[i] = NULL;
    }

    if (pthread_create(&mgr->worker_thread, NULL, terminal_manager_worker, mgr) == 0) {
        mgr->worker_started = true;
    } else {
        td_log_writef(TD_LOG_ERROR, "terminal_manager", "failed to start timer worker thread");
    }

    if (mgr->mac_locator_ops && mgr->mac_locator_ops->lookup && mgr->mac_locator_ops->subscribe) {
        uint64_t version = 0ULL;
        if (mgr->mac_locator_ops->get_version &&
            mgr->mac_locator_ops->get_version(mgr->adapter, &version) == TD_ADAPTER_OK) {
            mgr->mac_locator_version = version;
        }

        td_adapter_result_t sub_rc = mgr->mac_locator_ops->subscribe(mgr->adapter,
                                                                      mac_locator_on_refresh,
                                                                      mgr);
        if (sub_rc == TD_ADAPTER_OK) {
            mgr->mac_locator_subscribed = true;
        } else if (sub_rc == TD_ADAPTER_ERR_ALREADY) {
            td_log_writef(TD_LOG_WARN,
                          "terminal_manager",
                          "mac locator already has a subscriber; refresh callbacks disabled");
        } else {
            td_log_writef(TD_LOG_WARN,
                          "terminal_manager",
                          "mac locator subscribe failed: %d",
                          sub_rc);
        }
    }

    bind_active_manager(mgr);

    return mgr;
}

void terminal_manager_destroy(struct terminal_manager *mgr) {
    if (!mgr) {
        return;
    }

    mgr->destroying = true;
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
    mac_lookup_task_list_free(mgr->mac_need_refresh_head);
    mac_lookup_task_list_free(mgr->mac_pending_verify_head);
    mgr->mac_need_refresh_head = NULL;
    mgr->mac_need_refresh_tail = NULL;
    mgr->mac_pending_verify_head = NULL;
    mgr->mac_pending_verify_tail = NULL;
    for (size_t i = 0; i < TERMINAL_BUCKET_COUNT; ++i) {
        struct terminal_entry *node = mgr->table[i];
        while (node) {
            struct terminal_entry *next = node->next;
            free(node);
            node = next;
        }
        mgr->table[i] = NULL;
    }
    struct iface_record *record = mgr->iface_records;
    while (record) {
        struct iface_record *next_record = record->next;
        struct iface_prefix_entry *prefix = record->prefixes;
        while (prefix) {
            struct iface_prefix_entry *next_prefix = prefix->next;
            free(prefix);
            prefix = next_prefix;
        }
        struct iface_binding_entry *binding = record->bindings;
        while (binding) {
            struct iface_binding_entry *next_binding = binding->next;
            free(binding);
            binding = next_binding;
        }
        free(record);
        record = next_record;
    }
    mgr->iface_records = NULL;
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

static void mac_lookup_apply_result(struct terminal_manager *mgr,
                                    const struct mac_lookup_task *task,
                                    td_adapter_result_t rc,
                                    uint32_t ifindex,
                                    uint64_t version) {
    if (!mgr || !task) {
        return;
    }

    pthread_mutex_lock(&mgr->lock);

    if (mgr->destroying) {
        pthread_mutex_unlock(&mgr->lock);
        return;
    }

    if (version > mgr->mac_locator_version) {
        mgr->mac_locator_version = version;
    }

    size_t bucket = hash_key(&task->key) % TERMINAL_BUCKET_COUNT;
    struct terminal_entry *entry = find_entry(mgr, &task->key, bucket, NULL);
    if (!entry) {
        pthread_mutex_unlock(&mgr->lock);
        return;
    }

    if (task->verify) {
        entry->mac_verify_enqueued = false;
    } else {
        entry->mac_refresh_enqueued = false;
    }

    if (rc == TD_ADAPTER_OK) {
        uint32_t before_ifindex = entry->meta.ifindex;
        entry->meta.ifindex = ifindex;
        entry->meta.mac_view_version = version;
        if (mgr->event_cb && before_ifindex != entry->meta.ifindex) {
            queue_event(mgr, TERMINAL_EVENT_TAG_MOD, &entry->key, &entry->meta);
        }
        pthread_mutex_unlock(&mgr->lock);
        return;
    }

    if (task->verify) {
        uint32_t before_ifindex = entry->meta.ifindex;
        if (rc == TD_ADAPTER_ERR_NOT_READY) {
            entry->meta.ifindex = 0;
            if (version > 0 && version >= entry->meta.mac_view_version) {
                entry->meta.mac_view_version = version;
            }
        }
        if (mgr->event_cb && before_ifindex != entry->meta.ifindex) {
            queue_event(mgr, TERMINAL_EVENT_TAG_MOD, &entry->key, &entry->meta);
        }
    } else {
        if (rc == TD_ADAPTER_ERR_NOT_READY) {
            enqueue_need_refresh(mgr, entry);
        }
    }

    pthread_mutex_unlock(&mgr->lock);
}

static void mac_lookup_execute(struct terminal_manager *mgr,
                               struct mac_lookup_task *tasks) {
    if (!mgr) {
        mac_lookup_task_list_free(tasks);
        return;
    }

    struct mac_lookup_task *node = tasks;
    while (node) {
        struct mac_lookup_task *next = node->next;
        td_adapter_result_t rc = TD_ADAPTER_ERR_UNSUPPORTED;
        uint32_t ifindex = 0;
        uint64_t version = 0;

        if (mgr->mac_locator_ops && !mgr->destroying && node->vlan_id >= 0) {
            rc = mgr->mac_locator_ops->lookup(mgr->adapter,
                                              node->key.mac,
                                              (uint16_t)node->vlan_id,
                                              &ifindex,
                                              &version);
        }

        mac_lookup_apply_result(mgr, node, rc, ifindex, version);
        free(node);
        node = next;
    }

    terminal_manager_maybe_dispatch_events(mgr);
}

void terminal_manager_on_packet(struct terminal_manager *mgr,
                                const struct td_adapter_packet_view *packet) {
    if (!mgr || !packet) {
        return;
    }

    if (packet->payload_len < sizeof(struct ether_arp)) {
        return;
    }

    struct mac_lookup_task *lookup_head = NULL;
    struct mac_lookup_task *lookup_tail = NULL;

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

    if (mgr->mac_locator_ops && entry->meta.vlan_id >= 0) {
        bool version_ready = mgr->mac_locator_version > 0;
        bool wants_lookup = false;

        if (entry->meta.ifindex == 0) {
            wants_lookup = true;
        } else if (entry->meta.mac_view_version < mgr->mac_locator_version) {
            wants_lookup = true;
        } else if (entry->meta.mac_view_version == 0 && !version_ready) {
            wants_lookup = true;
        }

        if (wants_lookup) {
            if (version_ready) {
                struct mac_lookup_task *task = mac_lookup_task_create(&entry->key,
                                                                      entry->meta.vlan_id,
                                                                      false);
                if (task) {
                    mac_lookup_task_append_node(&lookup_head, &lookup_tail, task);
                } else {
                    td_log_writef(TD_LOG_WARN,
                                  "terminal_manager",
                                  "failed to allocate immediate mac lookup task");
                }
            } else {
                enqueue_need_refresh(mgr, entry);
            }
        }
    }

    if (newly_created) {
        queue_add_event(mgr, entry);
    } else if (have_before_snapshot) {
    queue_modify_event_if_ifindex_changed(mgr, &before_snapshot, entry);
    }

    pthread_mutex_unlock(&mgr->lock);

    mac_lookup_execute(mgr, lookup_head);
}

static bool is_iface_available(const struct terminal_entry *entry) {
    return entry && entry->tx_kernel_ifindex > 0 && entry->tx_source_ip.s_addr != 0;
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
    struct mac_lookup_task *lookup_head = NULL;
    struct mac_lookup_task *lookup_tail = NULL;

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

            if (mgr->mac_locator_ops && entry->meta.vlan_id >= 0) {
                if (entry->state == TERMINAL_STATE_IFACE_INVALID) {
                    if (mgr->mac_locator_version > 0 &&
                        entry->meta.mac_view_version < mgr->mac_locator_version) {
                        struct mac_lookup_task *task = mac_lookup_task_create(&entry->key,
                                                                              entry->meta.vlan_id,
                                                                              false);
                        if (task) {
                            mac_lookup_task_append_node(&lookup_head, &lookup_tail, task);
                        } else {
                            td_log_writef(TD_LOG_WARN,
                                          "terminal_manager",
                                          "failed to allocate timer mac lookup task");
                        }
                    } else if (mgr->mac_locator_version == 0 && entry->meta.ifindex == 0) {
                        enqueue_need_refresh(mgr, entry);
                    }
                }
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
                                task->request.tx_kernel_ifindex = entry->tx_kernel_ifindex;
                                task->request.source_ip = entry->tx_source_ip;
                                task->request.vlan_id = entry->meta.vlan_id;
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
                if (to_free->tx_kernel_ifindex > 0) {
                    iface_binding_detach(mgr, to_free->tx_kernel_ifindex, to_free);
                }
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
                    queue_modify_event_if_ifindex_changed(mgr, &before_snapshot, entry);
                }
                prev_next = &entry->next;
                entry = entry->next;
            }
        }
    }

    pthread_mutex_unlock(&mgr->lock);

    mac_lookup_execute(mgr, lookup_head);

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

static void mac_locator_on_refresh(uint64_t version, void *ctx) {
    struct terminal_manager *mgr = (struct terminal_manager *)ctx;
    if (!mgr || mgr->destroying) {
        return;
    }

    struct mac_lookup_task *refresh_head = NULL;
    struct mac_lookup_task *refresh_tail = NULL;
    struct mac_lookup_task *verify_head = NULL;
    struct mac_lookup_task *verify_tail = NULL;

    pthread_mutex_lock(&mgr->lock);

    if (mgr->destroying) {
        pthread_mutex_unlock(&mgr->lock);
        return;
    }

    if (version == 0ULL) {
        td_log_writef(TD_LOG_WARN,
                      "terminal_manager",
                      "mac locator refresh reported failure (version=0)");
        pthread_mutex_unlock(&mgr->lock);
        return;
    }

    if (version > mgr->mac_locator_version) {
        mgr->mac_locator_version = version;
    }

    refresh_head = mgr->mac_need_refresh_head;
    refresh_tail = mgr->mac_need_refresh_tail;
    mgr->mac_need_refresh_head = NULL;
    mgr->mac_need_refresh_tail = NULL;

    verify_head = mgr->mac_pending_verify_head;
    verify_tail = mgr->mac_pending_verify_tail;
    mgr->mac_pending_verify_head = NULL;
    mgr->mac_pending_verify_tail = NULL;

    for (size_t i = 0; i < TERMINAL_BUCKET_COUNT; ++i) {
        for (struct terminal_entry *entry = mgr->table[i]; entry; entry = entry->next) {
            if (entry->meta.vlan_id < 0) {
                continue;
            }

            if (entry->meta.ifindex == 0) {
                if (!entry->mac_refresh_enqueued) {
                    struct mac_lookup_task *task = mac_lookup_task_create(&entry->key,
                                                                          entry->meta.vlan_id,
                                                                          false);
                    if (task) {
                        mac_lookup_task_append_node(&refresh_head, &refresh_tail, task);
                        entry->mac_refresh_enqueued = true;
                    } else {
                        td_log_writef(TD_LOG_WARN,
                                      "terminal_manager",
                                      "failed to allocate mac refresh task on callback");
                    }
                }
            } else if (entry->meta.mac_view_version < version) {
                if (!entry->mac_verify_enqueued) {
                    struct mac_lookup_task *task = mac_lookup_task_create(&entry->key,
                                                                          entry->meta.vlan_id,
                                                                          true);
                    if (task) {
                        mac_lookup_task_append_node(&verify_head, &verify_tail, task);
                        entry->mac_verify_enqueued = true;
                    } else {
                        td_log_writef(TD_LOG_WARN,
                                      "terminal_manager",
                                      "failed to allocate mac verify task on callback");
                    }
                }
            }
        }
    }

    pthread_mutex_unlock(&mgr->lock);

    mac_lookup_execute(mgr, refresh_head);
    mac_lookup_execute(mgr, verify_head);
}

void terminal_manager_on_address_update(struct terminal_manager *mgr,
                                        const terminal_address_update_t *update) {
    if (!mgr || !update || update->kernel_ifindex <= 0) {
        return;
    }

    if (update->prefix_len > 32U) {
        td_log_writef(TD_LOG_WARN,
                      "terminal_manager",
                      "ignore address update with invalid prefix len %u",
                      update->prefix_len);
        return;
    }

    pthread_mutex_lock(&mgr->lock);
    mgr->stats.address_update_events += 1;

    struct in_addr network = prefix_network(update->address, update->prefix_len);
    if (update->is_add) {
        if (!iface_prefix_add(mgr,
                              update->kernel_ifindex,
                              network,
                              update->address,
                              update->prefix_len)) {
            pthread_mutex_unlock(&mgr->lock);
            return;
        }
    } else {
        iface_prefix_remove(mgr,
                            update->kernel_ifindex,
                            network,
                            update->address,
                            update->prefix_len);
    }

    struct iface_record **slot = find_iface_record_slot(mgr, update->kernel_ifindex);
    struct iface_record *record = slot ? *slot : NULL;
    if (!record) {
        pthread_mutex_unlock(&mgr->lock);
        return;
    }

    struct iface_binding_entry **binding_ref = &record->bindings;
    while (*binding_ref) {
        struct iface_binding_entry *binding = *binding_ref;
        struct terminal_entry *terminal = binding->terminal;
        if (update->is_add) {
            struct in_addr refreshed_ip;
            if (iface_record_select_ip(record, terminal->key.ip, &refreshed_ip)) {
                terminal->tx_source_ip = refreshed_ip;
            }
            binding_ref = &(*binding_ref)->next;
            continue;
        }

        if (!iface_record_matches_ip(record, terminal->key.ip)) {
            *binding_ref = binding->next;
            free(binding);
            terminal->tx_iface[0] = '\0';
            terminal->tx_kernel_ifindex = -1;
            terminal->tx_source_ip.s_addr = 0;
            monotonic_now(&terminal->last_seen);
            set_state(terminal, TERMINAL_STATE_IFACE_INVALID);
        } else {
            binding_ref = &(*binding_ref)->next;
        }
    }

    iface_record_prune_if_empty(slot);

    pthread_mutex_unlock(&mgr->lock);
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
        size_t dropped = mgr->events.size;
        free_event_queue(&mgr->events);
        if (dropped > 0) {
            mgr->stats.event_dispatch_failures += 1;
        }
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
                records[idx].ifindex = entry->meta.ifindex;
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
