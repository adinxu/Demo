#include "main.h"
#include "loop_protect.h"
#include "loop_protect_trace.h"
#include "api_if.h"
#include "frame.h"
#include "local_port_api.h"
#include "port.h"
#include "type_define.h"
#include "lib_rxtx_op.h"
#include <osa.h>
#include "pub_define.h"
#include "pub_err.h"
#include "lib_port_op.h"
#include "lib_switch_caps.h"
#include "lib_aggr_shm_op.h"

#ifdef SW_OPTION_SYSLOG
#include "syslog_api.h"
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/filter.h>
#include <netpacket/packet.h>
#include <sys/prctl.h>
#include "switch_loop_protect_api.h"
#include "loop_protect_epoll_loop.h"
#include "aggr.h"


LprotGlobal_t gLprotObj = {0};
static struct loop_protect_epoll_event_handler packet_event = {0};

static const uint8  dmac[6] = {0x01, 0x01, 0xc1, 0x00, 0x00, 0x00}; /* 01-01-c1-00-00-00 */
static const uint16 LprotEtype = 0x9003;        /* 环路检测帧类型 */
#define LPROT_IFNAME    "eth0"                  /* 默认网卡名 */
#define LPROT_MTU       1518                    /* 默认mtu大小 */

/*******************************************************************************
* 函数名  : LPROT_loopbackDetectEvent
* 描  述  : LPROT模块_环路冲突告警事件
* 输  入  : 
*         lPort--对外环路端口信息
* 输  出  :
*
* 返回值  : 
*
*******************************************************************************/
static void LPROT_loopbackDetectEvent(uint32 ifindex)
{
    int i = 0;
    LprotLoopbackDetectReg_t *reg = NULL;
	
  	LPROT_CB_MUTEX_LOCK();
    for (i = 0; i < gLprotObj.loopbackDetectTable.count; i++)
    {
        reg = &gLprotObj.loopbackDetectTable.reg[i];
        OSA_INFO("loopback detect callback, i: %d (%s)\n", i, module_names[reg->module_id]);
        reg->callback(ifindex);
    }
    LPROT_CB_MUTEX_UNLOCK();
}


/*******************************************************************************
* 函数名  : LPROT_disableRatelimit
* 描  述  : LPROT模块_判断最后一次环路检测时间
* 输  入  : 
*         pstate--端口Lprot状态
* 输  出  :
*
* 返回值  : 
*		  TRUE--允许disable
*		  FALSE--不允许
*******************************************************************************/
static BOOL LPROT_disableRatelimit(LprotPortState_t *pstate)
{
    uint32 now = time(NULL);
	
    if (gLprotObj.pLprotPortState[pstate->port - 1].lastDisable != now)
    {
        gLprotObj.pLprotPortState[pstate->port - 1].lastDisable = now;		
        return TRUE;
    }	
    return FALSE;
}


/*******************************************************************************
* 函数名  : LPROT_disablePort
* 描  述  : LPROT模块_端口使能/不使能
* 输  入  : 
*         lPort--端口ID
*         disable--使能
* 输  出  :
*
* 返回值  : 
*
*******************************************************************************/
static void LPROT_disablePort(uint32 lPort, BOOL disable)
{
	int32 ret = 0;
	PortConf_t portConf = {0};
	uint32 enable = 0;
	
    if (disable)
    {
        /* shutdown端口时更新最后时间 */
        gLprotObj.pLprotPortState[lPort - 1].lastDisable = time(NULL);		
    }

	ret = PORT_getConf(NULL, lPort, &portConf);
	if (OSA_SOK != ret)
	{
    	OSA_WARNR("get switch port %u conf failed.\n", lPort);
		return ;
	}

	OSA_INFO("LPROT_disablePort portConf.enable=%u, disable=%d\n", portConf.enable, disable);

	(void)getUserPortEnable(lPort, &enable);
	if (!enable)
	{
		OSA_WARNR("User Port(%u) is not enable.\n", lPort);
		return ;	
	}

	if (portConf.enable != (!disable))
	{
		portConf.enable = !disable;		
		OSA_INFO("switch port conf set port=%u, enable=%d\n", lPort, !disable);
		ret = setPortConf(lPort, &portConf);
		if (OSA_SOK != ret)
		{
	    	OSA_WARNR("set switch port %u conf failed.\n", lPort);
			return ;
		}
	}

	return ;
}


/*******************************************************************************
* 函数名  : LPROT_enable
* 描  述  : LPROT模块_环路检测端口使能入口
* 输  入  : 
*         pstate--端口Lprot状态
* 输  出  :
*
* 返回值  : 
*
*******************************************************************************/
static void LPROT_enable(LprotPortState_t *pstate)
{
    OSA_INFO("Re-enable port %u\n", pstate->port);
    pstate->info.disabled = FALSE;
    pstate->info.loopDetect = FALSE;
    pstate->transmitTimer = 0;
    LPROT_disablePort(pstate->port, FALSE);
}


