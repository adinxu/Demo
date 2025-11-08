#include "td_config.h"

#include <string.h>
#include <stdio.h>

#include "terminal_manager.h"

#include "td_logging.h"

#define TD_DEFAULT_ADAPTER "realtek"
#define TD_DEFAULT_RX_IFACE "eth0"
#define TD_DEFAULT_TX_IFACE "eth0"
#define TD_DEFAULT_TX_INTERVAL_MS 100U

int td_config_load_defaults(struct td_runtime_config *cfg) {
    if (!cfg) {
        return -1;
    }

    memset(cfg, 0, sizeof(*cfg));

    snprintf(cfg->adapter_name, sizeof(cfg->adapter_name), "%s", TD_DEFAULT_ADAPTER);
    snprintf(cfg->rx_iface, sizeof(cfg->rx_iface), "%s", TD_DEFAULT_RX_IFACE);
    snprintf(cfg->tx_iface, sizeof(cfg->tx_iface), "%s", TD_DEFAULT_TX_IFACE);
    cfg->tx_interval_ms = TD_DEFAULT_TX_INTERVAL_MS;
    cfg->keepalive_interval_sec = TD_DEFAULT_KEEPALIVE_INTERVAL_SEC;
    cfg->keepalive_miss_threshold = TD_DEFAULT_KEEPALIVE_MISS_THRESHOLD;
    cfg->iface_invalid_holdoff_sec = TD_DEFAULT_IFACE_INVALID_HOLDOFF_SEC;
    cfg->max_terminals = TD_DEFAULT_MAX_TERMINALS;
    cfg->stats_log_interval_sec = TD_DEFAULT_STATS_LOG_INTERVAL_SEC;
    cfg->log_level = TD_LOG_INFO;

    return 0;
}

int td_config_to_manager_config(const struct td_runtime_config *runtime,
                                struct terminal_manager_config *out) {
    if (!runtime || !out) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->keepalive_interval_sec = runtime->keepalive_interval_sec;
    out->keepalive_miss_threshold = runtime->keepalive_miss_threshold;
    out->iface_invalid_holdoff_sec = runtime->iface_invalid_holdoff_sec;
    out->scan_interval_ms = 0U;
    out->vlan_iface_format = NULL;
    out->max_terminals = runtime->max_terminals;

    return 0;
}
