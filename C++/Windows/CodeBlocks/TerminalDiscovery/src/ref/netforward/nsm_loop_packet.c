/******************************************************************************

  Copyright (C), 2001-2011, DCN Co., Ltd.

 ******************************************************************************
  File Name     : nsm_loop_packet.c
  Version       : Initial Draft
  Author        : 404775
  Created       : 2024/6/21
  Last Modified :
  Description   : nsm loop packet
  Function List :
              loopdetect_sock_handle_packet
              loopd_action
              loopd_poagTick
              loopd_rx_timer_cb
              loopd_tx_timer_cb
              Loopd_transmit
              loop_detect_sock_async_client_read
              loop_detect_sock_client_connect
              loop_detect_sock_client_create
              loop_detect_sock_client_disconnect
              loop_detect_sock_client_init
              loop_detect_sock_client_reconnect
              loop_detect_sock_reconnect
              loop_key_ifindex_cmp
              nsm_loop_detect_add_port
              nsm_loop_detect_del_port
              nsm_loop_detect_get_master
              nsm_loop_detect_master_exit
              nsm_loop_detect_master_init
              nsm_loop_detect_port_link_update
              nsm_loop_detect_sock_exit
              nsm_loop_detect_sock_init
              nsm_loop_detect_timer_start
  History       :
  1.Date        : 2024/6/21
    Author      : 404775
    Modification: Created file

******************************************************************************/
#include "pal.h"


#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <errno.h>
#include <sys/types.h>
#include <sys/inotify.h>

#include "config.h"
#include "avl_tree.h"
#include "lib.h"
#include "thread.h"
#include "message.h"
#include "nsmd.h"
#include "lib/L2/l2_timer.h"
#include "nsm_errdisd.h"

#include "parser_utils.h"
#include "nsm_interface.h"


#ifdef HAVE_USER_HSL
#include "message.h"
#include "network.h"
#endif /* HAVE_USER_HSL */

#include "nsm_vlan.h"
#include "nsm_loop_packet.h"
#include "nsm_api.h" 


#ifdef HAVE_CMLD
#include "ipi-loop-detect_auto_notif_api.h"
#endif /* HAVE_CMLD */
static pal_sock_handle_t iSockfd = -1;

#ifdef HAVE_USER_HSL

/*   Asynchronous messages. */
struct loop_detect_client g_loop_detect_link_async = { NULL };

#endif /* HAVE_USER_HSL */


void loopd_poagTick(struct loopd_conf_t *pLoop_detect_port,u_int16_t vid);
static void Loopd_transmit(u_int32_t ifindex, u_int16_t vid);
static int loopd_action(u_int32_t ifindex, enum loop_action_type loopd_action);

struct loopd_glb* nsm_loop_detect_get_master(void)
{
    struct nsm_master* pstNsmMaster = NULL;
    struct loopd_glb* pLoopdetect = NULL;

    pstNsmMaster = nsm_master_lookup_by_id(nzg, 0);
    if (NULL == pstNsmMaster)
    {
        return NULL;
    }

    pLoopdetect = pstNsmMaster->loopdetectm;
    if (NULL == pLoopdetect)
    {
        return NULL;
    }

    return pLoopdetect;
}

void nsm_loop_detect_add_port(struct interface* ifp)
{
    struct loopd_glb*    pLoop_detect = NULL;
	struct loopd_conf_t* pLoop_port = NULL;

	zlog_debug(nzg, "add port ifindex %u", ifp->ifindex);
    pLoop_detect = nsm_loop_detect_get_master();
    if (NULL == pLoop_detect)
    {
        zlog_err(nzg, "pLoop_detect get failed");
		return;
    }
	
    pLoop_port = XCALLOC(MTYPE_NSM_LOOP_PORT, sizeof(struct loopd_conf_t));
    if (NULL == pLoop_port)
    {
        return;
    }
	NSM_VLAN_BMP_INIT(pLoop_port->detect_vlans);
	pLoop_port->ifindex = ifp->ifindex;
	pLoop_port->action = LOOP_DETECT_ACTION_SHUTDOWN;
   (void) listnode_add_sort(pLoop_detect->loopd_if_list, pLoop_port);
    return;
}

void nsm_loop_detect_del_port(struct interface* ifp)
{
    struct loopd_glb*    pLoop_detect = NULL;
	struct loopd_conf_t* pLoop_port = NULL;
	struct listnode* node = NULL;
	struct listnode* node_next = NULL;
	
	zlog_debug(nzg, "del port ifindex %u", ifp->ifindex);
    pLoop_detect = nsm_loop_detect_get_master();
	if (NULL == pLoop_detect)
    {
        zlog_err(nzg, "pLoop_detect get failed");
		return;
    }
    LIST_LOOP_DEL(pLoop_detect->loopd_if_list, pLoop_port, node, node_next)
    {
		if(ifp->ifindex == pLoop_port->ifindex)
		{
			listnode_delete(pLoop_detect->loopd_if_list, pLoop_port);
			XFREE(MTYPE_NSM_LOOP_PORT,pLoop_port);
		}
    }
    return;
}