/*******************************************************************************
* 函数名  : LPROT_disable
* 描  述  : LPROT模块_环路检测端口不使能入口
* 输  入  : 
*         pstate--端口Lprot状态
* 输  出  :
*
* 返回值  : 
*
*******************************************************************************/
static void LPROT_disable(LprotPortState_t *pstate)
{
	LprotAction_e action;
	uint32 ifindex = 0;
	const LprotPortConf_t *pconf = NULL;
	char port_ifname[64];
	trunk_id_t trunk_id = INVALID_AGGR;
	aggr_group_tbl_t aggr_group_tbl = {0};

	if(NULL == pstate)
	{
		OSA_ERROR("Invalid input.\n");
		return ;
	}
	pconf = &gLprotObj.pLprotPortConf[pstate->port - 1];
	action = gLprotObj.pLprotPortConf[pstate->port - 1].action;
    OSA_INFO("Loop detected on port %u, action %d\n", pstate->port, action);
	memset(port_ifname, 0, sizeof(port_ifname));
	
    /* 含有shutdown动作*/
    if((LOOP_PROTECT_ACTION_SHUTDOWN == action) || (LOOP_PROTECT_ACTION_SHUT_LOG == action))
    {
        pstate->shutdownTimer = 0;
        pstate->info.disabled = TRUE;
        if (LPROT_disableRatelimit(pstate))
        {
            LPROT_disablePort(pstate->port, TRUE);
        }
        else
        {
            //OSA_INFO("Skip disable, already sent this very second, port %u\n", pstate->port);
        }
    }

	if(op_is_in_aggr(pstate->port))
	{
		if(GEN_OK == op_get_trunkid_by_lport(pstate->port, &trunk_id))
		{	
			if(GEN_OK == op_get_aggr_group(trunk_id, &aggr_group_tbl))
			{
				if(pstate->port == aggr_group_tbl.master_lport)
				{
#if defined(SW_OPTION_SYSLOG)
					if ((LOOP_PROTECT_ACTION_SHUT_LOG == action) || (LOOP_PROTECT_ACTION_LOG_ONLY == action))
					{
						if (!pstate->info.loopDetect)
						{ /* Only 1st time detected */
							char buf[256] = {0}, *p = &buf[0];
							p += snprintf(p, &buf[0] + sizeof(buf) - p,"%s %u, is loop detect %s.", 
									"Aggregation-group", trunk_id, pconf->action == LOOP_PROTECT_ACTION_SHUT_LOG ? "to shut down" : "");
							S_W("%s", buf);
						}
					}
#endif /* VTSS_SW_OPTION_SYSLOG */	
					ifindex = GLB_INDEX_TO_IFINDEX(GLB_IF_TYPE_AGGR, trunk_id);
				    pstate->info.loops++;
				    pstate->info.lastLoop = time(NULL);
				    pstate->info.loopDetect = TRUE;
					LPROT_MUTEX_UNLOCK();
					LPROT_loopbackDetectEvent(ifindex); /* 只有主端口进入此端口处理流程时，才会触发回调 */
					LPROT_MUTEX_LOCK();
					return ;
				}
			}
		}
	}
	else
	{
#if defined(SW_OPTION_SYSLOG)		
		op_get_lport_ifname(pstate->port, port_ifname, sizeof(port_ifname));
    	/* 含有log动作*/
    	if ((LOOP_PROTECT_ACTION_SHUT_LOG == action) || (LOOP_PROTECT_ACTION_LOG_ONLY == action))
   		{
        	if (!pstate->info.loopDetect)
       		{ /* Only 1st time detected */
            	char buf[256] = {0}, *p = &buf[0];
           		p += snprintf(p, &buf[0] + sizeof(buf) - p,"%s, is loop detect %s.", 
				          port_ifname, pconf->action == LOOP_PROTECT_ACTION_SHUT_LOG ? "to shut down" : "");
            S_W("%s", buf);
        	}
        }
		ifindex = GLB_INDEX_TO_IFINDEX(GLB_IF_TYPE_ETHPORT, pstate->port);
		pstate->info.loops++;
	    pstate->info.lastLoop = time(NULL);
	    pstate->info.loopDetect = TRUE;
		LPROT_MUTEX_UNLOCK();
		LPROT_loopbackDetectEvent(ifindex);
		LPROT_MUTEX_LOCK();
		return ;
#endif /* VTSS_SW_OPTION_SYSLOG */
    }

    pstate->info.loops++;
    pstate->info.lastLoop = time(NULL);
    pstate->info.loopDetect = TRUE;
}

