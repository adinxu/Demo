#define _DEFAULT_SOURCE

#include "terminal_manager.h"
#include "td_logging.h"

#include <arpa/inet.h>
#include <errno.h>
#include <ctype.h>
#include <netinet/if_ether.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

struct td_adapter {
    int placeholder;
};

#define MAX_CAPTURED_EVENTS 16

struct event_capture {
    terminal_event_record_t records[MAX_CAPTURED_EVENTS];
    size_t count;
    bool overflow;
};

struct probe_capture {
    terminal_probe_request_t last_request;
    size_t count;
};

static struct td_adapter g_stub_adapter;

struct mock_mac_locator_state {
    td_adapter_result_t lookup_rc;
    td_adapter_result_t lookup_by_vid_rc;
    uint32_t lookup_ifindex;
    uint32_t lookup_by_vid_ifindex;
    uint64_t version;
    size_t lookup_calls;
    size_t lookup_by_vid_calls;
    uint16_t last_lookup_vlan;
    uint16_t last_lookup_by_vid_vlan;
    uint8_t last_lookup_mac[ETH_ALEN];
    uint8_t last_lookup_by_vid_mac[ETH_ALEN];
    bool subscribed;
    td_adapter_mac_locator_refresh_cb refresh_cb;
    void *refresh_ctx;
};

static struct mock_mac_locator_state g_mock_locator;

static void mock_locator_reset(void) {
    memset(&g_mock_locator, 0, sizeof(g_mock_locator));
    g_mock_locator.lookup_rc = TD_ADAPTER_ERR_NOT_READY;
    g_mock_locator.lookup_by_vid_rc = TD_ADAPTER_ERR_NOT_FOUND;
    g_mock_locator.version = 1;
}

static void mock_locator_set_lookup(td_adapter_result_t rc, uint32_t ifindex) {
    g_mock_locator.lookup_rc = rc;
    g_mock_locator.lookup_ifindex = ifindex;
}

static void mock_locator_set_lookup_by_vid(td_adapter_result_t rc, uint32_t ifindex) {
    g_mock_locator.lookup_by_vid_rc = rc;
    g_mock_locator.lookup_by_vid_ifindex = ifindex;
}

static void mock_locator_clear_counters(void) {
    g_mock_locator.lookup_calls = 0;
    g_mock_locator.lookup_by_vid_calls = 0;
}

static td_adapter_result_t mock_locator_lookup(td_adapter_t *handle,
                                               const uint8_t mac[ETH_ALEN],
                                               uint16_t vlan_id,
                                               uint32_t *ifindex_out,
                                               uint64_t *version_out) {
    (void)handle;
    g_mock_locator.lookup_calls += 1;
    g_mock_locator.last_lookup_vlan = vlan_id;
    if (mac) {
        memcpy(g_mock_locator.last_lookup_mac, mac, ETH_ALEN);
    }
    if (ifindex_out) {
        *ifindex_out = g_mock_locator.lookup_ifindex;
    }
    if (version_out) {
        *version_out = g_mock_locator.version;
    }
    return g_mock_locator.lookup_rc;
}

static td_adapter_result_t mock_locator_lookup_by_vid(td_adapter_t *handle,
                                                      const uint8_t mac[ETH_ALEN],
                                                      uint16_t vlan_id,
                                                      uint32_t *ifindex_out) {
    (void)handle;
    g_mock_locator.lookup_by_vid_calls += 1;
    g_mock_locator.last_lookup_by_vid_vlan = vlan_id;
    if (mac) {
        memcpy(g_mock_locator.last_lookup_by_vid_mac, mac, ETH_ALEN);
    }
    if (ifindex_out) {
        *ifindex_out = g_mock_locator.lookup_by_vid_ifindex;
    }
    return g_mock_locator.lookup_by_vid_rc;
}

static td_adapter_result_t mock_locator_subscribe(td_adapter_t *handle,
                                                  td_adapter_mac_locator_refresh_cb cb,
                                                  void *ctx) {
    (void)handle;
    g_mock_locator.subscribed = true;
    g_mock_locator.refresh_cb = cb;
    g_mock_locator.refresh_ctx = ctx;
    return TD_ADAPTER_OK;
}

static td_adapter_result_t mock_locator_get_version(td_adapter_t *handle,
                                                    uint64_t *version_out) {
    (void)handle;
    if (!version_out) {
        return TD_ADAPTER_ERR_INVALID_ARG;
    }
    *version_out = g_mock_locator.version;
    return TD_ADAPTER_OK;
}

static const struct td_adapter_mac_locator_ops g_mock_mac_locator_ops = {
    .lookup = mock_locator_lookup,
    .lookup_by_vid = mock_locator_lookup_by_vid,
    .subscribe = mock_locator_subscribe,
    .get_version = mock_locator_get_version,
};

static const struct td_adapter_ops g_mock_adapter_ops = {
    .mac_locator_ops = &g_mock_mac_locator_ops,
};

static int mock_kernel_ifindex_for_vlan(int vlan_id) {
    return 1000 + vlan_id;
}

unsigned int if_nametoindex(const char *name) {
    if (!name) {
        return 0U;
    }
    if (strncmp(name, "vlan", 4) == 0) {
        char *endptr = NULL;
        long vlan = strtol(name + 4, &endptr, 10);
        if (endptr && *endptr == '\0' && vlan > 0 && vlan < 4096) {
            return (unsigned int)mock_kernel_ifindex_for_vlan((int)vlan);
        }
    }
    return 0U;
}

static void capture_reset(struct event_capture *capture) {
    if (!capture) {
        return;
    }
    memset(capture, 0, sizeof(*capture));
}

static void probe_reset(struct probe_capture *capture) {
    if (!capture) {
        return;
    }
    memset(capture, 0, sizeof(*capture));
}

struct sync_handler_state {
    pthread_mutex_t lock;
    size_t call_count;
    size_t success_after;
};

static void sync_handler_state_init(struct sync_handler_state *state, size_t success_after) {
    if (!state) {
        return;
    }
    pthread_mutex_init(&state->lock, NULL);
    state->call_count = 0U;
    state->success_after = success_after;
}

static void sync_handler_state_destroy(struct sync_handler_state *state) {
    if (!state) {
        return;
    }
    pthread_mutex_destroy(&state->lock);
}

static size_t sync_handler_state_get(struct sync_handler_state *state) {
    if (!state) {
        return 0U;
    }
    pthread_mutex_lock(&state->lock);
    size_t current = state->call_count;
    pthread_mutex_unlock(&state->lock);
    return current;
}

static void sync_handler_state_set_success_after(struct sync_handler_state *state, size_t value) {
    if (!state) {
        return;
    }
    pthread_mutex_lock(&state->lock);
    state->success_after = value;
    pthread_mutex_unlock(&state->lock);
}

static int sync_test_handler(void *ctx) {
    struct sync_handler_state *state = (struct sync_handler_state *)ctx;
    if (!state) {
        return -EINVAL;
    }
    pthread_mutex_lock(&state->lock);
    state->call_count += 1U;
    size_t current = state->call_count;
    size_t threshold = state->success_after;
    pthread_mutex_unlock(&state->lock);
    return current >= threshold ? 0 : -EAGAIN;
}

static bool wait_for_call_count(struct sync_handler_state *state,
                                size_t expected,
                                unsigned int timeout_ms) {
    const unsigned int step_ms = 10U;
    unsigned int waited = 0U;
    while (waited < timeout_ms) {
        if (sync_handler_state_get(state) >= expected) {
            return true;
        }
        usleep((useconds_t)step_ms * 1000U);
        waited += step_ms;
    }
    return sync_handler_state_get(state) >= expected;
}

