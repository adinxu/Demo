#include ".\lib\AD.h"
uchar wavedat;
//--------------------------------����ָ���----------------------------------
void ini_adc()
{P1ASF=0X01;//p1.0��Ϊģ�⹦��ʹ��
AUXR1=0X00;//��8λ��ADC_RES
ADC_CONTR=ADC_SPEED|ADC_INI;//90ʱ������ת��һ��
_nop_;
_nop_;
_nop_;
_nop_;//��Ҫ4��ʱ���������洢��Ϣ
}
//--------------------------------����ָ���----------------------------------
void start_adc()
{uchar a;
	a=ADC_CONTR;
	ADC_CONTR = a|ADC_START|ADC_POWER;//������Ҫ�� �� ��������
}

//--------------------------------����ָ���----------------------------------
//ADC�жϷ�ʽ
//IE=0XA0;//���ж�
//��������Ҫ����
//ini_adc();start_adc();

void int_adc() interrupt 5
{
ADC_CONTR&=~ADC_FLAG;
wavedat=ADC_RES;
date = date*19.53;     //ȡ10λʱ*4.883;
ini_adc();
start_adc();
}






//--------------------------------����ָ���----------------------------------
//ADC��ѯ��ʽ
//ע�����
//wavedat=read_adc();
/*uchar read_adc()
{uchar val=0;
	ini_adc();
start_adc();
while(!(ADC_CONTR&ADC_FLAG));//��flagλ��Ϊ1ʱ������ת�����ȴ�
val=ADC_RES;
 ini_adc();
 return val;
}
*/




//--------------------------------����ָ���----------------------------------
//********************************************
//*********��ʾ��ȡ����ת������8λled*********
//********************************************

uchar code num[10]={0xc0,0xf9,0xa4,0xb0,0x99,
                 0x92,0x82,0xf8,0x80,0x90};
void led_display(uchar which_led,uchar dat)
{P0 = dat;
 P2 = which_led;
}
void vin_display(uint date)  
{

  led_display(0x80,num[date%10]);
	delay5ms();
	led_display(0x40,num[date/10%10]);
	delay5ms();
	led_display(0x20,num[date/100%10]);
	delay5ms();
	led_display(0x10,(num[date/1000%10]&0x7f));
	delay5ms();
}
void delay5ms(void)   
{
    unsigned char a,b,c;
    for(c=2;c>0;c--)
        for(b=238;b>0;b--)
            for(a=30;a>0;a--);
}
//********************************************
//***********************����С��*************
//********************************************
/*void light_control(uint date)
{
	date = date*19.53;
	if(date>=700)
	{lamp= 1;
	}
	else
	{lamp= 0;
	}
}*/
//������
void main()
{
IE=0XA0;
ini_adc();
start_adc();
while(1)
{vin_display(wavedat);
//light_control(wavedat);
	}
}