/*******************************************************************************
* 函数名  : LPROT_rxHandle
* 描  述  : LPROT模块_收包处理函数
* 输  入  : 
*         frame--帧指针
*         rx_info--由specialtag解析的信息
* 输  出  :
*
* 返回值  : 
*
*******************************************************************************/
static BOOL LPROT_rxHandle(const uint8 *const frame, rx_info_t rx_info)
{
    LprotPdu_t *pdu = NULL;	
    uint32 inPort = rx_info.lport;
    uint32 outPort = 0;
	lport_t handlePort = 0;
	lport_t lport = 0;
	trunk_id_t trunk_id = INVALID_AGGR;
	trunk_id_t trunk_id_in = INVALID_AGGR;
	trunk_id_t trunk_id_out = INVALID_AGGR;
	lport_t msPort = 0;//aggr master port
    LprotPortState_t *pstate = NULL;
    const LprotPortConf_t *pconf_inport = NULL;
	const LprotPortConf_t *pconf_outport = NULL;
	aggr_group_tbl_t aggr_group_tbl = {0};
	lpmask_t lport_mask;

	if(NULL == frame)
	{
		OSA_ERROR("Invalid input.\n");
		return FALSE;
	}

	pdu = (LprotPdu_t*) frame;
	outPort = pdu->lport;

	if(!op_is_valid_lport(inPort) || !op_is_valid_lport(outPort))
	{
		OSA_DEBUG("Invalid portID.\n");
		return TRUE;
	}
	
    LPROT_MUTEX_LOCK();

    pconf_inport = &gLprotObj.pLprotPortConf[inPort - 1];
	pconf_outport = &gLprotObj.pLprotPortConf[outPort - 1];

    /* Enabled for loop protection ? */
    if(!gLprotObj.globalConf.enabled || !pconf_inport->enabled || !pconf_outport->enabled)
    {
        OSA_DEBUG("Discard - not enabled globally or on port\n");
        goto discard;
    }
	
    if(memcmp(pdu->dst, dmac, sizeof(pdu->dst)) != 0 ||     /* Proper DST */
        memcmp(pdu->src, gLprotObj.switchMac, sizeof(pdu->src)) != 0 || /* Proper SRC */
        pdu->oui != htons(LprotEtype) ||                          /* Proper OUI */
        pdu->version != htons(LPROT_PROTVERSION))
    {                 /* Proper version */
        //OSA_WARN("Discard - Basic PDU check fails (sda/sa, oui, protocol, version)\n");
    }
    else
    {
		OSA_INFO("Rx: %02x-%02x-%02x-%02x-%02x-%02x on inport %u and outport %u\n", frame[0], frame[1], frame[2], frame[3], frame[4], frame[5], inPort, outPort);
		/* 成环链路为单端口与聚合口，对端口进行处理 */
		if(op_is_in_aggr(inPort) && !op_is_in_aggr(outPort))
		{
			handlePort = outPort;
		}
		else if(!op_is_in_aggr(inPort) && op_is_in_aggr(outPort))
		{
			handlePort = inPort;
		}
		else if(!op_is_in_aggr(inPort) && !op_is_in_aggr(outPort)) /* 成环链路为单端口与单端口，对优先级更低的端口进行处理 */
		{
			handlePort = (inPort > outPort ? inPort : outPort);
		}
		else /* 成环链路为聚合口与聚合口,对聚合组id更大的聚合口进行处理 */
		{
			if ((GEN_OK != op_get_trunkid_by_lport(inPort, &trunk_id_in)) || (GEN_OK != op_get_trunkid_by_lport(outPort, &trunk_id_out)))
			{
				OSA_WARN("Get trunkid by port %u or port %u failed\n", inPort, outPort);
				goto discard;
			}
			trunk_id = (trunk_id_in > trunk_id_out ? trunk_id_in : trunk_id_out);
		}

		if(INVALID_AGGR == trunk_id)
		{
			pstate = &gLprotObj.pLprotPortState[handlePort - 1];
			LPROT_disable(pstate);
		}
		else
		{
			if (GEN_OK != op_get_aggr_group(trunk_id, &aggr_group_tbl))
			{
				OSA_WARN("Get trunkid %u information failed\n", trunk_id);
				goto discard;
			}
			msPort = aggr_group_tbl.master_lport;
			if (!op_is_valid_lport(msPort))
			{
        		goto discard;
			}
			op_clear_lport_mask_all(&lport_mask);
			
			if (GEN_OK != op_lport_mask_copy(&lport_mask, &aggr_group_tbl.config_members))
			{
				OSA_WARN("Copy trunkid %u information failed\n", trunk_id);
				goto discard;
			}
			
			LPORT_FOR(lport)
			{
				if(op_test_lport_mask_bit(lport, FALSE, &lport_mask))
				{
					pstate = &gLprotObj.pLprotPortState[lport - 1];
					LPROT_disable(pstate);
				}
			}
		}
    }
	
discard:
    LPROT_MUTEX_UNLOCK();
    return TRUE;
}


