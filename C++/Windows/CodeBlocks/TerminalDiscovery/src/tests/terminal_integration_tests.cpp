#define _DEFAULT_SOURCE

#include "terminal_discovery_api.hpp"

extern "C" {
#include "terminal_manager.h"
#include "td_logging.h"
}

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
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

extern "C" char *if_indextoname(unsigned int ifindex, char *ifname) {
    if (!ifname) {
        return nullptr;
    }
    if (ifindex >= 2000U) {
        unsigned int vlan = ifindex - 2000U;
        if (vlan > 0U && vlan < 4096U) {
            std::snprintf(ifname, IFNAMSIZ, "vlan%u", vlan);
            return ifname;
        }
    }
    return nullptr;
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

static std::string format_mac_string(const uint8_t mac[ETH_ALEN]) {
    char buf[18];
    std::snprintf(buf,
                  sizeof(buf),
                  "%02x:%02x:%02x:%02x:%02x:%02x",
                  mac[0],
                  mac[1],
                  mac[2],
                  mac[3],
                  mac[4],
                  mac[5]);
    return std::string(buf);
}

static const char *modify_tag_name(ModifyTag tag) {
    switch (tag) {
    case ModifyTag::ADD:
        return "ADD";
    case ModifyTag::DEL:
        return "DEL";
    case ModifyTag::MOD:
    default:
        return "MOD";
    }
}

static bool expect_single_event(const char *context,
                                ModifyTag expected_tag,
                                const std::string &expected_mac,
                                const std::string &expected_ip,
                                uint32_t expected_ifindex,
                                uint32_t expected_prev_ifindex) {
    if (g_inc_capture.calls != 1 || g_inc_capture.batches.size() != 1) {
        std::printf("[FAIL] %s: expected one batch, got calls=%zu batches=%zu\n",
                    context,
                    g_inc_capture.calls,
                    g_inc_capture.batches.size());
        return false;
    }
    const auto &batch = g_inc_capture.batches.front();
    if (batch.size() != 1) {
        std::printf("[FAIL] %s: expected batch size 1, got %zu\n", context, batch.size());
        return false;
    }
    const auto &info = batch.front();
    bool ok = true;
    if (info.tag != expected_tag) {
        std::printf("[FAIL] %s: expected tag %s, got %s\n",
                    context,
                    modify_tag_name(expected_tag),
                    modify_tag_name(info.tag));
        ok = false;
    }
    if (info.mac != expected_mac) {
        std::printf("[FAIL] %s: expected mac %s, got %s\n",
                    context,
                    expected_mac.c_str(),
                    info.mac.c_str());
        ok = false;
    }
    if (info.ip != expected_ip) {
        std::printf("[FAIL] %s: expected ip %s, got %s\n",
                    context,
                    expected_ip.c_str(),
                    info.ip.c_str());
        ok = false;
    }
    if (info.ifindex != expected_ifindex) {
        std::printf("[FAIL] %s: expected ifindex %u, got %u\n",
                    context,
                    expected_ifindex,
                    info.ifindex);
        ok = false;
    }
    if (info.prev_ifindex != expected_prev_ifindex) {
        std::printf("[FAIL] %s: expected prev_ifindex %u, got %u\n",
                    context,
                    expected_prev_ifindex,
                    info.prev_ifindex);
        ok = false;
    }
    return ok;
}

static bool expect_no_events(const char *context) {
    if (g_inc_capture.calls != 0 || !g_inc_capture.batches.empty()) {
        std::printf("[FAIL] %s: expected no events, got calls=%zu batches=%zu\n",
                    context,
                    g_inc_capture.calls,
                    g_inc_capture.batches.size());
        return false;
    }
    return true;
}

static bool expect_debug_line(struct terminal_manager *mgr,
                              const uint8_t mac[ETH_ALEN],
                              const char *ip,
                              const std::vector<std::string> &tokens,
                              const char *context) {
    if (!mgr || !ip) {
        std::printf("[FAIL] %s: invalid arguments for debug expectation\n", context);
        return false;
    }

    TdDebugDumpOptions opts;
    opts.verboseMetrics = true;
    opts.expandTerminals = true;
    opts.filterByMacPrefix = true;
    opts.macPrefixLen = ETH_ALEN;
    for (size_t i = 0; i < ETH_ALEN; ++i) {
        opts.macPrefix[i] = mac[i];
    }

    TerminalDebugSnapshot snapshot(mgr);
    if (!snapshot.valid()) {
        std::printf("[FAIL] %s: TerminalDebugSnapshot invalid\n", context);
        return false;
    }

    std::string dump = snapshot.dumpTerminalTable(opts);
    std::string mac_str = format_mac_string(mac);
    std::string expected_head = "terminal mac=" + mac_str + " ip=" + std::string(ip);
    size_t pos = dump.find(expected_head);
    if (pos == std::string::npos) {
        std::printf("[FAIL] %s: unable to locate terminal line for %s\n",
                    context,
                    mac_str.c_str());
        return false;
    }
    size_t end = dump.find('\n', pos);
    std::string line = dump.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    bool ok = true;
    for (const std::string &token : tokens) {
        if (line.find(token) == std::string::npos) {
            std::printf("[FAIL] %s: expected '%s' in line: %s\n",
                        context,
                        token.c_str(),
                        line.c_str());
            ok = false;
        }
    }
    return ok;
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

    TerminalDebugSnapshot debug_snapshot(mgr);
    if (!debug_snapshot.valid()) {
        std::printf("[FAIL] TerminalDebugSnapshot invalid\n");
        ok = false;
    } else {
        TdDebugDumpOptions options;
        options.verboseMetrics = true;
        options.expandTerminals = true;
        std::string terminals = debug_snapshot.dumpTerminalTable(options);
        if (terminals.find("terminal mac=") == std::string::npos) {
            std::printf("[FAIL] debug dump missing terminal entry\n");
            ok = false;
        }
        std::string bindings = debug_snapshot.dumpIfaceBindingTable(options);
        if (bindings.find("binding kernel_ifindex") == std::string::npos) {
            std::printf("[FAIL] debug binding dump missing header\n");
            ok = false;
        }
        TdDebugDumpOptions pending_options;
        pending_options.expandPendingVlans = true;
        std::string pending = debug_snapshot.dumpPendingVlanTable(pending_options);
        if (pending.find("pending_vlans") == std::string::npos) {
            std::printf("[FAIL] debug pending vlan dump missing summary\n");
            ok = false;
        }
        std::string prefixes = debug_snapshot.dumpIfacePrefixTable();
        if (prefixes.find("iface kernel_ifindex") == std::string::npos) {
            std::printf("[FAIL] debug prefix dump missing header\n");
            ok = false;
        }
        std::string queues = debug_snapshot.dumpMacLookupQueues();
        if (queues.find("mac_lookup") == std::string::npos) {
            std::printf("[FAIL] debug queue dump missing header\n");
            ok = false;
        }
        std::string locator = debug_snapshot.dumpMacLocatorState();
        if (locator.find("mac_locator") == std::string::npos) {
            std::printf("[FAIL] debug locator dump missing header\n");
            ok = false;
        }
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
    if (stats.terminals_discovered != 3) {
        std::printf("[FAIL] expected terminals_discovered=3, got %" PRIu64 "\n", stats.terminals_discovered);
        ok = false;
    }
    if (stats.terminals_removed != 3) {
        std::printf("[FAIL] expected terminals_removed=3, got %" PRIu64 "\n", stats.terminals_removed);
        ok = false;
    }
    if (stats.address_update_events < 8) {
        std::printf("[FAIL] expected address_update_events>=8, got %" PRIu64 "\n", stats.address_update_events);
        ok = false;
    }
    if (stats.current_terminals != 0) {
        std::printf("[FAIL] expected current_terminals=0, got %" PRIu64 "\n", stats.current_terminals);
        ok = false;
    }
    return ok;
}

static bool test_cross_vlan_migration(terminal_manager *mgr) {
    const int vlan_initial = 300;
    const int vlan_migrated = 301;
    const uint8_t mac[ETH_ALEN] = {0x00, 0x30, 0x31, 0x32, 0x33, 0x34};
    const char *ip = "198.18.0.42";
    const std::string mac_str = format_mac_string(mac);

    terminal_manager_flush_events(mgr);

    ether_arp arp;
    td_adapter_packet_view packet;

    g_inc_capture.reset();
    build_arp_packet(&packet, &arp, mac, ip, vlan_initial, 31);
    terminal_manager_on_packet(mgr, &packet);
    terminal_manager_flush_events(mgr);

    bool ok = expect_single_event("cross_vlan/initial_add",
                                  ModifyTag::ADD,
                                  mac_str,
                                  ip,
                                  31,
                                  0);

    ok &= expect_debug_line(mgr,
                            mac,
                            ip,
                            {"state=IFACE_INVALID",
                             "vlan=300",
                             "ifindex=31",
                             "tx_iface=<unset>",
                             "tx_kernel_ifindex=-1",
                             "tx_src=-"},
                            "cross_vlan/initial_state");

    terminal_manager_flush_events(mgr);
    g_inc_capture.reset();
    apply_address_update(mgr, mock_kernel_ifindex_for_vlan(vlan_initial), "198.18.0.1", 24, true);
    terminal_manager_flush_events(mgr);
    ok &= expect_no_events("cross_vlan/address_add_initial");

    ok &= expect_debug_line(mgr,
                            mac,
                            ip,
                            {"state=PROBING",
                             "vlan=300",
                             "ifindex=31",
                             "tx_iface=vlan300",
                             std::string("tx_kernel_ifindex=") + std::to_string(mock_kernel_ifindex_for_vlan(vlan_initial)),
                             "tx_src=198.18.0.1"},
                            "cross_vlan/after_initial_iface");

    terminal_manager_flush_events(mgr);
    g_inc_capture.reset();
    build_arp_packet(&packet, &arp, mac, ip, vlan_migrated, 61);
    terminal_manager_on_packet(mgr, &packet);
    terminal_manager_flush_events(mgr);

    ok &= expect_single_event("cross_vlan/migration_mod",
                              ModifyTag::MOD,
                              mac_str,
                              ip,
                              61,
                              31);

    ok &= expect_debug_line(mgr,
                            mac,
                            ip,
                            {"state=IFACE_INVALID",
                             "vlan=301",
                             "ifindex=61",
                             "tx_iface=<unset>",
                             "tx_kernel_ifindex=-1",
                             "tx_src=-"},
                            "cross_vlan/migrated_pending");

    terminal_manager_flush_events(mgr);
    g_inc_capture.reset();
    apply_address_update(mgr, mock_kernel_ifindex_for_vlan(vlan_migrated), "198.18.0.1", 24, true);
    terminal_manager_flush_events(mgr);
    ok &= expect_no_events("cross_vlan/address_add_migrated");

    ok &= expect_debug_line(mgr,
                            mac,
                            ip,
                            {"state=PROBING",
                             "vlan=301",
                             "ifindex=61",
                             "tx_iface=vlan301",
                             std::string("tx_kernel_ifindex=") + std::to_string(mock_kernel_ifindex_for_vlan(vlan_migrated)),
                             "tx_src=198.18.0.1"},
                            "cross_vlan/migrated_bound");

    terminal_manager_flush_events(mgr);
    g_inc_capture.reset();
    apply_address_update(mgr, mock_kernel_ifindex_for_vlan(vlan_migrated), "198.18.0.1", 24, false);
    terminal_manager_flush_events(mgr);
    ok &= expect_no_events("cross_vlan/address_remove_cleanup");

    sleep_ms(1100);
    terminal_manager_flush_events(mgr);
    g_inc_capture.reset();
    terminal_manager_on_timer(mgr);
    terminal_manager_flush_events(mgr);

    ok &= expect_single_event("cross_vlan/del",
                              ModifyTag::DEL,
                              mac_str,
                              ip,
                              61,
                              0);

    terminal_manager_flush_events(mgr);
    apply_address_update(mgr, mock_kernel_ifindex_for_vlan(vlan_initial), "198.18.0.1", 24, false);
    terminal_manager_flush_events(mgr);

    return ok;
}

static bool test_ipv4_recovery(terminal_manager *mgr) {
    const int vlan_id = 350;
    const uint8_t mac[ETH_ALEN] = {0x00, 0x40, 0x41, 0x42, 0x43, 0x44};
    const char *terminal_ip = "198.51.100.55";
    const char *iface_ip = "198.51.100.1";
    const std::string mac_str = format_mac_string(mac);

    terminal_manager_flush_events(mgr);
    apply_address_update(mgr, mock_kernel_ifindex_for_vlan(vlan_id), iface_ip, 24, true);
    terminal_manager_flush_events(mgr);

    ether_arp arp;
    td_adapter_packet_view packet;

    g_inc_capture.reset();
    build_arp_packet(&packet, &arp, mac, terminal_ip, vlan_id, 77);
    terminal_manager_on_packet(mgr, &packet);
    terminal_manager_flush_events(mgr);

    bool ok = expect_single_event("ipv4_recovery/add",
                                  ModifyTag::ADD,
                                  mac_str,
                                  terminal_ip,
                                  77,
                                  0);

    ok &= expect_debug_line(mgr,
                            mac,
                            terminal_ip,
                            {"state=ACTIVE",
                             "vlan=350",
                             "ifindex=77",
                             "tx_iface=vlan350",
                             std::string("tx_kernel_ifindex=") + std::to_string(mock_kernel_ifindex_for_vlan(vlan_id)),
                             std::string("tx_src=") + iface_ip},
                            "ipv4_recovery/bound_initial");

    terminal_manager_flush_events(mgr);
    g_inc_capture.reset();
    apply_address_update(mgr, mock_kernel_ifindex_for_vlan(vlan_id), iface_ip, 24, false);
    terminal_manager_flush_events(mgr);
    ok &= expect_no_events("ipv4_recovery/remove_ip");

    ok &= expect_debug_line(mgr,
                            mac,
                            terminal_ip,
                            {"state=IFACE_INVALID",
                             "vlan=350",
                             "ifindex=77",
                             "tx_iface=<unset>",
                             "tx_kernel_ifindex=-1",
                             "tx_src=-"},
                            "ipv4_recovery/after_remove");

    terminal_manager_flush_events(mgr);
    g_inc_capture.reset();
    apply_address_update(mgr, mock_kernel_ifindex_for_vlan(vlan_id), iface_ip, 24, true);
    terminal_manager_flush_events(mgr);
    ok &= expect_no_events("ipv4_recovery/readd_ip");

    ok &= expect_debug_line(mgr,
                            mac,
                            terminal_ip,
                            {"state=PROBING",
                             "vlan=350",
                             "ifindex=77",
                             "tx_iface=vlan350",
                             std::string("tx_kernel_ifindex=") + std::to_string(mock_kernel_ifindex_for_vlan(vlan_id)),
                             std::string("tx_src=") + iface_ip},
                            "ipv4_recovery/after_readd");

    terminal_manager_flush_events(mgr);
    apply_address_update(mgr, mock_kernel_ifindex_for_vlan(vlan_id), iface_ip, 24, false);
    terminal_manager_flush_events(mgr);

    sleep_ms(1100);
    terminal_manager_flush_events(mgr);
    g_inc_capture.reset();
    terminal_manager_on_timer(mgr);
    terminal_manager_flush_events(mgr);

    ok &= expect_single_event("ipv4_recovery/del",
                              ModifyTag::DEL,
                              mac_str,
                              terminal_ip,
                              77,
                              0);

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
    all_ok &= test_cross_vlan_migration(mgr);
    all_ok &= test_ipv4_recovery(mgr);
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
