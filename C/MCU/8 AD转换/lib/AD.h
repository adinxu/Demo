#ifndef _AD_H_
#define _AD_H_
#include <reg51.h>
#include <intrins.h>
#include ".\lib\typedef.h"
sfr ADC_CONTR=0XBC;
sfr ADC_RES=0XBD;
sfr ADC_RESL=0XBE;
sfr P1ASF=0X9D;
sfr AUXR1=0XA2;
#define ADC_POWER  0x80 
#define ADC_FLAG  0x10 
#define ADC_START  0x08 
#define ADC_SPEED  0x20 //90
#define ADC_INI  0x00
void ini_adc();
void start_adc();




//uchar read_adc();
void led_display(uchar which_led,uchar dat);
void vin_display(uint date) ;
void delay5ms(void);
void light_control(uint date);
sbit lamp=P1^7;
#endif