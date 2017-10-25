#include ".\lib\AD.h"
uchar wavedat;
//--------------------------------代码分割线----------------------------------
void ini_adc()
{P1ASF=0X01;//p1.0作为模拟功能使用
AUXR1=0X00;//高8位在ADC_RES
ADC_CONTR=ADC_SPEED|ADC_INI;//90时钟周期转换一次
_nop_;
_nop_;
_nop_;
_nop_;//需要4个时钟周期来存储信息
}
//--------------------------------代码分割线----------------------------------
void start_adc()
{uchar a;
	a=ADC_CONTR;
	ADC_CONTR = a|ADC_START|ADC_POWER;//尽量不要与 或 ？？？？
}

//--------------------------------代码分割线----------------------------------
//ADC中断方式
//IE=0XA0;//开中断
//主程序里要调用
//ini_adc();start_adc();

void int_adc() interrupt 5
{
ADC_CONTR&=~ADC_FLAG;
wavedat=ADC_RES;
date = date*19.53;     //取10位时*4.883;
ini_adc();
start_adc();
}






//--------------------------------代码分割线----------------------------------
//ADC查询方式
//注意调用
//wavedat=read_adc();
/*uchar read_adc()
{uchar val=0;
	ini_adc();
start_adc();
while(!(ADC_CONTR&ADC_FLAG));//即flag位不为1时（正在转换）等待
val=ADC_RES;
 ini_adc();
 return val;
}
*/




//--------------------------------代码分割线----------------------------------
//********************************************
//*********显示读取到的转换値，8位led*********
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
//***********************控制小灯*************
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
//调试用
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

