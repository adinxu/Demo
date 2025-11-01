#ifndef TERMINAL_MANAGER_H
#define TERMINAL_MANAGER_H

#include <pthread.h>
#include <stdbool.h>
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

typedef void (*terminal_probe_fn)(const struct terminal_entry *entry, void *user_ctx);

struct terminal_manager;

struct terminal_manager_config {
    unsigned int keepalive_interval_sec;
    unsigned int keepalive_miss_threshold;
    unsigned int iface_invalid_holdoff_sec;
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

#endif /* TERMINAL_MANAGER_H */
