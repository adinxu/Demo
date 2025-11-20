#include "terminal_discovery_embed.h"

#include "adapter_api.h"
#include "td_adapter_registry.h"
#include "td_config.h"
#include "td_logging.h"
#include "terminal_manager.h"
#include "terminal_netlink.h"

int terminal_northbound_attach_default_sink(struct terminal_manager *manager);

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct td_adapter {
    int placeholder;
};

struct terminal_manager {
    int active;
};

struct terminal_netlink_listener {
    int active;
};

struct stub_state {
    td_adapter_result_t init_result;
    td_adapter_result_t register_result;
    td_adapter_result_t start_result;
    int netlink_start_result;
    int northbound_attach_result;
    int set_event_sink_result;
    bool fail_manager_create;
    unsigned int init_calls;
    unsigned int shutdown_calls;
    unsigned int start_calls;
    unsigned int stop_calls;
    unsigned int register_calls;
    unsigned int destroy_calls;
    unsigned int flush_calls;
    unsigned int netlink_start_calls;
    unsigned int netlink_stop_calls;
    unsigned int set_sink_calls;
    unsigned int northbound_attach_calls;
    unsigned int set_sync_calls;
    unsigned int request_sync_calls;
    terminal_address_sync_fn last_sync_handler;
    void *last_sync_ctx;
    struct terminal_manager_config manager_cfg;
    terminal_event_callback_fn registered_callback;
    void *registered_ctx;
    terminal_event_callback_fn last_nonnull_callback;
    void *last_nonnull_ctx;
    struct terminal_manager *last_attached_manager;
    struct td_adapter_packet_subscription last_subscription;
};

static struct stub_state g_stub;
static struct td_adapter g_stub_adapter_handle;
static struct terminal_manager g_stub_manager_handle;
static struct terminal_netlink_listener g_stub_listener_handle;

static void stub_default_sink(const terminal_event_record_t *records,
                              size_t count,
                              void *ctx) {
    (void)records;
    (void)count;
    (void)ctx;
}

static td_adapter_result_t stub_adapter_init(const struct td_adapter_config *cfg,
                                             const struct td_adapter_env *env,
                                             td_adapter_t **handle) {
    (void)cfg;
    (void)env;
    g_stub.init_calls += 1;
    if (g_stub.init_result != TD_ADAPTER_OK) {
        return g_stub.init_result;
    }
    if (handle) {
        *handle = &g_stub_adapter_handle;
    }
    return TD_ADAPTER_OK;
}

static void stub_adapter_shutdown(td_adapter_t *handle) {
    (void)handle;
    g_stub.shutdown_calls += 1;
}

static td_adapter_result_t stub_adapter_start(td_adapter_t *handle) {
    (void)handle;
    g_stub.start_calls += 1;
    if (g_stub.start_result != TD_ADAPTER_OK) {
        return g_stub.start_result;
    }
    return TD_ADAPTER_OK;
}

static void stub_adapter_stop(td_adapter_t *handle) {
    (void)handle;
    g_stub.stop_calls += 1;
}

static td_adapter_result_t stub_adapter_register(td_adapter_t *handle,
                                                 const struct td_adapter_packet_subscription *sub) {
    (void)handle;
    g_stub.register_calls += 1;
    if (sub) {
        g_stub.last_subscription = *sub;
    } else {
        memset(&g_stub.last_subscription, 0, sizeof(g_stub.last_subscription));
    }
    if (g_stub.register_result != TD_ADAPTER_OK) {
        return g_stub.register_result;
    }
    return TD_ADAPTER_OK;
}

static const struct td_adapter_ops g_stub_adapter_ops = {
    .init = stub_adapter_init,
    .shutdown = stub_adapter_shutdown,
    .start = stub_adapter_start,
    .stop = stub_adapter_stop,
    .register_packet_rx = stub_adapter_register,
    .send_arp = NULL,
    .query_iface = NULL,
    .log_write = NULL,
    .mac_locator_ops = NULL,
};

static const struct td_adapter_descriptor g_stub_descriptor = {
    .name = "stub",
    .ops = &g_stub_adapter_ops,
};

const struct td_adapter_descriptor *td_adapter_registry_find(const char *name) {
    (void)name;
    return &g_stub_descriptor;
}

