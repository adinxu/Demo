#include ".\lib\scom.h"
void ini_scom(void)
{
SCON=0x50;//8位UART模式
TMOD=0x20;//T1工作在方式2 ，8位自动重装
PCON=0x00;//不加倍
TH1=0xfd;//波特9600  11.0592
TL1=0Xfd;
TR1=1;
}
void sendbyte_scom(int val)
{char sertrans[4];
uchar loop;
sprintf(sertrans,"%d ",val);
for(loop=0;loop<3;loop++)
{SBUF=sertrans[loop];	
while(!TI);	
TI=0;}
/*delay_ms(1);
SBUF=' ';	
while(!TI);	
TI=0;*/
}