struct debug_capture {
    char *data;
    size_t size;
    size_t capacity;
    bool had_error;
};

static void debug_capture_init(struct debug_capture *capture) {
    if (!capture) {
        return;
    }
    capture->data = NULL;
    capture->size = 0;
    capture->capacity = 0;
    capture->had_error = false;
}

static void debug_capture_reset(struct debug_capture *capture) {
    if (!capture) {
        return;
    }
    capture->size = 0;
    if (capture->data) {
        capture->data[0] = '\0';
    }
    capture->had_error = false;
}

static void debug_capture_free(struct debug_capture *capture) {
    if (!capture) {
        return;
    }
    free(capture->data);
    capture->data = NULL;
    capture->size = 0;
    capture->capacity = 0;
    capture->had_error = false;
}

static void debug_capture_writer(void *ctx, const char *line) {
    struct debug_capture *capture = (struct debug_capture *)ctx;
    if (!capture || !line) {
        if (capture) {
            capture->had_error = true;
        }
        return;
    }
    size_t len = strlen(line);
    size_t needed = capture->size + len + 1;
    if (needed > capture->capacity) {
        size_t new_capacity = capture->capacity ? capture->capacity * 2U : 256U;
        while (new_capacity < needed) {
            new_capacity *= 2U;
        }
        char *tmp = (char *)realloc(capture->data, new_capacity);
        if (!tmp) {
            capture->had_error = true;
            return;
        }
        capture->data = tmp;
        capture->capacity = new_capacity;
    }
    memcpy(capture->data + capture->size, line, len);
    capture->size += len;
    capture->data[capture->size] = '\0';
}

static void capture_callback(const terminal_event_record_t *records,
                             size_t count,
                             void *user_ctx) {
    struct event_capture *capture = (struct event_capture *)user_ctx;
    if (!capture || !records) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        if (capture->count < MAX_CAPTURED_EVENTS) {
            capture->records[capture->count++] = records[i];
        } else {
            capture->overflow = true;
        }
    }
}

static void probe_callback(const terminal_probe_request_t *request, void *user_ctx) {
    if (!request) {
        return;
    }
    struct probe_capture *capture = (struct probe_capture *)user_ctx;
    if (!capture) {
        return;
    }
    capture->last_request = *request;
    capture->count += 1;
}

static void sleep_ms(unsigned int ms) {
    struct timespec req;
    req.tv_sec = ms / 1000U;
    req.tv_nsec = (long)(ms % 1000U) * 1000000L;
    nanosleep(&req, NULL);
}

static void apply_address_update(struct terminal_manager *mgr,
                                 int kernel_ifindex,
                                 const char *address,
                                 uint8_t prefix_len,
                                 bool is_add) {
    terminal_address_update_t update;
    memset(&update, 0, sizeof(update));
    update.kernel_ifindex = kernel_ifindex;
    update.prefix_len = prefix_len;
    update.is_add = is_add;
    if (address) {
        inet_aton(address, &update.address);
    }
    terminal_manager_on_address_update(mgr, &update);
}

static void build_arp_packet(struct td_adapter_packet_view *packet,
                             struct ether_arp *arp,
                             const uint8_t mac[ETH_ALEN],
                             const char *sender_ip_text,
                             const char *target_ip_text,
                             int vlan_id,
                             uint32_t ifindex) {
    memset(arp, 0, sizeof(*arp));
    arp->ea_hdr.ar_hrd = htons(ARPHRD_ETHER);
    arp->ea_hdr.ar_pro = htons(ETHERTYPE_IP);
    arp->ea_hdr.ar_hln = ETH_ALEN;
    arp->ea_hdr.ar_pln = 4;
    arp->ea_hdr.ar_op = htons(ARPOP_REQUEST);
    memcpy(arp->arp_sha, mac, ETH_ALEN);
    uint32_t spa = sender_ip_text ? inet_addr(sender_ip_text) : 0U;
    memcpy(arp->arp_spa, &spa, sizeof(spa));

    uint32_t tpa = target_ip_text ? inet_addr(target_ip_text) : 0U;
    memcpy(arp->arp_tpa, &tpa, sizeof(tpa));

    memset(packet, 0, sizeof(*packet));
    packet->payload = (const uint8_t *)arp;
    packet->payload_len = sizeof(*arp);
    packet->ether_type = ETHERTYPE_ARP;
    packet->vlan_id = vlan_id;
    packet->ifindex = ifindex;
    memcpy(packet->src_mac, mac, ETH_ALEN);
}

static bool test_default_log_timestamp(void) {
    bool ok = true;
    const char *error_reason = NULL;

    void *prev_ctx = NULL;
    td_log_sink_fn prev_sink = td_log_get_sink(&prev_ctx);
    td_log_set_sink(NULL, NULL);

    td_log_level_t prev_level = td_log_get_level();
    td_log_set_level(TD_LOG_TRACE);

    int pipefd[2] = {-1, -1};

    int stderr_fd = dup(fileno(stderr));
    if (stderr_fd < 0) {
        error_reason = "dup stderr failed";
        ok = false;
        goto cleanup;
    }

    if (pipe(pipefd) != 0) {
        error_reason = "pipe creation failed";
        ok = false;
        goto cleanup;
    }

    if (fflush(stderr) != 0) {
        error_reason = "stderr flush failed";
        ok = false;
        goto cleanup_with_pipe;
    }

    if (dup2(pipefd[1], fileno(stderr)) < 0) {
        error_reason = "dup2 redirect failed";
        ok = false;
        goto cleanup_with_pipe;
    }

    close(pipefd[1]);
    pipefd[1] = -1;

    td_log_writef(TD_LOG_INFO, "logtest", "hello world");
    fflush(stderr);

    char buffer[256];
    ssize_t bytes = read(pipefd[0], buffer, sizeof(buffer) - 1);
    if (bytes <= 0) {
        error_reason = "failed to capture log output";
        ok = false;
        goto cleanup_with_pipe;
    }

    buffer[bytes] = '\0';
    close(pipefd[0]);
    pipefd[0] = -1;

    {
        char *newline = strchr(buffer, '\n');
        if (newline) {
            *newline = '\0';
        }

        size_t len = strlen(buffer);
        if (len < 21) {
            error_reason = "log line too short";
            ok = false;
            goto cleanup;
        }

        const int digit_positions[] = {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18};
        for (size_t i = 0; i < sizeof(digit_positions) / sizeof(digit_positions[0]); ++i) {
            int pos = digit_positions[i];
            if (!isdigit((unsigned char)buffer[pos])) {
                error_reason = "timestamp digits missing";
                ok = false;
                goto cleanup;
            }
        }

        if (buffer[4] != '-' || buffer[7] != '-' || buffer[10] != ' ' || buffer[13] != ':' ||
            buffer[16] != ':' || buffer[19] != ' ') {
            error_reason = "timestamp separators mismatch";
            ok = false;
            goto cleanup;
        }

        const char *expected_suffix = " [INFO] logtest: hello world";
        if (strcmp(buffer + 19, expected_suffix) != 0) {
            error_reason = "log suffix mismatch";
            ok = false;
            goto cleanup;
        }
    }

    goto cleanup;

cleanup_with_pipe:
    if (pipefd[0] >= 0) {
        close(pipefd[0]);
        pipefd[0] = -1;
    }
    if (pipefd[1] >= 0) {
        close(pipefd[1]);
        pipefd[1] = -1;
    }

cleanup:
    if (stderr_fd >= 0) {
        dup2(stderr_fd, fileno(stderr));
        close(stderr_fd);
    }

    td_log_set_level(prev_level);
    td_log_set_sink(prev_sink, prev_ctx);

    if (!ok && error_reason) {
        fprintf(stderr, "%s\n", error_reason);
    }

    return ok;
}

