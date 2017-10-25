#include ".\lib\12864.h"
uchar  code    disdata1[16]={"从左到右依次为  "};//开机显示画面第一行
uchar  code    disdata2[16]={"S0  确认 S1 向下"};//开机显示画面第二行
uchar  code    disdata3[16]={"S2  向上 S3 返回"};//开机显示画面第一行
uchar  code    disdata4[16]={"按S0进入功能选择"};//开机显示画面第二行
uchar  code    menu1[16]={"示波器          "};
uchar  code    menu2[16]={"绘图            "};
uchar  code    menu3[16]={"画交叉线        "};
uchar  code    menu4[16]={"画波形          "};
uchar  code    menu5[16]={"滚动显示        "};
uchar  xdata    scrolldata1[][16]={{"滚              "},{"  动            "},{"    显          "},{"      示        "},{"        测      "},{"          试    "}};//一级菜单
uchar  code		picture[1024]={
/*--  宽度x高度=128x64  --*/
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3F,0x80,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3F,0xE0,0x00,0xFF,0xE0,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x03,0xC0,0x03,0xFF,0xFF,0xE1,0xFF,0xF0,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0xF8,0xFF,0xFF,0xFF,0xFF,0xFF,0xFC,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x7F,0xFF,0xFF,0xFF,0xFC,0x1F,0xFF,0xFC,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0x9F,0xFF,0xFF,0xE7,0xFF,0xFC,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x01,0xFF,0xFE,0x7C,0x1F,0xFF,0xFD,0xFF,0xFC,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x01,0xFF,0xFD,0xC0,0x00,0x01,0xFD,0xFF,0xFE,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x03,0xFF,0xF7,0x80,0x00,0x00,0x3E,0x7F,0xFE,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x03,0xFF,0xFF,0x00,0x00,0x00,0x03,0xFF,0xFE,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x03,0xFF,0xEF,0x80,0x00,0x0F,0xC1,0xFF,0xFE,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x03,0xFF,0xFF,0x7F,0xC0,0x7F,0xC0,0xDF,0xFC,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x03,0xFF,0xFF,0xFF,0xC0,0x7F,0xF8,0x6F,0xFC,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x01,0xFF,0xBF,0xFF,0x80,0x3F,0xFF,0xBF,0xF8,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xAF,0xEF,0xE4,0xF0,0x07,0xFF,0xF0,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFE,0x1C,0x7D,0xFF,0xC3,0xFF,0xE0,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x7F,0xFD,0xFF,0xF9,0xBF,0xF9,0xBB,0x80,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x0E,0xEF,0xFF,0xE0,0x7F,0x7C,0x0F,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x06,0xF3,0xF0,0x39,0x8C,0x80,0x07,0x80,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x0F,0xE6,0x3F,0x79,0xCF,0xC0,0x07,0xC0,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x0F,0xED,0x00,0x70,0xC0,0x00,0x06,0xC0,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0xEE,0x00,0x00,0x00,0x00,0x03,0xE0,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0xC0,0x00,0xC0,0x20,0x00,0x01,0xE0,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x80,0x01,0xC0,0x38,0x00,0x03,0xF0,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x80,0x03,0x00,0x1C,0x00,0x00,0xF0,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x80,0x07,0x80,0x0E,0x00,0x00,0xF0,0x00,0x00,
0x00,0x00,0xFF,0xF8,0x00,0x00,0x1F,0x80,0x0F,0xFF,0xFF,0x00,0x01,0xF8,0x00,0x00,
0x00,0xFF,0xFF,0xFF,0x00,0x00,0x3F,0xC3,0x1F,0xFF,0xFB,0xC0,0x01,0xBC,0x00,0x00,
0x00,0xFF,0xFF,0xFF,0x80,0x00,0x7F,0xC3,0xFC,0x7F,0x03,0xF0,0x01,0xBF,0x00,0x00,
0x00,0xFF,0xFF,0xFF,0xF0,0x00,0x7B,0xE3,0xF8,0x00,0x01,0xF0,0x07,0xBF,0x80,0x00,
0x00,0x01,0xFF,0xFF,0xFC,0x00,0xFF,0xE3,0xBF,0xFF,0xFF,0xF0,0x07,0xBF,0x80,0x00,
0x00,0x00,0xFF,0xFF,0xFF,0x81,0xFF,0xF1,0xCF,0xFF,0xFF,0x40,0x07,0xFF,0x80,0x00,
0x00,0x00,0xFF,0xFF,0xFF,0xF3,0xFF,0xF0,0xC3,0xC0,0x3E,0x00,0x0F,0x7F,0x80,0x00,
0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFD,0xF8,0xC1,0xFF,0x78,0x00,0x1E,0xFF,0x80,0x00,
0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFD,0xF8,0x43,0x9F,0x00,0x02,0x3F,0xFF,0x80,0x00,
0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFE,0xFC,0x01,0xF0,0x00,0x02,0x7B,0xFF,0x80,0x00,
0x00,0x00,0x7F,0xFF,0xFF,0xFF,0xFF,0xBE,0x18,0x7F,0x80,0x01,0xE7,0xFF,0x80,0x00,
0x00,0x00,0x3F,0xFF,0xFF,0xFF,0xFF,0xDF,0x18,0x0F,0x01,0x0F,0xDF,0xFF,0x80,0x00,
0x00,0x00,0x3F,0xFF,0xFF,0xFF,0xFF,0xE3,0xC0,0x00,0x03,0xFF,0x7F,0xFF,0x80,0x00,
0x00,0x00,0x07,0xFF,0xFF,0xFF,0xFF,0xF8,0x78,0x00,0x7F,0xF8,0xFF,0xFF,0x80,0x00,
0x00,0x00,0x00,0x7F,0xFF,0xFF,0xFF,0xFF,0xC6,0xFF,0xFF,0x87,0xFF,0xFF,0x80,0x00,
0x00,0x00,0x00,0x1F,0xFF,0xFF,0xFF,0xFF,0xFC,0x01,0xB0,0xFF,0xFF,0xFF,0x80,0x00,
0x00,0x00,0x00,0x0F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xEF,0xFF,0xFF,0xFF,0x80,0x00,
0x00,0x00,0x00,0x01,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x80,0x00,
0x00,0x00,0x00,0x00,0x1F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x80,0x00,
0x00,0x00,0x00,0x00,0x03,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x1F,0xFE,0x0E,0x07,0xFF,0x03,0xC0,0x67,0x0F,0xFF,0x00,0x00,
0x00,0x00,0x00,0x00,0x1F,0xFE,0xFF,0xF7,0xFF,0x3F,0xF8,0x46,0x0F,0xFF,0x00,0x00,
0x00,0x00,0x00,0x00,0x1F,0xFE,0xFF,0xF1,0xCF,0x3C,0xD9,0xF7,0xE7,0xEC,0x00,0x00,
0x00,0x00,0x00,0x00,0x1F,0xFE,0x7F,0xC0,0x38,0x7F,0xFD,0xBD,0xE7,0xEC,0x00,0x00,
0x00,0x00,0x00,0x00,0x1C,0xC0,0x7F,0xC0,0x30,0x7F,0xFD,0xFE,0xFF,0xFF,0x00,0x00,
0x00,0x00,0x00,0x00,0x1C,0xFD,0xFF,0xF0,0x30,0x0F,0xD9,0xF7,0xFF,0xFF,0x00,0x00,
0x00,0x00,0x00,0x00,0x1D,0xFD,0xFF,0xF0,0x30,0x7F,0xFB,0xFB,0xE7,0xEC,0x00,0x00,
0x00,0x00,0x00,0x00,0x19,0x8C,0x7F,0xC0,0x30,0x7C,0x61,0xB8,0xE7,0xEC,0x00,0x00,
0x00,0x00,0x00,0x00,0x3F,0xBC,0x7F,0xC0,0xF0,0x1F,0xFD,0xFB,0xE7,0xFC,0x00,0x00,
0x00,0x00,0x00,0x00,0x3F,0x3C,0x7F,0xC0,0xF0,0x1F,0xBD,0xBB,0xC4,0x3C,0x00,0x00,
0x00,0x00,0x00,0x00,0x36,0x30,0x60,0x40,0xC0,0x18,0x18,0x03,0x80,0x38,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};
//--------------------------------代码分割线----------------------------------
void busych_12864(void)
{
  lcddata=0xff;
  rs=0;
  rw=1;
  e =1;
  delay1us(1);//12864M-3模块要求延时0.5微秒以上
  while(busy==1);
  e =0;
  delay1us(1);//12864M-3模块要求延时0.5微秒以上
}
//--------------------------------代码分割线----------------------------------
void wdat_12864(uchar dat_d)
{
//并口使用时
	busych_12864();
	rs=1;
	rw=0;
	lcddata=dat_d;
	e=1;
	delay1us(1);//12864M-3模块要求延时0.5微秒以上
	e=0;//
    delay1us(1);//12864M-3模块要求延时0.5微秒以上
}
//--------------------------------代码分割线----------------------------------
void wcom_12864(uchar com_d)
{
 //并口使用时
	   busych_12864();
		rs=0;
		rw=0;
		lcddata=com_d;
		e=1;
		delay1us(1);//12864M-3模块要求延时0.5微秒以上;
		e=0;//
        delay1us(1);//12864M-3模块要求延时0.5微秒以上
}
//--------------------------------代码分割线----------------------------------
unsigned char rdat_12864(void)
{
    unsigned char byReturnValue ;
   busych_12864();
    lcddata=0xff ;
    rs=1 ;
    rw=1 ;
    e=1 ;
	e=1 ;
	 byReturnValue=lcddata;
    e=0 ;
     return byReturnValue ;
}
//
//--------------------------------代码分割线----------------------------------
void delay1ms(unsigned int t)
{
uint i,j;
for(i=0;i<t;i++)
   for(j=0;j<2100;j++)
   ;
}
//--------------------------------代码分割线----------------------------------
void delay1us(unsigned int t)
{
    unsigned int i ;
    for(i=0;i<t;i++); 
}
//--------------------------------代码分割线----------------------------------
void ini_12864(void)
{
delay1ms(40);
lcd_ret = 0;
lcd_ret = 1;
wcom_12864(0x30); 
delay1us(100);
wcom_12864(0x30);
delay1us(37);
wcom_12864(0x0c);
delay1us(100);
wcom_12864(0x01);
delay1ms(10);
wcom_12864(0x06);
}
//--------------------------------代码分割线----------------------------------
void write_12864(uchar add,uchar num,uchar *disdata)
{uchar i;
wcom_12864(add);
for(i=0;i<num;i++)
   {wdat_12864(*(disdata+i));
   }
}
//--------------------------------代码分割线----------------------------------
void drawpic_12864(uchar *picture)
{uchar i,j,k;
wcom_12864(0x34);	//开扩展指令集
wcom_12864(0x36);
for(i=0;i<2;i++)//i=0上半屏,i=1下半屏
	{for(j=0;j<32;j++)//水平地址值，共32行
		{wcom_12864(0x80+j);//写水平地址
			if(i==0) wcom_12864(0x80);//写垂直
			else wcom_12864(0x88);
			for(k=0;k<16;k++)//写一行数据，有8个字，即16字节
		      wdat_12864(*picture++);
		}
	}
wcom_12864(0x30);
}
//--------------------------------代码分割线----------------------------------
void cleargd_12864(void)
{uchar i,j,k;
wcom_12864(0x34);	//开扩展指令集
for(i=0;i<2;i++)//i=0上半屏,i=1下半屏
	{for(j=0;j<32;j++)//水平地址值，共32行
		{wcom_12864(0x80+j);//写水平地址
			if(i==0) wcom_12864(0x80);//写垂直
			else wcom_12864(0x88);
			for(k=0;k<16;k++)//写一行数据，有8个字，即16字节
		      wdat_12864(0x00);
		}
	}
wcom_12864(0x36);
wcom_12864(0x30);
}
//--------------------------------代码分割线----------------------------------
void drawpoint_12864(uchar x,y,w)//值域问题 0<x<127;0<y<63; 0为写0,1为写1,2为写反
{uchar X,Y,K,write_flag=0;
uchar DL,DH;
if(y>=0&&y<=63&&x>=0&&x<=127)
{
if(y<32) 
{Y=0x80+y;
X=0x80+(x>>4);//x/16,将x看做16进制数，则x/16就等于把x小数点左移一位（参考十进制数除以10），又因为整数除法无小数点，所以可以直接舍掉一位
}

else 
{Y=0x80+(y-32);
X=0x88+(x>>4);//数据分上下两屏，所以加上基数8
}
wcom_12864(Y);
wcom_12864(X);
rdat_12864();//假读！！！！！！！！！！
DH=rdat_12864();
DL=rdat_12864();
K=x%16;
if((0x01<<(7-K%8))&(DH*(K<8)+DL*(K>=8)))
{	if(w!=1) 
	{
	write_flag=1;	
	if(K<8) DH&=~(0x01<<(7-K%8));	//数据是倒着送进去的。  //获得一个想要的数，用立即数进行运算得到，不必占用变量空间
	else    DL&=~(0x01<<(7-K%8));
	}
}
else
{
	if(w) 
  {
	write_flag=1;
	if(K<8) DH|=(0x01<<(7-K%8));
	else    DL|=(0x01<<(7-K%8));
	}
}
if(write_flag)
{wcom_12864(Y);
wcom_12864(X);
wdat_12864(DH);
wdat_12864(DL);}
}

}
//--------------------------------代码分割线----------------------------------
void drawline(uchar x1,y1,x2,y2,w)
{uchar i;
float k;	
wcom_12864(0x34);	//开扩展指令集
wcom_12864(0x36);
	  if(x1==x2)//画竖线
			{if(y2>=y1) i=y1;
			else {i=y2;y2=y1;}
			for(;i<=y2;i++)
			drawpoint_12864(x1,i,w);
			}
	  else 
		 {
			 if(x2>x1) 
			{
			k=(float)(y2-y1)/(x2-x1);
			i=x1;}
		 else 
			{
			k=(float)(y1-y2)/(x1-x2);
			i=x2;
			x2=x1;
			}  
			for(;i<=x2;i++)
			{drawpoint_12864(i,(uchar)((k*i+y1)+0.5),w);}
		  }
wcom_12864(0x30);		
}
//--------------------------------代码分割线----------------------------------
void drawplane_12864(uchar x1,y1,x2,y2,w)
{
	if(x1<x2&&y1<y2)
	  {wcom_12864(0x34);	//开扩展指令集
		for(;x1<=x2;x1++)
			   {drawline(x1,y1,x1,y2,w);
				 }	
 wcom_12864(0x36);
		 wcom_12864(0x30);
		}
}
//--------------------------------代码分割线----------------------------------
void drawsinx_12864()//只能显示正值函数波形！！！！！
{uchar i;
	wcom_12864(0x34);	//开扩展指令集
	for(i=0;i<128;i++)
      {drawpoint_12864(i,32-32*sin(0.05*i),1);			 
      }
wcom_12864(0x36);
wcom_12864(0x30);	
}
//--------------------------------代码分割线----------------------------------
void scroll_12864(uchar (*scrooldata)[16],uchar scroll_value)
{
uchar add=0x80,location=3,i,j;//要写入的ddram地址 卷动数据所在位置 变量i
for(i=0;i<6;i++)//先行写入 三行
write_12864(add+i*8,16,scrooldata+i/2+(i%2)*2);
i=1;
add=0xb0;
while(1)
{
write_12864(add,16,scrooldata+location%scroll_value);
write_12864(add+8,16,scrooldata+(location+2)%scroll_value);
location++;//溢出风险，超过255次之后
add+=0x10;
if(add==0xc0) add=0x80;
wcom_12864(0x34);	//开扩展指令集
wcom_12864(0x03);	//允许输入卷动地址
for(;i%16;i++)
	{
    wcom_12864(0x40+i);
    for(j=0;j<7;j++)		
		{if(trg==0x08)
			goto jump;
		delay1ms(5);}
	}	 
	if(i==64) i=0;
	wcom_12864(0x40+i);	
    for(j=0;j<7;j++)		
		{if(trg==0x08)
			goto jump;
		delay1ms(5);}
	i++;
	wcom_12864(0x30);	
}
jump:wcom_12864(0x34);	//开扩展指令集
wcom_12864(0x03);	//允许输入卷动地址
wcom_12864(0x40);
wcom_12864(0x30);	
}