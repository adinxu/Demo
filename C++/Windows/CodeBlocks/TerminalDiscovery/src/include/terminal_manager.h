#ifndef TERMINAL_MANAGER_H
#define TERMINAL_MANAGER_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "adapter_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TERMINAL_STATE_ACTIVE = 0,
    TERMINAL_STATE_PROBING,
    TERMINAL_STATE_IFACE_INVALID,
} terminal_state_t;

struct terminal_key {
    uint8_t mac[ETH_ALEN];
    struct in_addr ip;
};

struct terminal_metadata {
    int vlan_id;            /* -1 if unknown */
    uint32_t ifindex;       /* 0 when unavailable; logical port identifier (not tx_kernel_ifindex) */
    uint64_t mac_view_version; /* 0 when unresolved; snapshot version for last bridge lookup */
};

struct terminal_entry {
    struct terminal_key key;
    terminal_state_t state;
    struct timespec last_seen;
    struct timespec last_probe;
    uint32_t failed_probes;
    struct terminal_metadata meta;
    char tx_iface[IFNAMSIZ];
    int tx_kernel_ifindex;
    struct in_addr tx_source_ip;
    bool mac_refresh_enqueued;
    bool mac_verify_enqueued;
    struct terminal_entry *next;
};

typedef struct terminal_probe_request {
    struct terminal_key key;
    char tx_iface[IFNAMSIZ];
    int tx_kernel_ifindex;
    struct in_addr source_ip;
    int vlan_id;
    terminal_state_t state_before_probe;
} terminal_probe_request_t;

typedef void (*terminal_probe_fn)(const terminal_probe_request_t *request, void *user_ctx);

typedef bool (*terminal_iface_selector_fn)(const struct terminal_metadata *meta,
                                           char tx_iface[IFNAMSIZ],
                                           int *tx_kernel_ifindex,
                                           void *user_ctx);

typedef struct terminal_snapshot {
    struct terminal_key key;
    struct terminal_metadata meta;
} terminal_snapshot_t;

typedef enum {
    TERMINAL_EVENT_TAG_DEL = 0,
    TERMINAL_EVENT_TAG_ADD,
    TERMINAL_EVENT_TAG_MOD,
} terminal_event_tag_t;

typedef struct terminal_event_record {
    struct terminal_key key;
    uint32_t ifindex; /* 0 when unknown; logical port identifier */
    terminal_event_tag_t tag;
} terminal_event_record_t;

typedef void (*terminal_event_callback_fn)(const terminal_event_record_t *records,
                                           size_t count,
                                           void *user_ctx);

typedef bool (*terminal_query_callback_fn)(const terminal_event_record_t *record, void *user_ctx);

struct terminal_manager_stats {
    uint64_t terminals_discovered;
    uint64_t terminals_removed;
    uint64_t capacity_drops;
    uint64_t probes_scheduled;
    uint64_t probe_failures;
    uint64_t address_update_events;
    uint64_t events_dispatched;
    uint64_t event_dispatch_failures;
    uint64_t current_terminals;
};

struct terminal_manager;

typedef struct terminal_address_update {
    int kernel_ifindex;
    struct in_addr address;
    uint8_t prefix_len; /* 0-32 */
    bool is_add;        /* true = add/update, false = remove */
} terminal_address_update_t;

struct terminal_manager_config {
    unsigned int keepalive_interval_sec;
    unsigned int keepalive_miss_threshold;
    unsigned int iface_invalid_holdoff_sec;
    unsigned int scan_interval_ms;
    const char *vlan_iface_format; /* e.g. "vlan%u"; leave NULL to reuse ingress name */
    terminal_iface_selector_fn iface_selector;
    void *iface_selector_ctx;
    size_t max_terminals;
};

struct terminal_manager *terminal_manager_create(const struct terminal_manager_config *cfg,
                                                  td_adapter_t *adapter,
                                                  const struct td_adapter_ops *adapter_ops,
                                                  terminal_probe_fn probe_cb,
                                                  void *probe_ctx);

void terminal_manager_destroy(struct terminal_manager *mgr);

void terminal_manager_on_packet(struct terminal_manager *mgr,
                                const struct td_adapter_packet_view *packet);

void terminal_manager_on_timer(struct terminal_manager *mgr);

void terminal_manager_on_address_update(struct terminal_manager *mgr,
                                        const terminal_address_update_t *update);

int terminal_manager_set_event_sink(struct terminal_manager *mgr,
                                    terminal_event_callback_fn callback,
                                    void *callback_ctx);

int terminal_manager_query_all(struct terminal_manager *mgr,
                               terminal_query_callback_fn callback,
                               void *callback_ctx);

void terminal_manager_flush_events(struct terminal_manager *mgr);

struct terminal_manager *terminal_manager_get_active(void);

void terminal_manager_get_stats(struct terminal_manager *mgr,
                                struct terminal_manager_stats *out);

#ifdef __cplusplus
}
#endif

#endif /* TERMINAL_MANAGER_H */