struct query_counter {
    terminal_event_record_t last_record;
    size_t count;
};

static bool query_counter_callback(const terminal_event_record_t *record, void *user_ctx) {
    struct query_counter *counter = (struct query_counter *)user_ctx;
    if (!counter || !record) {
        return false;
    }
    counter->last_record = *record;
    counter->count += 1;
    return true;
}

static bool test_terminal_add_and_event(void) {
    const int vlan_id = 100;
    const int tx_kernel_ifindex = mock_kernel_ifindex_for_vlan(vlan_id);
    struct terminal_manager_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.keepalive_interval_sec = 5;
    cfg.keepalive_miss_threshold = 3;
    cfg.iface_invalid_holdoff_sec = 30;
    cfg.scan_interval_ms = 60000;
    cfg.vlan_iface_format = "vlan%u";
    cfg.max_terminals = 16;

    struct event_capture events;
    capture_reset(&events);
    struct probe_capture probes;
    probe_reset(&probes);

    struct terminal_manager *mgr = terminal_manager_create(&cfg,
                                                            &g_stub_adapter,
                                                            NULL,
                                                            probe_callback,
                                                            &probes);
    if (!mgr) {
        fprintf(stderr, "failed to create terminal manager\n");
        return false;
    }

    terminal_manager_set_event_sink(mgr, capture_callback, &events);

    apply_address_update(mgr, tx_kernel_ifindex, "192.0.2.1", 24, true);

    struct ether_arp arp;
    struct td_adapter_packet_view packet;
    const uint8_t mac[ETH_ALEN] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    build_arp_packet(&packet, &arp, mac, "192.0.2.10", "192.0.2.10", vlan_id, 7);

    terminal_manager_on_packet(mgr, &packet);
    terminal_manager_flush_events(mgr);

    bool ok = true;

    if (events.count != 1 || events.records[0].tag != TERMINAL_EVENT_TAG_ADD) {
        fprintf(stderr, "expected one ADD event, got %zu\n", events.count);
        ok = false;
        goto done;
    }

    if (events.records[0].ifindex != 7U) {
        fprintf(stderr, "unexpected ifindex value %u\n", events.records[0].ifindex);
        ok = false;
        goto done;
    }

    if (events.records[0].prev_ifindex != 0U) {
        fprintf(stderr, "expected prev_ifindex 0 for ADD event, got %u\n",
                events.records[0].prev_ifindex);
        ok = false;
        goto done;
    }

    struct query_counter counter = {0};
    if (terminal_manager_query_all(mgr, query_counter_callback, &counter) != 0) {
        fprintf(stderr, "query_all failed\n");
        ok = false;
        goto done;
    }
    if (counter.count != 1) {
        fprintf(stderr, "expected 1 terminal in query, got %zu\n", counter.count);
        ok = false;
        goto done;
    }

    struct terminal_manager_stats stats;
    memset(&stats, 0, sizeof(stats));
    terminal_manager_get_stats(mgr, &stats);
    if (stats.terminals_discovered != 1 || stats.events_dispatched == 0) {
        fprintf(stderr, "unexpected stats: discovered=%" PRIu64 ", dispatched=%" PRIu64 "\n",
                stats.terminals_discovered,
                stats.events_dispatched);
        ok = false;
        goto done;
    }

done:
    terminal_manager_destroy(mgr);
    return ok;
}

static bool test_point_lookup_success_sets_ifindex(void) {
    const int vlan_id = 120;
    struct terminal_manager_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.keepalive_interval_sec = 5;
    cfg.keepalive_miss_threshold = 3;
    cfg.iface_invalid_holdoff_sec = 30;
    cfg.scan_interval_ms = 60000;
    cfg.vlan_iface_format = "vlan%u";
    cfg.max_terminals = 8;

    struct event_capture events;
    capture_reset(&events);

    mock_locator_reset();
    g_mock_locator.version = 42;
    mock_locator_set_lookup_by_vid(TD_ADAPTER_OK, 55);
    mock_locator_set_lookup(TD_ADAPTER_ERR_NOT_READY, 0);

    struct terminal_manager *mgr = terminal_manager_create(&cfg,
                                                            &g_stub_adapter,
                                                            &g_mock_adapter_ops,
                                                            NULL,
                                                            NULL);
    if (!mgr) {
        fprintf(stderr, "failed to create terminal manager for point lookup success test\n");
        return false;
    }

    terminal_manager_set_event_sink(mgr, capture_callback, &events);

    struct ether_arp arp;
    struct td_adapter_packet_view packet;
    const uint8_t mac[ETH_ALEN] = {0x00, 0x15, 0x16, 0x17, 0x18, 0x19};
    build_arp_packet(&packet, &arp, mac, "203.0.113.70", "203.0.113.70", vlan_id, 0);

    terminal_manager_on_packet(mgr, &packet);
    terminal_manager_flush_events(mgr);

    bool ok = true;

    if (g_mock_locator.lookup_by_vid_calls != 1) {
        fprintf(stderr, "expected one lookup_by_vid call, got %zu\n", g_mock_locator.lookup_by_vid_calls);
        ok = false;
    }

    if (g_mock_locator.lookup_calls != 0) {
        fprintf(stderr, "unexpected full lookup count %zu\n", g_mock_locator.lookup_calls);
        ok = false;
    }

    if (events.count != 1 || events.records[0].tag != TERMINAL_EVENT_TAG_ADD) {
        fprintf(stderr, "expected single ADD event after point lookup success, got %zu\n", events.count);
        ok = false;
    } else if (events.records[0].ifindex != 55U) {
        fprintf(stderr, "expected ADD event ifindex 55, got %u\n", events.records[0].ifindex);
        ok = false;
    }

    struct query_counter counter = {0};
    if (terminal_manager_query_all(mgr, query_counter_callback, &counter) != 0) {
        fprintf(stderr, "query_all failed in point lookup success test\n");
        ok = false;
    } else if (counter.count != 1 || counter.last_record.ifindex != 55U) {
        fprintf(stderr, "expected query_all to report ifindex 55, count=%zu ifindex=%u\n",
                counter.count,
                counter.last_record.ifindex);
        ok = false;
    }

    terminal_manager_destroy(mgr);
    return ok;
}