void nsm_loop_detect_port_link_update(struct interface* ifp,int up)
{
	struct loopd_glb* pLoopdetect = NULL;
	struct loopd_conf_t* pLoop_port = NULL;
	struct listnode* node = NULL;
	
	struct nsm_if*          zif = NULL;
	struct nsm_bridge_port* br_port   = NULL;
	struct nsm_vlan_port*   vlan_port = NULL;
	u_int16_t vlan_id = 0;
	struct nsm_vlan_bmp conf_vlanmask = {0};
	bool_t bIsTrans = PAL_FALSE;
	bool_t bFindFlag = PAL_FALSE;

	zlog_debug(nzg,"ifp ifindex %u link change %u",ifp->ifindex,up);
	pLoopdetect = nsm_loop_detect_get_master();
    if (NULL == pLoopdetect)
    {
        zlog_err(nzg,"pLoopdetect get failed");
		return;
    }
	LIST_LOOP(pLoopdetect->loopd_if_list, pLoop_port, node)
    {
		if(ifp->ifindex == pLoop_port->ifindex)
		{
			bFindFlag = PAL_TRUE;
			break;
		}
	}
	/* BEGIN: Added by 404775, 2024/7/24   PN:solve problem */
	if(!bFindFlag)
	{
		return;
	}
	if(NULL == pLoop_port)
	{
		zlog_err(nzg,"pLoop_port %u get failed",pLoop_port->ifindex);
		return;
	}
    /* BEGIN: Modified by 404775, 2025/8/5   PN:DTS005611237 */
    pal_memset(pLoop_port->vlan_timer, 0, (GLB_MAX_VLAN_ID + 1) * sizeof(struct loopd_vlan_timer));
	if(TRUE == pLoop_port->b_loop_detect)
	{
#ifdef HAVE_CMLD
        smi_auto_loop_detect_notification_publish_notif(
            NSM_ZG, ifp->name, LOOP_DETECT_ACTION_LOG_ONLY, NTF_SEVERITY_MINOR);
#endif /* HAVE_CMLD */
	
		pLoop_port->b_loop_detect = FALSE;
		loopd_action(pLoop_port->ifindex, LOOP_DETECT_ACTION_LOG_ONLY);
		pLoop_port->action_state = LOOP_DETECT_ACTION_LOG_ONLY;
		zlog_critical(nzg, "all loops recovered on %s.", ifp->name);

	}
	NSM_VLAN_BMP_INIT(pLoop_port->detect_vlans);
    /* END:   Modified by 404775, 2025/8/5   PN:DTS005611237 */
    if (up == NSM_IF_UP)
	{
/* BEGIN: Added by 413158, 2024/7/11   PN:Distinguish packet sending between aggregation group and normal port */
		if (NSM_INTF_TYPE_AGGREGATOR(ifp) || NSM_INTF_TYPE_AGGREGATED(ifp))
		{
			return ;
		}	
/* END:   Added by 413158, 2024/7/11   PN:Distinguish packet sending between aggregation group and normal port */

		/* 端口使能 */
        if(PAL_TRUE == pLoop_port->loopd_enable)
        {
            bIsTrans = TRUE;
            memcpy(conf_vlanmask.bitmap, pLoop_port->loopd_vlans.bitmap, sizeof(struct nsm_vlan_bmp));
        }
        else if(PAL_TRUE == pLoopdetect->loopd_glb_conf.loopd_enable)
        {
            bIsTrans = TRUE;
            memcpy(conf_vlanmask.bitmap, pLoopdetect->loopd_glb_conf.loopd_vlans.bitmap, sizeof(struct nsm_vlan_bmp));
        }
		if(bIsTrans)
		{
			/* 遍历vlanid发包 */
	        /* 端口在vlan内且环路使能对应vlan才发 */
			zif = (struct nsm_if*)ifp->info;
		    if (zif == NULL || zif->switchport == NULL)
			{
				return ;
			}
		    br_port   = zif->switchport;
		    vlan_port = &br_port->vlan_port;
	        for(vlan_id=GLB_MIN_VLAN_ID;vlan_id <= GLB_MAX_VLAN_ID;vlan_id++)
	        {
	            if((NSM_VLAN_BMP_IS_MEMBER(vlan_port->staticMemberBmp, vlan_id)) &&
	                (NSM_VLAN_BMP_IS_MEMBER(conf_vlanmask,vlan_id)))
	            {
					Loopd_transmit(pLoop_port->ifindex, vlan_id);
					pLoop_port->vlan_timer[vlan_id].transmitTimer = 0;
	            }
	        }
		}
	}

    return;
}

s_int32_t loop_key_ifindex_cmp(void* m1, void* m2)
{
    struct loopd_conf_t* pLoop_detect_port1 = (struct loopd_conf_t*)m1;
    struct loopd_conf_t* pLoop_detect_port2 = (struct loopd_conf_t*)m2;
    u_int32_t ifindex1;
    u_int16_t ifindex2;

    if ((NULL == pLoop_detect_port1) || (NULL == pLoop_detect_port2))
    {
        return 0;
    }

    ifindex1 = pLoop_detect_port1->ifindex;
    ifindex2 = pLoop_detect_port2->ifindex;

    return ((ifindex1 < ifindex2) ? -1 : 1);
}

