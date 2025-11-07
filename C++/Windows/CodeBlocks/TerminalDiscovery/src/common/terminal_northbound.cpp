#include "terminal_discovery_api.hpp"

#include "terminal_manager.h"
#include "td_logging.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>

namespace {

std::string format_mac(const uint8_t mac[ETH_ALEN]) {
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

std::string format_ip(const struct in_addr &ip) {
    char buf[INET_ADDRSTRLEN];
    std::memset(buf, 0, sizeof(buf));
    const char *res = inet_ntop(AF_INET, &ip, buf, sizeof(buf));
    if (!res) {
        td_log_writef(TD_LOG_WARN,
                      "terminal_northbound",
                      "inet_ntop failed: %s",
                      std::strerror(errno));
        return std::string();
    }
    return std::string(buf);
}

ModifyTag to_modify_tag(terminal_event_tag_t tag) {
    switch (tag) {
    case TERMINAL_EVENT_TAG_DEL:
        return ModifyTag::DEL;
    case TERMINAL_EVENT_TAG_ADD:
        return ModifyTag::ADD;
    case TERMINAL_EVENT_TAG_MOD:
    default:
        return ModifyTag::MOD;
    }
}

struct QueryCtx {
    MAC_IP_INFO *info;
    bool ok;
};

bool accumulate_query(const terminal_event_record_t *record, void *user_ctx) {
    if (!record || !user_ctx) {
        return false;
    }

    auto *ctx = static_cast<QueryCtx *>(user_ctx);
    try {
        TerminalInfo info;
        info.mac = format_mac(record->key.mac);
        info.ip = format_ip(record->key.ip);
    info.ifindex = record->ifindex;
        info.tag = to_modify_tag(record->tag);
        ctx->info->push_back(std::move(info));
        return true;
    } catch (const std::exception &ex) {
        ctx->ok = false;
        td_log_writef(TD_LOG_ERROR,
                      "terminal_northbound",
                      "failed to append terminal snapshot: %s",
                      ex.what());
    } catch (...) {
        ctx->ok = false;
        td_log_writef(TD_LOG_ERROR,
                      "terminal_northbound",
                      "failed to append terminal snapshot: unknown exception");
    }
    return false;
}

std::mutex g_inc_report_mutex;
IncReportCb g_inc_report_cb = nullptr;

void inc_report_adapter(const terminal_event_record_t *records, size_t count, void *ctx) {
    (void)ctx;

    IncReportCb cb = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_inc_report_mutex);
        cb = g_inc_report_cb;
    }

    if (!cb) {
        return;
    }

    MAC_IP_INFO payload;
    try {
        payload.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            TerminalInfo info;
            info.mac = format_mac(records[i].key.mac);
            info.ip = format_ip(records[i].key.ip);
            info.ifindex = records[i].ifindex;
            info.tag = to_modify_tag(records[i].tag);
            payload.push_back(std::move(info));
        }
    } catch (const std::exception &ex) {
        td_log_writef(TD_LOG_ERROR,
                      "terminal_northbound",
                      "failed to prepare incremental payload: %s",
                      ex.what());
        return;
    } catch (...) {
        td_log_writef(TD_LOG_ERROR,
                      "terminal_northbound",
                      "failed to prepare incremental payload: unknown exception");
        return;
    }

    try {
        cb(payload);
    } catch (const std::exception &ex) {
        td_log_writef(TD_LOG_ERROR,
                      "terminal_northbound",
                      "IncReportCb raised exception: %s",
                      ex.what());
    } catch (...) {
        td_log_writef(TD_LOG_ERROR,
                      "terminal_northbound",
                      "IncReportCb raised unknown exception");
    }
}

} // namespace

extern "C" int getAllTerminalInfo(MAC_IP_INFO &allTerIpInfo) {
    allTerIpInfo.clear();

    struct terminal_manager *mgr = terminal_manager_get_active();
    if (!mgr) {
    td_log_writef(TD_LOG_WARN,
              "terminal_northbound",
              "getAllTerminalInfo called without an active terminal manager");
        return -ENODEV;
    }

    QueryCtx ctx{&allTerIpInfo, true};
    int rc = terminal_manager_query_all(mgr, accumulate_query, &ctx);
    if (rc != 0) {
        td_log_writef(TD_LOG_ERROR,
                      "terminal_northbound",
                      "terminal_manager_query_all failed: %d",
                      rc);
        return rc;
    }

    if (!ctx.ok) {
        allTerIpInfo.clear();
        return -ENOMEM;
    }

    return 0;
}

extern "C" int setIncrementReport(IncReportCb cb) {
    if (!cb) {
        td_log_writef(TD_LOG_WARN,
                      "terminal_northbound",
                      "setIncrementReport requires a non-null callback");
        return -EINVAL;
    }

    struct terminal_manager *mgr = terminal_manager_get_active();
    if (!mgr) {
        td_log_writef(TD_LOG_WARN,
                      "terminal_northbound",
                      "setIncrementReport called without an active terminal manager");
        return -ENODEV;
    }

    {
        std::lock_guard<std::mutex> lock(g_inc_report_mutex);
        if (g_inc_report_cb) {
            td_log_writef(TD_LOG_WARN,
                          "terminal_northbound",
                          "setIncrementReport invoked multiple times");
            return -EALREADY;
        }
        g_inc_report_cb = cb;
    }

    int rc = terminal_manager_set_event_sink(mgr, inc_report_adapter, nullptr);
    if (rc != 0) {
        std::lock_guard<std::mutex> lock(g_inc_report_mutex);
        g_inc_report_cb = nullptr;
        td_log_writef(TD_LOG_ERROR,
                      "terminal_northbound",
                      "terminal_manager_set_event_sink failed: %d",
                      rc);
        return rc;
    }

    return 0;
}