static bool test_point_lookup_miss_triggers_refresh(void) {
    const int vlan_id = 130;
    struct terminal_manager_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.keepalive_interval_sec = 5;
    cfg.keepalive_miss_threshold = 3;
    cfg.iface_invalid_holdoff_sec = 30;
    cfg.scan_interval_ms = 60000;
    cfg.vlan_iface_format = "vlan%u";
    cfg.max_terminals = 8;

    struct event_capture events;
    capture_reset(&events);

    mock_locator_reset();
    g_mock_locator.version = 64;
    mock_locator_set_lookup_by_vid(TD_ADAPTER_ERR_NOT_FOUND, 0);
    mock_locator_set_lookup(TD_ADAPTER_OK, 77);

    struct terminal_manager *mgr = terminal_manager_create(&cfg,
                                                            &g_stub_adapter,
                                                            &g_mock_adapter_ops,
                                                            NULL,
                                                            NULL);
    if (!mgr) {
        fprintf(stderr, "failed to create terminal manager for point lookup miss test\n");
        return false;
    }

    terminal_manager_set_event_sink(mgr, capture_callback, &events);

    struct ether_arp arp;
    struct td_adapter_packet_view packet;
    const uint8_t mac[ETH_ALEN] = {0x00, 0x25, 0x26, 0x27, 0x28, 0x29};
    build_arp_packet(&packet, &arp, mac, "198.51.100.80", "198.51.100.80", vlan_id, 0);

    terminal_manager_on_packet(mgr, &packet);
    terminal_manager_flush_events(mgr);

    bool ok = true;

    if (g_mock_locator.lookup_by_vid_calls != 1) {
        fprintf(stderr, "expected one lookup_by_vid call on miss path, got %zu\n",
                g_mock_locator.lookup_by_vid_calls);
        ok = false;
    }

    if (g_mock_locator.lookup_calls != 1) {
        fprintf(stderr, "expected one fallback lookup call, got %zu\n", g_mock_locator.lookup_calls);
        ok = false;
    }

    if (events.count != 2) {
        fprintf(stderr, "expected two events (ADD+MOD) after fallback, got %zu\n", events.count);
        ok = false;
    } else {
        if (events.records[0].tag != TERMINAL_EVENT_TAG_ADD) {
            fprintf(stderr, "expected first event ADD, got %d\n", events.records[0].tag);
            ok = false;
        }
        if (events.records[1].tag != TERMINAL_EVENT_TAG_MOD || events.records[1].ifindex != 77U) {
            fprintf(stderr, "expected MOD event with ifindex 77, got tag=%d ifindex=%u\n",
                    events.records[1].tag,
                    events.records[1].ifindex);
            ok = false;
        }
        if (events.records[1].prev_ifindex != 0U) {
            fprintf(stderr, "expected MOD prev_ifindex 0, got %u\n", events.records[1].prev_ifindex);
            ok = false;
        }
    }

    struct query_counter counter = {0};
    if (terminal_manager_query_all(mgr, query_counter_callback, &counter) != 0) {
        fprintf(stderr, "query_all failed in point lookup miss test\n");
        ok = false;
    } else if (counter.count != 1 || counter.last_record.ifindex != 77U) {
        fprintf(stderr, "expected query_all to report resolved ifindex 77, count=%zu ifindex=%u\n",
                counter.count,
                counter.last_record.ifindex);
        ok = false;
    }

    terminal_manager_destroy(mgr);
    return ok;
}

static bool test_point_lookup_retries_on_vlan_change(void) {
    const int vlan_initial = 140;
    const int vlan_migrated = 141;
    struct terminal_manager_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.keepalive_interval_sec = 5;
    cfg.keepalive_miss_threshold = 3;
    cfg.iface_invalid_holdoff_sec = 30;
    cfg.scan_interval_ms = 60000;
    cfg.vlan_iface_format = "vlan%u";
    cfg.max_terminals = 8;

    struct event_capture events;
    capture_reset(&events);

    mock_locator_reset();
    g_mock_locator.version = 90;
    mock_locator_set_lookup_by_vid(TD_ADAPTER_OK, 88);
    mock_locator_set_lookup(TD_ADAPTER_ERR_NOT_READY, 0);

    struct terminal_manager *mgr = terminal_manager_create(&cfg,
                                                            &g_stub_adapter,
                                                            &g_mock_adapter_ops,
                                                            NULL,
                                                            NULL);
    if (!mgr) {
        fprintf(stderr, "failed to create terminal manager for vlan retry test\n");
        return false;
    }

    terminal_manager_set_event_sink(mgr, capture_callback, &events);

    struct ether_arp arp;
    struct td_adapter_packet_view packet;
    const uint8_t mac[ETH_ALEN] = {0x00, 0x35, 0x36, 0x37, 0x38, 0x39};
    build_arp_packet(&packet, &arp, mac, "192.0.2.90", "192.0.2.90", vlan_initial, 0);

    terminal_manager_on_packet(mgr, &packet);
    terminal_manager_flush_events(mgr);

    bool ok = true;

    if (events.count != 1 || events.records[0].tag != TERMINAL_EVENT_TAG_ADD ||
        events.records[0].ifindex != 88U) {
        int first_tag = events.count ? (int)events.records[0].tag : -1;
        unsigned int first_ifindex = events.count ? events.records[0].ifindex : 0U;
        fprintf(stderr,
                "expected ADD event with resolved ifindex 88, got count=%zu tag=%d ifindex=%u\n",
                events.count,
                first_tag,
                first_ifindex);
        ok = false;
    }

    capture_reset(&events);
    mock_locator_clear_counters();
    mock_locator_set_lookup_by_vid(TD_ADAPTER_OK, 99);

    build_arp_packet(&packet, &arp, mac, "192.0.2.90", "192.0.2.90", vlan_migrated, 0);
    terminal_manager_on_packet(mgr, &packet);
    terminal_manager_flush_events(mgr);

    if (g_mock_locator.lookup_by_vid_calls != 1) {
        fprintf(stderr, "expected lookup_by_vid call after vlan change, got %zu\n",
                g_mock_locator.lookup_by_vid_calls);
        ok = false;
    }

    if (events.count != 1 || events.records[0].tag != TERMINAL_EVENT_TAG_MOD ||
        events.records[0].ifindex != 99U || events.records[0].prev_ifindex != 88U) {
        int mod_tag = events.count ? (int)events.records[0].tag : -1;
        unsigned int mod_ifindex = events.count ? events.records[0].ifindex : 0U;
        unsigned int mod_prev_ifindex = events.count ? events.records[0].prev_ifindex : 0U;
        fprintf(stderr,
                "expected MOD event with new ifindex 99 prev 88, got count=%zu tag=%d ifindex=%u prev=%u\n",
                events.count,
                mod_tag,
                mod_ifindex,
                mod_prev_ifindex);
        ok = false;
    }

    struct query_counter counter = {0};
    if (terminal_manager_query_all(mgr, query_counter_callback, &counter) != 0) {
        fprintf(stderr, "query_all failed in vlan retry test\n");
        ok = false;
    } else if (counter.count != 1 || counter.last_record.ifindex != 99U) {
        fprintf(stderr, "expected query_all to report latest ifindex 99, count=%zu ifindex=%u\n",
                counter.count,
                counter.last_record.ifindex);
        ok = false;
    }

    terminal_manager_destroy(mgr);
    return ok;
}

