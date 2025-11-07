#define _POSIX_C_SOURCE 200112L

#include "td_switch_mac_bridge.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static void test_get_capacity(void) {
    uint32_t capacity = 0;
    int rc = td_switch_mac_get_capacity(&capacity);
    assert(rc == 0);
    assert(capacity >= 1);
}

static void test_snapshot_defaults(void) {
    SwUcMacEntry entries[8];
    uint32_t count = 0;
    int rc = td_switch_mac_snapshot(entries, &count);
    assert(rc == 0);
    assert(count > 0);
    printf("snapshot returned %u rows\n", count);
}

static void test_snapshot_with_limit(void) {
    setenv("TD_SWITCH_MAC_STUB_COUNT", "2", 1);

    SwUcMacEntry entries[2];
    uint32_t count = 0;
    int rc = td_switch_mac_snapshot(entries, &count);
    assert(rc == 0);
    assert(count == 2);

    unsetenv("TD_SWITCH_MAC_STUB_COUNT");
}

static void test_invalid_arguments(void) {
    int rc = td_switch_mac_get_capacity(NULL);
    assert(rc == -EINVAL);

    SwUcMacEntry entries[1];
    rc = td_switch_mac_snapshot(entries, NULL);
    assert(rc == -EINVAL);
}

int main(void) {
    test_invalid_arguments();
    test_get_capacity();
    test_snapshot_defaults();
    test_snapshot_with_limit();
    return 0;
}
