#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "td_adapter_registry.h"
#include "td_config.h"
#include "td_logging.h"
#include "terminal_manager.h"
#include "terminal_netlink.h"

struct app_context {
    struct terminal_manager *manager;
    td_adapter_t *adapter;
    const struct td_adapter_ops *ops;
    struct terminal_netlink_listener *netlink_listener;
};

static volatile sig_atomic_t g_should_stop = 0;
static volatile sig_atomic_t g_should_dump_stats = 0;
static const char *g_program_name = "terminal_discovery";

static void handle_signal(int sig) {
    g_should_stop = sig;
}

static void handle_stats_signal(int sig) {
    (void)sig;
    g_should_dump_stats = 1;
}

static void adapter_log_bridge(void *user_data,
                               td_log_level_t level,
                               const char *component,
                               const char *message) {
    (void)user_data;
    td_log_writef(level, component ? component : "adapter", "%s", message ? message : "");
}

static void format_mac(const uint8_t mac[ETH_ALEN], char out[18]) {
    if (!out) {
        return;
    }
    if (!mac) {
        snprintf(out, 18, "<null>");
        return;
    }
    snprintf(out,
             18,
             "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0],
             mac[1],
             mac[2],
             mac[3],
             mac[4],
             mac[5]);
}

static const char *event_tag_to_string(terminal_event_tag_t tag) {
    switch (tag) {
    case TERMINAL_EVENT_TAG_ADD:
        return "ADD";
    case TERMINAL_EVENT_TAG_DEL:
        return "DEL";
    case TERMINAL_EVENT_TAG_MOD:
        return "MOD";
    default:
        return "UNK";
    }
}

static const char *state_to_string(terminal_state_t state) {
    switch (state) {
    case TERMINAL_STATE_ACTIVE:
        return "ACTIVE";
    case TERMINAL_STATE_PROBING:
        return "PROBING";
    case TERMINAL_STATE_IFACE_INVALID:
        return "IFACE_INVALID";
    default:
        return "UNKNOWN";
    }
}

static void log_manager_stats(struct terminal_manager *manager) {
    if (!manager) {
        return;
    }

    struct terminal_manager_stats stats;
    memset(&stats, 0, sizeof(stats));
    terminal_manager_get_stats(manager, &stats);

    td_log_writef(TD_LOG_INFO,
                  "terminal_stats",
                  "current=%" PRIu64 " discovered=%" PRIu64 " removed=%" PRIu64
                  " probes=%" PRIu64 " probe_failures=%" PRIu64
                  " capacity_drops=%" PRIu64
                  " events=%" PRIu64 " dispatch_failures=%" PRIu64
                  " addr_updates=%" PRIu64,
                  stats.current_terminals,
                  stats.terminals_discovered,
                  stats.terminals_removed,
                  stats.probes_scheduled,
                  stats.probe_failures,
                  stats.capacity_drops,
                  stats.events_dispatched,
                  stats.event_dispatch_failures,
                  stats.address_update_events);
}

static void terminal_event_logger(const terminal_event_record_t *records,
                                  size_t count,
                                  void *user_ctx) {
    (void)user_ctx;
    for (size_t i = 0; i < count; ++i) {
        const terminal_event_record_t *rec = &records[i];
        char mac_buf[18];
        char ip_buf[INET_ADDRSTRLEN];
        format_mac(rec->key.mac, mac_buf);
        if (!inet_ntop(AF_INET, &rec->key.ip, ip_buf, sizeof(ip_buf))) {
            snprintf(ip_buf, sizeof(ip_buf), "<invalid>");
        }
        td_log_writef(TD_LOG_INFO,
                      "terminal_events",
                      "event=%s mac=%s ip=%s port=%u",
                      event_tag_to_string(rec->tag),
                      mac_buf,
                      ip_buf,
                      rec->port);
    }
}

static void adapter_packet_callback(const struct td_adapter_packet_view *packet, void *user_ctx) {
    struct app_context *ctx = (struct app_context *)user_ctx;
    if (!ctx || !ctx->manager || !packet) {
        return;
    }
    terminal_manager_on_packet(ctx->manager, packet);
}