void nsm_loop_detect_master_init(struct nsm_master* nm)
{
    struct loopd_glb* pLoop_detect = NULL;
    char*             addr         = NULL;

    if (NULL != nm->loopdetectm)
    {
		zlog_err(nzg,"loopdetectm is null");
		return;
    }
	
    pLoop_detect = XCALLOC(MTYPE_NSM_LOOP_GLB, sizeof(struct loopd_glb));
    if (NULL == pLoop_detect)
    {
        return;
    }

    nm->loopdetectm = pLoop_detect;

	pLoop_detect->loopd_glb_conf.interval = LOOP_DETECT_INTERVAL_DEFAULT;
	pLoop_detect->loopd_glb_conf.action = LOOP_DETECT_ACTION_SHUTDOWN;
	NSM_VLAN_BMP_INIT(pLoop_detect->loopd_glb_conf.detect_vlans);
	NSM_VLAN_BMP_INIT(pLoop_detect->loopd_glb_conf.loopd_vlans);
	pLoop_detect->iSockfd = -1;

    pLoop_detect->loopd_if_list = list_create(loop_key_ifindex_cmp, NULL);		
    if (NULL == pLoop_detect->loopd_if_list)
    {
        XFREE(MTYPE_NSM_LOOP_GLB, pLoop_detect);
        return;
    }

    addr = LIB_plat_info.base_mac;
    if(addr)
    {
        pal_mem_cpy(pLoop_detect->loopd_glb_conf.switchMac, addr, ETHER_ADDR_LEN);
        pLoop_detect->is_get_mac = TRUE;
    }
    return;
}
void nsm_loop_detect_master_exit(struct nsm_master* nm)
{
     struct loopd_glb*    pLoop_detect = NULL;

    if ((NULL == nm) || (NULL == nm->loopdetectm))
    {
        return;
    }

    pLoop_detect = nm->loopdetectm;

    XFREE(MTYPE_NSM_LOOP_GLB, pLoop_detect);

    nm->loopdetectm = NULL;

    return;
}


/* 010f-e200-0007 */
char g_loopd_dmac[ETHER_ADDR_LEN] = {0x01, 0x01, 0xc1, 0x00, 0x00, 0x00};
static int loopd_action(u_int32_t ifindex, enum loop_action_type loopd_action)
{
    int                         ret_code = NSM_SUCCESS;
	struct ipi_vr*              vr;
	struct nsm_master*          nm = NULL;
	struct nsm_bridge_master*   master = NULL;
	struct interface*           ifp; 
	struct nsm_bridge*          bridge;

	struct nsm_if*     nif;
	vr = ipi_vr_get_privileged(nzg);
	nm = vr->proto;
	if(nm)
	{
		master = nsm_bridge_get_master(nm);
	}
	if (!master)
	{
		return NSM_FAILURE;
	}
	bridge = nsm_get_default_bridge(master);
    if (!bridge)
	{
		return NSM_BRIDGE_ERR_NOTFOUND;
	}
    zlog_debug(nzg, "Port %u recv loop detect packet action %u", ifindex, loopd_action);
	ifp = if_lookup_by_index(&vr->ifm, ifindex);
	if (!ifp)
	{
		return NSM_FAILURE;
	}
    nif = (struct nsm_if*)ifp->info;
    if (NULL == nif || NULL == nif->switchport) {
        return NSM_FAILURE;
    }
    switch(loopd_action)
    {
        case LOOP_DETECT_ACTION_LOG_ONLY:
            if (NSM_BRIDGE_LOOPBACK_DETECTION == nif->switchport->instance_port.module) {
                nsm_bridge_set_port_state(master, bridge->name, ifp, NSM_BRIDGE_PORT_STATE_FORWARDING, 0);
            }
            break;
        case LOOP_DETECT_ACTION_BLOCK:
            if (NSM_BRIDGE_LOOPBACK_DETECTION == nif->switchport->instance_port.module) {
                nsm_bridge_set_port_state(master, bridge->name, ifp, NSM_BRIDGE_PORT_STATE_BLOCKING, 0);
                /* BEGIN: Modified by 404775, 2025/8/18   PN:解决问题单DTS005637243 */
                hal_bridge_flush_fdb_by_port(bridge->name, ifp->ifindex, -1, 0, 0);
            }
            break;
        case LOOP_DETECT_ACTION_SHUTDOWN:
            if (CHECK_FLAG(ifp->flags, IFF_UP)) {
			    nsm_if_flag_up_unset(ifp->vr->id, ifp->name, PAL_TRUE);
                SET_FLAG(nif->errDisReason, NSM_ERRDIS_LOOP_DETECT);
                nif->isErrDisabled = PAL_TRUE;
                if(nif->errdisable_timer)
                {
                    l2_stop_timer(&nif->errdisable_timer);
                }
                nif->errdisable_timer = l2_start_timer(
                    nsm_errdisable_timer_handler, ifp, nm->errdis->errdisable_timeout_interval  * L2_TIMER_SCALE_FACT, nzg);
            }
			break;
    	default:
			zlog_debug(nzg, "loopd action %d is not support", loopd_action);
    		ret_code = NSM_FAILURE;
    		break;
    }

    return ret_code;
}