struct terminal_manager *terminal_manager_create(const struct terminal_manager_config *cfg,
                                                  td_adapter_t *adapter,
                                                  const struct td_adapter_ops *adapter_ops,
                                                  terminal_probe_fn probe_cb,
                                                  void *probe_ctx) {
    (void)adapter;
    (void)adapter_ops;
    (void)probe_cb;
    (void)probe_ctx;
    if (g_stub.fail_manager_create) {
        return NULL;
    }
    if (cfg) {
        g_stub.manager_cfg = *cfg;
    } else {
        memset(&g_stub.manager_cfg, 0, sizeof(g_stub.manager_cfg));
    }
    return &g_stub_manager_handle;
}

void terminal_manager_destroy(struct terminal_manager *mgr) {
    (void)mgr;
    g_stub.destroy_calls += 1;
}

void terminal_manager_on_packet(struct terminal_manager *mgr,
                                const struct td_adapter_packet_view *packet) {
    (void)mgr;
    (void)packet;
}

void terminal_manager_set_address_sync_handler(struct terminal_manager *mgr,
                                               terminal_address_sync_fn handler,
                                               void *handler_ctx) {
    (void)mgr;
    g_stub.set_sync_calls += 1;
    g_stub.last_sync_handler = handler;
    g_stub.last_sync_ctx = handler_ctx;
}

void terminal_manager_request_address_sync(struct terminal_manager *mgr) {
    (void)mgr;
    g_stub.request_sync_calls += 1;
}

int terminal_manager_set_event_sink(struct terminal_manager *mgr,
                                    terminal_event_callback_fn callback,
                                    void *callback_ctx) {
    (void)mgr;
    g_stub.set_sink_calls += 1;
    if (callback) {
        g_stub.last_nonnull_callback = callback;
        g_stub.last_nonnull_ctx = callback_ctx;
        if (g_stub.set_event_sink_result != 0) {
            int err = g_stub.set_event_sink_result;
            g_stub.set_event_sink_result = 0;
            return err;
        }
    }
    g_stub.registered_callback = callback;
    g_stub.registered_ctx = callback_ctx;
    return 0;
}

void terminal_manager_flush_events(struct terminal_manager *mgr) {
    (void)mgr;
    g_stub.flush_calls += 1;
}

void terminal_manager_get_stats(struct terminal_manager *mgr,
                                struct terminal_manager_stats *out) {
    (void)mgr;
    if (out) {
        memset(out, 0, sizeof(*out));
    }
}

int terminal_netlink_start(struct terminal_manager *manager,
                           struct terminal_netlink_listener **listener_out) {
    (void)manager;
    g_stub.netlink_start_calls += 1;
    if (g_stub.netlink_start_result != 0) {
        return g_stub.netlink_start_result;
    }
    if (listener_out) {
        *listener_out = &g_stub_listener_handle;
    }
    return 0;
}

void terminal_netlink_stop(struct terminal_netlink_listener *listener) {
    (void)listener;
    g_stub.netlink_stop_calls += 1;
}

int terminal_northbound_attach_default_sink(struct terminal_manager *manager) {
    g_stub.northbound_attach_calls += 1;
    g_stub.last_attached_manager = manager;
    if (g_stub.northbound_attach_result != 0) {
        return g_stub.northbound_attach_result;
    }
    return terminal_manager_set_event_sink(manager, stub_default_sink, NULL);
}

static void stub_reset(void) {
    memset(&g_stub, 0, sizeof(g_stub));
    g_stub.init_result = TD_ADAPTER_OK;
    g_stub.register_result = TD_ADAPTER_OK;
    g_stub.start_result = TD_ADAPTER_OK;
    g_stub.netlink_start_result = 0;
    g_stub.northbound_attach_result = 0;
    g_stub.set_event_sink_result = 0;
    g_stub.fail_manager_create = false;
    g_stub.registered_callback = NULL;
    g_stub.registered_ctx = NULL;
    g_stub.last_nonnull_callback = NULL;
    g_stub.last_nonnull_ctx = NULL;
    g_stub.last_attached_manager = NULL;
    g_stub.set_sync_calls = 0U;
    g_stub.request_sync_calls = 0U;
    g_stub.last_sync_handler = NULL;
    g_stub.last_sync_ctx = NULL;
    memset(&g_stub.last_subscription, 0, sizeof(g_stub.last_subscription));
    td_log_set_level(TD_LOG_NONE);
}

static bool test_accessors_before_init(void) {
    stub_reset();
    if (terminal_discovery_get_manager() != NULL) {
        fprintf(stderr, "manager accessor should be NULL before init\n");
        return false;
    }
    if (terminal_discovery_get_app_context() != NULL) {
        fprintf(stderr, "app_context accessor should be NULL before init\n");
        return false;
    }
    return true;
}

