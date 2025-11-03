#ifndef TERMINAL_MANAGER_H
#define TERMINAL_MANAGER_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "adapter_api.h"

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
    char ingress_ifname[IFNAMSIZ];
    int ingress_ifindex;
};

struct terminal_entry {
    struct terminal_key key;
    terminal_state_t state;
    struct timespec last_seen;
    struct timespec last_probe;
    uint32_t failed_probes;
    struct terminal_metadata meta;
    char tx_iface[IFNAMSIZ];
    int tx_ifindex;
    struct terminal_entry *next;
};

typedef struct terminal_probe_request {
    struct terminal_key key;
    struct terminal_metadata meta;
    char tx_iface[IFNAMSIZ];
    int tx_ifindex;
    terminal_state_t state_before_probe;
    uint32_t failed_probes;
} terminal_probe_request_t;

typedef void (*terminal_probe_fn)(const terminal_probe_request_t *request, void *user_ctx);

typedef bool (*terminal_iface_selector_fn)(const struct terminal_metadata *meta,
                                           char tx_iface[IFNAMSIZ],
                                           int *tx_ifindex,
                                           void *user_ctx);

typedef struct terminal_snapshot {
    struct terminal_key key;
    struct terminal_metadata meta;
    terminal_state_t state;
    char tx_iface[IFNAMSIZ];
    int tx_ifindex;
    struct timespec last_seen;
    struct timespec last_probe;
    uint32_t failed_probes;
} terminal_snapshot_t;

typedef enum {
    TERMINAL_EVENT_TAG_DEL = 0,
    TERMINAL_EVENT_TAG_ADD,
    TERMINAL_EVENT_TAG_MOD,
} terminal_event_tag_t;

typedef struct terminal_event_record {
    struct terminal_key key;
    uint32_t port; /* 0 when unknown */
    terminal_event_tag_t tag;
} terminal_event_record_t;

typedef void (*terminal_event_callback_fn)(const terminal_event_record_t *records,
                                           size_t count,
                                           void *user_ctx);

typedef bool (*terminal_query_callback_fn)(const terminal_event_record_t *record, void *user_ctx);

struct terminal_manager;

struct terminal_manager_config {
    unsigned int keepalive_interval_sec;
    unsigned int keepalive_miss_threshold;
    unsigned int iface_invalid_holdoff_sec;
    unsigned int scan_interval_ms;
    const char *vlan_iface_format; /* e.g. "vlan%u"; leave NULL to reuse ingress name */
    terminal_iface_selector_fn iface_selector;
    void *iface_selector_ctx;
    unsigned int event_throttle_sec;
};

struct terminal_manager *terminal_manager_create(const struct terminal_manager_config *cfg,
                                                  td_adapter_t *adapter,
                                                  terminal_probe_fn probe_cb,
                                                  void *probe_ctx);

void terminal_manager_destroy(struct terminal_manager *mgr);

void terminal_manager_on_packet(struct terminal_manager *mgr,
                                const struct td_adapter_packet_view *packet);

void terminal_manager_on_timer(struct terminal_manager *mgr);

void terminal_manager_on_iface_event(struct terminal_manager *mgr,
                                     const struct td_adapter_iface_event *event);

int terminal_manager_set_event_sink(struct terminal_manager *mgr,
                                    unsigned int throttle_sec,
                                    terminal_event_callback_fn callback,
                                    void *callback_ctx);

int terminal_manager_query_all(struct terminal_manager *mgr,
                               terminal_query_callback_fn callback,
                               void *callback_ctx);

void terminal_manager_flush_events(struct terminal_manager *mgr);

struct terminal_manager *terminal_manager_get_active(void);

#endif /* TERMINAL_MANAGER_H */