static s_int32_t loopdetect_sock_handle_packet(struct sockaddr_vlan* psL2_skaddr, u_char* pucBuf, s_int32_t iBufLen)
{
	struct loopd_pdu_t *loopd_pdu = NULL;
	u_int16_t               vid                 = 0;
    u_int32_t               inPort_ifindex      = 0;
    u_int32_t               outPort_ifindex     = 0;
	u_int32_t               handlePort_ifindex  = 0;
	struct listnode*        node                = NULL;
	struct loopd_conf_t*    pLoop_port          = NULL;
	struct loopd_conf_t*    pPort               = NULL;
	u_int8_t                conf_action         = LOOP_DETECT_ACTION_SHUTDOWN;
	struct loopd_glb*       pLoopdetect         = NULL;
    struct interface*       ifp                 = NULL;
    struct ipi_vr*          vr                  = ipi_vr_get_privileged(nzg);
	
	if((NULL == psL2_skaddr) || (NULL == pucBuf))
    {
        return RESULT_ERROR;
    }

	pLoopdetect = nsm_loop_detect_get_master();
    if (NULL == pLoopdetect)
    {
        zlog_err(nzg, "pLoopdetect get failed");
		return RESULT_ERROR;
    }
	#if 0
	u_int32_t i =0;
	for(i =0;i<iBufLen;i++)
	{
		if(0 == (i%16))
		{
			CONSOLE_PRINT_DBG("%02X",pucBuf[i]);
		}
		else if(15 == (i%16))
		{
			CONSOLE_PRINT_DBG(" %02X",pucBuf[i]);
			CONSOLE_PRINT_DBG("\n");
		}
		else
		{
			CONSOLE_PRINT_DBG(" %02X",pucBuf[i]);
		}		
	}
	CONSOLE_PRINT_DBG("\n");
	#endif

    zlog_debug(nzg,"enter loopdetect_sock_handle_packet");

	//todo pkt offset
	vid = psL2_skaddr->vlanid;
    inPort_ifindex = psL2_skaddr->port;
	loopd_pdu = (struct loopd_pdu_t *)pucBuf;
/* Added by 413158, 2024/7/9   PN:Modify byte order */
    outPort_ifindex = ntohl(loopd_pdu->port);
	zlog_debug(nzg,"inPort_ifindex:%u,outPort_ifindex:%u",inPort_ifindex,outPort_ifindex);
    handlePort_ifindex = (inPort_ifindex > outPort_ifindex ? inPort_ifindex : outPort_ifindex);
	LIST_LOOP(pLoopdetect->loopd_if_list, pPort, node)
    {
        if (pPort->ifindex == handlePort_ifindex)
        {
            pLoop_port = pPort;
            ifp = if_lookup_by_index(&vr->ifm, pLoop_port->ifindex);
            if (NULL == ifp) {
                zlog_debug(nzg,"Interface is NULL");
                return RESULT_ERROR;
            }

            if(!if_is_running(ifp)) {
                zlog_debug(nzg,"Interface %s state is down", ifp->name);
                return RESULT_OK;
            }
            break;
        }
    }
	if(!pLoop_port)
	{
		zlog_err(nzg, "[%s.%d]:pLoop_port get failed\n",__FUNCTION__, __LINE__);
		return RESULT_ERROR;
	}
	if(memcmp(loopd_pdu->da, g_loopd_dmac, sizeof(loopd_pdu->da)) == 0 &&     /* Proper DST */
        memcmp(loopd_pdu->sa, pLoopdetect->loopd_glb_conf.switchMac, sizeof(loopd_pdu->sa)) == 0 && /* Proper SRC */
        loopd_pdu->type == htons(LoopdEtype) &&                          /* Proper type */
        loopd_pdu->version == htons(Loopd_version))
    {
        pLoop_port->b_loop_detect = TRUE;
        //zlog_debug(nzg, "Port %u vlan %u recv loopd packet.", pLoop_port->ifindex,vid);
		
    }
	
	if(LOOP_DETECT_ACTION_SHUTDOWN != pLoop_port->action)
    {
        conf_action = pLoop_port->action;
    }
    else if(LOOP_DETECT_ACTION_SHUTDOWN !=  pLoopdetect->loopd_glb_conf.action)
    {
        conf_action = pLoopdetect->loopd_glb_conf.action;
    }
	
    if(TRUE == pLoop_port->b_loop_detect)
    {
        if((vid >= GLB_MIN_VLAN_ID) && (vid <= GLB_MAX_VLAN_ID))
        {
            pLoop_port->vlan_timer[vid].recoverTimer = 0;
        }
        else 
        {
            zlog_err(nzg, "loopdetect_sock_handle_packet get vid %d failed", vid);
		    return RESULT_ERROR;
        }

        if((pLoop_port->action_state == conf_action) &&
            (NSM_VLAN_BMP_IS_MEMBER(pLoop_port->detect_vlans,vid)))
        {
			zlog_debug(nzg, "loopd %u state %d is same", pLoop_port->ifindex, pLoop_port->action_state);
            return RESULT_OK;
        }
		zlog_critical(nzg, "loop detect on %s in vlan %u.", ifp->name,vid);
        NSM_VLAN_BMP_SET(pLoop_port->detect_vlans,vid);
		/* BEGIN: Added by 404775, 2024/7/24   PN:avoid repeat action hw*/
        if(pLoop_port->action_state != conf_action)
        {
#ifdef HAVE_CMLD
            smi_auto_loop_detect_notification_publish_notif(
                NSM_ZG, ifp->name, conf_action, NTF_SEVERITY_MINOR);
#endif /* HAVE_CMLD */
        
        	pLoop_port->action_state = conf_action;
            loopd_action(handlePort_ifindex, conf_action);
        }
    }
	return RESULT_OK;
}