static bool test_initialize_adapter_init_failure(void) {
    stub_reset();
    g_stub.init_result = TD_ADAPTER_ERR_SYS;

    struct terminal_discovery_init_params params;
    memset(&params, 0, sizeof(params));

    int rc = terminal_discovery_initialize(&params);
    if (rc != TD_ADAPTER_ERR_SYS) {
        fprintf(stderr, "expected %d, got %d\n", TD_ADAPTER_ERR_SYS, rc);
        return false;
    }
    if (g_stub.init_calls != 1U) {
        fprintf(stderr, "expected init_calls=1, got %u\n", g_stub.init_calls);
        return false;
    }
    if (g_stub.shutdown_calls != 0U) {
        fprintf(stderr, "adapter shutdown should not run\n");
        return false;
    }
    if (g_stub.northbound_attach_calls != 0U) {
        fprintf(stderr, "northbound attach should not run\n");
        return false;
    }
    if (terminal_discovery_get_manager() != NULL || terminal_discovery_get_app_context() != NULL) {
        fprintf(stderr, "accessors should remain NULL when adapter init fails\n");
        return false;
    }
    return true;
}

static bool test_initialize_manager_create_failure(void) {
    stub_reset();
    g_stub.fail_manager_create = true;

    struct terminal_discovery_init_params params;
    memset(&params, 0, sizeof(params));

    int rc = terminal_discovery_initialize(&params);
    if (rc != -1) {
        fprintf(stderr, "expected -1, got %d\n", rc);
        return false;
    }
    if (g_stub.init_calls != 1U) {
        fprintf(stderr, "expected adapter init once\n");
        return false;
    }
    if (g_stub.shutdown_calls != 1U) {
        fprintf(stderr, "expected adapter shutdown after failure\n");
        return false;
    }
    if (g_stub.destroy_calls != 0U) {
        fprintf(stderr, "manager should not be destroyed when creation fails\n");
        return false;
    }
    if (g_stub.northbound_attach_calls != 0U) {
        fprintf(stderr, "northbound attach should not run\n");
        return false;
    }
    if (g_stub.netlink_start_calls != 0U) {
        fprintf(stderr, "netlink should not start when manager creation fails\n");
        return false;
    }
    if (terminal_discovery_get_manager() != NULL || terminal_discovery_get_app_context() != NULL) {
        fprintf(stderr, "accessors should remain NULL when manager creation fails\n");
        return false;
    }
    return true;
}

static bool test_initialize_netlink_failure_triggers_cleanup(void) {
    stub_reset();
    g_stub.netlink_start_result = -5;

    struct terminal_discovery_init_params params;
    memset(&params, 0, sizeof(params));

    int rc = terminal_discovery_initialize(&params);
    if (rc != -1) {
        fprintf(stderr, "expected -1, got %d\n", rc);
        return false;
    }
    if (g_stub.netlink_start_calls != 1U) {
        fprintf(stderr, "expected netlink start once\n");
        return false;
    }
    if (g_stub.netlink_stop_calls != 0U) {
        fprintf(stderr, "netlink stop should not run when start fails\n");
        return false;
    }
    if (g_stub.destroy_calls != 1U) {
        fprintf(stderr, "manager should be destroyed\n");
        return false;
    }
    if (g_stub.shutdown_calls != 1U) {
        fprintf(stderr, "adapter shutdown should run\n");
        return false;
    }
    if (g_stub.flush_calls != 1U) {
        fprintf(stderr, "manager events should be flushed once\n");
        return false;
    }
    if (g_stub.set_sink_calls != 1U) {
        fprintf(stderr, "cleanup should reset event sink once\n");
        return false;
    }
    if (g_stub.northbound_attach_calls != 0U) {
        fprintf(stderr, "northbound attach should not run after netlink failure\n");
        return false;
    }
    if (terminal_discovery_get_manager() != NULL || terminal_discovery_get_app_context() != NULL) {
        fprintf(stderr, "accessors should remain NULL when netlink start fails\n");
        return false;
    }
    return true;
}

