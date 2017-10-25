#include "mySerialPort.h"
namespace ctb
/** \brief 返回0代表正常打开，负数代表出错
 *
 * \param
 * \param
 * \return
 *
 */
{
    char mySerialPort::OpenCom()
    {
        char result=Open(devnumber,baudrate);
        return result;
    }
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

    void mySerialPort::End_RXFLAG()
    {
        DWORD Event;
        GetCommMask(fd,&Event);
        Event&=~EV_RXFLAG;
        SetCommMask(fd,Event);
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
/** \brief 读取两eos字符间数据
 *
 * \param ReadBYteNum 要读取的数据数量
 * \param eos 帧头
 * \return 存放接收成功的数据的数组指针
 *          警告：记得不用之后要删除delete[] bufname;
 *          读取到一副完整图像后才返回，每次尝试读取超时时间为100ms
 */

char* mySerialPort::ReadBetweenEos(DWORD ReadBYteNum,char eos)
{
    /*int padding=0;
    if(data_num%4)
    padding=4-data_num%4;*/
    char* buffer=new char[ReadBYteNum];
    DWORD Event;//wiatcommevent用
    DWORD read;//GetOverlappedResult，ReadFile用
    char ch;//校检帧头用
    Set_RXFLAG(&eos);
    while(1)
    {
        PurgeComm(fd,PURGE_RXCLEAR);
        if(!WaitCommEvent(fd,&Event,&m_ov))//等待事件没有立即发生
        {
            switch(GetLastError())
            {
            case ERROR_IO_PENDING: break;
            default : continue;
            }
        }
    WaitForSingleObject(m_ov.hEvent,INFINITE);
    if(!GetOverlappedResult(fd,&m_ov,&read,false))
        continue;
    while(1)
    {
        ReadFile(fd,&ch,1,&read,&m_ov);
        if(ch==eos) break;
    }
    if(GetCacheByteNum()<(ReadBYteNum+1))
    {Sleep(100);
    if(GetCacheByteNum()<(ReadBYteNum+1))
    continue;}//1是结尾帧头
    ReadFile(fd,buffer,ReadBYteNum,&read,&m_ov);
    ReadFile(fd,&ch,1,&read,&m_ov);
    if(ch==eos) return buffer;
    }
}


}
