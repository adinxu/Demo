#ifndef TERMINAL_DISCOVERY_API_HPP
#define TERMINAL_DISCOVERY_API_HPP

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "terminal_manager.h"
}

enum class ModifyTag : std::uint32_t {
    DEL = 0,
    ADD = 1,
    MOD = 2,
};

struct TerminalInfo {
    std::string mac;
    std::string ip;
    std::uint32_t ifindex;
    std::uint32_t prev_ifindex;
    ModifyTag tag;
};

using MAC_IP_INFO = std::vector<TerminalInfo>;
using IncReportCb = void (*)(const MAC_IP_INFO &info);

extern "C" int getAllTerminalInfo(MAC_IP_INFO &allTerIpInfo);
extern "C" int setIncrementReport(IncReportCb cb);

struct terminal_manager;
extern "C" int terminal_northbound_attach_default_sink(struct terminal_manager *manager);

struct TdDebugDumpOptions {
    bool filterByState = false;
    terminal_state_t state = TERMINAL_STATE_ACTIVE;
    bool filterByVlan = false;
    int vlanId = -1;
    bool filterByIfindex = false;
    std::uint32_t ifindex = 0;
    bool filterByMacPrefix = false;
    std::array<std::uint8_t, ETH_ALEN> macPrefix{{0}};
    std::size_t macPrefixLen = 0;
    bool verboseMetrics = false;
    bool expandTerminals = false;

    td_debug_dump_opts_t to_c() const {
    td_debug_dump_opts_t opts;
    std::memset(&opts, 0, sizeof(opts));
        opts.filter_by_state = filterByState;
        opts.state = state;
        opts.filter_by_vlan = filterByVlan;
        opts.vlan_id = vlanId;
        opts.filter_by_ifindex = filterByIfindex;
        opts.ifindex = ifindex;
        opts.filter_by_mac_prefix = filterByMacPrefix;
        opts.mac_prefix_len = macPrefixLen > ETH_ALEN ? ETH_ALEN : macPrefixLen;
        if (opts.mac_prefix_len > 0) {
            std::memcpy(opts.mac_prefix, macPrefix.data(), opts.mac_prefix_len);
        }
        opts.verbose_metrics = verboseMetrics;
        opts.expand_terminals = expandTerminals;
        return opts;
    }
};

class TerminalDebugSnapshot {
public:
    explicit TerminalDebugSnapshot(struct terminal_manager *manager) noexcept;

    bool valid() const noexcept;

    std::string dumpTerminalTable(const TdDebugDumpOptions &options = {}) const;
    std::string dumpIfacePrefixTable() const;
    std::string dumpIfaceBindingTable(const TdDebugDumpOptions &options = {}) const;
    std::string dumpMacLookupQueues() const;
    std::string dumpMacLocatorState() const;

private:
    struct terminal_manager *manager_;
};

#endif /* TERMINAL_DISCOVERY_API_HPP */
