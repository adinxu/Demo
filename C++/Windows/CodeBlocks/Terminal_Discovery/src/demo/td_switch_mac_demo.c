#include "td_switch_mac_bridge.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TD_SWITCH_MAC_DEFAULT_CAPACITY 1024U
#define TD_SWITCH_MAC_MAX_CAPACITY 131072U

static const char *td_switch_mac_attr_to_string(uint32_t attr) {
    switch (attr) {
        case 0:
            return "dynamic";
        case 1:
            return "static";
        case 2:
            return "deleted";
        default:
            return "unknown";
    }
}

static void td_switch_mac_print_entry(FILE *out, const td_switch_mac_entry_t *entry, uint32_t index) {
    fprintf(out,
            "%5" PRIu32 "  %02x:%02x:%02x:%02x:%02x:%02x  %5" PRIu16 "  %7" PRIu32 "  %s (%" PRIu32 ")\n",
            index,
            entry->mac[0], entry->mac[1], entry->mac[2],
            entry->mac[3], entry->mac[4], entry->mac[5],
            entry->vlan,
            entry->ifindex,
            td_switch_mac_attr_to_string(entry->attr),
            entry->attr);
}

int td_switch_mac_demo_dump(void) {
    uint32_t capacity_hint = 0;
    int rc_capacity = td_switch_mac_get_capacity(&capacity_hint);
    if (rc_capacity != 0) {
        fprintf(stderr,
                "[switch-mac-demo] 调用 td_switch_mac_get_capacity 失败: rc=%d, 使用默认容量 %u\n",
                rc_capacity,
                TD_SWITCH_MAC_DEFAULT_CAPACITY);
        capacity_hint = TD_SWITCH_MAC_DEFAULT_CAPACITY;
    } else if (capacity_hint == 0) {
        fprintf(stderr,
                "[switch-mac-demo] td_switch_mac_get_capacity 返回 0, 回退到默认容量 %u\n",
                TD_SWITCH_MAC_DEFAULT_CAPACITY);
        capacity_hint = TD_SWITCH_MAC_DEFAULT_CAPACITY;
    }

    if (capacity_hint > TD_SWITCH_MAC_MAX_CAPACITY) {
        fprintf(stderr,
                "[switch-mac-demo] td_switch_mac_get_capacity 返回 %u, 超出上限 %u, 取上限值\n",
                capacity_hint,
                TD_SWITCH_MAC_MAX_CAPACITY);
        capacity_hint = TD_SWITCH_MAC_MAX_CAPACITY;
    }

    uint32_t capacity = capacity_hint;
    if (capacity == 0) {
        capacity = TD_SWITCH_MAC_DEFAULT_CAPACITY;
    }

    td_switch_mac_entry_t *entries = calloc(capacity, sizeof(*entries));
    if (entries == NULL) {
        fprintf(stderr, "[switch-mac-demo] 分配 %u 项缓存失败: %s\n", capacity, strerror(errno));
        return -ENOMEM;
    }

    uint32_t count = capacity;
    int rc = td_switch_mac_snapshot(entries, &count);
    if (rc != 0) {
        fprintf(stderr, "[switch-mac-demo] 调用 td_switch_mac_snapshot 失败: rc=%d\n", rc);
        free(entries);
        return rc;
    }

    if (count > capacity) {
        fprintf(stderr,
                "[switch-mac-demo] 警告: 桥接返回条目数 %u 超过缓冲区容量 %u，输出结果可能不完整\n",
                count,
                capacity);
        count = capacity;
    }

    fprintf(stderr,
            "[switch-mac-demo] 快照成功: 容量提示=%u, 使用缓存=%u, 条目数=%u\n",
            capacity_hint,
            capacity,
            count);

    printf("索引  MAC 地址              VLAN   IFINDEX  属性 (原始值)\n");
    printf("-----  -------------------  -----  -------  -------------\n");

    for (uint32_t i = 0; i < count; ++i) {
        td_switch_mac_print_entry(stdout, &entries[i], i);
    }

    free(entries);
    return 0;
}
