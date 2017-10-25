#ifndef MYSERIALPORT_H_INCLUDED
#define MYSERIALPORT_H_INCLUDED
#include "ctb-0.16/serport.h"
namespace ctb
{
   class mySerialPort:public SerialPort
   {
   public:
    char OpenCom();
    DWORD GetCacheByteNum();//��ý��ջ������ַ���
    void Set_RXFLAG(char* eos);
    void End_RXFLAG();
    //DWORD read_t(char*& readbuf);//�ǵ�ɾ��������
    char* ReadBetweenEos(DWORD ReadBYteNum,char eos); //��ȡһ��ͼ������,����ָ�����ݵ�ָ��
    int devnumber=1;
    int baudrate=9600;
   private:
        DWORD dwErrorFlags;
        COMSTAT ComStat;
        //DWORD num;//4�ֽڣ�0x0000 0000
        //DWORD readedBytes;

    };

}



#endif // MYSERIALPORT_H_INCLUDED
