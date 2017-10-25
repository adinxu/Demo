#include<reg51.h>
void dis1(void);
void dis2(void);
void delay1ms(int t);
int i;
void main(void)
{while(1)
   {dis1();
   dis2();}
}
void dis1(void)
{i=7;
P1=0xfe; 
  for(;i>0;i--)
   {delay1ms(50);
    P1<<=1;
    P1|=0x01;
	}
delay1ms(50);
P1=0xff;
}
void dis2(void)
{i=7;
P3=0xfe; 
  for(;i>0;i--)
   {delay1ms(50);
    P3<<=1;
    P3|=0x01;
	}
delay1ms(50);
P3=0xff;
}
void delay1ms(int t)  
{
    unsigned char a,b;
for(;t>0;t--)
    for(b=129;b>0;b--)
        for(a=45;a>0;a--);
}