static bool test_terminal_packet_on_ignored_vlan(void) {
    const int vlan_id = 200;
    const int tx_kernel_ifindex = mock_kernel_ifindex_for_vlan(vlan_id);
    struct terminal_manager_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.keepalive_interval_sec = 5;
    cfg.keepalive_miss_threshold = 3;
    cfg.iface_invalid_holdoff_sec = 30;
    cfg.scan_interval_ms = 60000;
    cfg.vlan_iface_format = "vlan%u";
    cfg.max_terminals = 16;
    cfg.ignored_vlan_count = 1;
    cfg.ignored_vlans[0] = (uint16_t)vlan_id;

    struct event_capture events;
    capture_reset(&events);

    struct terminal_manager *mgr = terminal_manager_create(&cfg,
                                                            &g_stub_adapter,
                                                            NULL,
                                                            probe_callback,
                                                            NULL);
    if (!mgr) {
        fprintf(stderr, "failed to create terminal manager with ignored vlan\n");
        return false;
    }

    terminal_manager_set_event_sink(mgr, capture_callback, &events);

    apply_address_update(mgr, tx_kernel_ifindex, "198.51.100.1", 24, true);

    struct ether_arp arp;
    struct td_adapter_packet_view packet;
    const uint8_t mac[ETH_ALEN] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    build_arp_packet(&packet, &arp, mac, "198.51.100.10", "198.51.100.10", vlan_id, 9);

    terminal_manager_on_packet(mgr, &packet);
    terminal_manager_flush_events(mgr);

    bool ok = true;

    if (events.count != 0) {
        fprintf(stderr, "expected no events for ignored vlan, got %zu\n", events.count);
        ok = false;
    }

    struct query_counter counter = {0};
    if (terminal_manager_query_all(mgr, query_counter_callback, &counter) != 0) {
        fprintf(stderr, "query_all failed\n");
        ok = false;
    } else if (counter.count != 0) {
        fprintf(stderr, "expected zero terminals for ignored vlan, got %zu\n", counter.count);
        ok = false;
    }

    struct terminal_manager_stats stats;
    memset(&stats, 0, sizeof(stats));
    terminal_manager_get_stats(mgr, &stats);
    if (stats.terminals_discovered != 0 || stats.events_dispatched != 0) {
        fprintf(stderr,
                "unexpected stats for ignored vlan: discovered=%" PRIu64 " dispatched=%" PRIu64 "\n",
                stats.terminals_discovered,
                stats.events_dispatched);
        ok = false;
    }

    terminal_manager_destroy(mgr);
    return ok;
}

static bool test_terminal_ignored_vlan_runtime_update(void) {
    const int vlan_id = 250;
    const int tx_kernel_ifindex = mock_kernel_ifindex_for_vlan(vlan_id);
    struct terminal_manager_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.keepalive_interval_sec = 5;
    cfg.keepalive_miss_threshold = 3;
    cfg.iface_invalid_holdoff_sec = 30;
    cfg.scan_interval_ms = 60000;
    cfg.vlan_iface_format = "vlan%u";
    cfg.max_terminals = 16;

    struct event_capture events;
    capture_reset(&events);

    struct terminal_manager *mgr = terminal_manager_create(&cfg,
                                                            &g_stub_adapter,
                                                            NULL,
                                                            probe_callback,
                                                            NULL);
    if (!mgr) {
        fprintf(stderr, "failed to create terminal manager for runtime ignore vlan test\n");
        return false;
    }

    terminal_manager_set_event_sink(mgr, capture_callback, &events);

    apply_address_update(mgr, tx_kernel_ifindex, "198.51.100.1", 24, true);

    if (terminal_manager_add_ignored_vlan(mgr, (uint16_t)vlan_id) != 0) {
        fprintf(stderr, "failed to add ignored vlan at runtime\n");
        terminal_manager_destroy(mgr);
        return false;
    }

    struct ether_arp arp;
    struct td_adapter_packet_view packet;
    const uint8_t mac[ETH_ALEN] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    build_arp_packet(&packet, &arp, mac, "198.51.100.20", "198.51.100.20", vlan_id, 9);

    terminal_manager_on_packet(mgr, &packet);
    terminal_manager_flush_events(mgr);

    bool ok = true;

    if (events.count != 0) {
        fprintf(stderr, "expected no events while vlan ignored, got %zu\n", events.count);
        ok = false;
    }

    struct query_counter counter = {0};
    if (terminal_manager_query_all(mgr, query_counter_callback, &counter) != 0) {
        fprintf(stderr, "query_all failed while vlan ignored\n");
        ok = false;
    } else if (counter.count != 0) {
        fprintf(stderr, "expected zero terminals while vlan ignored, got %zu\n", counter.count);
        ok = false;
    }

    struct terminal_manager_stats stats;
    memset(&stats, 0, sizeof(stats));
    terminal_manager_get_stats(mgr, &stats);
    if (stats.terminals_discovered != 0 || stats.events_dispatched != 0) {
        fprintf(stderr,
                "unexpected stats while vlan ignored: discovered=%" PRIu64 " dispatched=%" PRIu64 "\n",
                stats.terminals_discovered,
                stats.events_dispatched);
        ok = false;
    }

    if (terminal_manager_remove_ignored_vlan(mgr, (uint16_t)vlan_id) != 0) {
        fprintf(stderr, "failed to remove ignored vlan at runtime\n");
        ok = false;
        goto done;
    }

    capture_reset(&events);

    terminal_manager_on_packet(mgr, &packet);
    terminal_manager_flush_events(mgr);

    if (events.count != 1 || events.records[0].tag != TERMINAL_EVENT_TAG_ADD) {
        fprintf(stderr, "expected one ADD event after removing ignore, got %zu\n", events.count);
        ok = false;
        goto done;
    }

    struct query_counter counter_after = {0};
    if (terminal_manager_query_all(mgr, query_counter_callback, &counter_after) != 0) {
        fprintf(stderr, "query_all failed after removing ignore\n");
        ok = false;
        goto done;
    }
    if (counter_after.count != 1) {
        fprintf(stderr, "expected one terminal after removing ignore, got %zu\n", counter_after.count);
        ok = false;
        goto done;
    }

    memset(&stats, 0, sizeof(stats));
    terminal_manager_get_stats(mgr, &stats);
    if (stats.terminals_discovered != 1 || stats.current_terminals != 1) {
        fprintf(stderr,
                "unexpected stats after removing ignore: discovered=%" PRIu64 " current=%" PRIu64 "\n",
                stats.terminals_discovered,
                stats.current_terminals);
        ok = false;
    }

    int rc = terminal_manager_add_ignored_vlan(mgr, (uint16_t)(vlan_id + 1));
    if (rc != 0) {
        fprintf(stderr, "failed to add secondary vlan before clear (rc=%d)\n", rc);
        ok = false;
        goto done;
    }
    terminal_manager_clear_ignored_vlans(mgr);
    rc = terminal_manager_remove_ignored_vlan(mgr, (uint16_t)(vlan_id + 1));
    if (rc != -ENOENT) {
        fprintf(stderr, "expected -ENOENT after clear, got %d\n", rc);
        ok = false;
    }

done:
    terminal_manager_destroy(mgr);
    return ok;
}

static bool test_gratuitous_arp_uses_target_ip(void) {
    const int vlan_id = 150;
    const int tx_kernel_ifindex = mock_kernel_ifindex_for_vlan(vlan_id);
    struct terminal_manager_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.keepalive_interval_sec = 5;
    cfg.keepalive_miss_threshold = 3;
    cfg.iface_invalid_holdoff_sec = 30;
    cfg.scan_interval_ms = 60000;
    cfg.vlan_iface_format = "vlan%u";
    cfg.max_terminals = 16;

    struct event_capture events;
    capture_reset(&events);

    struct terminal_manager *mgr = terminal_manager_create(&cfg,
                                                            &g_stub_adapter,
                                                            NULL,
                                                            NULL,
                                                            NULL);
    if (!mgr) {
        fprintf(stderr, "failed to create terminal manager\n");
        return false;
    }

    terminal_manager_set_event_sink(mgr, capture_callback, &events);

    apply_address_update(mgr, tx_kernel_ifindex, "172.16.1.1", 24, true);

    struct ether_arp arp;
    struct td_adapter_packet_view packet;
    const uint8_t mac[ETH_ALEN] = {0x00, 0x66, 0x77, 0x88, 0x99, 0xaa};
    build_arp_packet(&packet,
                     &arp,
                     mac,
                     "0.0.0.0",
                     "172.16.1.55",
                     vlan_id,
                     9);

    terminal_manager_on_packet(mgr, &packet);
    terminal_manager_flush_events(mgr);

    bool ok = true;

    if (events.count != 1 || events.records[0].tag != TERMINAL_EVENT_TAG_ADD) {
        fprintf(stderr, "expected one ADD event for gratuitous ARP, got %zu\n", events.count);
        ok = false;
        goto done;
    }

    char ip_buf[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &events.records[0].key.ip, ip_buf, sizeof(ip_buf))) {
        fprintf(stderr, "inet_ntop failed\n");
        ok = false;
        goto done;
    }

    if (strcmp(ip_buf, "172.16.1.55") != 0) {
        fprintf(stderr, "expected target IP to be recorded, saw %s\n", ip_buf);
        ok = false;
        goto done;
    }

    struct query_counter counter = {0};
    if (terminal_manager_query_all(mgr, query_counter_callback, &counter) != 0) {
        fprintf(stderr, "query_all failed\n");
        ok = false;
        goto done;
    }
    if (counter.count != 1) {
        fprintf(stderr, "expected one terminal in query after gratuitous ARP\n");
        ok = false;
        goto done;
    }

    char query_ip_buf[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &counter.last_record.key.ip, query_ip_buf, sizeof(query_ip_buf))) {
        fprintf(stderr, "inet_ntop failed for query record\n");
        ok = false;
        goto done;
    }
    if (strcmp(query_ip_buf, "172.16.1.55") != 0) {
        fprintf(stderr, "expected query record to reflect target IP, saw %s\n", query_ip_buf);
        ok = false;
        goto done;
    }

