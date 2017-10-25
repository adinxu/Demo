#include ".\lib\drwawave.h"
uchar xdata wavedat[256];//定义缓存空间
uchar point=0;//示波器数组元素变量
uchar trg=0,hold=0;//按键扫描 触发 连续按下 标志
uchar	KeyFuncIndex=0;
typedef struct //结构体类型声明
{ uchar   KeyStateIndex; //当前状态索引号
  uchar   KeyDnState;    //按下“向下”键时转向的索引号
  uchar   KeyUpState;    //按下“向上”键时转向的索引号
  uchar   KeyCrState;    //按下“回车”键时转向的索引号
  uchar   KeyBackState;  //按下“退回”键时转向的索引号
}  KbdTabStruct;//结构体变量声明
KbdTabStruct code KeyTab[sizeofkeybdmenu]={
{0,0,0,1,0},
{1,2,1,6,0},//示波器
{2,3,1,7,0},//绘图
{3,4,2,8,0},//画任意直线
{4,5,3,9,0},//画波形
{5,5,4,10,0},//滚动显示
{6,1,1,1,1},
{7,1,1,1,2},
{8,1,1,1,3},
{9,1,1,1,4},
{10,1,1,1,5},
};
void main()
{
ini_12864();
BLACK=0;
TMOD=0x01;
TH0=0x3c;//50ms 触发时trg有50ms为1
TL0=0xb0;
EA=1;
ET0=1;
TR0=1;//开定时中断
while(1)
	{ GetKeylnput();
	    perform();
      while(trg==0);
     }
}
//--------------------------------代码分割线----------------------------------
 void GetKeylnput()//查询菜单编号
{
	switch(trg)
	{
		case 0x01:
		KeyFuncIndex = KeyTab[KeyFuncIndex].KeyCrState;//回车键，找出新的菜单状态编号
		break;
	 case 0x02:
	  KeyFuncIndex = KeyTab[KeyFuncIndex].KeyDnState;	//向下键，找出新的菜单状态编号
		break;
   case 0x04:
		KeyFuncIndex = KeyTab[KeyFuncIndex].KeyUpState;//向上键，找出新的菜单状态编号
		break;
   case 0x08:
    KeyFuncIndex = KeyTab[KeyFuncIndex].KeyBackState;//回退键，找出新的菜单状态编号
	  break;
		 }
}
//--------------------------------代码分割线----------------------------------
	void perform()
	{if(KeyFuncIndex<5&&KeyFuncIndex)
		{	write_12864(0x80,16,menu1);
			write_12864(0x90,16,menu2);
			write_12864(0x88,16,menu3);
			write_12864(0x98,16,menu4);	//菜单内容
		}
		switch(KeyFuncIndex)
		{ case 0:
			cleargd_12864();
		  write_12864(0x80,16,disdata1);
      write_12864(0x90,16,disdata2);
      write_12864(0x88,16,disdata3);
      write_12864(0x98,16,disdata4);//开机显示画面
			break;
		  case 1: cleargd_12864();  drawplane_12864(0,0,127,15,2);break;//移动光标绘制
			case 2: cleargd_12864();	drawplane_12864(0,16,127,31,2);	break;
			case 3: cleargd_12864();	drawplane_12864(0,32,127,47,2);break;
			case 4: cleargd_12864();	drawplane_12864(0,48,127,63,2);break;
			case 5:
			wcom_12864(0x01);
			cleargd_12864();
			write_12864(0x80,16,menu5);
			drawplane_12864(0,0,127,15,2);
			break;
			case 6: wcom_12864(0x01);cleargd_12864();drwawave();break;
			case 7: wcom_12864(0x01);cleargd_12864();drawpic_12864(picture);while(trg!=0x08);	break;
			case 8: wcom_12864(0x01);cleargd_12864();drawline(0,0,127,63,1);drawline(0,63,127,0,1);while(trg!=0x08);break;
			case 9: wcom_12864(0x01);cleargd_12864();drawsinx_12864();while(trg!=0x08);break;
			case 10: cleargd_12864();scroll_12864(scrolldata1,6);break;
			}
	}
//--------------------------------代码分割线----------------------------------
void drwawave()
{ uchar res[2];//定义转换结果存放位
	uchar x_t=0;//定义周期控制
	uchar x;
  wcom_12864(0x34);
  wcom_12864(0x36);//开绘图显示
wavedat[0]=getone_adc();//第一次写ADC_CONTR要等四个时钟周期
EADC=1;//开AD中断
    while(trg!=0x08)
        {
        for(x=0;x<2;x++)
				{point=point+128;
				if(!wavedat[point])
				res[x]=63;
				else res[x]=trans(wavedat[point]);
			  }//如果是0，直接赋值，不计算
	      ADC_CONTR=contr;
	      drawpoint_12864(point%128,res[0],0);
        drawpoint_12864(point%128,res[1],1);
				if(trg&0x06)
				{x_t=x_t+(trg&0x02)*1+(trg&0x04)*(-1);
				if(x_t>10) x_t=0;}
				if(x_t)
				delay1ms(x_t);//控制扫描周期调试
		    point++;
        }
wcom_12864(0x30);
ADC_CONTR=0;//停止转换
P1ASF=0X00;//关闭P1.0模拟功能
EADC=0;//关闭AD中断
}
//--------------------------------代码分割线----------------------------------
void int_t0(void) interrupt 1
{ static uchar check=0;
	register uchar readdata=PINB^0xff;//获取键值（触发者被置1）
	if(hold==readdata) trg=check;//消抖                   跳变不会引发赋值，跳变点后赋值 （每次只能按下一键，不然可能出错 eg：按了一键之后在一周期内再按）
  check=readdata&(readdata^hold);//保证trg只被触发一次    记录跳变点 键值稳定时为0，异或保证了跳变准确记录 与 为了干扰的情况，即只记录保持了一周期的跳变
  hold =readdata;//状态更新
TH0=0x3c;
TL0=0xb0;}
/*
readata hold
0         0有效跳变/平稳状态
1         0跳变点/干扰（假的按下）
1         1有效跳变/平稳状态
0         1跳变点/干扰（假的松键）
*/
//--------------------------------代码分割线----------------------------------
void int_adc() interrupt 5
{ADC_CONTR&=~ADC_FLAG;//转换停止
point=point+1;
wavedat[point]=ADC_RES;
point=point-1;
}