/*******************************************************************************
* 函数名  : LPROT_createSocket
* 描  述  : LPROT模块_创建套接字
* 输  入  : 
*         ifname--网卡名
* 输  出  :
*         fd--套接字
* 返回值  : 
*
*******************************************************************************/
static int32 LPROT_createSocket(const char *ifname, int *fd)
{
    uint32 ifindex = if_nametoindex(ifname);
    int32 rc = OSA_SOK;
    
	if ((*fd = socket(PF_PACKET, SOCK_RAW,
		    htons(ETH_P_ALL))) < 0) {
		rc = errno;
		return rc;
	}
    struct sockaddr_ll sa = {
        .sll_family = AF_PACKET,
        .sll_ifindex = ifindex
    };
	
    if (bind(*fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        rc = errno;
        OSA_WARN("unable to bind to raw socket for interface %s",
            ifname);
        return rc;
    }

    /* Set filter */
    OSA_INFO("set BPF filter for %s\n", ifname);
    static struct sock_filter lldpd_filter_f[] = { SPECIAL_TAG_LOOP_PROTECT_FILTER_F };
    struct sock_fprog prog = {
        .filter = lldpd_filter_f,
        .len = sizeof(lldpd_filter_f) / sizeof(struct sock_filter)
    };
    if (setsockopt(*fd, SOL_SOCKET, SO_ATTACH_FILTER,
                &prog, sizeof(prog)) < 0) {
        rc = errno;
        OSA_WARN("unable to change filter for %s\n", ifname);
        return rc;
    }
            
#ifdef SO_LOCK_FILTER
    int enable = 1;
    if (setsockopt(*fd, SOL_SOCKET, SO_LOCK_FILTER,
        &enable, sizeof(enable)) < 0) {
        if (errno != ENOPROTOOPT) {
            rc = errno;
            OSA_WARN("unable to lock filter for %s\n", ifname);
            return rc;
        }
    }
#endif
            
    return 0;
}


/*******************************************************************************
* 函数名  : LPROT_confDefault
* 描  述  : LPROT模块_初始化默认配置
* 输  入  : 
*
* 输  出  :
*
* 返回值  : 
*
*******************************************************************************/
static void LPROT_confDefault(void)
{
    uint32 lPort = 0;

    gLprotObj.globalConf.enabled           = LOOP_PROTECT_DEFAULT_GLOBAL_ENABLED;       /* Disabled */
    gLprotObj.globalConf.transmissionTime  = LOOP_PROTECT_DEFAULT_GLOBAL_TX_TIME;       /* 5 seconds */
    gLprotObj.globalConf.shutdownTime      = LOOP_PROTECT_DEFAULT_GLOBAL_SHUTDOWN_TIME; /* 3 minutes */
    
    for(lPort = 0; lPort < API_IF_portCnt(); lPort++)
    {
        gLprotObj.pLprotPortConf[lPort].enabled  = LOOP_PROTECT_DEFAULT_PORT_ENABLED;
        gLprotObj.pLprotPortConf[lPort].action   = LOOP_PROTECT_DEFAULT_PORT_ACTION;
        gLprotObj.pLprotPortConf[lPort].transmit = LOOP_PROTECT_DEFAULT_PORT_TX_MODE;
    }

	if(OSA_SOK != sdsi_set_loop_protect_enable(gLprotObj.globalConf.enabled))
	{
		OSA_WARN("Set loop protect is failed!\n");
		return ;
	}

    return ;
}


/*******************************************************************************
* 函数名  : LPROT_create
* 描  述  : LPROT模块_创建模块内存等
* 输  入  : 
*         p--模块全局指针
* 输  出  :
*
* 返回值  : 
*         OSA_SOK--成功
*         OSA_EFAIL--失败
*******************************************************************************/
static int32 LPROT_create(LprotGlobal_t *pLprotGlobal)
{
    uint32 portCnt = API_IF_portCnt();
		
    if (!pLprotGlobal)
    {
        return OSA_EFAIL;
    }

    SA_GOTO_FREE_MEMCALLOC(pLprotGlobal->pLprotPortConf, sizeof(LprotPortConf_t) * portCnt);
    SA_GOTO_FREE_MEMCALLOC(pLprotGlobal->pLprotPortState, sizeof(LprotPortState_t) * portCnt);

    if (OSA_mutexCreate(OSA_MUTEX_NORMAL, &pLprotGlobal->hMutex) < 0)
    {
        OSA_ERROR("Fail to create mutex!\n");
        goto freeMem;
    }
	
    if (OSA_mutexCreate(OSA_MUTEX_NORMAL, &pLprotGlobal->hCbMutex) < 0)
    {
        OSA_ERROR("Fail to create mutex!\n");
        goto delMutex;
    }
		
    return OSA_SOK;
    
delMutex:
    if(pLprotGlobal->hMutex)
    {
        OSA_mutexDelete(pLprotGlobal->hMutex);
    }
    if(pLprotGlobal->hCbMutex)
    {
        OSA_mutexDelete(pLprotGlobal->hCbMutex);
    }

freeMem:
    if(!pLprotGlobal->pLprotPortState)
    {
        OSA_memFree(pLprotGlobal->pLprotPortState);
    }
    if(!pLprotGlobal->pLprotPortConf)
    {
        OSA_memFree(pLprotGlobal->pLprotPortConf);
    }

    return OSA_EFAIL;
}


/*******************************************************************************
* 函数名  : LPROT_transmit
* 描  述  : LPROT模块_组包及发送
* 输  入  : 
*         egress_port--出接口id
*         pstate--端口Lprot状态
* 输  出  :
*
* 返回值  : 
*
*******************************************************************************/
static void LPROT_transmit(uint32 egress_port, LprotPortState_t *pstate)
{
    int fd = gLprotObj.fd;	
    uint32 pktlen = 0;
    int32 length = LPROT_MTU;
    uint8 *frame = NULL;
    uint8 *pos = NULL;
	tx_info_t cpu_tag = {0};

	frame = OSA_memCalloc(length);
	if (NULL == frame)
	{
		OSA_ERROR("transmit malloc fail port %u\n", egress_port);	
		return;
	}
	
	pos = frame;

    if(!(POKE_BYTES(dmac, MAC_NUM_LEN) &&
         POKE_BYTES(gLprotObj.switchMac, MAC_NUM_LEN)))
    {
        goto toobig;
    }

    if(!POKE_UINT16(LprotEtype))
    {
        goto toobig;
    }

    if(!(POKE_UINT16(LPROT_PROTVERSION) &&
         POKE_BYTES(gLprotObj.switchMac, MAC_NUM_LEN) &&
         POKE_UINT16(egress_port)))
    {
        goto toobig;
    }

	pktlen = pos - frame;

	/* 发包优先级是0-7 */
	op_set_lport_mask_bit(egress_port, FALSE, &cpu_tag.lport_mask);
	cpu_tag.tx_flag = FWD_DIRECT_PHY_PORT | DESIGN_TX_PASS_ALL_CHECK | DESIGN_TX_PRI;
	cpu_tag.tx_pri = 6;
	
    if(0 != op_insert_cpu_tag(&cpu_tag, (uint8 *)frame, (uint32 *)&pktlen))
    {
        OSA_ERROR("port %u remove cputag failed.\n", egress_port);
        goto toobig;
    }

	if(pktlen > LPROT_MTU)
	{
		goto toobig;
	}
    if(-1 == write(fd, frame, pktlen > 66 ? pktlen : 66))
    {
        OSA_ERROR("transmit fail port %u\n", egress_port);
    }
	
toobig:
    OSA_memFree(frame);
}


/*
 * Main thread helper
 */

static void LPROT_poagTick(LprotPortState_t *pstate, const LprotPortConf_t *pconf)
{
    BOOL tx = pconf->transmit;
    lport_t egress_port = pstate->port;	
	PortStatus_t Status = {0};
	uint32      shutdownTime, transmitTimer;
	aggr_group_tbl_t aggr_group_tbl = {0};
	lpmask_t lport_mask;
	trunk_id_t trunk_id = INVALID_AGGR;

	op_clear_lport_mask_all(&lport_mask);	
	shutdownTime = gLprotObj.globalConf.shutdownTime;
	transmitTimer = gLprotObj.globalConf.transmissionTime;

	if(GEN_OK == op_get_trunkid_by_lport(egress_port, &trunk_id))
	{
		if(op_is_valid_aggr((lport_t)trunk_id))
		{	
			if (GEN_OK != op_get_aggr_group(trunk_id, &aggr_group_tbl))
			{
				OSA_WARN("Get trunkid %u information failed\n", trunk_id);
				return ;
			}
		}
	}
	
    if (tx)
    {
        if (OSA_SOK == getPortStatus(pstate->port, &Status))
        {
            tx = Status.link;
        }
        else
        {
            OSA_WARN("getPortStatus(%u) failed, skip port tx\n", pstate->port);
            tx = FALSE;
        }
    }
		
    if (pstate->info.disabled)
    { /* Has the port been shut down? */
        pstate->shutdownTimer++;
        if ((0 != shutdownTime) && (pstate->shutdownTimer >= shutdownTime))
        {
			LPROT_enable(pstate);
        }
    }
    else
    {
        if (pconf->transmit)
        { /* Are we an active transmitter? */
            pstate->transmitTimer++;
			
            if(tx && pstate->transmitTimer >= transmitTimer)
            {
				if(INVALID_AGGR != trunk_id)
				{
					if(egress_port == aggr_group_tbl.master_lport)
					{
						LPROT_transmit(egress_port, pstate); /* Immediately TX */
						pstate->transmitTimer = 0;
					}					
				}
				else
				{
					LPROT_transmit(egress_port, pstate); /* Immediately TX */
					pstate->transmitTimer = 0;
				}
            }
        }
    }
}


/*
 * Main thread
 */

static void LPROT_periodic(void)
{
	BOOL   switchena; 
    uint32 lPort = 0;
    uint32 portCnt = API_IF_portCnt();
    const LprotPortConf_t *pconf = NULL;
    LprotPortState_t *pstate = NULL;

	LPROT_MUTEX_LOCK();
	switchena = gLprotObj.globalConf.enabled;
	LPROT_MUTEX_UNLOCK();

    for (lPort = 0; lPort < portCnt; lPort++)
    {
		LPROT_MUTEX_LOCK();
    	pconf = &gLprotObj.pLprotPortConf[lPort];
    	pstate = &gLprotObj.pLprotPortState[lPort];

		/* 整个芯片或者该端口环路检测未使能 */
	    if ((!switchena) || (!pconf->enabled))
	 	{
	 		/* 未使能的情况下需要将shutdown的端口恢复成enable状态 */
			if (pstate->info.disabled)
			{
				LPROT_enable(pstate);
			}
		}
		else
        {
			LPROT_poagTick(pstate, pconf);
        }
		LPROT_MUTEX_UNLOCK();
    }
}

/*******************************************************************************
* 函数名  : LPROT_getEthInfo
* 描  述  : LPROT模块_通过网卡名称获取网卡信息
* 输  入  : 
*         ifname--网卡名称
* 输  出  :
*         mac--网卡硬件MAC地址
* 返回值  : 
*         OSA_SOK--成功
*         OSA_EFAIL--失败
*******************************************************************************/
static uint32 LPROT_getEthInfo(const char * ifname, char *mac)
{
    int fd = 0;
    struct ifreq ifreq;

	memset(&ifreq, 0x0, sizeof(struct ifreq));
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) >= 0)
    {
        strncpy(ifreq.ifr_name, ifname, strlen(ifname));
        if(ioctl(fd,SIOCGIFHWADDR,&ifreq) < 0)
        {
            OSA_ERROR("%s ioctl addr fail\n", __func__);
            close(fd);
            return OSA_EFAIL;
        }
        memcpy(mac, ifreq.ifr_hwaddr.sa_data, 6);
    }
    else
    {
        OSA_ERROR("%s socket fail\n", __func__);
        return OSA_EFAIL;
    }

    close(fd);
    return OSA_SOK;
}