done:
    terminal_manager_destroy(mgr);
    return ok;
}

static bool test_zero_ip_arp_is_ignored(void) {
    const int vlan_id = 155;
    const int tx_kernel_ifindex = mock_kernel_ifindex_for_vlan(vlan_id);
    struct terminal_manager_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.keepalive_interval_sec = 5;
    cfg.keepalive_miss_threshold = 3;
    cfg.iface_invalid_holdoff_sec = 30;
    cfg.scan_interval_ms = 60000;
    cfg.vlan_iface_format = "vlan%u";
    cfg.max_terminals = 16;

    struct event_capture events;
    capture_reset(&events);

    struct terminal_manager *mgr = terminal_manager_create(&cfg,
                                                            &g_stub_adapter,
                                                            NULL,
                                                            NULL,
                                                            NULL);
    if (!mgr) {
        fprintf(stderr, "failed to create terminal manager\n");
        return false;
    }

    terminal_manager_set_event_sink(mgr, capture_callback, &events);

    apply_address_update(mgr, tx_kernel_ifindex, "172.16.2.1", 24, true);

    struct ether_arp arp;
    struct td_adapter_packet_view packet;
    const uint8_t mac[ETH_ALEN] = {0x00, 0x12, 0x34, 0x56, 0x78, 0x9a};
    build_arp_packet(&packet,
                     &arp,
                     mac,
                     "0.0.0.0",
                     "0.0.0.0",
                     vlan_id,
                     4);

    terminal_manager_on_packet(mgr, &packet);
    terminal_manager_flush_events(mgr);

    bool ok = true;

    if (events.count != 0) {
        fprintf(stderr, "expected zero events for all-zero ARP, got %zu\n", events.count);
        ok = false;
        goto done;
    }

    struct query_counter counter = {0};
    if (terminal_manager_query_all(mgr, query_counter_callback, &counter) != 0) {
        fprintf(stderr, "query_all failed\n");
        ok = false;
        goto done;
    }
    if (counter.count != 0) {
        fprintf(stderr, "expected no terminals after all-zero ARP\n");
        ok = false;
        goto done;
    }

done:
    terminal_manager_destroy(mgr);
    return ok;
}

static bool test_probe_failure_removes_terminal(void) {
    const int vlan_id = 200;
    const int tx_kernel_ifindex = mock_kernel_ifindex_for_vlan(vlan_id);
    struct terminal_manager_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.keepalive_interval_sec = 1;
    cfg.keepalive_miss_threshold = 1;
    cfg.iface_invalid_holdoff_sec = 30;
    cfg.scan_interval_ms = 60000;
    cfg.vlan_iface_format = "vlan%u";
    cfg.max_terminals = 16;

    struct event_capture events;
    capture_reset(&events);
    struct probe_capture probes;
    probe_reset(&probes);

    struct terminal_manager *mgr = terminal_manager_create(&cfg,
                                                            &g_stub_adapter,
                                                            NULL,
                                                            probe_callback,
                                                            &probes);
    if (!mgr) {
        fprintf(stderr, "failed to create terminal manager\n");
        return false;
    }

    terminal_manager_set_event_sink(mgr, capture_callback, &events);

    apply_address_update(mgr, tx_kernel_ifindex, "198.51.100.1", 24, true);

    struct ether_arp arp;
    struct td_adapter_packet_view packet;
    const uint8_t mac[ETH_ALEN] = {0x00, 0xaa, 0xbb, 0xcc, 0xdd, 0xee};
    build_arp_packet(&packet, &arp, mac, "198.51.100.42", "198.51.100.42", vlan_id, 11);

    terminal_manager_on_packet(mgr, &packet);
    terminal_manager_flush_events(mgr);
    capture_reset(&events);

    sleep_ms(1100);
    terminal_manager_on_timer(mgr);
    terminal_manager_flush_events(mgr);

    bool ok = true;

    if (probes.count != 1) {
        fprintf(stderr, "expected one probe, got %zu\n", probes.count);
        ok = false;
        goto done;
    }

    if (events.count != 1 || events.records[0].tag != TERMINAL_EVENT_TAG_DEL) {
        fprintf(stderr, "expected one DEL event after probe failure, got %zu\n", events.count);
        ok = false;
        goto done;
    }

    struct terminal_manager_stats stats;
    memset(&stats, 0, sizeof(stats));
    terminal_manager_get_stats(mgr, &stats);
    if (stats.probes_scheduled != 1 || stats.probe_failures != 1 || stats.terminals_removed != 1) {
        fprintf(stderr, "unexpected stats after probe failure: scheduled=%" PRIu64 ", failures=%" PRIu64 ", removed=%" PRIu64 "\n",
                stats.probes_scheduled,
                stats.probe_failures,
                stats.terminals_removed);
        ok = false;
        goto done;
    }

done:
    terminal_manager_destroy(mgr);
    return ok;
}

