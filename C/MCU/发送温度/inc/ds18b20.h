#ifndef _DS18B20_H_
#define _DS18B20_H_
#include <reg52.h>
#include ".\lib\delay.h"
#include ".\lib\scom.h"
unsigned char ini_ds18b20(void);
void write_byte(unsigned char com);
unsigned char read_byte(void);
void temp_trans(void);
void start_convert(void);
extern unsigned char dat[8];
#endif