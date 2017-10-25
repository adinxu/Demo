#include "mySerialPort.h"
namespace ctb
{
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
	
	void mySerialPort::Set_RXCHAR()
	{
		DWORD Event;
        GetCommMask(fd,&Event);
        Event|=EV_RXCHAR;
        SetCommMask(fd,Event);//�趨���ڽ����¼�
	}
	
	void mySerialPort::Clean_RXFLAG()
	{
		DWORD Event;
        GetCommMask(fd,&Event);
        Event&=~EV_RXCHAR;
        SetCommMask(fd,Event);
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

    void mySerialPort::Clean_RXFLAG()
    {
        DWORD Event;
        GetCommMask(fd,&Event);
        Event&=~EV_RXFLAG;
        SetCommMask(fd,Event);
    }

/** \brief ��ȡ��eos�ַ�������
 *
 * \param ReadBYteNum Ҫ��ȡ����������
 * \param sof ֡ͷ eof ֡β
 * \return ��Ž��ճɹ������ݵ�����ָ��
 *          ���棺�ǵò���֮��Ҫɾ��delete[] bufname;
 *          ��ȡ��һ������ͼ���ŷ��أ�ÿ�γ��Զ�ȡ��ʱʱ��Ϊ100ms*10
 */

char* mySerialPort::ReadBetweenEos(DWORD ReadBYteNum,char sof,char eof)
{
    char* buffer=new char[ReadBYteNum];
    DWORD dwEvtMask;//�����¼���־
    DWORD readnum;//GetOverlappedResult��ReadFile��
    char ch;//У��֡ͷ��
    Set_RXFLAG(&sof);
    while(1)
    {
		ResetEvent(m_ov.hEvent);
        PurgeComm(fd,PURGE_RXCLEAR);//��ս��ջ�����
        if(!WaitCommEvent(fd,&dwEvtMask,&m_ov))//�ȴ��¼�û����������
        {
            switch(GetLastError())
            {
            case ERROR_IO_PENDING: break;
            default : continue;
            }
        }
    WaitForSingleObject(m_ov.hEvent,INFINITE);//�ȴ��¼�����
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
	}//1�ǽ�β֡ͷ
    read: ReadFile(fd,buffer,ReadBYteNum,&readnum,&m_ov);
    ReadFile(fd,&ch,1,&read,&m_ov);
    if(ch==eof) return buffer;
    }
}


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
