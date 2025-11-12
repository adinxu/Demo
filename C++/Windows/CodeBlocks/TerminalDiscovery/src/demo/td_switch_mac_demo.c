#include "td_switch_mac_bridge.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TD_SWITCH_MAC_DEFAULT_CAPACITY 1024U
#define TD_SWITCH_MAC_MAX_CAPACITY 131072U

static uint32_t g_cached_capacity_hint = 0;
static int g_capacity_initialized = 0;

static const char *td_switch_mac_state_to_string(mac_state_e state) {
    switch (state) {
        case MAC_STATE_DYNAMIC:
            return "dynamic";
        case MAC_STATE_STATIC:
            return "static";
        case MAC_STATE_DELETE:
            return "deleted";
        case MAC_STATE_EMD:
            return "emd";
        default:
            return "unknown";
    }
}

static void td_switch_mac_print_entry(FILE *out, const SwUcMacEntry *entry, uint32_t index) {
    fprintf(out,
            "%5" PRIu32 "  %02x:%02x:%02x:%02x:%02x:%02x  %5" PRIu16 "  %7" PRIu32 "  %s (%u)\n",
            index,
            entry->mac[0], entry->mac[1], entry->mac[2],
            entry->mac[3], entry->mac[4], entry->mac[5],
            entry->vlan,
            entry->ifindex,
            td_switch_mac_state_to_string(entry->attr),
            (unsigned)entry->attr);
}

int td_switch_mac_demo_dump(void) {
    static SwUcMacEntry *entries = NULL;

    if (!g_capacity_initialized) {
        uint32_t queried_capacity = 0;
        int rc_capacity = td_switch_mac_get_capacity(&queried_capacity);
        if (rc_capacity != 0) {
        fprintf(stdout,
                    "[switch-mac-demo] 调用 td_switch_mac_get_capacity 失败: rc=%d, 使用默认容量 %u\n",
                    rc_capacity,
                    TD_SWITCH_MAC_DEFAULT_CAPACITY);
            queried_capacity = TD_SWITCH_MAC_DEFAULT_CAPACITY;
        } else if (queried_capacity == 0) {
        fprintf(stdout,
                    "[switch-mac-demo] td_switch_mac_get_capacity 返回 0, 回退到默认容量 %u\n",
                    TD_SWITCH_MAC_DEFAULT_CAPACITY);
            queried_capacity = TD_SWITCH_MAC_DEFAULT_CAPACITY;
        }

        if (queried_capacity > TD_SWITCH_MAC_MAX_CAPACITY) {
        fprintf(stdout,
                    "[switch-mac-demo] td_switch_mac_get_capacity 返回 %u, 超出上限 %u, 取上限值\n",
                    queried_capacity,
                    TD_SWITCH_MAC_MAX_CAPACITY);
            queried_capacity = TD_SWITCH_MAC_MAX_CAPACITY;
        }

        g_cached_capacity_hint = queried_capacity;
        g_capacity_initialized = 1;
    }

    uint32_t capacity = g_cached_capacity_hint;

    if (entries == NULL) {
        entries = calloc(capacity, sizeof(*entries));
        if (entries == NULL) {
            fprintf(stdout, "[switch-mac-demo] 分配 %u 项缓存失败: %s\n", capacity, strerror(errno));
            return -ENOMEM;
        }
    }

    uint32_t count = 0;
    int rc_snapshot = td_switch_mac_snapshot(entries, &count);
    if (rc_snapshot != 0) {
    fprintf(stdout, "[switch-mac-demo] 调用 td_switch_mac_snapshot 失败: rc=%d\n", rc_snapshot);
        return rc_snapshot;
    }

    if (count > capacity) {
    fprintf(stdout,
                "[switch-mac-demo] 警告: 桥接返回条目数 %u 超过缓冲区容量 %u，输出结果可能不完整\n",
                count,
                capacity);
        count = capacity;
    }

    fprintf(stdout,
        "[switch-mac-demo] 快照成功: 容量提示=%u, 使用缓存=%u, 条目数=%u\n",
        g_cached_capacity_hint,
            capacity,
            count);

    printf("索引  MAC 地址              VLAN   IFINDEX  属性 (原始值)\n");
    printf("-----  -------------------  -----  -------  -------------\n");

    {
        uint32_t i;
        for (i = 0; i < count; ++i) {
            td_switch_mac_print_entry(stdout, &entries[i], i);
        }
    }

    return 0;
}