#if 0
/*****************************************************************************
 Prototype    : dhcpsnoop_sock_packet_recv
 Description  :
 Input        : struct thread* thread
 Output       : None
 Return Value :
 Calls        :
 Called By    :

  History        :
  1.Date         : 2024/5/12
    Author       : zhanghang 445365
    Modification : Created function

*****************************************************************************/
s_int32_t loopdetect_sock_packet_recv(struct thread* thread)
{
    s_int32_t ret = 0;
    struct sockaddr_vlan l2_skaddr;
    pal_socklen_t        fromlen  = sizeof(struct sockaddr_vlan);
    u_int8_t             data[RCV_BUFSIZ];
    struct lib_globals* zg = thread->zg;
    struct ipi_vr*       vr       = ipi_vr_get_privileged(zg);
    struct nsm_master* nsm = vr->proto;
    loopd_glb* pLoopdetect = nsm->loopdetectm;

    if (iSockfd < 0)
    {
        zlog_err(zg, "PDU[RECV]: socket is not open (%d)", iSockfd);
        return -1;
    }

    pal_mem_set(data, 0, RCV_BUFSIZ);

    ret = pal_sock_recvfrom(iSockfd, (char*)data, RCV_BUFSIZ, MSG_TRUNC, (struct pal_sockaddr*)&l2_skaddr, &fromlen);
    if (ret >= 0)
    {
        loopdetect_sock_handle_packet(zg, pLoopdetect, &l2_skaddr, data, ret);
    }

    pLoopdetect->pRcv_thread = thread_add_read_high(zg, loopdetect_sock_packet_recv, NULL, iSockfd);

    return ret;
}
#endif
#ifdef HAVE_USER_HSL
static void Loopd_transmit(u_int32_t ifindex, u_int16_t vid)
{
    int iRet;
    u_int32_t pktlen = 0;
	u_int8_t  send_buf[RCV_BUFSIZ] = { 0 };
	struct sockaddr_vlan l2_skaddr;
	struct loopd_glb* pLoopdetect = NULL;
	struct loopd_pdu_t loopd_tx;
	u_int16_t            tolen;
	
    zlog_debug(nzg, "ifindex %u send loop detect packet in vlan %u", ifindex, vid);
	
	pLoopdetect = nsm_loop_detect_get_master();
    if (NULL == pLoopdetect)
    {
        zlog_err(nzg, "pLoopdetect get failed\n");
		return;
    }
    pktlen = sizeof(struct loopd_pdu_t);
    memset(&loopd_tx, 0, pktlen);

    memcpy(loopd_tx.da, g_loopd_dmac, ETHER_ADDR_LEN);
    memcpy(loopd_tx.sa, pLoopdetect->loopd_glb_conf.switchMac , ETHER_ADDR_LEN);

    loopd_tx.type = htons(LoopdEtype);
    loopd_tx.version = htons(Loopd_version);
    memcpy(loopd_tx.switch_mac, pLoopdetect->loopd_glb_conf.switchMac, ETHER_ADDR_LEN);
/* Added by 413158, 2024/7/9   PN: Modify byte order */
	loopd_tx.port = htonl(ifindex);

    pal_mem_set(&l2_skaddr, 0, sizeof(struct sockaddr_vlan));

    /* Fill out the l2_skaddr structure here */
    l2_skaddr.port   = ifindex;
    l2_skaddr.vlanid = vid;
    pal_mem_cpy(l2_skaddr.dest_mac, g_loopd_dmac, ETHER_ADDR_LEN);
    pal_mem_cpy(l2_skaddr.src_mac, pLoopdetect->loopd_glb_conf.switchMac, ETHER_ADDR_LEN);
	tolen = pktlen + sizeof(struct sockaddr_vlan);
    l2_skaddr.length = tolen;
	

    pal_mem_cpy(send_buf, &l2_skaddr, sizeof(struct sockaddr_vlan));
    pal_mem_cpy((send_buf + sizeof(struct sockaddr_vlan)), &loopd_tx, pktlen);
	iRet = writen(g_loop_detect_link_async.mc->sock, send_buf, tolen);
	if (tolen != iRet)
    {
        zlog_err(nzg,"tx Packet Failed. IfIndex[%d]", ifindex);
    }
    return;
}

