#ifndef TD_CONFIG_H
#define TD_CONFIG_H

#include "adapter_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TD_ADAPTER_NAME_MAX 64

struct td_runtime_config {
    char adapter_name[TD_ADAPTER_NAME_MAX];
    char rx_iface[IFNAMSIZ];
    char tx_iface[IFNAMSIZ];
    unsigned int tx_interval_ms;
    td_log_level_t log_level;
};

int td_config_load_defaults(struct td_runtime_config *cfg);

#ifdef __cplusplus
}
#endif

#endif /* TD_CONFIG_H */
