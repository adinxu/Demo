#ifndef TD_SWITCH_MAC_BRIDGE_H
#define TD_SWITCH_MAC_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef uint8
typedef unsigned char uint8;
#endif

#ifndef uint16
typedef unsigned short uint16;
#endif

#ifndef uint32
typedef unsigned int uint32;
#endif

#ifndef GLB_MAC_ADDR_LEN
#define GLB_MAC_ADDR_LEN 6
#endif

#ifndef mac_addr_t
typedef uint8 mac_addr_t[GLB_MAC_ADDR_LEN];
#endif

#ifndef vlan_id_t
typedef uint16 vlan_id_t;
#endif

#ifndef mac_state_e
typedef enum {
    MAC_STATE_DYNAMIC = 0,
    MAC_STATE_STATIC,
    MAC_STATE_DELETE,
    MAC_STATE_EMD
} mac_state_e;
#endif

#ifndef SwUcMacEntry
typedef struct __SwUcMacEntry {
    mac_addr_t mac;
    vlan_id_t vlan;
    mac_state_e attr;
    uint32 ifindex;
} SwUcMacEntry;
#endif

int td_switch_mac_get_capacity(uint32_t *out_capacity);
int td_switch_mac_snapshot(SwUcMacEntry *entries, uint32_t *inout_count);

#ifdef __cplusplus
}
#endif

#endif /* TD_SWITCH_MAC_BRIDGE_H */