void loopd_poagTick(struct loopd_conf_t *pLoop_detect_port,u_int16_t vid)
{
	struct loopd_glb* pLoopdetect = NULL;

	pLoopdetect = nsm_loop_detect_get_master();
    if (NULL == pLoopdetect)
    {
        zlog_err(nzg,"pLoopdetect get failed");
		return;
    }
    
    pLoop_detect_port->vlan_timer[vid].transmitTimer++;
    if(pLoopdetect->loopd_glb_conf.interval <= pLoop_detect_port->vlan_timer[vid].transmitTimer)
    {
        Loopd_transmit(pLoop_detect_port->ifindex, vid);
        pLoop_detect_port->vlan_timer[vid].transmitTimer = 0;
    }

    return;
}

int loopd_rx_timer_cb(struct thread* t)
{
    struct loopd_glb*       pLoopdetect = (struct loopd_glb*)t->arg;
	struct loopd_conf_t*    pLoop_port  = NULL;
	struct listnode*        node        = NULL;
	struct ipi_vr*          vr          = ipi_vr_get_privileged(nzg);
	struct interface*       ifp         = NULL;
    u_int32_t               interval_recover    = 0;
	u_int16_t               vlan_id             = 0;

    interval_recover = 3 * pLoopdetect->loopd_glb_conf.interval;

    LIST_LOOP(pLoopdetect->loopd_if_list, pLoop_port, node)
    {
		ifp = if_lookup_by_index(&vr->ifm, pLoop_port->ifindex);
	    if (NULL == ifp)
	    {
	        return RESULT_ERROR;
	    }

        if(TRUE == pLoop_port->b_loop_detect)
        {
            NSM_VLAN_SET_BMP_ITER_BEGIN(pLoop_port->detect_vlans, vlan_id)    
            { 
                pLoop_port->vlan_timer[vlan_id].recoverTimer++;
                if(pLoop_port->vlan_timer[vlan_id].recoverTimer >= interval_recover)
                {
                    pLoop_port->vlan_timer[vlan_id].recoverTimer = 0;
                    NSM_VLAN_BMP_UNSET(pLoop_port->detect_vlans, vlan_id);
                    if(NSM_VLAN_BMP_IS_NULL(pLoop_port->detect_vlans)) 
                    {
                        pLoop_port->b_loop_detect = FALSE;
                        if(LOOP_DETECT_ACTION_LOG_ONLY != pLoop_port->action_state) 
                        {
#ifdef HAVE_CMLD
                            smi_auto_loop_detect_notification_publish_notif(
                                NSM_ZG, ifp->name, LOOP_DETECT_ACTION_LOG_ONLY, NTF_SEVERITY_MINOR);
#endif /* HAVE_CMLD */
                        
                            loopd_action(pLoop_port->ifindex, LOOP_DETECT_ACTION_LOG_ONLY);
                            pLoop_port->action_state = LOOP_DETECT_ACTION_LOG_ONLY;
                        } 
                    }
                    zlog_critical(nzg,"loop detect on %s recovered in vlan %u.", ifp->name, vlan_id);
                }
            }
            NSM_VLAN_SET_BMP_ITER_END(pLoop_port->detect_vlans, vlan_id); 
        }
    }

    thread_add_timer(nzg,loopd_rx_timer_cb,pLoopdetect,LOOPD_INTERVAL_STEP);
    return 0;
}

int  loopd_tx_timer_cb(struct thread* t)
{
    struct loopd_glb*       pLoopdetect = (struct loopd_glb*)t->arg;
	struct loopd_conf_t*    pLoop_port  = NULL;
	struct listnode*        node        = NULL;
	struct ipi_vr*          vr          = ipi_vr_get_privileged(nzg);
	struct interface*       ifp         = NULL;
	bool_t                  bIsTrans    = PAL_FALSE;
	struct nsm_vlan_bmp     conf_vlanmask       = {0};
	struct nsm_if*          zif                 = NULL;
	struct nsm_bridge_port* br_port             = NULL;
	struct nsm_vlan_port*   vlan_port           = NULL;
	u_int16_t               vlan_id             = 0;

	LIST_LOOP(pLoopdetect->loopd_if_list, pLoop_port, node)
    {
    	bIsTrans = FALSE;
		ifp = if_lookup_by_index(&vr->ifm, pLoop_port->ifindex);
	    if (NULL == ifp)
	    {
	        return RESULT_ERROR;
	    }
		if(!if_is_running(ifp))
        {
            continue;
        }

        /* if agg member ,return*/
        if(NSM_INTF_TYPE_AGGREGATED(ifp))
        {
            continue;
        }
        
		/* 端口使能 */
        if(PAL_TRUE == pLoop_port->loopd_enable)
        {
            bIsTrans = TRUE;
            memcpy(conf_vlanmask.bitmap, pLoop_port->loopd_vlans.bitmap, sizeof(struct nsm_vlan_bmp));
        }
        else if(PAL_TRUE == pLoopdetect->loopd_glb_conf.loopd_enable)
        {
            bIsTrans = TRUE;
            memcpy(conf_vlanmask.bitmap, pLoopdetect->loopd_glb_conf.loopd_vlans.bitmap, sizeof(struct nsm_vlan_bmp));
        }
		if(TRUE == bIsTrans)
        {
            /* 遍历vlanid发包 */
            /* 端口在vlan�?�?环路使能对应vlan才发�?*/
			zif = (struct nsm_if*)ifp->info;
		    if (zif == NULL || zif->switchport == NULL)
			{
				return 0;
			}
		    br_port   = zif->switchport;
		    vlan_port = &br_port->vlan_port;
            for(vlan_id = GLB_MIN_VLAN_ID; vlan_id <= GLB_MAX_VLAN_ID; vlan_id++)
            {
                if((NSM_VLAN_BMP_IS_MEMBER(vlan_port->staticMemberBmp, vlan_id)) && (NSM_VLAN_BMP_IS_MEMBER(conf_vlanmask,vlan_id)))
                {
                    loopd_poagTick(pLoop_port, vlan_id);
                }
            }
        }
    }

	thread_add_timer(nzg,loopd_tx_timer_cb,pLoopdetect,LOOPD_INTERVAL_STEP);
    return 0;
}

