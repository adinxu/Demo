#include ".\lib\scom.h"
void ini_scom(void)
{
SCON=0x50;//8λUARTģʽ
TMOD=0x20;//T1�����ڷ�ʽ2 ��8λ�Զ���װ
PCON=0x00;//���ӱ�
TH1=0xfd;//����9600  11.0592
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