static bool test_initialize_northbound_helper_failure(void) {
    stub_reset();
    g_stub.northbound_attach_result = -9;

    struct terminal_discovery_init_params params;
    memset(&params, 0, sizeof(params));

    int rc = terminal_discovery_initialize(&params);
    if (rc != -1) {
        fprintf(stderr, "expected -1, got %d\n", rc);
        return false;
    }
    if (g_stub.northbound_attach_calls != 1U) {
        fprintf(stderr, "expected northbound attach once\n");
        return false;
    }
    if (g_stub.set_sink_calls != 1U) {
        fprintf(stderr, "cleanup should reset event sink once\n");
        return false;
    }
    if (g_stub.last_nonnull_callback != NULL) {
        fprintf(stderr, "default sink should not be recorded on helper failure\n");
        return false;
    }
    if (g_stub.netlink_stop_calls != 1U) {
        fprintf(stderr, "expected netlink stop during cleanup\n");
        return false;
    }
    if (g_stub.destroy_calls != 1U) {
        fprintf(stderr, "manager should be destroyed\n");
        return false;
    }
    if (g_stub.shutdown_calls != 1U) {
        fprintf(stderr, "adapter shutdown should run\n");
        return false;
    }
    if (terminal_discovery_get_manager() != NULL || terminal_discovery_get_app_context() != NULL) {
        fprintf(stderr, "accessors should remain NULL when northbound helper fails\n");
        return false;
    }
    return true;
}

static bool test_initialize_event_sink_failure_stops_listener(void) {
    stub_reset();
    g_stub.set_event_sink_result = -7;

    struct terminal_discovery_init_params params;
    memset(&params, 0, sizeof(params));

    int rc = terminal_discovery_initialize(&params);
    if (rc != -1) {
        fprintf(stderr, "expected -1, got %d\n", rc);
        return false;
    }
    if (g_stub.northbound_attach_calls != 1U) {
        fprintf(stderr, "expected northbound attach once\n");
        return false;
    }
    if (g_stub.set_sink_calls != 2U) {
        fprintf(stderr, "expected attach attempt and cleanup reset\n");
        return false;
    }
    if (g_stub.last_nonnull_callback != stub_default_sink) {
        fprintf(stderr, "default sink should be attempted\n");
        return false;
    }
    if (g_stub.registered_callback != NULL || g_stub.registered_ctx != NULL) {
        fprintf(stderr, "default sink should not remain registered on failure\n");
        return false;
    }
    if (g_stub.netlink_stop_calls != 1U) {
        fprintf(stderr, "expected netlink stop during cleanup\n");
        return false;
    }
    if (g_stub.destroy_calls != 1U) {
        fprintf(stderr, "manager should be destroyed\n");
        return false;
    }
    if (g_stub.shutdown_calls != 1U) {
        fprintf(stderr, "adapter shutdown should run\n");
        return false;
    }
    if (terminal_discovery_get_manager() != NULL || terminal_discovery_get_app_context() != NULL) {
        fprintf(stderr, "accessors should remain NULL when sink set fails\n");
        return false;
    }
    return true;
}

static bool test_initialize_register_failure_cleans_up(void) {
    stub_reset();
    g_stub.register_result = TD_ADAPTER_ERR_SYS;

    struct terminal_discovery_init_params params;
    memset(&params, 0, sizeof(params));

    int rc = terminal_discovery_initialize(&params);
    if (rc != g_stub.register_result) {
        fprintf(stderr, "expected %d, got %d\n", g_stub.register_result, rc);
        return false;
    }
    if (g_stub.northbound_attach_calls != 1U) {
        fprintf(stderr, "expected northbound attach once\n");
        return false;
    }
    if (g_stub.set_sink_calls != 2U) {
        fprintf(stderr, "expected default sink set + cleanup reset\n");
        return false;
    }
    if (g_stub.last_nonnull_callback != stub_default_sink) {
        fprintf(stderr, "default sink should be recorded\n");
        return false;
    }
    if (g_stub.registered_callback != NULL || g_stub.registered_ctx != NULL) {
        fprintf(stderr, "cleanup should clear event sink\n");
        return false;
    }
    if (g_stub.flush_calls != 1U) {
        fprintf(stderr, "events should be flushed once\n");
        return false;
    }
    if (g_stub.netlink_stop_calls != 1U) {
        fprintf(stderr, "netlink stop should run during cleanup\n");
        return false;
    }
    if (g_stub.destroy_calls != 1U) {
        fprintf(stderr, "manager should be destroyed\n");
        return false;
    }
    if (g_stub.shutdown_calls != 1U) {
        fprintf(stderr, "adapter shutdown should run\n");
        return false;
    }
    if (terminal_discovery_get_manager() != NULL || terminal_discovery_get_app_context() != NULL) {
        fprintf(stderr, "accessors should remain NULL when registration fails\n");
        return false;
    }
    return true;
}