void nsm_loop_detect_timer_start(struct loopd_glb* pLoopdetect)
{
    thread_add_timer(nzg,loopd_rx_timer_cb,pLoopdetect,LOOPD_INTERVAL_STEP);
	thread_add_timer(nzg,loopd_tx_timer_cb,pLoopdetect,LOOPD_INTERVAL_STEP);
    return;
}

int loop_detect_sock_async_client_read(struct message_handler* mc, struct message_entry* me, pal_sock_handle_t sock)
{
    s_int32_t            nbytes          = 0;
    u_char               buf[RCV_BUFSIZ] = { 0 };
    struct sockaddr_vlan vlan_skaddr;
    s_int32_t            rem_msg_len;
    s_int32_t iRet = RESULT_OK;

    pal_mem_set(&vlan_skaddr, 0, sizeof(struct sockaddr_vlan));

    /* Peek at least Control Information */
    nbytes = pal_sock_recvfrom(sock, (void*)&vlan_skaddr, sizeof(struct sockaddr_vlan), MSG_PEEK, NULL, NULL);

    if (nbytes <= 0)
    {
        return nbytes;
    }

    rem_msg_len = vlan_skaddr.length;
    do {
        if (rem_msg_len > RCV_BUFSIZ)
        {
            nbytes = pal_sock_read(sock, buf, RCV_BUFSIZ);
        }
        else
        {
            nbytes = pal_sock_read(sock, buf, rem_msg_len);
        }

        if (nbytes <= 0)
        {
            return nbytes;
        }
        rem_msg_len = rem_msg_len - nbytes;

    } while (rem_msg_len > 0);

    if (vlan_skaddr.length > RCV_BUFSIZ)
    {
        return nbytes;
    }

    /* Decode the raw packet and get src mac and dest mac */
    pal_mem_cpy(&vlan_skaddr, buf, sizeof(struct sockaddr_vlan));


    iRet = loopdetect_sock_handle_packet(&vlan_skaddr, (buf + sizeof(struct sockaddr_vlan)), (nbytes - sizeof(struct sockaddr_vlan)));
    if (RESULT_OK != iRet)
    {
        zlog_err(nzg,"handle Packet Failed");
    }

    return nbytes;
}

/*****************************************************************************
 Prototype    : dhcpsnoop_sock_client_connect
 Description  :
 Input        : struct message_handler* mc
                struct message_entry* me
                pal_sock_handle_t sock
 Output       : None
 Return Value :
 Calls        :
 Called By    :

  History        :
  1.Date         : 2024/5/12
    Author       : zhanghang 445365
    Modification : Created function

*****************************************************************************/
int loop_detect_sock_client_connect(struct message_handler* mc, struct message_entry* me, pal_sock_handle_t sock)
{
    int             ret;
    struct preg_msg preg;

    /* Make the client socket blocking. */
    pal_sock_set_nonblocking(sock, PAL_FALSE);

    /* Register read thread.  */
    if ((struct loop_detect_client*)mc->info == &g_loop_detect_link_async)
        message_client_read_register(mc);

    preg.len   = MESSAGE_REGMSG_SIZE;
    preg.value = HAL_SOCK_PROTO_LOOP_DETECT;

    /* Encode protocol identifier and send to HSL server */
    ret = pal_sock_write(sock, &preg, MESSAGE_REGMSG_SIZE);
    if (ret <= 0)
    {
        return RESULT_ERROR;
    }

    return RESULT_OK;
}

/*****************************************************************************
 Prototype    : dhcpsnoop_sock_reconnect
 Description  :
 Input        : struct thread* t
 Output       : None
 Return Value : static
 Calls        :
 Called By    :

  History        :
  1.Date         : 2024/5/12
    Author       : zhanghang 445365
    Modification : Created function

*****************************************************************************/
static int loop_detect_sock_reconnect(struct thread* t)
{
    struct message_handler* mc;

    mc            = THREAD_ARG(t);
    mc->t_connect = NULL;
    message_client_start(mc);
    return 0;
}

