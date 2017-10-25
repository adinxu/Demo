#ifndef _12864_h_
#define _12864_h_
#include <reg51.h>
#include <math.h>
#include ".\lib\typedef.h"
#define lcddata P2      //12864液晶数据口
sbit busy=lcddata^7;    //12864液晶"忙"接口
sbit lcd_ret=P0^6;    //液晶复位，低电平有效
sbit rs=P3^7;       //1表示数据, 0表示命令
sbit rw=P3^6;       //1表示读, 0表示写
sbit e=P3^5;        //使能口,下降沿有效
sbit BLACK=P3^4;
void busych_12864(void);
void wdat_12864(uchar dat_d);
void wcom_12864(uchar dat_d);
uchar rdata_12864(void);
void delay1ms(unsigned int t);
void delay1us(unsigned int t);
void ini_12864(void);
void write_12864(uchar add,uchar num,uchar *disdata);
void drawpic_12864(uchar *picture);
void cleargd_12864(void);
void drawpoint_12864(uchar x,y,w);
void drawline(uchar x1,y1,x2,y2,x);
void drawplane_12864(uchar x1,y1,x2,y2,i);
void drawsinx_12864();
void scroll_12864(uchar (*scrooldata)[16],uchar scroll_value);
extern uchar trg;
#endif