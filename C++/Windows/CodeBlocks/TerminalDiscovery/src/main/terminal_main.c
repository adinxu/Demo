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
#include <sys/select.h>

#include "td_adapter_registry.h"
#include "td_config.h"
#include "td_logging.h"
#include "terminal_discovery_embed.h"
#include "terminal_manager.h"
#include "terminal_netlink.h"

int terminal_northbound_attach_default_sink(struct terminal_manager *manager);

#ifndef TD_DISABLE_APP_MAIN
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
#endif

struct app_context {
    struct terminal_manager *manager;
    td_adapter_t *adapter;
    const struct td_adapter_ops *ops;
    struct terminal_netlink_listener *netlink_listener;
    bool adapter_started;
    bool packet_rx_registered;
};

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

#ifndef TD_DISABLE_APP_MAIN

static void print_prompt(void) {
    fprintf(stdout, "td> ");
    fflush(stdout);
}

static void show_runtime_config(const struct td_runtime_config *cfg) {
    if (!cfg) {
        return;
    }

    td_log_writef(TD_LOG_INFO,
                  "terminal_daemon",
                  "config adapter=%s rx=%s tx=%s tx_interval_ms=%u keepalive=%us miss=%u holdoff=%us max=%u log_level=%d",
                  cfg->adapter_name,
                  cfg->rx_iface,
                  cfg->tx_iface,
                  cfg->tx_interval_ms,
                  cfg->keepalive_interval_sec,
                  cfg->keepalive_miss_threshold,
                  cfg->iface_invalid_holdoff_sec,
                  cfg->max_terminals,
                  (int)cfg->log_level);
}

static bool parse_unsigned_value(const char *value, unsigned long long max, unsigned long long *out) {
    if (!value || !out) {
        return false;
    }
    errno = 0;
    char *endptr = NULL;
    unsigned long long parsed = strtoull(value, &endptr, 10);
    if (errno != 0 || !endptr || *endptr != '\0' || parsed > max) {
        return false;
    }
    *out = parsed;
    return true;
}

static void log_manager_stats(struct terminal_manager *manager);

