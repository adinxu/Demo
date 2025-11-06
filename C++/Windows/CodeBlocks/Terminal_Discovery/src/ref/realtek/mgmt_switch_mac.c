//以下为二层转发表获取函数getDevUcMacAddress在c++中使用时的相关代码片段

//指针初始化
m_switchDevPtr = CSwitchSystemWrapper::getSwitchDev();

SwitchDev* CSwitchSystemWrapper::getSwitchDev()
{
	static SwitchDev* stSwitchDevPtr = NULL;
	if(NULL == stSwitchDevPtr)
	{
		if(createSwitch(NULL, &stSwitchDevPtr) < 0)
		{
			errorf("create new Switch failed\n");
			assert(stSwitchDevPtr != NULL);
		}
	}
	return stSwitchDevPtr;
}


//函数使用方式
ret_code_t ret = m_switchDevPtr->getDevUcMacAddress(m_switchDevPtr, &mac_entry, &mac_num);


//以下为二层转发表获取函数getDevUcMacAddress在libswitchapp.so实现中相关代码片段

/* ========================================================================== */
/*                          函数声明区                                        */
/* ========================================================================== */
/// 创建createSwitch接口
///
/// \param [in] pDesc switch设备接口描述结构 SwitchDesc 指针
/// \param [out] switchDev 指针的指针
/// \retval <0 创建失败
/// \retval  0 创建成功
int createSwitch(SwitchDesc *pDesc, SwitchDev **switchDev);

#define GLB_MAC_ADDR_LEN  6
typedef unsigned char			uint8;
typedef unsigned short			uint16;

typedef uint8 mac_addr_t[GLB_MAC_ADDR_LEN];

typedef uint16 vlan_id_t;

/*mac地址状态*/
typedef enum {
    MAC_STATE_DYNAMIC = 0,
    MAC_STATE_STATIC, 	
    MAC_STATE_DELETE,
    MAC_STATE_EMD
}mac_state_e;
//交换机单播表
typedef struct __SwUcMacEntry {
    mac_addr_t  mac;            //mac地址
    vlan_id_t   vlan;		      //从哪个vlan学习到的mac地址： vlan, 1-4094, vlan = 0 无效
    mac_state_e attr;          //学习到mac的属性：dynamic/static/delete
    uint32   	ifindex;        //ifindex包含聚合和普通端口
}SwUcMacEntry;

/// 交换机模块对象
typedef struct SwitchDev
{
	/// 底层私有数据
	void *priv;

	/// 增加接口引用
	int (*addRef)(struct SwitchDev *thiz);

	/// 释放接口
	int (*release)(struct SwitchDev *thiz);

    ///许多函数指针在此省略
    ret_code_t (*getDevMacMaxSize)(struct SwitchDev *thiz, uint32 *p_size);
	/*******************************************************************************
	* 函数名: getDevUcMacAddress 
	* 描  述	: 获取设备对应的Mac表
	* 输  入	: thiz			设备对象
	* 输 出 : p_mac_entry   mac缓存区
	*		  p_num 		mac数目
	* 返回值  : RET_OK	:  成功
	*		  RET_ERR: 失败
	*		  RET_ERR_PARAM: 参数错误
	*******************************************************************************/
    ret_code_t (*getDevUcMacAddress)(struct SwitchDev *thiz, SwUcMacEntry *p_mac_entry, uint32 *p_num);

    ///许多函数指针在此省略
	/// 保留
	void* reserved[12];

} SwitchDev;
/*******************************************************************************
* 函数名: __SW_DevMacMaxSize
* 描  述	: 获取设备Mac表大小
* 输  入	: thiz 		    设备对象
* 输 出 : p_size          mac表大小
* 返回值  : RET_OK	:  成功
*         RET_ERR: 失败
*	      RET_ERR_PARAM: 参数错误
*******************************************************************************/
static ret_code_t __SW_DevMacMaxSize(struct SwitchDev *thiz, uint32 *p_size)
{
    SwitchDev_Priv  *priv = NULL;
    ret_code_t       ret = RET_OK;

    if ((thiz == NULL) || (thiz->priv == NULL))
    {
        OSA_ERROR("Parameter thiz or thiz->priv is NULL!\n");
        return RET_ERR_PARAM;
    }

    priv = (SwitchDev_Priv*)thiz->priv;

    OSA_mutexLock(priv->hMutex);
    ret = SW_DevMacMaxSize(priv->hSwDev, p_size);
    OSA_mutexUnlock(priv->hMutex);

    return ret;
}

