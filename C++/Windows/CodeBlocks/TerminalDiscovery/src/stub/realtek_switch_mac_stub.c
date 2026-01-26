#include "td_switch_mac_bridge.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef TD_SWITCH_MAC_WEAK
#if defined(__GNUC__) || defined(__clang__)
#define TD_SWITCH_MAC_WEAK __attribute__((weak))
#else
#define TD_SWITCH_MAC_WEAK
#endif
#endif

#define TD_SWITCH_MAC_STUB_ENABLED 1

static const SwUcMacEntry kStubEntries[] = {
    {{0x00, 0x11, 0x22, 0x33, 0x44, 0x55}, 1, MAC_STATE_DYNAMIC, 7},
    {{0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb}, 10, MAC_STATE_STATIC, 12},
    {{0xcc, 0xdd, 0xee, 0xff, 0x00, 0x11}, 4094, MAC_STATE_DELETE, 18},
};

static uint32_t g_stub_capacity_hint = 1024U;

static size_t stub_entry_count(void) {
    return sizeof(kStubEntries) / sizeof(kStubEntries[0]);
}

static void log_stub_message(const char *message) {
    static int logged = 0;
    if (!logged) {
        fprintf(stdout, "[switch-mac-stub] %s\n", message);
        logged = 1;
    }
}

static void log_lookup_stub_message(void) {
    static int logged = 0;
    if (!logged) {
        fprintf(stdout, "[switch-mac-stub] stubbed td_switch_mac_get_ifindex_by_vid active\n");
        logged = 1;
    }
}

static uint32_t resolve_stub_count(void) {
    const size_t total = stub_entry_count();
    const char *env = getenv("TD_SWITCH_MAC_STUB_COUNT");
    if (env && *env) {
        char *endptr = NULL;
        unsigned long parsed = strtoul(env, &endptr, 10);
        if (endptr != env && parsed > 0) {
            if (parsed > total) {
                return (uint32_t)total;
            }
            return (uint32_t)parsed;
        }
    }
    return (uint32_t)total;
}

static int string_equals_ci(const char *lhs, const char *rhs) {
    if (lhs == NULL || rhs == NULL) {
        return 0;
    }

    while (*lhs && *rhs) {
        unsigned char lc = (unsigned char)*lhs;
        unsigned char rc = (unsigned char)*rhs;
        lc = (unsigned char)tolower(lc);
        rc = (unsigned char)tolower(rc);
        if (lc != rc) {
            return 0;
        }
        ++lhs;
        ++rhs;
    }

    return (*lhs == '\0') && (*rhs == '\0');
}

typedef enum {
    LOOKUP_MODE_AUTO = 0,
    LOOKUP_MODE_FORCE_HIT,
    LOOKUP_MODE_FORCE_MISS
} lookup_mode_t;

static lookup_mode_t resolve_lookup_mode(size_t *forced_index) {
    const char *env = getenv("TD_SWITCH_MAC_STUB_LOOKUP");
    if (forced_index) {
        *forced_index = 0U;
    }

    if (env == NULL || *env == '\0') {
        return LOOKUP_MODE_AUTO;
    }

    if (string_equals_ci(env, "miss")) {
        return LOOKUP_MODE_FORCE_MISS;
    }

    if (string_equals_ci(env, "hit")) {
        return LOOKUP_MODE_FORCE_HIT;
    }

    char *endptr = NULL;
    unsigned long parsed = strtoul(env, &endptr, 10);
    if (endptr != env && *endptr == '\0') {
        size_t total = stub_entry_count();
        if (total == 0U) {
            return LOOKUP_MODE_FORCE_MISS;
        }
        size_t idx = (size_t)parsed;
        if (idx >= total) {
            idx = total - 1U;
        }
        if (forced_index) {
            *forced_index = idx;
        }
        return LOOKUP_MODE_FORCE_HIT;
    }

    return LOOKUP_MODE_AUTO;
}

static const SwUcMacEntry *find_stub_entry(const SwUcMacEntry *request) {
    if (request == NULL) {
        return NULL;
    }

    size_t i;
    size_t total = stub_entry_count();
    for (i = 0; i < total; ++i) {
        if (kStubEntries[i].vlan == request->vlan &&
            memcmp(kStubEntries[i].mac, request->mac, sizeof(request->mac)) == 0) {
            return &kStubEntries[i];
        }
    }

    return NULL;
}