static void LPROT_switchInit(void)
{
    uint i = 0;

    for(i = 0; i < API_IF_portCnt(); i++)
    {
        gLprotObj.pLprotPortState[i].port = i + 1;
    }

    LPROT_getEthInfo(LPROT_IFNAME, gLprotObj.switchMac);
}

static int32 LPROT_threadTx(Ptr pUserArgs)
{
	const char *new_name = "LoopProtectTx";

    if (0 == prctl(PR_SET_NAME, new_name)) 
	{
        printf("Process name set to: %s\n", new_name);
    } 
	else 
	{
        perror("prctl PR_SET_NAME");
    }
	
    for(;;)
    {
        OSA_msleep(1000);
        LPROT_periodic();
    }
    
    return OSA_SOK;
}

/*******************************************************************************
* 函数名  : LPROT_portChangeCallback
* 描  述  : LPROT模块_端口link change回调函数
* 输  入  : 
*         p--预留指针
*         pInfo--回调信息
* 输  出  :
*
* 返回值  : 
*         OSA_SOK--成功
*         OSA_EFAIL--失败
*******************************************************************************/
static int32 LPROT_portChangeCallback(void *p, PortLinkChangeInfo_t *pInfo)
{
	BOOL   switchena; 
    lport_t lPort = pInfo->lPortId;
    LprotPortState_t *pLprotState = NULL;
    LprotPortConf_t *pLprotConf = NULL;
    trunk_id_t trunk_id = INVALID_AGGR;
	aggr_group_tbl_t aggr_group_tbl = {0};
	
    if(OSA_SOK != API_IF_portCheckValid(lPort))
    {
        OSA_WARN("Port %u is invalid!\n", lPort);
        return OSA_EFAIL;
    }

    LPROT_MUTEX_LOCK();
    switchena = gLprotObj.globalConf.enabled;
    pLprotState = &gLprotObj.pLprotPortState[lPort - 1];
    pLprotConf = &gLprotObj.pLprotPortConf[lPort - 1];

    if (pInfo->status.link)
    {
        /* 聚合口非主端口link，不需要发包 */
        (void)op_get_trunkid_by_lport(lPort, &trunk_id);
        if (INVALID_AGGR != trunk_id)
        {
			(void)op_get_aggr_group(trunk_id, &aggr_group_tbl);
            if (lPort != aggr_group_tbl.master_lport)
            {
                LPROT_MUTEX_UNLOCK();
                return OSA_SOK;
            }
        }

        if (switchena)
        {
            if (!pLprotState->info.disabled)
            {
                if (pLprotConf->enabled && pLprotConf->transmit)
                {
                    /* Immediate transmit triggering */
                    LPROT_transmit(lPort, pLprotState);
                    pLprotState->transmitTimer = 0;
                }
            }
        }

    }
    else
    {
        if (gLprotObj.globalConf.enabled && pLprotConf->enabled)
        {
            if (pLprotState->info.disabled)
            {
                OSA_INFO("Link down on disabled uport %u  - ignore\n", lPort);
            }
            else
            {
                /* Reset state to initial state if link down and loop was not disabled. */
                OSA_INFO("Link down on active uport %u  - reset it\n", lPort);	
                LPROT_enable(pLprotState);
            }
        }
    }
	LPROT_MUTEX_UNLOCK();
	
    return OSA_SOK;
}

