#ifndef _12864_h_
#define _12864_h_
#include <reg51.h>
#include <math.h>
#include ".\lib\typedef.h"
#define lcddata P2      //12864Һ�����ݿ�
sbit busy=lcddata^7;    //12864Һ��"æ"�ӿ�
sbit lcd_ret=P0^6;    //Һ����λ���͵�ƽ��Ч
sbit rs=P3^7;       //1��ʾ����, 0��ʾ����
sbit rw=P3^6;       //1��ʾ��, 0��ʾд
sbit e=P3^5;        //ʹ�ܿ�,�½�����Ч
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