static bool test_initialize_success_then_repeat_guard(void) {
    stub_reset();

    struct td_runtime_config cfg;
    if (td_config_load_defaults(&cfg) != 0) {
        fprintf(stderr, "failed to load defaults\n");
        return false;
    }
    cfg.keepalive_interval_sec = 42U;
    cfg.keepalive_miss_threshold = 7U;
    cfg.iface_invalid_holdoff_sec = 99U;
    cfg.max_terminals = 123U;

    struct terminal_discovery_init_params params;
    memset(&params, 0, sizeof(params));
    params.runtime_config = &cfg;

    int rc = terminal_discovery_initialize(&params);
    if (rc != 0) {
        fprintf(stderr, "expected success, got %d\n", rc);
        return false;
    }
    if (g_stub.init_calls != 1U || g_stub.start_calls != 1U || g_stub.register_calls != 1U) {
        fprintf(stderr, "unexpected adapter call counts\n");
        return false;
    }
    if (g_stub.northbound_attach_calls != 1U) {
        fprintf(stderr, "default sink should be attached once\n");
        return false;
    }
    if (g_stub.set_sink_calls != 1U) {
        fprintf(stderr, "default sink should be installed once\n");
        return false;
    }
    if (g_stub.last_nonnull_callback != stub_default_sink || g_stub.registered_callback != stub_default_sink) {
        fprintf(stderr, "default sink registration mismatch\n");
        return false;
    }
    if (g_stub.registered_ctx != NULL) {
        fprintf(stderr, "default sink should not receive context\n");
        return false;
    }
    if (g_stub.manager_cfg.keepalive_interval_sec != cfg.keepalive_interval_sec ||
        g_stub.manager_cfg.keepalive_miss_threshold != cfg.keepalive_miss_threshold ||
        g_stub.manager_cfg.iface_invalid_holdoff_sec != cfg.iface_invalid_holdoff_sec ||
        g_stub.manager_cfg.max_terminals != cfg.max_terminals) {
        fprintf(stderr, "manager config does not reflect runtime overrides\n");
        return false;
    }
    if (g_stub.last_attached_manager != &g_stub_manager_handle) {
        fprintf(stderr, "default sink should attach manager handle\n");
        return false;
    }
    if (terminal_discovery_get_manager() != &g_stub_manager_handle) {
        fprintf(stderr, "manager accessor did not return expected handle\n");
        return false;
    }
    if (!terminal_discovery_get_app_context()) {
        fprintf(stderr, "app_context accessor returned NULL after init\n");
        return false;
    }

    rc = terminal_discovery_initialize(&params);
    if (rc != -EALREADY) {
        fprintf(stderr, "expected -EALREADY on repeat, got %d\n", rc);
        return false;
    }
    if (g_stub.init_calls != 1U) {
        fprintf(stderr, "repeat call should not reinitialize adapter\n");
        return false;
    }

    return true;
}

int main(void) {
    struct {
        const char *name;
        bool (*fn)(void);
    } tests[] = {
        {"accessors_before_init", test_accessors_before_init},
        {"adapter_init_failure", test_initialize_adapter_init_failure},
        {"manager_create_failure", test_initialize_manager_create_failure},
        {"netlink_failure_cleanup", test_initialize_netlink_failure_triggers_cleanup},
        {"northbound_helper_failure", test_initialize_northbound_helper_failure},
        {"event_sink_failure", test_initialize_event_sink_failure_stops_listener},
        {"register_failure", test_initialize_register_failure_cleans_up},
        {"success_then_repeat", test_initialize_success_then_repeat_guard},
    };

    size_t total = sizeof(tests) / sizeof(tests[0]);
    size_t failures = 0;

    for (size_t i = 0; i < total; ++i) {
        bool ok = tests[i].fn ? tests[i].fn() : false;
        if (ok) {
            printf("[PASS] %s\n", tests[i].name);
        } else {
            printf("[FAIL] %s\n", tests[i].name);
            failures += 1;
        }
    }

    if (failures > 0) {
        printf("%zu/%zu tests failed\n", failures, total);
        return EXIT_FAILURE;
    }

    printf("all %zu tests passed\n", total);
    return EXIT_SUCCESS;
}

