#include ".\lib\AD.h"
//--------------------------------����ָ���----------------------------------
uchar getone_adc()
{P1ASF=0X01;//p1.0��Ϊģ�⹦��ʹ��
AUXR1=0X00;//��8λ��ADC_RES
ADC_CONTR=contr;//90ʱ������ת��һ��
_nop_;
_nop_;
_nop_;
_nop_;//��Ҫ4��ʱ���������洢��Ϣ
while(!(ADC_CONTR&ADC_FLAG));
ADC_CONTR=0;
return ADC_RES*19.53;
}