#include ".\lib\led_msc.h"
sbit datase =P2^6;//��������ѡ��
sbit bitse  =P2^7;//��ʾλѡ��  //�����������ʾλ����P0����
unsigned char codetable[]=
{ 
0x3F, //"0"
0x06, //"1"
0x5B, //"2"
0x4F, //"3"
0x66, //"4"
0x6D, //"5"
0x7D, //"6"
0x07, //"7"
0x7F, //"8"
0x6F, //"9"
0x40, //"-"
0x00 //Ϩ��
};//��������ܣ���������������������������������ɨ���ֵ͵�ƽ�����������иߵ�ƽ����
//*********************************************************************************************
void display(unsigned char *dat)
{unsigned char i,bitcon=0x01;
for(i=0;i<8;i++)
	 {bitse=1;
		P0=~bitcon;
		bitse=0;
		datase=1;
		P0=codetable[dat[i]];	
    delay_ms(1);	
		P0=0x00;	
		datase=0;
    bitcon<<=1;//�����c����λ������		 
	 }
}


/*void main(void)
{unsigned char dat[8]={1,9,9,5,0,9,1,9};
while(1)
{	
display(dat);	
}
}*/

