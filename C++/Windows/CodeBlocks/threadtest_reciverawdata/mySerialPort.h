#ifndef MYSERIALPORT_H_INCLUDED
#define MYSERIALPORT_H_INCLUDED
#include "ctb-0.16/serport.h"
namespace ctb
{
   class mySerialPort:public SerialPort
   {
   public:
    DWORD GetCacheByteNum();//��ý��ջ������ַ���

	void Set_RXCHAR();//���뻺�������յ����ַ�
	void Clean_RXCHAR();
    void Set_RXFLAG(char* eos);// ʹ��SetCommState()�������õ�DCB�ṹ�еĵȴ��ַ��ѱ��������뻺������
    void Clean_RXFLAG();

    char* ReadBetweenEos(DWORD ReadBYteNum,char sof,char eof)//���Զ�ȡ֡ͷ֡β��ָ����������,����ָ�򱣴����������ָ�룬������������

   private:
		//GetCacheByteNum()��
        DWORD dwErrorFlags;
        COMSTAT ComStat;


    };

}


    //DWORD read_t(char*& readbuf);//�ǵ�ɾ��������
#endif // MYSERIALPORT_H_INCLUDED
