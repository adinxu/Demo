#ifndef _DS18B20_H_
#define _DS18B20_H_
#include <reg52.h>
#include <intrins.h>
#include ".\lib\delay_1T.h"
unsigned char ini_ds18b20(void);
void write_byte(unsigned char com);
unsigned char read_byte(void);
void temp_trans(void);
void start_convert(void);
void temp_set(void);
#endif