static bool test_iface_invalid_holdoff(void) {
    const int vlan_id = 300;
    const int tx_kernel_ifindex = mock_kernel_ifindex_for_vlan(vlan_id);
    struct terminal_manager_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.keepalive_interval_sec = 10;
    cfg.keepalive_miss_threshold = 3;
    cfg.iface_invalid_holdoff_sec = 1;
    cfg.scan_interval_ms = 60000;
    cfg.vlan_iface_format = "vlan%u";
    cfg.max_terminals = 16;

    struct event_capture events;
    capture_reset(&events);
    struct probe_capture probes;
    probe_reset(&probes);

    struct terminal_manager *mgr = terminal_manager_create(&cfg,
                                                            &g_stub_adapter,
                                                            NULL,
                                                            probe_callback,
                                                            &probes);
    if (!mgr) {
        fprintf(stderr, "failed to create terminal manager\n");
        return false;
    }

    terminal_manager_set_event_sink(mgr, capture_callback, &events);

    apply_address_update(mgr, tx_kernel_ifindex, "203.0.113.1", 24, true);

    struct ether_arp arp;
    struct td_adapter_packet_view packet;
    const uint8_t mac[ETH_ALEN] = {0x00, 0xde, 0xad, 0xbe, 0xef, 0x01};
    build_arp_packet(&packet, &arp, mac, "203.0.113.9", "203.0.113.9", vlan_id, 3);

    terminal_manager_on_packet(mgr, &packet);
    terminal_manager_flush_events(mgr);
    capture_reset(&events);

    apply_address_update(mgr, tx_kernel_ifindex, "203.0.113.1", 24, false);
    terminal_manager_on_timer(mgr);
    terminal_manager_flush_events(mgr);

    bool ok = true;

    if (events.count != 0) {
        fprintf(stderr, "iface removal should not emit immediate events, got %zu\n", events.count);
        ok = false;
        goto done;
    }

    struct query_counter counter = {0};
    if (terminal_manager_query_all(mgr, query_counter_callback, &counter) != 0) {
        fprintf(stderr, "query_all failed\n");
        ok = false;
        goto done;
    }
    if (counter.count != 1) {
        fprintf(stderr, "expected terminal to be retained during holdoff\n");
        ok = false;
        goto done;
    }

    sleep_ms(1100);
    capture_reset(&events);
    terminal_manager_on_timer(mgr);
    terminal_manager_flush_events(mgr);

    if (events.count != 1 || events.records[0].tag != TERMINAL_EVENT_TAG_DEL) {
        fprintf(stderr, "expected DEL after holdoff expiry, got %zu\n", events.count);
        ok = false;
        goto done;
    }

    struct terminal_manager_stats stats;
    memset(&stats, 0, sizeof(stats));
    terminal_manager_get_stats(mgr, &stats);
    if (stats.terminals_removed == 0) {
        fprintf(stderr, "expected removal counter to increment\n");
        ok = false;
        goto done;
    }

done:
    terminal_manager_destroy(mgr);
    return ok;
}

static bool test_ifindex_change_emits_mod(void) {
    const int vlan_id = 400;
    const int tx_kernel_ifindex = mock_kernel_ifindex_for_vlan(vlan_id);
    struct terminal_manager_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.keepalive_interval_sec = 10;
    cfg.keepalive_miss_threshold = 3;
    cfg.iface_invalid_holdoff_sec = 30;
    cfg.scan_interval_ms = 60000;
    cfg.vlan_iface_format = "vlan%u";
    cfg.max_terminals = 16;

    struct event_capture events;
    capture_reset(&events);
    struct probe_capture probes;
    probe_reset(&probes);

    struct terminal_manager *mgr = terminal_manager_create(&cfg,
                                                            &g_stub_adapter,
                                                            NULL,
                                                            probe_callback,
                                                            &probes);
    if (!mgr) {
        fprintf(stderr, "failed to create terminal manager\n");
        return false;
    }

    terminal_manager_set_event_sink(mgr, capture_callback, &events);

    apply_address_update(mgr, tx_kernel_ifindex, "10.10.10.1", 24, true);

    struct ether_arp arp;
    struct td_adapter_packet_view packet;
    const uint8_t mac[ETH_ALEN] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    build_arp_packet(&packet, &arp, mac, "10.10.10.20", "10.10.10.20", vlan_id, 1);

    terminal_manager_on_packet(mgr, &packet);
    terminal_manager_flush_events(mgr);

    if (events.count != 1 || events.records[0].tag != TERMINAL_EVENT_TAG_ADD) {
        fprintf(stderr, "expected initial ADD event\n");
        terminal_manager_destroy(mgr);
        return false;
    }

    capture_reset(&events);

    build_arp_packet(&packet, &arp, mac, "10.10.10.20", "10.10.10.20", vlan_id, 2);
    terminal_manager_on_packet(mgr, &packet);
    terminal_manager_flush_events(mgr);

    bool ok = true;
    if (events.count != 1 || events.records[0].tag != TERMINAL_EVENT_TAG_MOD) {
        fprintf(stderr, "expected MOD event on ifindex change, got %zu\n", events.count);
        ok = false;
        goto done;
    }
    if (events.records[0].ifindex != 2U) {
        fprintf(stderr, "expected ifindex 2 in MOD event, got %u\n", events.records[0].ifindex);
        ok = false;
        goto done;
    }

    if (events.records[0].prev_ifindex != 1U) {
        fprintf(stderr, "expected prev_ifindex 1 in MOD event, got %u\n",
                events.records[0].prev_ifindex);
        ok = false;
        goto done;
    }

    if (probes.count != 0) {
        fprintf(stderr, "probe callback should not fire during metadata update\n");
        ok = false;
        goto done;
    }

    struct terminal_manager_stats stats;
    memset(&stats, 0, sizeof(stats));
    terminal_manager_get_stats(mgr, &stats);
    if (stats.events_dispatched < 2) {
        fprintf(stderr, "expected events_dispatched >= 2, got %" PRIu64 "\n", stats.events_dispatched);
        ok = false;
        goto done;
    }

done:
    terminal_manager_destroy(mgr);
    return ok;
}

static bool test_address_sync_retry(void) {
    struct terminal_manager_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.keepalive_interval_sec = 10;
    cfg.keepalive_miss_threshold = 3;
    cfg.iface_invalid_holdoff_sec = 30;
    cfg.scan_interval_ms = 10;
    cfg.vlan_iface_format = "vlan%u";
    cfg.max_terminals = 16;

    struct terminal_manager *mgr = terminal_manager_create(&cfg,
                                                            &g_stub_adapter,
                                                            NULL,
                                                            NULL,
                                                            NULL);
    if (!mgr) {
        fprintf(stderr, "failed to create terminal manager for sync retry test\n");
        return false;
    }

    struct sync_handler_state state;
    sync_handler_state_init(&state, 3U);
    terminal_manager_set_address_sync_handler(mgr, sync_test_handler, &state);

    bool ok = true;

    terminal_manager_request_address_sync(mgr);

    if (!wait_for_call_count(&state, 1U, 200U)) {
        fprintf(stderr, "expected first sync attempt within timeout\n");
        ok = false;
        goto done;
    }
    if (!wait_for_call_count(&state, 2U, 400U)) {
        fprintf(stderr, "expected second sync attempt after failure\n");
        ok = false;
        goto done;
    }
    if (!wait_for_call_count(&state, 3U, 600U)) {
        fprintf(stderr, "expected sync to succeed by third attempt\n");
        ok = false;
        goto done;
    }

    usleep(100000);
    if (sync_handler_state_get(&state) != 3U) {
        fprintf(stderr, "unexpected extra sync attempts after success\n");
        ok = false;
        goto done;
    }

    sync_handler_state_set_success_after(&state, 5U);
    terminal_manager_request_address_sync(mgr);

    if (!wait_for_call_count(&state, 4U, 200U)) {
        fprintf(stderr, "expected retry sequence to restart on new request\n");
        ok = false;
        goto done;
    }
    if (!wait_for_call_count(&state, 5U, 400U)) {
        fprintf(stderr, "expected second sequence to succeed\n");
        ok = false;
        goto done;
    }

    usleep(100000);
    if (sync_handler_state_get(&state) != 5U) {
        fprintf(stderr, "unexpected extra attempts after second success\n");
        ok = false;
    }

done:
    terminal_manager_set_address_sync_handler(mgr, NULL, NULL);
    sync_handler_state_destroy(&state);
    terminal_manager_destroy(mgr);
    return ok;
}