/*******************************************************************************
* 函数名: SW_DevMacMaxSize 
* 描  述	: 获取设备中Mac表大小
* 输  入	: swDev 		设备对象
* 输 出 : p_size          Mac表大小
* 返回值  : RET_OK	:  成功
*         RET_ERR: 失败
*******************************************************************************/
ret_code_t SW_DevMacMaxSize(switchHandle swDev, uint32 *p_size)
{
	mac_cap_t *p_mac_cap = NULL;

	if (NULL == p_size)
	{
		OSA_ERROR("input parameters (%p) error!\n", p_size);
		return RET_ERR_PARAM;
	}

	MAC_MUTEX_LOCK();
	p_mac_cap = &(g_mac_info.mac_cap);
	*p_size = p_mac_cap->total_mac_num;
	MAC_MUTEX_UNLOCK();

    return RET_OK; 
}
/*******************************************************************************
* 函数名: __SW_getDevUcMacAddress
* 描  述	: 获取设备的单播Mac表
* 输  入	: thiz 		    设备对象
* 输 出 : p_mac_entry   mac缓存区
*         p_num         mac数目
* 返回值  : RET_OK	:  成功
*         RET_ERR: 失败
*	      RET_ERR_PARAM: 参数错误
*******************************************************************************/
static ret_code_t __SW_getDevUcMacAddress(struct SwitchDev *thiz, SwUcMacEntry *p_mac_entry, uint32 *p_num)
{
    SwitchDev_Priv  *priv = NULL;
    ret_code_t       ret = RET_OK;

    if ((thiz == NULL) || (thiz->priv == NULL))
    {
        OSA_ERROR("Parameter thiz or thiz->priv is NULL!\n");
        return RET_ERR_PARAM;
    }

    priv = (SwitchDev_Priv*)thiz->priv;

    OSA_mutexLock(priv->hMutex);
    ret = SW_getDevUcMacAddress(priv->hSwDev, p_mac_entry, p_num);
    OSA_mutexUnlock(priv->hMutex);

    return ret;
}


/*******************************************************************************
* 函数名: SW_getDevUcMacAddress 
* 描  述	: 获取设备中的单播Mac表
* 输  入	: swDev 		设备对象
*         ifindex       端口信息
*         mac_type      mac类型
* 输 出 : pp_mac_info   应用mac缓存区
*         p_num         mac数目
* 返回值  : RET_OK	:  成功
*         RET_ERR: 失败
*******************************************************************************/
ret_code_t SW_getDevUcMacAddress(switchHandle swDev, SwUcMacEntry *p_mac_entry, uint32 *p_num)
{
	ret_code_t ret = 0;

	if ((NULL == p_mac_entry) || (NULL == p_num))
	{
		OSA_ERROR("input parameters (%p, %p) error!\n", p_mac_entry, p_num);
		return RET_ERR_PARAM;
	}

	ret= SW_cpySwUcMacToBuf(p_mac_entry, p_num);
	if (RET_OK != ret)
	{
		OSA_ERROR("mac table copy from buffer failed!\n");
		return RET_ERR;
	}
	
    return RET_OK; 
}



