#ifndef TD_SWITCH_MAC_BRIDGE_H
#define TD_SWITCH_MAC_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct td_switch_mac_entry_t {
    uint8_t mac[6];
    uint16_t vlan;
    uint32_t ifindex;
    uint32_t attr;
} td_switch_mac_entry_t;

int td_switch_mac_get_capacity(uint32_t *out_capacity);
int td_switch_mac_snapshot(td_switch_mac_entry_t *entries, uint32_t *inout_count);

#ifdef __cplusplus
}
#endif

#endif /* TD_SWITCH_MAC_BRIDGE_H */
