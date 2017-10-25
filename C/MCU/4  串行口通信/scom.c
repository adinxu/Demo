#include ".\lib\scom.h"
#include "reg52.h";
uchar tflag=1;//转换结束标志位
char sertrans;
void ini_scom(void)
{
SCON=0x40;//8位UART模式
TMOD=0x20;//T1工作在方式2 ，8位自动重装
PCON=0x00;//不加倍
TH1=0xfd;//波特率9600,11.0592M
TL1=0Xfd;
TR1=1;
IE=0X90;//串行口中断开
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