static void handle_command(const char *command,
                           struct app_context *ctx,
                           struct td_runtime_config *runtime_cfg) {
    if (!command || !ctx) {
        return;
    }

    if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0) {
        td_log_writef(TD_LOG_INFO, "terminal_daemon", "CLI requested shutdown");
        g_should_stop = SIGINT;
        return;
    }

    if (strcmp(command, "stats") == 0) {
        log_manager_stats(ctx->manager);
        return;
    }

    if (strcmp(command, "dump terminal") == 0) {
        if (ctx->manager) {
            td_debug_dump_context_t dump_ctx;
            td_debug_context_reset(&dump_ctx, NULL);
            struct td_debug_file_writer_ctx writer_ctx;
            td_debug_file_writer_ctx_init(&writer_ctx, stdout, &dump_ctx);
            int dump_rc = td_debug_dump_terminal_table(ctx->manager,
                                                       NULL,
                                                       td_debug_writer_file,
                                                       &writer_ctx,
                                                       &dump_ctx);
            if (dump_rc != 0) {
                td_log_writef(TD_LOG_WARN, "terminal_daemon", "dump terminal failed: %d", dump_rc);
            }
            fflush(stdout);
        }
        return;
    }

    if (strcmp(command, "dump prefix") == 0) {
        if (ctx->manager) {
            td_debug_dump_context_t dump_ctx;
            td_debug_context_reset(&dump_ctx, NULL);
            struct td_debug_file_writer_ctx writer_ctx;
            td_debug_file_writer_ctx_init(&writer_ctx, stdout, &dump_ctx);
            int dump_rc = td_debug_dump_iface_prefix_table(ctx->manager,
                                                           td_debug_writer_file,
                                                           &writer_ctx,
                                                           &dump_ctx);
            if (dump_rc != 0) {
                td_log_writef(TD_LOG_WARN, "terminal_daemon", "dump prefix failed: %d", dump_rc);
            }
            fflush(stdout);
        }
        return;
    }

    if (strcmp(command, "dump binding") == 0) {
        if (ctx->manager) {
            td_debug_dump_context_t dump_ctx;
            td_debug_context_reset(&dump_ctx, NULL);
            struct td_debug_file_writer_ctx writer_ctx;
            td_debug_file_writer_ctx_init(&writer_ctx, stdout, &dump_ctx);
            int dump_rc = td_debug_dump_iface_binding_table(ctx->manager,
                                                            NULL,
                                                            td_debug_writer_file,
                                                            &writer_ctx,
                                                            &dump_ctx);
            if (dump_rc != 0) {
                td_log_writef(TD_LOG_WARN, "terminal_daemon", "dump binding failed: %d", dump_rc);
            }
            fflush(stdout);
        }
        return;
    }

    if (strcmp(command, "dump mac queue") == 0) {
        if (ctx->manager) {
            td_debug_dump_context_t dump_ctx;
            td_debug_context_reset(&dump_ctx, NULL);
            struct td_debug_file_writer_ctx writer_ctx;
            td_debug_file_writer_ctx_init(&writer_ctx, stdout, &dump_ctx);
            int dump_rc = td_debug_dump_mac_lookup_queue(ctx->manager,
                                                         td_debug_writer_file,
                                                         &writer_ctx,
                                                         &dump_ctx);
            if (dump_rc != 0) {
                td_log_writef(TD_LOG_WARN, "terminal_daemon", "dump mac queue failed: %d", dump_rc);
            }
            fflush(stdout);
        }
        return;
    }

    if (strcmp(command, "dump mac state") == 0) {
        if (ctx->manager) {
            td_debug_dump_context_t dump_ctx;
            td_debug_context_reset(&dump_ctx, NULL);
            struct td_debug_file_writer_ctx writer_ctx;
            td_debug_file_writer_ctx_init(&writer_ctx, stdout, &dump_ctx);
            int dump_rc = td_debug_dump_mac_locator_state(ctx->manager,
                                                          td_debug_writer_file,
                                                          &writer_ctx,
                                                          &dump_ctx);
            if (dump_rc != 0) {
                td_log_writef(TD_LOG_WARN, "terminal_daemon", "dump mac state failed: %d", dump_rc);
            }
            fflush(stdout);
        }
        return;
    }

    if (strcmp(command, "show config") == 0) {
        show_runtime_config(runtime_cfg);
        return;
    }

    if (strcmp(command, "help") == 0) {
        td_log_writef(TD_LOG_INFO,
                      "terminal_daemon",
                      "commands: stats | dump terminal | dump prefix | dump binding | dump mac queue | dump mac state | show config | set <option> <value> | exit | quit | help");
        return;
    }

    if (strncmp(command, "set ", 4) == 0) {
        char workbuf[64];
        snprintf(workbuf, sizeof(workbuf), "%s", command);

        char *saveptr = NULL;
        char *token = strtok_r(workbuf, " ", &saveptr); /* set */
        (void)token;
        char *key = strtok_r(NULL, " ", &saveptr);
        char *value = strtok_r(NULL, " ", &saveptr);

        if (!key || !value) {
            td_log_writef(TD_LOG_WARN,
                          "terminal_daemon",
                          "usage: set <keepalive|miss|holdoff|max|log-level> <value>");
            return;
        }

        if (strcmp(key, "keepalive") == 0) {
            unsigned long long parsed = 0ULL;
            if (!parse_unsigned_value(value, UINT32_MAX, &parsed)) {
                td_log_writef(TD_LOG_WARN, "terminal_daemon", "invalid keepalive value '%s'", value);
                return;
            }
            if (parsed == 0ULL) {
                parsed = TD_DEFAULT_KEEPALIVE_INTERVAL_SEC;
            }
            if (ctx->manager && terminal_manager_set_keepalive_interval(ctx->manager, (unsigned int)parsed) == 0) {
                runtime_cfg->keepalive_interval_sec = (unsigned int)parsed;
                td_log_writef(TD_LOG_INFO,
                              "terminal_daemon",
                              "keepalive interval updated to %us",
                              runtime_cfg->keepalive_interval_sec);
            }
            return;
        }

        if (strcmp(key, "miss") == 0) {
            unsigned long long parsed = 0ULL;
            if (!parse_unsigned_value(value, UINT32_MAX, &parsed)) {
                td_log_writef(TD_LOG_WARN, "terminal_daemon", "invalid miss threshold '%s'", value);
                return;
            }
            if (parsed == 0ULL) {
                parsed = TD_DEFAULT_KEEPALIVE_MISS_THRESHOLD;
            }
            if (ctx->manager && terminal_manager_set_keepalive_miss_threshold(ctx->manager, (unsigned int)parsed) == 0) {
                runtime_cfg->keepalive_miss_threshold = (unsigned int)parsed;
                td_log_writef(TD_LOG_INFO,
                              "terminal_daemon",
                              "keepalive miss threshold updated to %u",
                              runtime_cfg->keepalive_miss_threshold);
            }
            return;
        }

        if (strcmp(key, "holdoff") == 0) {
            unsigned long long parsed = 0ULL;
            if (!parse_unsigned_value(value, UINT32_MAX, &parsed)) {
                td_log_writef(TD_LOG_WARN, "terminal_daemon", "invalid holdoff value '%s'", value);
                return;
            }
            if (parsed == 0ULL) {
                parsed = TD_DEFAULT_IFACE_INVALID_HOLDOFF_SEC;
            }
            if (ctx->manager && terminal_manager_set_iface_invalid_holdoff(ctx->manager, (unsigned int)parsed) == 0) {
                runtime_cfg->iface_invalid_holdoff_sec = (unsigned int)parsed;
                td_log_writef(TD_LOG_INFO,
                              "terminal_daemon",
                              "iface invalid holdoff updated to %us",
                              runtime_cfg->iface_invalid_holdoff_sec);
            }
            return;
        }

        if (strcmp(key, "max") == 0) {
            unsigned long long parsed = 0ULL;
            if (!parse_unsigned_value(value, UINT32_MAX, &parsed) || parsed == 0ULL) {
                td_log_writef(TD_LOG_WARN, "terminal_daemon", "invalid max terminals '%s'", value);
                return;
            }
            if (ctx->manager && terminal_manager_set_max_terminals(ctx->manager, (size_t)parsed) == 0) {
                runtime_cfg->max_terminals = (unsigned int)parsed;
                td_log_writef(TD_LOG_INFO,
                              "terminal_daemon",
                              "max terminals updated to %u",
                              runtime_cfg->max_terminals);
            }
            return;
        }

        if (strcmp(key, "log-level") == 0) {
            bool ok = false;
            td_log_level_t level = td_log_level_from_string(value, &ok);
            if (!ok) {
                td_log_writef(TD_LOG_WARN, "terminal_daemon", "invalid log level '%s'", value);
                return;
            }
            runtime_cfg->log_level = level;
            td_log_set_level(level);
            td_log_writef(TD_LOG_INFO, "terminal_daemon", "log level updated to %s", value);
            return;
        }

        td_log_writef(TD_LOG_WARN,
                      "terminal_daemon",
                      "unknown set option '%s'",
                      key);
        return;
    }

    td_log_writef(TD_LOG_WARN,
                  "terminal_daemon",
                  "unknown command '%s' (try 'help')",
                  command);
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

