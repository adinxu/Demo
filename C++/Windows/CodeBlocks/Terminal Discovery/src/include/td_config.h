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

struct td_runtime_config {
    char adapter_name[TD_ADAPTER_NAME_MAX];
    char rx_iface[IFNAMSIZ];
    char tx_iface[IFNAMSIZ];
    unsigned int tx_interval_ms;
    unsigned int keepalive_interval_sec;
    unsigned int keepalive_miss_threshold;
    unsigned int iface_invalid_holdoff_sec;
    unsigned int max_terminals;
    td_log_level_t log_level;
};

int td_config_load_defaults(struct td_runtime_config *cfg);
int td_config_to_manager_config(const struct td_runtime_config *runtime,
                                struct terminal_manager_config *out);

#ifdef __cplusplus
}
#endif

#endif /* TD_CONFIG_H */
