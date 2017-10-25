#ifndef MYSERIALPORT_H_INCLUDED
#define MYSERIALPORT_H_INCLUDED
#include "ctb-0.16/serport.h"
namespace ctb
{
   class mySerialPort:public SerialPort
   {
   public:
    char OpenCom();
    DWORD GetCacheByteNum();//获得接收缓冲区字符数
    void Set_RXFLAG(char* eos);
    void End_RXFLAG();
    //DWORD read_t(char*& readbuf);//记得删除缓冲区
    char* ReadBetweenEos(DWORD ReadBYteNum,char eos); //读取一副图像数据,返回指向数据的指针
    int devnumber=1;
    int baudrate=9600;
   private:
        DWORD dwErrorFlags;
        COMSTAT ComStat;
        //DWORD num;//4字节，0x0000 0000
        //DWORD readedBytes;

    };

}



#endif // MYSERIALPORT_H_INCLUDED
