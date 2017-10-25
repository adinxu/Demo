#ifndef _drwawave_H_
#define _drwawave_H_
#include ".\lib\12864.h"
#include ".\lib\AD.h"
#include ".\lib\typedef.h"
#define PINB P1
# define sizeofkeybdmenu 11 
#define y_offset 4 //y偏移量
#define trans(x) y_offset+64-12.6*(((x*19.53)+500)/1000)//10?*4.883; +500????
sbit key0=P1^0;//回车
sbit key1=P1^1;//向下
sbit key2=P1^2;//向上
sbit key3=P1^3;//退回
sbit EADC = 0XAD;
void drwawave();
void GetKeylnput();
void perform();
extern uchar  code    disdata1[16];
extern uchar  code    disdata2[16];
extern uchar  code    disdata3[16];
extern uchar  code    disdata4[16];
extern uchar  code    menu1[16];
extern uchar  code    menu2[16];
extern uchar  code    menu3[16];
extern uchar  code    menu4[16];
extern uchar  code    menu5[16];
extern uchar  code		picture[1024];
extern uchar  xdata    scrolldata1[][16];
//sbit p10 = P1^0;//	测试用
#endif
