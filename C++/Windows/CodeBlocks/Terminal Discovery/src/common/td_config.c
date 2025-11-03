#include "td_config.h"

#include <string.h>
#include <stdio.h>

#include "td_logging.h"

#define TD_DEFAULT_ADAPTER "realtek"
#define TD_DEFAULT_RX_IFACE "eth0"
#define TD_DEFAULT_TX_IFACE "vlan1"
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
    cfg->log_level = TD_LOG_INFO;

    return 0;
}
