#ifndef TD_ADAPTER_API_H
#define TD_ADAPTER_API_H

#include <linux/if_ether.h>
#include <net/if.h>
#include <netinet/in.h>
#ifndef IFNAMSIZ
#define IFNAMSIZ IF_NAMESIZE
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TD_ADAPTER_OK = 0,
    TD_ADAPTER_ERR_INVALID_ARG = -1,
    TD_ADAPTER_ERR_NO_MEMORY = -2,
    TD_ADAPTER_ERR_SYS = -3,
    TD_ADAPTER_ERR_NOT_READY = -4,
    TD_ADAPTER_ERR_UNSUPPORTED = -5,
    TD_ADAPTER_ERR_BACKPRESSURE = -6,
} td_adapter_result_t;

typedef enum {
    TD_LOG_TRACE = 0,
    TD_LOG_DEBUG = 1,
    TD_LOG_INFO = 2,
    TD_LOG_WARN = 3,
    TD_LOG_ERROR = 4,
    TD_LOG_NONE = 5,
} td_log_level_t;

typedef struct td_adapter td_adapter_t;

struct td_adapter_config {
    const char *rx_iface;           /* inbound raw socket interface */
    const char *tx_iface;           /* outbound VLAN interface */
    unsigned int tx_interval_ms;    /* minimum gap between ARP probes */
    unsigned int rx_ring_size;      /* optional fan-out / ring size hint */
};

struct td_adapter_env {
    void (*log_fn)(void *user_data,
                   td_log_level_t level,
                   const char *component,
                   const char *message);
    void *log_user_data;
};

struct td_adapter_packet_view {
    const uint8_t *frame;       /* pointer to full Ethernet frame */
    size_t frame_len;           /* length of full frame */
    const uint8_t *payload;     /* pointer to protocol payload start */
    size_t payload_len;         /* payload length */
    uint16_t ether_type;        /* host-byte-order inner EtherType */
    int vlan_id;                /* -1 if no VLAN present */
    struct timespec ts;         /* capture timestamp */
    char ifname[IFNAMSIZ];      /* ingress interface */
    int ifindex;                /* ingress ifindex */
    uint8_t src_mac[ETH_ALEN];
    uint8_t dst_mac[ETH_ALEN];
};

typedef void (*td_adapter_packet_cb)(const struct td_adapter_packet_view *packet,
                                     void *user_ctx);

struct td_adapter_packet_subscription {
    td_adapter_packet_cb callback;
    void *user_ctx;
};

struct td_adapter_iface_info {
    char ifname[IFNAMSIZ];
    uint8_t mac[ETH_ALEN];
    struct in_addr ipv4;
    uint32_t flags; /* platform specific bitmask */
};

struct td_adapter_iface_event {
    char ifname[IFNAMSIZ];
    uint32_t flags_before;
    uint32_t flags_after;
};

typedef void (*td_adapter_iface_event_cb)(const struct td_adapter_iface_event *event,
                                          void *user_ctx);

struct td_adapter_iface_event_subscription {
    td_adapter_iface_event_cb callback;
    void *user_ctx;
};

typedef void (*td_adapter_timer_cb)(void *user_ctx);

struct td_adapter_timer_request {
    struct timespec expires_at; /* absolute time */
    td_adapter_timer_cb callback;
    void *user_ctx;
};

struct td_adapter_timer_token {
    uint64_t token_id;
};

struct td_adapter_arp_request {
    struct in_addr sender_ip;
    uint8_t sender_mac[ETH_ALEN];
    struct in_addr target_ip;
    uint8_t target_mac[ETH_ALEN];
    char tx_iface[IFNAMSIZ];        /* optional override transmit iface */
    int tx_ifindex;                 /* optional, -1 if unknown */
    bool tx_iface_valid;            /* true when tx_iface contains a preference */
};

struct td_adapter_ops {
    td_adapter_result_t (*init)(const struct td_adapter_config *cfg,
                                const struct td_adapter_env *env,
                                td_adapter_t **handle);
    void (*shutdown)(td_adapter_t *handle);
    td_adapter_result_t (*start)(td_adapter_t *handle);
    void (*stop)(td_adapter_t *handle);
    td_adapter_result_t (*register_packet_rx)(td_adapter_t *handle,
                                              const struct td_adapter_packet_subscription *sub);
    td_adapter_result_t (*send_arp)(td_adapter_t *handle,
                                    const struct td_adapter_arp_request *req);
    td_adapter_result_t (*query_iface)(td_adapter_t *handle,
                                       const char *ifname,
                                       struct td_adapter_iface_info *info_out);
    td_adapter_result_t (*subscribe_iface_events)(td_adapter_t *handle,
                                                  const struct td_adapter_iface_event_subscription *sub);
    td_adapter_result_t (*schedule_timer)(td_adapter_t *handle,
                                          const struct td_adapter_timer_request *req,
                                          struct td_adapter_timer_token *token_out);
    td_adapter_result_t (*cancel_timer)(td_adapter_t *handle,
                                        const struct td_adapter_timer_token *token);
    void (*log_write)(td_adapter_t *handle,
                      td_log_level_t level,
                      const char *component,
                      const char *message);
};

struct td_adapter_descriptor {
    const char *name;
    const struct td_adapter_ops *ops;
};

#ifdef __cplusplus
}
#endif

#endif /* TD_ADAPTER_API_H */
