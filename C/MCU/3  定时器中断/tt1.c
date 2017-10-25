#include<reg51.h>
sbit led=P1^0;
void main(void)
{TMOD=0x01;
TH0=0xFC;
TL0=0x18;
EA=1;
ET0=1;
TR0=1;
while(1);}
void t0(void) interrupt 1
{led=!led;
TH0=0xFC;
TL0=0x18;}

