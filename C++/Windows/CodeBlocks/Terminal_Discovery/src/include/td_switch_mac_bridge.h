#ifndef TD_SWITCH_MAC_BRIDGE_H
#define TD_SWITCH_MAC_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TD_SWITCH_MAC_STATE_T_DEFINED
#define TD_SWITCH_MAC_STATE_T_DEFINED
typedef enum td_switch_mac_state_t {
    TD_SWITCH_MAC_STATE_DYNAMIC = 0,
    TD_SWITCH_MAC_STATE_STATIC = 1,
    TD_SWITCH_MAC_STATE_DELETE = 2,
    TD_SWITCH_MAC_STATE_EMD = 3
} td_switch_mac_state_t;
#endif /* TD_SWITCH_MAC_STATE_T_DEFINED */

#ifndef TD_SWITCH_MAC_ENTRY_T_DEFINED
#define TD_SWITCH_MAC_ENTRY_T_DEFINED

/* 桥接模块应复用静态 SwUcMacEntry 缓冲区与 SDK 交互，再转换填充此结构。布局与 Realtek SwUcMacEntry 对齐。 */
typedef struct td_switch_mac_entry_t {
    uint8_t mac[6];
    uint16_t vlan;
    td_switch_mac_state_t attr;
    uint32_t ifindex;
} td_switch_mac_entry_t;

#endif /* TD_SWITCH_MAC_ENTRY_T_DEFINED */

int td_switch_mac_get_capacity(uint32_t *out_capacity);
int td_switch_mac_snapshot(td_switch_mac_entry_t *entries, uint32_t *inout_count);

#ifdef __cplusplus
}
#endif

#endif /* TD_SWITCH_MAC_BRIDGE_H */