static int32 LPROT_aggrChangeCallback(void *p, trunk_id_t trunkid, aggr_callback_info_t callback_info)
{
    lport_t lPort = 0;
    lpmask_t lport_mask;
    LprotPortState_t *pstate = NULL;
    LprotPortState_t *pMemberPortState = NULL;
    LprotPortConf_t *pconf = NULL;
    aggr_group_tbl_t aggr_new_info = {0};

	op_clear_lport_mask_all(&lport_mask);
    /* 聚合成员端口enable，主端口发包 */
    aggr_new_info = callback_info.new_info;
	if (GEN_OK != op_lport_mask_copy(&lport_mask, &aggr_new_info.config_members))
	{
		OSA_WARN("Copy trunkid %u information failed\n", trunkid);
		return OSA_EFAIL;
	}


    LPROT_MUTEX_LOCK();

    if(!gLprotObj.globalConf.enabled)
    {
        LPROT_MUTEX_UNLOCK();
        return OSA_SOK;
    }

    LPORT_FOR(lPort)
    {
        if(op_test_lport_mask_bit(lPort, FALSE, &lport_mask))
        {
            pMemberPortState = &gLprotObj.pLprotPortState[lPort - 1];
            pconf = &gLprotObj.pLprotPortConf[lPort - 1];
            if(pconf->enabled)
            {
				
				if (pMemberPortState->info.disabled)
            	{
               		//OSA_INFO("Link down on disabled uport %u  - ignore\n", lPort);
           		}
            	else
            	{
                	/* Reset state to initial state if link down and loop was not disabled. */
                	LPROT_enable(pMemberPortState);
					if (lPort == aggr_new_info.master_lport)
                	{
                   		pstate = &gLprotObj.pLprotPortState[lPort - 1];
                    	LPROT_transmit(lPort, pstate);
                    	pstate->transmitTimer = 0;
               		}
            	}
				

            }
        }
    }

    LPROT_MUTEX_UNLOCK();

    return OSA_SOK;

}

