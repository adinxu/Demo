#include<reg51.h>
sbit led=P1^0;
sbit led1=P1^1;
int i=0;
void main(void)
{TMOD=0x21;
TH0=0xFC;
TL0=0x18;
TH1=0x06;
TL1=0x06;
EA=1;
ET0=1;
ET1=1;
TR0=1;
TR1=1;
while(1);}
void t0(void) interrupt 1
{led=!led;
TH0=0xFC;
TL0=0x18;}
void t1(void) interrupt 3
{if (i==1)
{led1=!led1;
i=0;}
else i=!i;
}
