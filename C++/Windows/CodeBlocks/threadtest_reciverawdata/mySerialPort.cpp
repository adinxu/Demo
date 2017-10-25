#include "mySerialPort.h"
namespace ctb
{
/** \brief 查看读缓冲区还有多少可读数据
 *
 * \param
 * \param
 * \return 串口读缓冲区的字节数
 *
 */	
    DWORD mySerialPort::GetCacheByteNum()
    {
        ClearCommError(fd,&dwErrorFlags,&ComStat);
        return ComStat.cbInQue;
    }
	
	void mySerialPort::Set_RXCHAR()
	{
		DWORD Event;
        GetCommMask(fd,&Event);
        Event|=EV_RXCHAR;
        SetCommMask(fd,Event);//设定串口接收事件
	}
	
	void mySerialPort::Clean_RXFLAG()
	{
		DWORD Event;
        GetCommMask(fd,&Event);
        Event&=~EV_RXCHAR;
        SetCommMask(fd,Event);
	}
/** \brief 设定串口监视-接收特定事件字符
            设定所用字符
 *
 * \param eos 要设置的事件字符，当读缓冲区收到此字符会触发事件监视
 * \param
 * \return
 *
 */

    void mySerialPort::Set_RXFLAG(char* eos)
    {
        DCB dcb;
        GetCommState(fd,&dcb);
        dcb.EvtChar=*eos;
        SetCommState(fd,&dcb);
        DWORD Event;
        GetCommMask(fd,&Event);
        Event|=EV_RXFLAG;
        SetCommMask(fd,Event);//设定串口接收事件
    }
    /** \brief 取消串口监视事件-接收特定事件字符
     *          并没有清除事件字符
     * \param
     * \param
     * \return
     *
     */

    void mySerialPort::Clean_RXFLAG()
    {
        DWORD Event;
        GetCommMask(fd,&Event);
        Event&=~EV_RXFLAG;
        SetCommMask(fd,Event);
    }

/** \brief 读取两eos字符间数据
 *
 * \param ReadBYteNum 要读取的数据数量
 * \param sof 帧头 eof 帧尾
 * \return 存放接收成功的数据的数组指针
 *          警告：记得不用之后要删除delete[] bufname;
 *          读取到一副完整图像后才返回，每次尝试读取超时时间为100ms*10
 */

char* mySerialPort::ReadBetweenEos(DWORD ReadBYteNum,char sof,char eof)
{
    char* buffer=new char[ReadBYteNum];
    DWORD dwEvtMask;//接收事件标志
    DWORD readnum;//GetOverlappedResult，ReadFile用
    char ch;//校检帧头用
    Set_RXFLAG(&sof);
    while(1)
    {
		ResetEvent(m_ov.hEvent);
        PurgeComm(fd,PURGE_RXCLEAR);//清空接收缓冲区
        if(!WaitCommEvent(fd,&dwEvtMask,&m_ov))//等待事件没有立即发生
        {
            switch(GetLastError())
            {
            case ERROR_IO_PENDING: break;
            default : continue;
            }
        }
    WaitForSingleObject(m_ov.hEvent,INFINITE);//等待事件发生
    if(!GetOverlappedResult(fd,&m_ov,&readnum,false))
		continue;
    while(1)
    {
        ReadFile(fd,&ch,1,&readnum,&m_ov);
        if(ch==sof) break;
    }
	
    if(GetCacheByteNum()<(ReadBYteNum+1))
    {
		for(unsigned char i=0;i<10;i++)
		{
			Sleep(100);
			if(GetCacheByteNum()>=(ReadBYteNum+1))
				goto read;
		}
		continue;
	}//1是结尾帧头
    read: ReadFile(fd,buffer,ReadBYteNum,&readnum,&m_ov);
    ReadFile(fd,&ch,1,&read,&m_ov);
    if(ch==eof) return buffer;
    }
}


}

















    /*
     * \brief 用在定时器中使用
     *
     * \param
     * \param
     * \return
     *
     *

    DWORD mySerialPort::read_t(char*& readbuf)
    {

        if(num=GetCacheByteNum())//是否有数据
        {
            if(num<0x100000)//大于1M？
            {
                readbuf=new char[num+5];//分配空间
                readedBytes=Read(readbuf,num);
                readbuf[readedBytes]=0;//添加结束符
                return readedBytes;
            }
            else return -1;
        }
        return 0;
    }*/