static void format_mac(const mac_addr_t mac, char *buffer, size_t len) {
    if (buffer == NULL || len < 18U) {
        return;
    }

    snprintf(buffer,
             len,
             "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0],
             mac[1],
             mac[2],
             mac[3],
             mac[4],
             mac[5]);
}

TD_SWITCH_MAC_WEAK int td_switch_mac_get_capacity(uint32_t *out_capacity) {
    if (out_capacity == NULL) {
        fprintf(stdout, "[switch-mac-stub] td_switch_mac_get_capacity: out_capacity is NULL\n");
        return -EINVAL;
    }

    log_stub_message("stubbed td_switch_mac_get_capacity/td_switch_mac_snapshot active");

    *out_capacity = 1024U;
    g_stub_capacity_hint = 1024U;
    fprintf(stdout, "[switch-mac-stub] returning fixed capacity 1024\n");
    return 0;
}

TD_SWITCH_MAC_WEAK int td_switch_mac_snapshot(SwUcMacEntry *entries, uint32_t *out_count) {
    if (entries == NULL || out_count == NULL) {
        fprintf(stdout, "[switch-mac-stub] td_switch_mac_snapshot: argument is NULL\n");
        return -EINVAL;
    }

    log_stub_message("stubbed td_switch_mac_get_capacity/td_switch_mac_snapshot active");

    uint32_t desired = resolve_stub_count();
    uint32_t cap = g_stub_capacity_hint;
    if (cap == 0U) {
        cap = 1024U;
    }
    if (desired > cap) {
        fprintf(stdout,
                "[switch-mac-stub] capacity hint %u smaller than stub rows %u, truncating\n",
                cap,
                desired);
        desired = cap;
    }

    memcpy(entries, kStubEntries, desired * sizeof(SwUcMacEntry));
    *out_count = desired;

    fprintf(stdout, "[switch-mac-stub] returning %u sample rows\n", desired);
    return 0;
}

TD_SWITCH_MAC_WEAK int td_switch_mac_get_ifindex_by_vid(SwUcMacEntry *entry) {
    if (entry == NULL) {
        fprintf(stdout, "[switch-mac-stub] td_switch_mac_get_ifindex_by_vid: entry is NULL\n");
        return -EINVAL;
    }

    log_lookup_stub_message();

    size_t forced_index = 0U;
    lookup_mode_t mode = resolve_lookup_mode(&forced_index);
    const SwUcMacEntry *result = NULL;
    size_t total = stub_entry_count();

    if (mode == LOOKUP_MODE_FORCE_MISS) {
        char mac_str[18];
        format_mac(entry->mac, mac_str, sizeof(mac_str));
        fprintf(stdout,
                "[switch-mac-stub] td_switch_mac_get_ifindex_by_vid forced miss via env: mac=%s vlan=%u rc=%d\n",
                mac_str,
                (unsigned)entry->vlan,
                -ENOENT);
        return -ENOENT;
    }

    if (mode == LOOKUP_MODE_FORCE_HIT) {
        if (total == 0U) {
            fprintf(stdout, "[switch-mac-stub] td_switch_mac_get_ifindex_by_vid: no stub entries available\n");
            return -ENOENT;
        }
        if (forced_index >= total) {
            forced_index = total - 1U;
        }
        result = &kStubEntries[forced_index];
    } else {
        result = find_stub_entry(entry);
    }

    if (result == NULL) {
        char mac_str[18];
        format_mac(entry->mac, mac_str, sizeof(mac_str));
        fprintf(stdout,
                "[switch-mac-stub] td_switch_mac_get_ifindex_by_vid miss: mac=%s vlan=%u rc=%d\n",
                mac_str,
                (unsigned)entry->vlan,
                -ENOENT);
        return -ENOENT;
    }

    memcpy(entry, result, sizeof(SwUcMacEntry));

    {
        char mac_str[18];
        format_mac(entry->mac, mac_str, sizeof(mac_str));
        fprintf(stdout,
                "[switch-mac-stub] td_switch_mac_get_ifindex_by_vid hit: mac=%s vlan=%u ifindex=%u attr=%u\n",
                mac_str,
                (unsigned)entry->vlan,
                (unsigned)entry->ifindex,
                (unsigned)entry->attr);
    }

    return 0;
}
