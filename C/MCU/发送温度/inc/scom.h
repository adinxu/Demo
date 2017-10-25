#ifndef _SCOM_H_
#define _SCOM_H_
#include <reg52.h>
#include <stdio.h>
#include <string.h>
#include ".\lib\typedef.h"
#include ".\lib\delay.h"
void ini_scom(void);
void sendbyte_scom(int val);
#endif
