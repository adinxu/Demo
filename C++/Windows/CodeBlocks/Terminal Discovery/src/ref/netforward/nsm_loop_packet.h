/******************************************************************************

  Copyright (C), 2001-2011, DCN Co., Ltd.

 ******************************************************************************
  File Name     : nsm_dhcps_sock.h
  Version       : Initial Draft
  Author        : zhanghang 445365
  Created       : 2024/4/15
  Last Modified :
  Description   : DHCP Snooping报文处理
  Function List :
  History       :
  1.Date        : 2024/4/15
    Author      : zhanghang 445365
    Modification: Created file

******************************************************************************/

#ifndef NSM_LOOP_PACKET_H
#define NSM_LOOP_PACKET_H

#ifdef HAVE_L2
#include "nsm_bridge.h"
#include "ptree.h"
#include "avl_tree.h"
#ifdef HAVE_VLAN
#include "nsm_vlan.h"
#endif                                  /* HAVE_VLAN */
#endif  

#define LOOP_DETECT_ASYNC 1
#define LOOP_DETECT_INTERVAL_DEFAULT 30
//#define MAC_NUM_LEN 6
#define LoopdEtype      0x9003        /* 环路检测帧类型 */
#define Loopd_version   1
#define LOOPD_INTERVAL_STEP    1
#define LOOPD_DEFAULT_INTERVAL_TIME 30
#define MAX_VLAN_STR_LEN 8192
typedef char vlan_str_t[MAX_VLAN_STR_LEN];

#define VLAN_UINT32_BITS 32
#define GLB_MIN_VLAN_ID 1
#define GLB_MAX_VLAN_ID 4094
#define _VLAN_MASK_OP(_vidmask, _vid, _op)     \
    ((_vidmask[(int)(_vid) / VLAN_UINT32_BITS]) _op (1U << ((int)(_vid) % VLAN_UINT32_BITS)))
/* test mask bit set */
#define TEST_VLAN_MASK_BIT(vid, mask)          \
    _VLAN_MASK_OP((mask), (vid), &)
#define FOR_EACH_VLAN_EXISTS(_vid, _vidmask) \
		for((_vid) = GLB_MIN_VLAN_ID; (_vid) <= GLB_MAX_VLAN_ID; (_vid)++) \
			if(TEST_VLAN_MASK_BIT(_vid, _vidmask))	

struct loop_detect_client
{
    struct message_handler* mc;
};

/*环路检测动作 */
enum loop_action_type{
    //此枚举也用于上报netconf的环路告警事件，如涉及枚举顺序修改，需要同步修改相关代码
    LOOP_DETECT_ACTION_LOG_ONLY,         /* LOG */
    LOOP_DETECT_ACTION_BLOCK,        /* LOG + 禁止mac学习 + 堵塞端口 */
    LOOP_DETECT_ACTION_SHUTDOWN,     /* LOG + shutdown */
    LOOP_DETECT_ACTION_INVALID
};

struct loopd_pdu_t
{
    u_int8_t        da[ETHER_ADDR_LEN]; /* 01-01-c1-00-00-00 */
    u_int8_t        sa[ETHER_ADDR_LEN];
    //uint16       tpid;  /* 0x8100 */
    //uint16       vlantag;
    u_int16_t       type;  /* 0x9003 */
    u_int16_t       version;
    u_int8_t        switch_mac[ETHER_ADDR_LEN];
/* Added by 413158, 2024/7/9   PN:Modify byte order */
    u_int32_t       port;
    u_int8_t        reserved[40];
};

struct loopd_vlan_timer {
    u_int16_t   transmitTimer;   /* 报文发送计时*/
    u_int16_t   recoverTimer;    /* 环路恢复时间计时 */
};

struct loopd_conf_t{
    bool_t                  loopd_enable;               /* 环路使能状态*/
    struct nsm_vlan_bmp     loopd_vlans;                /* 环路使能vlan */
	u_int32_t               interval;                   /* 环路检测周期 */
    enum loop_action_type   action;                     /* 环路检测动作 */
	enum loop_action_type   action_state;               /* 环路检测状态*/
    //bool_t                bIsDetect;                  /*是否检测到环路 {显示时方便判断}*/
    struct nsm_vlan_bmp     detect_vlans;               /* 检测到环路的vlanmask*/
    u_int32_t               ifindex;                    /* ifindex */
    char                    switchMac[ETHER_ADDR_LEN];  /*设备mac地址ַ*/
    bool_t                  b_loop_detect;              /* 是否检测到环路*/
    struct loopd_vlan_timer vlan_timer[GLB_MAX_VLAN_ID + 1];
    bool_t                  is_enable_vlan_all;
};

struct loopd_glb {
    struct loopd_conf_t loopd_glb_conf;
	struct list* loopd_if_list;            /* 端口list */
	pal_sock_handle_t iSockfd;
    struct thread* pRcv_thread;
	bool_t is_get_mac;
	bool_t is_debug;//控制调试
    bool_t is_enable_vlan_all;
};

pal_sock_handle_t nsm_loop_detect_sock_init(struct lib_globals* zg, struct loopd_glb* pLoopm);
void nsm_loop_detect_sock_exit(struct lib_globals* zg, struct loopd_glb* pLoopm);
void nsm_loop_detect_master_init(struct nsm_master* nm);
void nsm_loop_detect_add_port(struct interface* ifp);
void nsm_loop_detect_del_port(struct interface* ifp);
void nsm_loop_detect_timer_start(struct loopd_glb* pLoopdetect);
void nsm_loop_detect_master_exit(struct nsm_master* nm);
struct loopd_glb* nsm_loop_detect_get_master(void);
void nsm_loop_detect_port_link_update(struct interface* ifp,int up);








#endif /* NSM_LOOP_PACKET_H */