#endif /* TD_DISABLE_APP_MAIN */

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
    bool fallback_possible = (request->tx_kernel_ifindex > 0 && request->tx_iface[0] != '\0');
    arp_req.tx_kernel_ifindex = fallback_possible ? request->tx_kernel_ifindex : -1;
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

static void terminal_discovery_cleanup(struct app_context *ctx) {
    if (!ctx) {
        return;
    }

    if (ctx->ops && ctx->adapter && ctx->adapter_started) {
        ctx->ops->stop(ctx->adapter);
        ctx->adapter_started = false;
    }

    if (ctx->netlink_listener) {
        terminal_netlink_stop(ctx->netlink_listener);
        ctx->netlink_listener = NULL;
    }

    if (ctx->manager) {
        terminal_manager_flush_events(ctx->manager);
        terminal_manager_set_event_sink(ctx->manager, NULL, NULL);
        terminal_manager_destroy(ctx->manager);
        ctx->manager = NULL;
    }

    if (ctx->ops && ctx->adapter) {
        ctx->ops->shutdown(ctx->adapter);
        ctx->adapter = NULL;
    }

    ctx->ops = NULL;
    ctx->packet_rx_registered = false;
}

static int terminal_discovery_bootstrap(const struct td_runtime_config *runtime_cfg,
                                        struct app_context *ctx) {
    if (!runtime_cfg || !ctx) {
        return -EINVAL;
    }

    memset(ctx, 0, sizeof(*ctx));

    const struct td_adapter_descriptor *adapter_desc = td_adapter_registry_find(runtime_cfg->adapter_name);
    if (!adapter_desc || !adapter_desc->ops) {
        td_log_writef(TD_LOG_ERROR, "terminal_daemon", "adapter '%s' not found", runtime_cfg->adapter_name);
        return -ENOENT;
    }

    struct td_adapter_config adapter_cfg = {
        .rx_iface = runtime_cfg->rx_iface,
        .tx_iface = runtime_cfg->tx_iface,
        .tx_interval_ms = runtime_cfg->tx_interval_ms,
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
        return adapter_rc;
    }

    ctx->adapter = adapter_handle;
    ctx->ops = adapter_desc->ops;

    struct terminal_manager_config manager_cfg;
    if (td_config_to_manager_config(runtime_cfg, &manager_cfg) != 0) {
        td_log_writef(TD_LOG_ERROR, "terminal_daemon", "failed to translate runtime config");
        terminal_discovery_cleanup(ctx);
        return -1;
    }

    struct terminal_manager *manager = terminal_manager_create(&manager_cfg,
                                                               adapter_handle,
                                                               adapter_desc->ops,
                                                               terminal_probe_handler,
                                                               ctx);
    if (!manager) {
        td_log_writef(TD_LOG_ERROR, "terminal_daemon", "failed to create terminal manager");
        terminal_discovery_cleanup(ctx);
        return -1;
    }
    ctx->manager = manager;

    struct terminal_netlink_listener *netlink_listener = NULL;
    if (terminal_netlink_start(manager, &netlink_listener) != 0) {
        td_log_writef(TD_LOG_ERROR, "terminal_daemon", "failed to start netlink listener");
        terminal_discovery_cleanup(ctx);
        return -1;
    }
    ctx->netlink_listener = netlink_listener;

    if (terminal_northbound_attach_default_sink(manager) != 0) {
        td_log_writef(TD_LOG_ERROR, "terminal_daemon", "failed to attach northbound event sink");
        terminal_discovery_cleanup(ctx);
        return -1;
    }

    struct td_adapter_packet_subscription packet_sub = {
        .callback = adapter_packet_callback,
        .user_ctx = ctx,
    };

    adapter_rc = adapter_desc->ops->register_packet_rx(adapter_handle, &packet_sub);
    if (adapter_rc != TD_ADAPTER_OK) {
        td_log_writef(TD_LOG_ERROR, "terminal_daemon", "register_packet_rx failed: %d", adapter_rc);
        terminal_discovery_cleanup(ctx);
        return adapter_rc;
    }
    ctx->packet_rx_registered = true;

    adapter_rc = adapter_desc->ops->start(adapter_handle);
    if (adapter_rc != TD_ADAPTER_OK) {
        td_log_writef(TD_LOG_ERROR, "terminal_daemon", "adapter start failed: %d", adapter_rc);
        terminal_discovery_cleanup(ctx);
        return adapter_rc;
    }
    ctx->adapter_started = true;

    return 0;
}

