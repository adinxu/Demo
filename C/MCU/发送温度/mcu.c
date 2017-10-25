#include ".\lib\mcu.h"
//--------------------------------代码分割线----------------------------------
void main(void)
{uchar num;
ini_scom();
while(1)
    {
		start_convert();//读取，转换，发送温度值
    for(num=0;num<150;num++)display(dat);				
    }
}