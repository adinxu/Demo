#include ".\lib\scom.h"
#include "reg52.h";
uchar tflag=1;//ת��������־λ
char sertrans;
void ini_scom(void)
{
SCON=0x40;//8λUARTģʽ
TMOD=0x20;//T1�����ڷ�ʽ2 ��8λ�Զ���װ
PCON=0x00;//���ӱ�
TH1=0xfd;//������9600,11.0592M
TL1=0Xfd;
TR1=1;
IE=0X90;//���п��жϿ�
}
void scom_work(void)
{if(tflag==1)
    {SBUF=sertrans;					
			TI=0;
			tflag=0;					  
	   }					
}

void main()
{sertrans=64;
	ini_scom();
	while(1)
	{
	scom_work();
	delay_ms(100);}
}

void ser_int()interrupt 4
{if(TI==1) tflag=1;
 }	