static void terminal_probe_handler(const terminal_probe_request_t *request, void *user_ctx) {
    struct app_context *ctx = (struct app_context *)user_ctx;
    if (!ctx || !ctx->ops || !ctx->adapter || !request) {
        return;
    }

    struct td_adapter_arp_request arp_req;
    memset(&arp_req, 0, sizeof(arp_req));

    arp_req.target_ip = request->key.ip;
    memcpy(arp_req.target_mac, request->key.mac, ETH_ALEN);
    arp_req.sender_ip = request->source_ip;
    arp_req.vlan_id = request->vlan_id;
    bool fallback_possible = (request->tx_ifindex > 0 && request->tx_iface[0] != '\0');
    arp_req.tx_ifindex = fallback_possible ? request->tx_ifindex : -1;
    if (fallback_possible) {
        snprintf(arp_req.tx_iface, sizeof(arp_req.tx_iface), "%s", request->tx_iface);
    }

    td_adapter_result_t rc = ctx->ops->send_arp(ctx->adapter, &arp_req);
    if (rc != TD_ADAPTER_OK && fallback_possible) {
        struct td_adapter_arp_request fallback_req = arp_req;
        fallback_req.tx_iface_valid = true;
        rc = ctx->ops->send_arp(ctx->adapter, &fallback_req);
        if (rc == TD_ADAPTER_OK) {
            td_log_writef(TD_LOG_INFO,
                          "terminal_probe",
                          "fallback to %s for VLAN %d probes succeeded",
                          request->tx_iface,
                          request->vlan_id);
        }
    }

    if (rc != TD_ADAPTER_OK) {
        char mac_buf[18];
        char ip_buf[INET_ADDRSTRLEN];
        format_mac(request->key.mac, mac_buf);
        if (!inet_ntop(AF_INET, &request->key.ip, ip_buf, sizeof(ip_buf))) {
            snprintf(ip_buf, sizeof(ip_buf), "<invalid>");
        }
        td_log_writef(TD_LOG_WARN,
                      "terminal_probe",
                      "failed to send probe (state=%s mac=%s ip=%s rc=%d)",
                      state_to_string(request->state_before_probe),
                      mac_buf,
                      ip_buf,
                      rc);
    }
}

static void print_usage(FILE *stream) {
    fprintf(stream,
            "Usage: %s [options]\n"
            "Options:\n"
            "  --adapter NAME            Adapter name (default: realtek)\n"
            "  --rx-iface NAME           Interface to capture ARP (default: eth0)\n"
            "  --tx-iface NAME           Interface to transmit ARP (default: eth0)\n"
            "  --tx-interval MS          Minimum milliseconds between probes (default: 100)\n"
            "  --keepalive-interval SEC  Keepalive interval seconds (default: 120)\n"
            "  --keepalive-miss COUNT    Probe failure threshold (default: 3)\n"
            "  --iface-holdoff SEC       Holdoff after iface invalid (default: 1800)\n"
            "  --max-terminals COUNT     Maximum tracked terminals (default: 1000)\n"
            "  --stats-interval SEC      Stats log interval seconds, 0 disables (default: 0)\n"
            "  --log-level LEVEL         Log level trace|debug|info|warn|error|none (default: info)\n"
            "  --help                    Show this help message\n",
            g_program_name);
}

static int parse_unsigned_option(const char *opt_name, const char *value, unsigned int *out) {
    if (!value || !out) {
        return -1;
    }
    errno = 0;
    char *endptr = NULL;
    unsigned long parsed = strtoul(value, &endptr, 10);
    if (errno != 0 || !endptr || *endptr != '\0' || parsed > UINT32_MAX) {
        fprintf(stderr, "%s: invalid value '%s' for %s\n", g_program_name, value, opt_name);
        return -1;
    }
    *out = (unsigned int)parsed;
    return 0;
}