#ifndef TD_DISABLE_APP_MAIN

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

    struct app_context ctx;
    int bootstrap_rc = terminal_discovery_bootstrap(&runtime_cfg, &ctx);
    if (bootstrap_rc != 0) {
        return EXIT_FAILURE;
    }

    unsigned int stats_elapsed_sec = 0;
    fd_set read_fds;
    int stdin_fd = fileno(stdin);
    int max_fd = stdin_fd;
    struct timeval poll_timeout;

    td_log_writef(TD_LOG_INFO,
                  "terminal_daemon",
                  "interactive commands enabled (type 'help' for list)");
    print_prompt();

    while (!g_should_stop) {
        FD_ZERO(&read_fds);
        FD_SET(stdin_fd, &read_fds);

        poll_timeout.tv_sec = 1;
        poll_timeout.tv_usec = 0;

        int sel_rc = select(max_fd + 1, &read_fds, NULL, NULL, &poll_timeout);
        if (sel_rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            td_log_writef(TD_LOG_ERROR, "terminal_daemon", "select failed: %s", strerror(errno));
            break;
        }

        if (g_should_stop) {
            break;
        }

        if (sel_rc > 0 && FD_ISSET(stdin_fd, &read_fds)) {
            char command_buf[64];
            if (!fgets(command_buf, sizeof(command_buf), stdin)) {
                if (feof(stdin)) {
                    td_log_writef(TD_LOG_INFO, "terminal_daemon", "stdin closed; shutting down");
                    break;
                }
                if (ferror(stdin)) {
                    clearerr(stdin);
                }
            } else {
                char *newline = strpbrk(command_buf, "\r\n");
                if (newline) {
                    *newline = '\0';
                }

                if (command_buf[0] == '\0') {
                    print_prompt();
                    continue;
                }

                handle_command(command_buf, &ctx, &runtime_cfg);
                if (!g_should_stop) {
                    print_prompt();
                }
            }
        }

        if (g_should_dump_stats) {
            g_should_dump_stats = 0;
            log_manager_stats(ctx.manager);
        }

        if (runtime_cfg.stats_log_interval_sec > 0) {
            if (++stats_elapsed_sec >= runtime_cfg.stats_log_interval_sec) {
                stats_elapsed_sec = 0;
                log_manager_stats(ctx.manager);
            }
        }
    }

    if (g_should_dump_stats) {
        g_should_dump_stats = 0;
        log_manager_stats(ctx.manager);
    }

    td_log_writef(TD_LOG_INFO, "terminal_daemon", "signal %d received, shutting down", g_should_stop);
    log_manager_stats(ctx.manager);

    terminal_discovery_cleanup(&ctx);

    td_log_writef(TD_LOG_INFO, "terminal_daemon", "shutdown complete");
    return EXIT_SUCCESS;
}

