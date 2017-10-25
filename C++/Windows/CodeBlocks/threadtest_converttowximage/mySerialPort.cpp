#include "mySerialPort.h"
namespace ctb
/** \brief ����0���������򿪣������������
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
/** \brief �鿴�����������ж��ٿɶ�����
 *
 * \param
 * \param
 * \return ���ڶ����������ֽ���
 *
 */

    DWORD mySerialPort::GetCacheByteNum()
    {
        ClearCommError(fd,&dwErrorFlags,&ComStat);
        return ComStat.cbInQue;
    }
/** \brief �趨���ڼ���-�����ض��¼��ַ�
            �趨�����ַ�
 *
 * \param eos Ҫ���õ��¼��ַ��������������յ����ַ��ᴥ���¼�����
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
        SetCommMask(fd,Event);//�趨���ڽ����¼�
    }
    /** \brief ȡ�����ڼ����¼�-�����ض��¼��ַ�
     *          ��û������¼��ַ�
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
     * \brief ���ڶ�ʱ����ʹ��
     *
     * \param
     * \param
     * \return
     *
     *

    DWORD mySerialPort::read_t(char*& readbuf)
    {

        if(num=GetCacheByteNum())//�Ƿ�������
        {
            if(num<0x100000)//����1M��
            {
                readbuf=new char[num+5];//����ռ�
                readedBytes=Read(readbuf,num);
                readbuf[readedBytes]=0;//��ӽ�����
                return readedBytes;
            }
            else return -1;
        }
        return 0;
    }*/
/** \brief ��ȡ��eos�ַ�������
 *
 * \param ReadBYteNum Ҫ��ȡ����������
 * \param eos ֡ͷ
 * \return ��Ž��ճɹ������ݵ�����ָ��
 *          ���棺�ǵò���֮��Ҫɾ��delete[] bufname;
 *          ��ȡ��һ������ͼ���ŷ��أ�ÿ�γ��Զ�ȡ��ʱʱ��Ϊ100ms
 */

char* mySerialPort::ReadBetweenEos(DWORD ReadBYteNum,char eos)
{
    /*int padding=0;
    if(data_num%4)
    padding=4-data_num%4;*/
    char* buffer=new char[ReadBYteNum];
    DWORD Event;//wiatcommevent��
    DWORD read;//GetOverlappedResult��ReadFile��
    char ch;//У��֡ͷ��
    Set_RXFLAG(&eos);
    while(1)
    {
        PurgeComm(fd,PURGE_RXCLEAR);
        if(!WaitCommEvent(fd,&Event,&m_ov))//�ȴ��¼�û����������
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
    continue;}//1�ǽ�β֡ͷ
    ReadFile(fd,buffer,ReadBYteNum,&read,&m_ov);
    ReadFile(fd,&ch,1,&read,&m_ov);
    if(ch==eos) return buffer;
    }
}


}
