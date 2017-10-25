#include<reg51.h>
void DL(unsigned int t);
sbit d1=P1^0;
sbit d2=P1^1;
sbit d3=P1^2;
void main(void)
{while(1)
   {switch(~P2)
       {case 0: {DL(50);break;}
  		case 1: {d1=~d1;DL(500);break;}
 		case 2: {d2=~d2;DL(500);break;}
 		case 4: {d3=~d3;DL(500);break;}
		default: {DL(50);break;}     
        }

   }
}
 void DL(unsigned int t)
{register unsigned char a,b;
for (; t; t--)
    for (b=2;b;b--)
        for (a=250; --a; );}   