static void loop_protect_packet_rcv(uint32_t events, struct loop_protect_epoll_event_handler *h)
{
	rx_info_t rx_info;
	uchar ucPacket[BUF_SIZE2048] = {0};
	uint32 ucPacketLen = 0;

	if(NULL == h)
	{
		OSA_WARN("invalid input.");
		return ;
	}
    ucPacketLen = recv(h->fd, ucPacket, sizeof(ucPacket), MSG_TRUNC);
    if ((ucPacketLen <= 0) ||(ucPacketLen >BUF_SIZE2048))
    {
		OSA_WARN("recv is failed,and ucPacketLen = %u", ucPacketLen);
        return;
    }
	memset(&rx_info, 0x0, sizeof(rx_info));
	if(SDSI_RET_OK != op_rmv_and_parse_cpu_tag(ucPacket, &ucPacketLen, TRUE, &rx_info))
	{
		OSA_WARN("recv is failed,remove cpu tag failed");
		return;
	}
	LPROT_rxHandle(ucPacket, rx_info);

	return;
}

int packet_rev_init(void)
{
	packet_event.fd = gLprotObj.fd;
	packet_event.handler = loop_protect_packet_rcv;
	if (0 != loop_protect_add_epoll(&packet_event))
	{
	    return OSA_EFAIL;
	}
	return OSA_SOK;
}

