#ifndef MYSERIALPORT_H_INCLUDED
#define MYSERIALPORT_H_INCLUDED
#include "ctb-0.16/serport.h"
namespace ctb
{
   class mySerialPort:public SerialPort
   {
   public:
    DWORD GetCacheByteNum();//获得接收缓冲区字符数

	void Set_RXCHAR();//输入缓冲区接收到新字符
	void Clean_RXCHAR();
    void Set_RXFLAG(char* eos);// 使用SetCommState()函数设置的DCB结构中的等待字符已被传入输入缓冲区中
    void Clean_RXFLAG();

    char* ReadBetweenEos(DWORD ReadBYteNum,char sof,char eof)//尝试读取帧头帧尾间指定数量数据,返回指向保存数据数组的指针，具有阻塞特性

   private:
		//GetCacheByteNum()用
        DWORD dwErrorFlags;
        COMSTAT ComStat;


    };

}


    //DWORD read_t(char*& readbuf);//记得删除缓冲区
#endif // MYSERIALPORT_H_INCLUDED
