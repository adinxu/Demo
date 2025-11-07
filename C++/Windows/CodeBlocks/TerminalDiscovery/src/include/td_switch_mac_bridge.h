#ifndef TD_SWITCH_MAC_BRIDGE_H
#define TD_SWITCH_MAC_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TD_SWITCH_MAC_FALLBACK_TYPES_DEFINED
#define TD_SWITCH_MAC_FALLBACK_TYPES_DEFINED

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;

#ifndef GLB_MAC_ADDR_LEN
#define GLB_MAC_ADDR_LEN 6
#endif

typedef uint8 mac_addr_t[GLB_MAC_ADDR_LEN];
typedef uint16 vlan_id_t;

typedef enum {
    MAC_STATE_DYNAMIC = 0,
    MAC_STATE_STATIC,
    MAC_STATE_DELETE,
    MAC_STATE_EMD
} mac_state_e;

typedef struct __SwUcMacEntry {
    mac_addr_t mac;
    vlan_id_t vlan;
    mac_state_e attr;
    uint32 ifindex;
} SwUcMacEntry;

#endif /* TD_SWITCH_MAC_FALLBACK_TYPES_DEFINED */

int td_switch_mac_get_capacity(uint32_t *out_capacity);
int td_switch_mac_snapshot(SwUcMacEntry *entries, uint32_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* TD_SWITCH_MAC_BRIDGE_H */