/*****************************************************************************
 Prototype    : dhcpsnoop_sock_client_reconnect
 Description  :
 Input        : struct message_handler* mc
 Output       : None
 Return Value : static
 Calls        :
 Called By    :

  History        :
  1.Date         : 2024/5/12
    Author       : zhanghang 445365
    Modification : Created function

*****************************************************************************/
static int loop_detect_sock_client_reconnect(struct message_handler* mc)
{
    /* Start reconnect timer.  */
    mc->t_connect = thread_add_timer(mc->zg, loop_detect_sock_reconnect, mc, HAL_CLIENT_RECONNECT_INTERVAL);
    if (!mc->t_connect)
        return -1;

    return 0;
}

/*****************************************************************************
 Prototype    : dhcpsnoop_sock_client_disconnect
 Description  :
 Input        : struct message_handler* mc
                struct message_entry* me
                pal_sock_handle_t sock
 Output       : None
 Return Value :
 Calls        :
 Called By    :

  History        :
  1.Date         : 2024/5/12
    Author       : zhanghang 445365
    Modification : Created function

*****************************************************************************/
int loop_detect_sock_client_disconnect(struct message_handler* mc, struct message_entry* me, pal_sock_handle_t sock)
{
    /* Stop message client.  */
    message_client_stop(mc);

    loop_detect_sock_client_reconnect(mc);

    return RESULT_OK;
}

/*****************************************************************************
 Prototype    : dhcpsnoop_sock_client_create
 Description  :
 Input        : struct lib_globals* zg
                DHCPSNOOP_CLIENT_S* nl
                u_char async
 Output       : None
 Return Value :
 Calls        :
 Called By    :

  History        :
  1.Date         : 2024/5/12
    Author       : zhanghang 445365
    Modification : Created function

*****************************************************************************/
int loop_detect_sock_client_create(struct lib_globals* zg, struct loop_detect_client* nl, u_char async)
{
    struct message_handler* mc;
    int                     ret;

    /* Create async message client.  */
    mc = message_client_create(zg, MESSAGE_TYPE_ASYNC);
    if (mc == NULL)
    {
        return RESULT_ERROR;
    }

#ifdef HAVE_TCP_MESSAGE
    message_client_set_style_tcp(mc, HSL_ASYNC_PORT);
#else
    message_client_set_style_domain(mc, HSL_ASYNC_PATH);
#endif /* HAVE_TCP_MESSAGE */

    /* Initiate connection using lacp client connection manager.  */
    message_client_set_callback(mc, MESSAGE_EVENT_CONNECT, loop_detect_sock_client_connect);
    message_client_set_callback(mc, MESSAGE_EVENT_DISCONNECT, loop_detect_sock_client_disconnect);

    if (async)
    {
        message_client_set_callback(mc, MESSAGE_EVENT_READ_MESSAGE, loop_detect_sock_async_client_read);
    }

    /* Link each other.  */
    mc->info = nl;
    nl->mc   = mc;

    /* Start the lacp client. */
    ret = message_client_start(nl->mc);

    return ret;
}

/*****************************************************************************
 Prototype    : dhcpsnoop_sock_client_init
 Description  :
 Input        : struct lib_globals* zg
                DHCP_SNOOP_MASTER_S* pDhcpm
 Output       : None
 Return Value :
 Calls        :
 Called By    :

  History        :
  1.Date         : 2024/5/12
    Author       : zhanghang 445365
    Modification : Created function

*****************************************************************************/
pal_sock_handle_t loop_detect_sock_client_init(struct lib_globals* zg, struct loopd_glb* pLoopm)
{
    pal_sock_handle_t sock = -1;

    /* Open sockets to HSL. */
    sock = loop_detect_sock_client_create(zg, &g_loop_detect_link_async, LOOP_DETECT_ASYNC);
    if (sock < 0)
    {
        return RESULT_ERROR;
    }

    pLoopm->iSockfd = sock;
    pLoopm->pRcv_thread = NULL;

    return sock;
}

#endif /* HAVE_USER_HSL */

/*****************************************************************************
 Prototype    : nsm_dhcpsnoop_sock_init
 Description  : 初始�? Input        : struct lib_globals* zg
                DHCP_SNOOP_MASTER_S* pDhcpm
 Output       : None
 Return Value :
 Calls        :
 Called By    :

  History        :
  1.Date         : 2024/5/9
    Author       : zhanghang 445365
    Modification : Created function

*****************************************************************************/
pal_sock_handle_t nsm_loop_detect_sock_init(struct lib_globals* zg, struct loopd_glb* pLoopm)
{
    if (iSockfd < 0)
    {
        iSockfd = loop_detect_sock_client_init(zg, pLoopm);
    }

    if (iSockfd < 0)
    {
        zlog_err(zg, "Can't initialize socket.");
        return RESULT_ERROR;
    }

    return iSockfd;
}
void nsm_loop_detect_sock_exit(struct lib_globals* zg, struct loopd_glb* pLoopm)
{
    if (pLoopm->iSockfd < 0)
    {
        return;
    }

    pal_sock_close(zg, pLoopm->iSockfd);
    pLoopm->iSockfd = -1;

    return;
}




