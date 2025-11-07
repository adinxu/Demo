#include "td_switch_mac_bridge.h"

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

static void log_stub_message(const char *message) {
    static int logged = 0;
    if (!logged) {
        fprintf(stdout, "[switch-mac-stub] %s\n", message);
        logged = 1;
    }
}

static uint32_t resolve_stub_count(void) {
    const size_t total = sizeof(kStubEntries) / sizeof(kStubEntries[0]);
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
