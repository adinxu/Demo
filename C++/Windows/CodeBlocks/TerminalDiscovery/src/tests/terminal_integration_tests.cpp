#define _DEFAULT_SOURCE

#include "terminal_discovery_api.hpp"

extern "C" {
#include "terminal_manager.h"
#include "td_logging.h"
}

#include <arpa/inet.h>
#include <netinet/if_ether.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include <inttypes.h>

struct td_adapter {
    int placeholder;
};

static td_adapter g_stub_adapter;

static int mock_kernel_ifindex_for_vlan(int vlan_id) {
    return 2000 + vlan_id;
}

extern "C" unsigned int if_nametoindex(const char *name) {
    if (!name) {
        return 0U;
    }
    if (std::strncmp(name, "vlan", 4) == 0) {
        char *endptr = nullptr;
        long vlan = std::strtol(name + 4, &endptr, 10);
        if (endptr && *endptr == '\0' && vlan > 0 && vlan < 4096) {
            return static_cast<unsigned int>(mock_kernel_ifindex_for_vlan(static_cast<int>(vlan)));
        }
    }
    return 0U;
}

struct probe_capture {
    terminal_probe_request_t last_request;
    size_t count;
};

static void probe_capture_reset(probe_capture *capture) {
    if (!capture) {
        return;
    }
    std::memset(capture, 0, sizeof(*capture));
}

static void probe_callback(const terminal_probe_request_t *request, void *user_ctx) {
    if (!request || !user_ctx) {
        return;
    }
    auto *capture = static_cast<probe_capture *>(user_ctx);
    capture->last_request = *request;
    capture->count += 1;
}