/*******************************************************************************
* 函数名  : LPROT_threadRx
* 描  述  : LPROT模块_环路检测收包线程
* 输  入  : 
*         pUserArgs--模块句柄
* 输  出  :
*
* 返回值  : 
*         OSA_SOK--成功
*         OSA_EFAIL--失败
*******************************************************************************/
static int32 LPROT_threadRx(Ptr pUserArgs)
{
	const char *new_name = "LoopProtectRx";

    if (0 == prctl(PR_SET_NAME, new_name)) 
	{
        printf("Process name set to: %s\n", new_name);
    } 
	else 
	{
        perror("prctl PR_SET_NAME");
    }
	
	(void)loop_protect_init_epoll();
	(void)packet_rev_init();
	(void)loop_protect_epoll_main_loop();
	return OSA_SOK;
}


/* 阶段注释仅供参考，可根据自身属性使用 */
/* Initialize module */
int32 LPROT_init(init_data_t *data)
{
	int32 ret = OSA_SOK;
    OSA_ThrCreate lprotThread;
    OSA_ThrCreate lprotRxThread;

    switch (data->cmd) {
        case INIT_CMD_EARLY_INIT:
            /* trace初始化及注册 */
            break;
        case INIT_CMD_INIT:
            OSA_INFO("%s\n", "Lprot module init!");
            LPROT_createSocket(LPROT_IFNAME, &gLprotObj.fd);
            OSA_INFO("Lprot fd %d\n", gLprotObj.fd);
            /* 锁、信号量、配置文件注册、命令行注册、web初始化、mib节点初始化 */

            if(OSA_SOK != LPROT_create(&gLprotObj))
            {
                OSA_ERROR("Fail to create loop protect point!\n");
                return OSA_EFAIL;
            }
            break;
        case INIT_CMD_START:
            /* 配置变量初始化、业务线程创建、回调注册等 */
            LPROT_switchInit();
            LPROT_confDefault();
		
			OSA_clear(&lprotThread);
            lprotThread.OpThrRun = LPROT_threadTx;
            lprotThread.stackSize = OSA_THR_STACK_SIZE_DEFAULT;
            lprotThread.thrPol = OSA_SCHED_OTHER;
            lprotThread.thrPri = (Uint16)OSA_THR_PRI_DEFAULT(OSA_SCHED_OTHER);
            lprotThread.pUsrArgs = (Ptr)&gLprotObj;
            if (OSA_isFail(OSA_thrCreate(&lprotThread, &gLprotObj.phTxThr)))
            {
                OSA_ERROR("Fail to create loop protect thread!\n");
                return OSA_EFAIL;
            }

			OSA_clear(&lprotRxThread);
            lprotRxThread.OpThrRun = LPROT_threadRx;
            lprotRxThread.stackSize = OSA_THR_STACK_SIZE_DEFAULT;
            lprotRxThread.thrPol = OSA_SCHED_OTHER;
            lprotRxThread.thrPri = (Uint16)OSA_THR_PRI_DEFAULT(OSA_SCHED_OTHER);
            lprotRxThread.pUsrArgs = (Ptr)&gLprotObj;
            if (OSA_isFail(OSA_thrCreate(&lprotRxThread, &gLprotObj.phRxThr))) 
            {
                OSA_ERROR("Fail to create loop protect thread!\n");
                return OSA_EFAIL;
            }

            Port_reglinkChange(MODULE_ID_LOOP_PROTECT, LPROT_portChangeCallback, 0);
			aggr_change_callback_register(MODULE_ID_LOOP_PROTECT, LPROT_aggrChangeCallback, 0);
            break;
        case INIT_CMD_CONF_DEF:
            /* 初始化恢复默认配置调用 */
            break;
        case INIT_CMD_MASTER_UP:
            /* 主备倒换，备->主 
               一般初始化默认配置 */
            break;
        case INIT_CMD_MASTER_DOWN:
            /* 主备倒换，主->备 */
            break;
        case INIT_CMD_SWITCH_ADD:
            /* 添加主控板卡 */
            break;
        case INIT_CMD_SWITCH_DEL:
            /* 删除主控板卡 */
            break;
        default:
            break;
    }
    return ret;
}
