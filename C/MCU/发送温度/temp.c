#include ".\lib\ds18b20.h"
 unsigned char dat[8],re[9];//����ȫ�ֱ���
 signed int temp;//�¶�ֵ������
sbit DQ=P2^2;
//--------------------------------����ָ���----------------------------------
unsigned char ini_ds18b20(void)
{unsigned char f=0;
	DQ=0;
delay_us(240);//��ʱ480us
DQ=1;
delay_us(33);//��ʱ66us
f=!DQ;//8us
delay_us(203);//406us
return f;//f����
}
//--------------------------------����ָ���----------------------------------
void write_byte(unsigned char com)
{unsigned char i;
for(i=0;i<8;i++)
	{DQ=0;
	delay_us(1);
	 DQ=com&0x01;//2~3us    RRC A     MOV DQ,C
	delay_us(30);
	 DQ=1;
	 com>>=1;//4us
	}
}
//--------------------------------����ָ���----------------------------------
unsigned char read_byte(void)
{unsigned char i,date;
	for(i=0;i<8;i++)
	{DQ=0;
  date>>=1;//4us
	 DQ=1;
		delay_us(1);
   if(DQ) date|=0x80;//��������=����==�����ǣ���������4us
   delay_us(25);
		
	}
	return date;
}
//--------------------------------����ָ���----------------------------------
void temp_trans(void)
{unsigned char y=0;
	if(temp<0) {temp=(~temp+1)*0.0625;y=!y;} 
 else temp=temp*0.0625;
dat[0]=11;
dat[1]=11;
dat[2]=11;
dat[3]=11;
dat[4]=11;
dat[5]=(temp%1000)/100;
dat[6]=temp%100/10;
dat[7]=temp%10;
if(y==1)                        //�¶�Ϊ��
   {
    if(dat[6]==0) {dat[6]=10;dat[5]=11;}
		else dat[5]=10;
		temp=-temp;
	 }
else                           //�¶�Ϊ��
   {if(dat[5]==0) {dat[5]=11; if(dat[6]==0) dat[6]=11;}
   }	
	 sendbyte_scom(temp);
}
//--------------------------------����ָ���----------------------------------
void start_convert(void)
{unsigned char x;
ini_ds18b20();
write_byte(0xcc);//skiprom
write_byte(0x44);//����һ���¶�ת�� 
while(!DQ);//�ȴ�ת������
ini_ds18b20();
write_byte(0xcc);//skiprom
write_byte(0xbe);//���Ͷ�ȡ�¶�ָ��
for(x=0;x<9;x++) re[x]=read_byte();	
temp=((temp=re[1])<<8)|re[0];
temp_trans();
}