static void sleep_ms(unsigned int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

static void apply_address_update(terminal_manager *mgr,
                                 int kernel_ifindex,
                                 const char *address,
                                 uint8_t prefix_len,
                                 bool is_add) {
    terminal_address_update_t update;
    std::memset(&update, 0, sizeof(update));
    update.kernel_ifindex = kernel_ifindex;
    update.prefix_len = prefix_len;
    update.is_add = is_add;
    if (address) {
        inet_aton(address, &update.address);
    }
    terminal_manager_on_address_update(mgr, &update);
}

static void build_arp_packet(td_adapter_packet_view *packet,
                             ether_arp *arp,
                             const uint8_t mac[ETH_ALEN],
                             const char *ip_text,
                             int vlan_id,
                             uint32_t ifindex) {
    std::memset(arp, 0, sizeof(*arp));
    arp->ea_hdr.ar_hrd = htons(ARPHRD_ETHER);
    arp->ea_hdr.ar_pro = htons(ETHERTYPE_IP);
    arp->ea_hdr.ar_hln = ETH_ALEN;
    arp->ea_hdr.ar_pln = 4;
    arp->ea_hdr.ar_op = htons(ARPOP_REQUEST);
    std::memcpy(arp->arp_sha, mac, ETH_ALEN);
    uint32_t spa = inet_addr(ip_text);
    std::memcpy(arp->arp_spa, &spa, sizeof(spa));

    std::memset(packet, 0, sizeof(*packet));
    packet->payload = reinterpret_cast<const uint8_t *>(arp);
    packet->payload_len = sizeof(*arp);
    packet->ether_type = ETHERTYPE_ARP;
    packet->vlan_id = vlan_id;
    packet->ifindex = ifindex;
    std::memcpy(packet->src_mac, mac, ETH_ALEN);
}

struct inc_capture {
    size_t calls = 0;
    std::vector<MAC_IP_INFO> batches;

    void reset() {
        calls = 0;
        batches.clear();
    }
};

static inc_capture g_inc_capture;

static void inc_report_capture(const MAC_IP_INFO &info) {
    g_inc_capture.calls += 1;
    g_inc_capture.batches.push_back(info);
}

static bool test_duplicate_registration() {
    int rc = setIncrementReport(inc_report_capture);
    if (rc != -EALREADY) {
        std::printf("[FAIL] duplicate registration expected -EALREADY, got %d\n", rc);
        return false;
    }
    return true;
}

static bool test_increment_add_and_get_all(terminal_manager *mgr,
                                           int ifindex,
                                           int vlan_id) {
    g_inc_capture.reset();

    apply_address_update(mgr, ifindex, "192.0.2.1", 24, true);

    ether_arp arp;
    td_adapter_packet_view packet;
    const uint8_t mac[ETH_ALEN] = {0x00, 0x21, 0x22, 0x23, 0x24, 0x25};
    build_arp_packet(&packet, &arp, mac, "192.0.2.42", vlan_id, 7);

    terminal_manager_on_packet(mgr, &packet);
    terminal_manager_flush_events(mgr);

    bool ok = true;
    if (g_inc_capture.calls != 1 || g_inc_capture.batches.size() != 1) {
        std::printf("[FAIL] expected one incremental batch, calls=%zu batches=%zu\n",
                    g_inc_capture.calls,
                    g_inc_capture.batches.size());
        ok = false;
    } else {
        const auto &batch = g_inc_capture.batches.front();
        if (batch.size() != 1) {
            std::printf("[FAIL] expected batch size 1, got %zu\n", batch.size());
            ok = false;
        } else {
            const auto &info = batch.front();
            if (info.tag != ModifyTag::ADD) {
                std::printf("[FAIL] expected ModifyTag::ADD\n");
                ok = false;
            }
            if (info.ifindex != 7U) {
                std::printf("[FAIL] expected ifindex 7, got %u\n", info.ifindex);
                ok = false;
            }
        }
    }

    MAC_IP_INFO snapshot;
    int rc = getAllTerminalInfo(snapshot);
    if (rc != 0) {
        std::printf("[FAIL] getAllTerminalInfo returned %d\n", rc);
        ok = false;
    } else if (snapshot.size() != 1) {
        std::printf("[FAIL] expected snapshot size 1, got %zu\n", snapshot.size());
        ok = false;
    } else if (snapshot.front().ifindex != 7U) {
        std::printf("[FAIL] expected snapshot ifindex 7, got %u\n", snapshot.front().ifindex);
        ok = false;
    }

    return ok;
}

static bool test_netlink_removal(terminal_manager *mgr,
                                 int ifindex,
                                 const char *address) {
    g_inc_capture.reset();

    apply_address_update(mgr, ifindex, address, 24, false);
    sleep_ms(1100);
    terminal_manager_on_timer(mgr);
    terminal_manager_flush_events(mgr);

    bool ok = true;
    if (g_inc_capture.calls != 1 || g_inc_capture.batches.size() != 1) {
        std::printf("[FAIL] expected one DEL batch, calls=%zu batches=%zu\n",
                    g_inc_capture.calls,
                    g_inc_capture.batches.size());
        ok = false;
    } else {
        const auto &batch = g_inc_capture.batches.front();
        if (batch.size() != 1) {
            std::printf("[FAIL] expected DEL batch size 1, got %zu\n", batch.size());
            ok = false;
        } else if (batch.front().tag != ModifyTag::DEL) {
            std::printf("[FAIL] expected ModifyTag::DEL\n");
            ok = false;
        }
    }

    MAC_IP_INFO snapshot;
    int rc = getAllTerminalInfo(snapshot);
    if (rc != 0) {
        std::printf("[FAIL] getAllTerminalInfo after removal returned %d\n", rc);
        ok = false;
    } else if (!snapshot.empty()) {
        std::printf("[FAIL] expected empty snapshot after removal, got %zu\n", snapshot.size());
        ok = false;
    }

    return ok;
}

static bool test_stats_tracking(terminal_manager *mgr) {
    terminal_manager_stats stats;
    std::memset(&stats, 0, sizeof(stats));
    terminal_manager_get_stats(mgr, &stats);

    bool ok = true;
    if (stats.terminals_discovered != 1) {
        std::printf("[FAIL] expected terminals_discovered=1, got %" PRIu64 "\n", stats.terminals_discovered);
        ok = false;
    }
    if (stats.terminals_removed != 1) {
        std::printf("[FAIL] expected terminals_removed=1, got %" PRIu64 "\n", stats.terminals_removed);
        ok = false;
    }
    if (stats.address_update_events < 2) {
        std::printf("[FAIL] expected address_update_events>=2, got %" PRIu64 "\n", stats.address_update_events);
        ok = false;
    }
    if (stats.current_terminals != 0) {
        std::printf("[FAIL] expected current_terminals=0, got %" PRIu64 "\n", stats.current_terminals);
        ok = false;
    }
    return ok;
}

int main() {
    td_log_set_level(TD_LOG_ERROR);

    const int vlan_id = 200;
    const int tx_kernel_ifindex = mock_kernel_ifindex_for_vlan(vlan_id);
    terminal_manager_config cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    cfg.keepalive_interval_sec = 1;
    cfg.keepalive_miss_threshold = 1;
    cfg.iface_invalid_holdoff_sec = 1;
    cfg.scan_interval_ms = 60000;
    cfg.vlan_iface_format = "vlan%u";
    cfg.max_terminals = 16;

    probe_capture probes;
    probe_capture_reset(&probes);

    terminal_manager *mgr = terminal_manager_create(&cfg,
                                                    &g_stub_adapter,
                                                    nullptr,
                                                    probe_callback,
                                                    &probes);
    if (!mgr) {
        std::printf("[FAIL] failed to create terminal manager\n");
        return 1;
    }

    int rc = setIncrementReport(inc_report_capture);
    if (rc != 0) {
        std::printf("[FAIL] setIncrementReport returned %d\n", rc);
        terminal_manager_destroy(mgr);
        return 1;
    }

    bool all_ok = true;
    all_ok &= test_duplicate_registration();
    all_ok &= test_increment_add_and_get_all(mgr, tx_kernel_ifindex, vlan_id);
    all_ok &= test_netlink_removal(mgr, tx_kernel_ifindex, "192.0.2.1");
    all_ok &= test_stats_tracking(mgr);

    terminal_manager_set_event_sink(mgr, nullptr, nullptr);
    terminal_manager_destroy(mgr);

    if (all_ok) {
        std::printf("integration tests passed\n");
        return 0;
    }

    std::printf("integration tests failed\n");
    return 1;
}
