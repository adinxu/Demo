#include".\lib\led.h"
void display(unsigned char *dat)
{unsigned char i;
unsigned char codetable[]={0xc0,0xf9,0xa4,0xb0,0x99,0x92,0x82,0xf8,0x80,0x90,0xbf,0xff};
//                           0    1    2    3     4    5    6    7    8    9   -   Ï¨Ãð
 P2=0x01;
 for(i=0;i<8;i++)
    {P0=codetable[dat[i]];
		if(i==6) P0&=0x7f;
    delay_ms(1);
     P0=0xff;
     P2<<=1;
	 }
}