static bool test_debug_dump_interfaces(void) {
    const int vlan_id = 310;
    const int pending_vlan_id = vlan_id + 1;
    const int tx_kernel_ifindex = mock_kernel_ifindex_for_vlan(vlan_id);
    struct terminal_manager_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.keepalive_interval_sec = 5;
    cfg.keepalive_miss_threshold = 3;
    cfg.iface_invalid_holdoff_sec = 30;
    cfg.scan_interval_ms = 60000;
    cfg.vlan_iface_format = "vlan%u";
    cfg.max_terminals = 16;

    struct probe_capture probes;
    probe_reset(&probes);

    struct terminal_manager *mgr = terminal_manager_create(&cfg,
                                                            &g_stub_adapter,
                                                            NULL,
                                                            probe_callback,
                                                            &probes);
    if (!mgr) {
        fprintf(stderr, "failed to create terminal manager for debug dump test\n");
        return false;
    }

    apply_address_update(mgr, tx_kernel_ifindex, "203.0.113.1", 24, true);

    struct ether_arp arp;
    struct td_adapter_packet_view packet;
    const uint8_t mac[ETH_ALEN] = {0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0xee};
    build_arp_packet(&packet, &arp, mac, "203.0.113.20", "203.0.113.20", vlan_id, 5);

    terminal_manager_on_packet(mgr, &packet);
    terminal_manager_flush_events(mgr);

    struct ether_arp pending_arp;
    struct td_adapter_packet_view pending_packet;
    const uint8_t pending_mac[ETH_ALEN] = {0x02, 0xcc, 0xdd, 0xee, 0xff, 0x10};
    build_arp_packet(&pending_packet,
                     &pending_arp,
                     pending_mac,
                     "203.0.113.30",
                     "203.0.113.30",
                     pending_vlan_id,
                     6);

    terminal_manager_on_packet(mgr, &pending_packet);
    terminal_manager_flush_events(mgr);

    struct debug_capture capture;
    debug_capture_init(&capture);

    td_debug_dump_opts_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.verbose_metrics = true;

    td_debug_dump_context_t ctx;
    td_debug_context_reset(&ctx, &opts);

    bool ok = true;

    int rc = td_debug_dump_terminal_table(mgr, &opts, debug_capture_writer, &capture, &ctx);
    if (rc != 0 || ctx.had_error || !capture.data || !strstr(capture.data, "terminal mac=")) {
        fprintf(stderr, "terminal table dump failed rc=%d had_error=%d\n", rc, ctx.had_error ? 1 : 0);
        ok = false;
        goto cleanup;
    }

    debug_capture_reset(&capture);
    opts.filter_by_vlan = true;
    opts.vlan_id = 999;
    td_debug_context_reset(&ctx, &opts);
    rc = td_debug_dump_terminal_table(mgr, &opts, debug_capture_writer, &capture, &ctx);
    if (rc != 0 || ctx.had_error || (capture.data && strstr(capture.data, "terminal mac="))) {
        fprintf(stderr, "unexpected terminal output when filter excludes entries\n");
        ok = false;
        goto cleanup;
    }

    debug_capture_reset(&capture);
    opts.filter_by_vlan = false;
    opts.expand_terminals = true;
    td_debug_context_reset(&ctx, &opts);
    rc = td_debug_dump_iface_binding_table(mgr, &opts, debug_capture_writer, &capture, &ctx);
    if (rc != 0 || ctx.had_error || !capture.data || !strstr(capture.data, "binding kernel_ifindex")) {
        fprintf(stderr, "iface binding dump failed rc=%d\n", rc);
        ok = false;
        goto cleanup;
    }
    if (!strstr(capture.data, "  terminal mac=")) {
        fprintf(stderr, "binding dump missing expanded terminal entries\n");
        ok = false;
        goto cleanup;
    }

    debug_capture_reset(&capture);
    td_debug_context_reset(&ctx, NULL);
    rc = td_debug_dump_iface_prefix_table(mgr, debug_capture_writer, &capture, &ctx);
    if (rc != 0 || ctx.had_error || !capture.data || !strstr(capture.data, "iface kernel_ifindex")) {
        fprintf(stderr, "iface prefix dump failed rc=%d\n", rc);
        ok = false;
        goto cleanup;
    }

    debug_capture_reset(&capture);
    td_debug_context_reset(&ctx, NULL);
    rc = td_debug_dump_mac_lookup_queue(mgr, debug_capture_writer, &capture, &ctx);
    if (rc != 0 || ctx.had_error || !capture.data || !strstr(capture.data, "mac_lookup")) {
        fprintf(stderr, "mac lookup queue dump failed rc=%d\n", rc);
        ok = false;
        goto cleanup;
    }

    debug_capture_reset(&capture);
    td_debug_context_reset(&ctx, NULL);
    rc = td_debug_dump_mac_locator_state(mgr, debug_capture_writer, &capture, &ctx);
    if (rc != 0 || ctx.had_error || !capture.data || !strstr(capture.data, "mac_locator")) {
        fprintf(stderr, "mac locator dump failed rc=%d\n", rc);
        ok = false;
        goto cleanup;
    }

    debug_capture_reset(&capture);
    memset(&opts, 0, sizeof(opts));
    opts.filter_by_vlan = true;
    opts.vlan_id = pending_vlan_id;
    opts.expand_pending_vlans = true;
    td_debug_context_reset(&ctx, &opts);
    rc = td_debug_dump_pending_vlan_table(mgr, &opts, debug_capture_writer, &capture, &ctx);
    if (rc != 0 || ctx.had_error || !capture.data || !strstr(capture.data, "pending vlan=")) {
        fprintf(stderr, "pending vlan dump failed rc=%d had_error=%d\n", rc, ctx.had_error ? 1 : 0);
        ok = false;
        goto cleanup;
    }
    if (!strstr(capture.data, "  terminal mac=")) {
        fprintf(stderr, "pending vlan dump missing terminal detail\n");
        ok = false;
        goto cleanup;
    }

cleanup:
    debug_capture_free(&capture);
    terminal_manager_destroy(mgr);
    return ok;
}

int main(void) {
    td_log_set_level(TD_LOG_ERROR);

    struct {
        const char *name;
        bool (*fn)(void);
    } tests[] = {
        {"default_log_timestamp", test_default_log_timestamp},
        {"terminal_add_and_event", test_terminal_add_and_event},
        {"point_lookup_success_sets_ifindex", test_point_lookup_success_sets_ifindex},
        {"point_lookup_miss_triggers_refresh", test_point_lookup_miss_triggers_refresh},
        {"point_lookup_retries_on_vlan_change", test_point_lookup_retries_on_vlan_change},
        {"terminal_packet_on_ignored_vlan", test_terminal_packet_on_ignored_vlan},
        {"terminal_ignored_vlan_runtime_update", test_terminal_ignored_vlan_runtime_update},
        {"gratuitous_arp_uses_target_ip", test_gratuitous_arp_uses_target_ip},
        {"zero_ip_arp_is_ignored", test_zero_ip_arp_is_ignored},
        {"probe_failure_removes_terminal", test_probe_failure_removes_terminal},
        {"iface_invalid_holdoff", test_iface_invalid_holdoff},
        {"ifindex_change_emits_mod", test_ifindex_change_emits_mod},
        {"address_sync_retry", test_address_sync_retry},
        {"debug_dump_interfaces", test_debug_dump_interfaces},
    };

    size_t total = sizeof(tests) / sizeof(tests[0]);
    size_t failures = 0;

    for (size_t i = 0; i < total; ++i) {
        if (tests[i].fn && tests[i].fn()) {
            printf("[PASS] %s\n", tests[i].name);
        } else {
            printf("[FAIL] %s\n", tests[i].name);
            failures += 1;
        }
    }

    if (failures > 0) {
        printf("%zu/%zu tests failed\n", failures, total);
        return 1;
    }

    printf("all %zu tests passed\n", total);
    return 0;
}
