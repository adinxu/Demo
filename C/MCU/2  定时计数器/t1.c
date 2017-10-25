#include<reg51.h>
void dis(void);
void del(void);
void main(void)
{TMOD=0x01;
while(1)
{dis();}
}
void dis(void)
{int i=7;
P1=0xfe;

  for(;i>0;i--)
   {del();
    P1<<=1;
    P1|=0x01;
	}
del();
}
void del(void)
{
TH0=0x3c;
TL0=0xb0;
TR0=1;
while(!TF0);
TF0=0;
}
