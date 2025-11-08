#define _DEFAULT_SOURCE

#include "terminal_manager.h"
#include "td_logging.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <netinet/if_ether.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
                             const char *ip_text,
                             int vlan_id,
                             uint32_t ifindex) {
    memset(arp, 0, sizeof(*arp));
    arp->ea_hdr.ar_hrd = htons(ARPHRD_ETHER);
    arp->ea_hdr.ar_pro = htons(ETHERTYPE_IP);
    arp->ea_hdr.ar_hln = ETH_ALEN;
    arp->ea_hdr.ar_pln = 4;
    arp->ea_hdr.ar_op = htons(ARPOP_REQUEST);
    memcpy(arp->arp_sha, mac, ETH_ALEN);
    uint32_t spa = inet_addr(ip_text);
    memcpy(arp->arp_spa, &spa, sizeof(spa));

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
    build_arp_packet(&packet, &arp, mac, "192.0.2.10", vlan_id, 7);

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
    build_arp_packet(&packet, &arp, mac, "198.51.100.42", vlan_id, 11);

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
    build_arp_packet(&packet, &arp, mac, "203.0.113.9", vlan_id, 3);

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
    build_arp_packet(&packet, &arp, mac, "10.10.10.20", vlan_id, 1);

    terminal_manager_on_packet(mgr, &packet);
    terminal_manager_flush_events(mgr);

    if (events.count != 1 || events.records[0].tag != TERMINAL_EVENT_TAG_ADD) {
        fprintf(stderr, "expected initial ADD event\n");
        terminal_manager_destroy(mgr);
        return false;
    }

    capture_reset(&events);

    build_arp_packet(&packet, &arp, mac, "10.10.10.20", vlan_id, 2);
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

int main(void) {
    td_log_set_level(TD_LOG_ERROR);

    struct {
        const char *name;
        bool (*fn)(void);
    } tests[] = {
        {"default_log_timestamp", test_default_log_timestamp},
        {"terminal_add_and_event", test_terminal_add_and_event},
        {"probe_failure_removes_terminal", test_probe_failure_removes_terminal},
        {"iface_invalid_holdoff", test_iface_invalid_holdoff},
    {"ifindex_change_emits_mod", test_ifindex_change_emits_mod},
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
