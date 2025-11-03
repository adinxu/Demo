#ifndef TERMINAL_DISCOVERY_API_HPP
#define TERMINAL_DISCOVERY_API_HPP

#include <cstdint>
#include <string>
#include <vector>

enum class ModifyTag : std::uint32_t {
    DEL = 0,
    ADD = 1,
    MOD = 2,
};

struct TerminalInfo {
    std::string mac;
    std::string ip;
    std::uint32_t port;
    ModifyTag tag;
};

using MAC_IP_INFO = std::vector<TerminalInfo>;
using IncReportCb = void (*)(const MAC_IP_INFO &info);

extern "C" int getAllTerminalIpInfo(MAC_IP_INFO &allTerIpInfo);
extern "C" int setIncrementReport(IncReportCb cb);

#endif /* TERMINAL_DISCOVERY_API_HPP */