#endif /* TD_DISABLE_APP_MAIN */

static struct app_context g_embedded_ctx;
static bool g_embedded_initialized = false;

struct terminal_manager *terminal_discovery_get_manager(void) {
    if (!g_embedded_initialized) {
        return NULL;
    }
    return g_embedded_ctx.manager;
}

const struct app_context *terminal_discovery_get_app_context(void) {
    if (!g_embedded_initialized) {
        return NULL;
    }
    return &g_embedded_ctx;
}

int terminal_discovery_initialize(const struct terminal_discovery_init_params *params) {
    if (!params) {
        return -EINVAL;
    }

    if (g_embedded_initialized) {
        return -EALREADY;
    }

    struct td_runtime_config runtime_cfg;
    if (td_config_load_defaults(&runtime_cfg) != 0) {
        td_log_writef(TD_LOG_ERROR, "terminal_daemon", "failed to load default runtime configuration");
        return -EIO;
    }

    if (params->runtime_config) {
        runtime_cfg = *params->runtime_config;
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

    int rc = terminal_discovery_bootstrap(&runtime_cfg, &g_embedded_ctx);
    if (rc != 0) {
        terminal_discovery_cleanup(&g_embedded_ctx);
        memset(&g_embedded_ctx, 0, sizeof(g_embedded_ctx));
        return rc;
    }

    g_embedded_initialized = true;
    return 0;
}
