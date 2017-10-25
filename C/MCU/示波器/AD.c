#include ".\lib\AD.h"
//--------------------------------代码分割线----------------------------------
uchar getone_adc()
{P1ASF=0X01;//p1.0作为模拟功能使用
AUXR1=0X00;//高8位在ADC_RES
ADC_CONTR=contr;//90时钟周期转换一次
_nop_;
_nop_;
_nop_;
_nop_;//需要4个时钟周期来存储信息
while(!(ADC_CONTR&ADC_FLAG));
ADC_CONTR=0;
return ADC_RES*19.53;
}