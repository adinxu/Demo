#ifndef TD_CONFIG_H
#define TD_CONFIG_H

#include "adapter_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TD_ADAPTER_NAME_MAX 64

struct terminal_manager_config;

#define TD_DEFAULT_KEEPALIVE_INTERVAL_SEC 120U
#define TD_DEFAULT_KEEPALIVE_MISS_THRESHOLD 3U
#define TD_DEFAULT_MAX_TERMINALS 1000U
#define TD_DEFAULT_IFACE_INVALID_HOLDOFF_SEC 1800U
#define TD_DEFAULT_STATS_LOG_INTERVAL_SEC 0U
#ifndef TD_MAX_IGNORED_VLANS
#define TD_MAX_IGNORED_VLANS 32U
#endif

struct td_runtime_config {
    char adapter_name[TD_ADAPTER_NAME_MAX];
    char rx_iface[IFNAMSIZ];
    char tx_iface[IFNAMSIZ];
    unsigned int tx_interval_ms;
    unsigned int keepalive_interval_sec;
    unsigned int keepalive_miss_threshold;
    unsigned int iface_invalid_holdoff_sec;
    unsigned int max_terminals;
    unsigned int stats_log_interval_sec;
    td_log_level_t log_level;
    size_t ignored_vlan_count;
    uint16_t ignored_vlans[TD_MAX_IGNORED_VLANS];
};

int td_config_load_defaults(struct td_runtime_config *cfg);
int td_config_to_manager_config(const struct td_runtime_config *runtime,
                                struct terminal_manager_config *out);
int td_config_add_ignored_vlan(struct td_runtime_config *cfg, unsigned int vlan_id);
int td_config_remove_ignored_vlan(struct td_runtime_config *cfg, unsigned int vlan_id);
void td_config_clear_ignored_vlans(struct td_runtime_config *cfg);

#ifdef __cplusplus
}
#endif

#endif /* TD_CONFIG_H */