/*******************************************************************************
* 函数名: SW_cpySwUcMacToBuf 
* 描  述	: 将mac缓存表复制到buf缓存
* 输  入	: 无
* 输 出 : p_uc_entry 	 buf缓存
*		  p_num        mac表项数量
* 返回值  : RET_OK	:  	   成功
*         RET_ERR：     失败
*******************************************************************************/
static ret_code_t SW_cpySwUcMacToBuf(SwUcMacEntry *p_uc_entry, uint32 *p_num)
{
	uint32 i = 0;
	uint32 lport;	
	uint32 mac_index = 0;
	mac_table_t *p_mac_table = NULL;
	uc_entry_t *p_uc_entry_tmp = NULL;

	if ((NULL == p_uc_entry) || (NULL == p_num))
	{
		OSA_ERROR("input parameters error(%p, %p)!\n", p_uc_entry, p_num);
		return RET_ERR_PARAM;
	}

	MAC_MUTEX_LOCK();		
	p_mac_table = &(g_mac_info.mac_table);
	
    LPORT_FOR(lport)
    {    
		p_uc_entry_tmp = p_mac_table->p_static_uc;
		/* copy静态mac地址 */
        for (i = 0; i < p_mac_table->static_uc_cnt; i++)
        {    
        	if (GLB_INDEX_TO_IFINDEX(GLB_IF_TYPE_ETHPORT, lport) == p_uc_entry_tmp[i].ifindex)
        	{
				/* 数据结构不一致，可能引起数据错误 */
				memcpy(p_uc_entry[mac_index].mac, p_uc_entry_tmp[i].mac, GLB_MAC_ADDR_LEN);
				p_uc_entry[mac_index].ifindex = p_uc_entry_tmp[i].ifindex;
				p_uc_entry[mac_index].attr = p_uc_entry_tmp[i].attr;
				p_uc_entry[mac_index].vlan = p_uc_entry_tmp[i].vlan;
				
				/* 记录真实拷贝的mac数目 */
				mac_index++; 
			}
        }

		p_uc_entry_tmp = p_mac_table->p_dynamic_uc;
		/* copy动态mac地址 */
		for (i = 0; i < p_mac_table->dynamic_uc_cnt; i++)
        {    
        	if ((GLB_INDEX_TO_IFINDEX(GLB_IF_TYPE_ETHPORT, lport) == p_uc_entry_tmp[i].ifindex) &&
				(MAC_STATE_DELETE != p_uc_entry_tmp[i].attr))
        	{
				/* 数据结构不一致，可能引起数据错误 */
				memcpy(p_uc_entry[mac_index].mac, p_uc_entry_tmp[i].mac, GLB_MAC_ADDR_LEN);
				p_uc_entry[mac_index].ifindex = p_uc_entry_tmp[i].ifindex;
				p_uc_entry[mac_index].attr = p_uc_entry_tmp[i].attr;
				p_uc_entry[mac_index].vlan = p_uc_entry_tmp[i].vlan;
				
				/* 记录真实拷贝的mac数目 */
				mac_index++; 
			}
        }
    }

	AGGR_FOR(lport)
	{	
		p_uc_entry_tmp = p_mac_table->p_static_uc;
		/* copy静态mac地址 */
		for (i = 0; i < p_mac_table->static_uc_cnt; i++)
        {
            if (GLB_INDEX_TO_IFINDEX(GLB_IF_TYPE_AGGR, lport) == p_uc_entry_tmp[i].ifindex)
        	{
				/* 数据结构不一致，可能引起数据错误 */
				memcpy(p_uc_entry[mac_index].mac, p_uc_entry_tmp[i].mac, GLB_MAC_ADDR_LEN);
				p_uc_entry[mac_index].ifindex = p_uc_entry_tmp[i].ifindex;
				p_uc_entry[mac_index].attr = p_uc_entry_tmp[i].attr;
				p_uc_entry[mac_index].vlan = p_uc_entry_tmp[i].vlan;
				
				/* 记录真实拷贝的mac数目 */
				mac_index++;
        	}
        }
		
		p_uc_entry_tmp = p_mac_table->p_dynamic_uc;		
		/* copy动态mac地址 */
		for (i = 0; i < p_mac_table->dynamic_uc_cnt; i++)
        {    
        	if ((GLB_INDEX_TO_IFINDEX(GLB_IF_TYPE_AGGR, lport) == p_uc_entry_tmp[i].ifindex) &&
				(MAC_STATE_DELETE != p_uc_entry_tmp[i].attr))
        	{
				/* 数据结构不一致，可能引起数据错误 */
				memcpy(p_uc_entry[mac_index].mac, p_uc_entry_tmp[i].mac, GLB_MAC_ADDR_LEN);
				p_uc_entry[mac_index].ifindex = p_uc_entry_tmp[i].ifindex;
				p_uc_entry[mac_index].attr = p_uc_entry_tmp[i].attr;
				p_uc_entry[mac_index].vlan = p_uc_entry_tmp[i].vlan;
				
				/* 记录真实拷贝的mac数目 */
				mac_index++; 
			}
        }
	}
	
	/* 存在删除的mac，长度会变小 */
    *p_num = mac_index;	

	/* 需要更新缓存表 */
	p_mac_table->mac_table_state = MAC_NEED_UPDATE;
	OSA_semPost(g_mac_info.mac_sem);	
	MAC_MUTEX_UNLOCK();		

	return RET_OK;
}
