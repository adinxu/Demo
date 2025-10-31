#ifndef _LOOP_PROCTECT_H_
#define _LOOP_PROCTECT_H_

#ifdef __cplusplus
            extern "C" {
#endif
/* ========================================================================== */
/*                              头文件区                                      */
/* ========================================================================== */

#include "main.h"
#include "loop_protect_api.h"


/* ========================================================================== */
/*                           宏和类型定义区                                   */
/* ========================================================================== */
#define LPROT_MUTEX_LOCK()     OSA_mutexLock(gLprotObj.hMutex)
#define LPROT_MUTEX_UNLOCK()   OSA_mutexUnlock(gLprotObj.hMutex)

#define LPROT_CB_MUTEX_LOCK()     OSA_mutexLock(gLprotObj.hCbMutex)
#define LPROT_CB_MUTEX_UNLOCK()   OSA_mutexUnlock(gLprotObj.hCbMutex)

/** Default value for global enable */
#define LOOP_PROTECT_DEFAULT_GLOBAL_ENABLED         FALSE
/** Default value for global transmisssion interval */
#define LOOP_PROTECT_DEFAULT_GLOBAL_TX_TIME         5
/** Default value for global shutdown interval */
#define LOOP_PROTECT_DEFAULT_GLOBAL_SHUTDOWN_TIME   180
/** Default value for port enable */
#define LOOP_PROTECT_DEFAULT_PORT_ENABLED           TRUE
/** Default value for port action */
#define LOOP_PROTECT_DEFAULT_PORT_ACTION            LOOP_PROTECT_ACTION_SHUTDOWN
/** Default value for port transmit mode */
#define LOOP_PROTECT_DEFAULT_PORT_TX_MODE           TRUE

#define LOOP_PROTECT_LOOPBACK_DETECT_REG_MAX 4

/* MAC地址的长度 */
#define MAC_NUM_LEN                6	       

/* tcpdump -dd "ether dst 01:01:c1:00:00:00 and" */
#define SPECIAL_TAG_LOOP_PROTECT_FILTER_F   \
    { 0x20, 0, 0, 0x00000002 },             \
    { 0x15, 0, 3, 0xc1000000 },             \
    { 0x28, 0, 0, 0x00000000 },             \
    { 0x15, 0, 1, 0x00000101 },             \
    { 0x6, 0, 0, 0x00040000 },             \
    { 0x6, 0, 0, 0x00000000 },             \

typedef struct {
    uint32      port;                  /* Port_no or aggr_id */
    uint32      transmitTimer;
    uint32      shutdownTimer;
    uint32      lastDisable;
    LprotPortInfo_t info;
} LprotPortState_t;


/* Port change registration */
typedef struct {
    LprotLoopbackDetectCallback callback;   /* User callback function */
    module_id_t                 module_id;  /* Module ID */
} LprotLoopbackDetectReg_t;


/* Port change registration table */
typedef struct {
    uint32                      count;
    LprotLoopbackDetectReg_t    reg[LOOP_PROTECT_LOOPBACK_DETECT_REG_MAX];
} LprotLoopbackDetectTable_t;


typedef struct
{
    OSA_ThrHandle       phTxThr;        /* tx线程句柄 */
    OSA_ThrHandle       phRxThr;        /* rx线程句柄 */
    OSA_MutexHandle     hMutex;         /* 资源互斥锁 */
    OSA_MutexHandle     hCbMutex;         /* 资源互斥锁 */
    int32               fd;
    char                switchMac[MAC_NUM_LEN];

    LprotLoopbackDetectTable_t loopbackDetectTable;

    LprotConf_t         globalConf;
    LprotPortConf_t    *pLprotPortConf;
    LprotPortState_t   *pLprotPortState;
} LprotGlobal_t;


typedef struct {
    uint8  dst[6];
    uint8  src[6];
    uint16 oui;                    /* 9003 */
#define LPROT_PROTVERSION 1
    uint16 version;
    uint8  switchmac[6];
    uint16 lport;
    uint8 reserved[36];
    uint32 CRC;
} __attribute__((packed)) LprotPdu_t;


int32 LPROT_init(init_data_t *data);

const char * LPROT_init_errorTxt(int32 ret);

#ifdef __cplusplus
        }
#endif

#endif




