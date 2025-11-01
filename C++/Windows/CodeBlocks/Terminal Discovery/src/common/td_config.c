#include "td_config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "td_logging.h"

#define TD_ENV_ADAPTER_NAME "TD_ADAPTER_NAME"
#define TD_ENV_RX_IFACE "TD_ADAPTER_RX_IFACE"
#define TD_ENV_TX_IFACE "TD_ADAPTER_TX_IFACE"
#define TD_ENV_TX_INTERVAL "TD_ADAPTER_TX_INTERVAL_MS"
#define TD_ENV_LOG_LEVEL "TD_LOG_LEVEL"

#define TD_DEFAULT_ADAPTER "realtek"
#define TD_DEFAULT_RX_IFACE "eth0"
#define TD_DEFAULT_TX_IFACE "vlan1"
#define TD_DEFAULT_TX_INTERVAL_MS 100U

static void copy_env_string(const char *env_key, char *dst, size_t dst_len, const char *fallback) {
    const char *value = getenv(env_key);
    if (value && *value != '\0') {
        snprintf(dst, dst_len, "%s", value);
    } else if (fallback) {
        snprintf(dst, dst_len, "%s", fallback);
    } else if (dst_len > 0) {
        dst[0] = '\0';
    }
}

int td_config_load_from_env(struct td_runtime_config *cfg) {
    if (!cfg) {
        return -1;
    }

    memset(cfg, 0, sizeof(*cfg));

    copy_env_string(TD_ENV_ADAPTER_NAME, cfg->adapter_name, sizeof(cfg->adapter_name), TD_DEFAULT_ADAPTER);
    copy_env_string(TD_ENV_RX_IFACE, cfg->rx_iface, sizeof(cfg->rx_iface), TD_DEFAULT_RX_IFACE);
    copy_env_string(TD_ENV_TX_IFACE, cfg->tx_iface, sizeof(cfg->tx_iface), TD_DEFAULT_TX_IFACE);

    const char *interval_str = getenv(TD_ENV_TX_INTERVAL);
    if (interval_str && *interval_str) {
        char *endptr = NULL;
        long value = strtol(interval_str, &endptr, 10);
        if (*endptr == '\0' && value >= 0) {
            cfg->tx_interval_ms = (unsigned int)value;
        } else {
            cfg->tx_interval_ms = TD_DEFAULT_TX_INTERVAL_MS;
        }
    } else {
        cfg->tx_interval_ms = TD_DEFAULT_TX_INTERVAL_MS;
    }

    const char *log_level = getenv(TD_ENV_LOG_LEVEL);
    bool ok = false;
    cfg->log_level = td_log_level_from_string(log_level, &ok);
    if (!ok) {
        cfg->log_level = TD_LOG_INFO;
    }

    return 0;
}