static int parse_size_t_option(const char *opt_name, const char *value, size_t *out) {
    if (!value || !out) {
        return -1;
    }
    errno = 0;
    char *endptr = NULL;
    unsigned long parsed = strtoul(value, &endptr, 10);
    if (errno != 0 || !endptr || *endptr != '\0') {
        fprintf(stderr, "%s: invalid value '%s' for %s\n", g_program_name, value, opt_name);
        return -1;
    }
    *out = (size_t)parsed;
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 0 && argv && argv[0]) {
        g_program_name = argv[0];
    }

    struct td_runtime_config runtime_cfg;
    if (td_config_load_defaults(&runtime_cfg) != 0) {
        fprintf(stderr, "%s: failed to load default runtime configuration\n", g_program_name);
        return EXIT_FAILURE;
    }

    static const struct option long_opts[] = {
        {"adapter", required_argument, NULL, 'a'},
        {"rx-iface", required_argument, NULL, 'r'},
        {"tx-iface", required_argument, NULL, 't'},
        {"tx-interval", required_argument, NULL, 'T'},
    {"keepalive-interval", required_argument, NULL, 'k'},
    {"keepalive-miss", required_argument, NULL, 'm'},
    {"iface-holdoff", required_argument, NULL, 'H'},
    {"max-terminals", required_argument, NULL, 'M'},
    {"stats-interval", required_argument, NULL, 'S'},
    {"log-level", required_argument, NULL, 'l'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'a':
            snprintf(runtime_cfg.adapter_name, sizeof(runtime_cfg.adapter_name), "%s", optarg);
            break;
        case 'r':
            snprintf(runtime_cfg.rx_iface, sizeof(runtime_cfg.rx_iface), "%s", optarg);
            break;
        case 't':
            snprintf(runtime_cfg.tx_iface, sizeof(runtime_cfg.tx_iface), "%s", optarg);
            break;
        case 'T':
            if (parse_unsigned_option("--tx-interval", optarg, &runtime_cfg.tx_interval_ms) != 0) {
                return EXIT_FAILURE;
            }
            break;
        case 'k':
            if (parse_unsigned_option("--keepalive-interval", optarg, &runtime_cfg.keepalive_interval_sec) != 0) {
                return EXIT_FAILURE;
            }
            break;
        case 'm':
            if (parse_unsigned_option("--keepalive-miss", optarg, &runtime_cfg.keepalive_miss_threshold) != 0) {
                return EXIT_FAILURE;
            }
            break;
        case 'H':
            if (parse_unsigned_option("--iface-holdoff", optarg, &runtime_cfg.iface_invalid_holdoff_sec) != 0) {
                return EXIT_FAILURE;
            }
            break;
        case 'M':
        {
            size_t parsed = 0;
            if (parse_size_t_option("--max-terminals", optarg, &parsed) != 0) {
                return EXIT_FAILURE;
            }
            if (parsed == 0 || parsed > UINT32_MAX) {
                fprintf(stderr, "%s: max-terminals must be between 1 and %u\n", g_program_name, UINT32_MAX);
                return EXIT_FAILURE;
            }
            runtime_cfg.max_terminals = (unsigned int)parsed;
            break;
        }
        case 'S':
            if (parse_unsigned_option("--stats-interval", optarg, &runtime_cfg.stats_log_interval_sec) != 0) {
                return EXIT_FAILURE;
            }
            break;
        case 'l':
        {
            bool ok = false;
            td_log_level_t level = td_log_level_from_string(optarg, &ok);
            if (!ok) {
                fprintf(stderr, "%s: invalid log level '%s'\n", g_program_name, optarg);
                return EXIT_FAILURE;
            }
            runtime_cfg.log_level = level;
            break;
        }
        case 'h':
            print_usage(stdout);
            return EXIT_SUCCESS;
        default:
            print_usage(stderr);
            return EXIT_FAILURE;
        }
    }

    td_log_set_level(runtime_cfg.log_level);
    td_log_writef(TD_LOG_INFO,
                  "terminal_daemon",
                  "starting (adapter=%s rx=%s tx=%s keepalive=%us miss=%u holdoff=%us max=%u stats=%us)",
                  runtime_cfg.adapter_name,
                  runtime_cfg.rx_iface,
                  runtime_cfg.tx_iface,
                  runtime_cfg.keepalive_interval_sec,
                  runtime_cfg.keepalive_miss_threshold,
                  runtime_cfg.iface_invalid_holdoff_sec,
                  runtime_cfg.max_terminals,
                  runtime_cfg.stats_log_interval_sec);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGUSR1, handle_stats_signal);

    const struct td_adapter_descriptor *adapter_desc = td_adapter_registry_find(runtime_cfg.adapter_name);
    if (!adapter_desc || !adapter_desc->ops) {
        td_log_writef(TD_LOG_ERROR, "terminal_daemon", "adapter '%s' not found", runtime_cfg.adapter_name);
        return EXIT_FAILURE;
    }

    struct td_adapter_config adapter_cfg = {
        .rx_iface = runtime_cfg.rx_iface,
        .tx_iface = runtime_cfg.tx_iface,
        .tx_interval_ms = runtime_cfg.tx_interval_ms,
        .rx_ring_size = 0,
    };

    struct td_adapter_env adapter_env = {
        .log_fn = adapter_log_bridge,
        .log_user_data = NULL,
    };

    td_adapter_t *adapter_handle = NULL;
    td_adapter_result_t adapter_rc = adapter_desc->ops->init(&adapter_cfg, &adapter_env, &adapter_handle);
    if (adapter_rc != TD_ADAPTER_OK) {
        td_log_writef(TD_LOG_ERROR, "terminal_daemon", "adapter init failed: %d", adapter_rc);
        return EXIT_FAILURE;
    }

    struct terminal_manager_config manager_cfg;
    if (td_config_to_manager_config(&runtime_cfg, &manager_cfg) != 0) {
        td_log_writef(TD_LOG_ERROR, "terminal_daemon", "failed to translate runtime config");
        adapter_desc->ops->shutdown(adapter_handle);
        return EXIT_FAILURE;
    }

    struct app_context ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.adapter = adapter_handle;
    ctx.ops = adapter_desc->ops;
    ctx.netlink_listener = NULL;

    struct terminal_manager *manager = terminal_manager_create(&manager_cfg,
                                                               adapter_handle,
                                                               terminal_probe_handler,
                                                               &ctx);
    if (!manager) {
        td_log_writef(TD_LOG_ERROR, "terminal_daemon", "failed to create terminal manager");
        adapter_desc->ops->shutdown(adapter_handle);
        return EXIT_FAILURE;
    }
    ctx.manager = manager;

    struct terminal_netlink_listener *netlink_listener = NULL;
    if (terminal_netlink_start(manager, &netlink_listener) != 0) {
        td_log_writef(TD_LOG_ERROR, "terminal_daemon", "failed to start netlink listener");
        terminal_manager_destroy(manager);
        adapter_desc->ops->shutdown(adapter_handle);
        return EXIT_FAILURE;
    }
    ctx.netlink_listener = netlink_listener;

    if (terminal_manager_set_event_sink(manager, terminal_event_logger, &ctx) != 0) {
        td_log_writef(TD_LOG_ERROR, "terminal_daemon", "failed to set event sink");
        terminal_netlink_stop(ctx.netlink_listener);
        ctx.netlink_listener = NULL;
        terminal_manager_destroy(manager);
        adapter_desc->ops->shutdown(adapter_handle);
        return EXIT_FAILURE;
    }

    struct td_adapter_packet_subscription packet_sub = {
        .callback = adapter_packet_callback,
        .user_ctx = &ctx,
    };

    adapter_rc = adapter_desc->ops->register_packet_rx(adapter_handle, &packet_sub);
    if (adapter_rc != TD_ADAPTER_OK) {
        td_log_writef(TD_LOG_ERROR, "terminal_daemon", "register_packet_rx failed: %d", adapter_rc);
        terminal_netlink_stop(ctx.netlink_listener);
        ctx.netlink_listener = NULL;
        terminal_manager_set_event_sink(manager, NULL, NULL);
        terminal_manager_destroy(manager);
        adapter_desc->ops->shutdown(adapter_handle);
        return EXIT_FAILURE;
    }

    adapter_rc = adapter_desc->ops->start(adapter_handle);
    if (adapter_rc != TD_ADAPTER_OK) {
        td_log_writef(TD_LOG_ERROR, "terminal_daemon", "adapter start failed: %d", adapter_rc);
        terminal_netlink_stop(ctx.netlink_listener);
        ctx.netlink_listener = NULL;
        terminal_manager_set_event_sink(manager, NULL, NULL);
        terminal_manager_destroy(manager);
        adapter_desc->ops->shutdown(adapter_handle);
        return EXIT_FAILURE;
    }

    unsigned int stats_elapsed_sec = 0;
    while (!g_should_stop) {
        (void)sleep(1);
        if (g_should_stop) {
            break;
        }
        if (g_should_dump_stats) {
            g_should_dump_stats = 0;
            log_manager_stats(manager);
        }
        if (runtime_cfg.stats_log_interval_sec > 0) {
            if (++stats_elapsed_sec >= runtime_cfg.stats_log_interval_sec) {
                stats_elapsed_sec = 0;
                log_manager_stats(manager);
            }
        }
    }

    if (g_should_dump_stats) {
        g_should_dump_stats = 0;
        log_manager_stats(manager);
    }

    td_log_writef(TD_LOG_INFO, "terminal_daemon", "signal %d received, shutting down", g_should_stop);
    log_manager_stats(manager);

    adapter_desc->ops->stop(adapter_handle);
    terminal_netlink_stop(ctx.netlink_listener);
    ctx.netlink_listener = NULL;
    terminal_manager_flush_events(manager);
    terminal_manager_set_event_sink(manager, NULL, NULL);
    terminal_manager_destroy(manager);
    adapter_desc->ops->shutdown(adapter_handle);

    td_log_writef(TD_LOG_INFO, "terminal_daemon", "shutdown complete");
    return EXIT_SUCCESS;
}
