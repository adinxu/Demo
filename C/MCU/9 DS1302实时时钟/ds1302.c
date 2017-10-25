#define flag 0//1为实际调试，0为仿真
#if flag
#include"STC12C5A16S2.H"
sbit CE=P4^6;
sbit SCLK=P4^5;
sbit IO=P4^1;
#else
#include<reg51.h>
sbit CE=P1^3;
sbit SCLK=P1^1;
sbit IO=P1^2;
sbit key1=P3^5;//调整
sbit key2=P3^6;//加1
sbit BUZZ=P3^7;
#endif

#define uchar unsigned char
#define uint unsigned int
#define time 50
uchar s_reg,m_reg,h_reg,setflag;//程序级全局变量定义  指针变量（数组）可在主函数中定义并以参数传递的方式以扩展作用域与生存时间
void reset();
void write_onebyte(uchar com);
uchar read_onebyte();
void readtime();
void deal(uchar *);
void keywork(uchar *dis);
void statetrans(uchar *);
void display(uchar *);
void delay1ms(int t);
void reset()//初始化???????????????????????????????????????
{
   CE = 0;
   SCLK = 0;
   CE = 0;
   SCLK = 0;
   CE = 1;
}
void write_onebyte(uchar com)
{uchar i;
for(i=0;i<8;i++)
  {	SCLK=0;
	IO =com&0x01;
	SCLK = 1;
	com>>= 1;
   }
}
uchar read_onebyte()
{uchar i,val;
IO=1;//置1读引脚                 读引脚与读锁存？？？？？？？？？？？？？？？？？？？？？？？？？？？？
for(i=0;i<8;i++)
  {	SCLK=0;
	val>>= 1;
	if(IO) val|=0x80;
	SCLK = 1;
  }
return val;
}
void readtime()
{
reset();
write_onebyte(0xbf);//连续读模式
s_reg=read_onebyte()&0x7f;//有返回值函数的用法
m_reg=read_onebyte()&0x7f;
h_reg=read_onebyte()&0x3f;
reset();
}
void deal(uchar *dis)//用指针表示数组元素及用指针作为形参
{dis[0]=h_reg>>4;
dis[1]=h_reg&0x0f;
dis[2]=10;
dis[3]=m_reg>>4;
dis[4]=m_reg&0x0f;
dis[5]=10;
dis[6]=s_reg>>4;
dis[7]=s_reg&0x0f;
}
void keywork(uchar *dis)
{if(key1==0)
  {setflag++;

  while(!key1) display(dis);
  }
 if(key2==0)
    { switch(setflag)
     {case 0 :break;
	  case 1 :break;
	  case 2:h_reg++;if ((h_reg&0x0f)>9) h_reg=(h_reg&0xf0)+0x10;if (h_reg>0x23) h_reg=0;break;
	  case 3:m_reg++;if ((m_reg&0x0f)>9) m_reg=(m_reg&0xf0)+0x10;if (m_reg>0x59) m_reg=0;break;
      } 
     while(!key2) display(dis);
    }
    if(setflag==5)  setflag=0;
}
void statetrans(uchar *dis)  //指针变量的类型标识符表示的是所指向变量的变量类型
{
static uchar counter;
 if(setflag)
   {uchar i;
   if(setflag==1) 
       {reset();
	   write_onebyte(0x80);
        write_onebyte(0x80);//停止计数
		reset();
		s_reg=0;//clear
		setflag++; 
       }
   if(setflag==2) i=0;
    if(setflag==3) i=3;
   if(counter<time) 
	   {dis[i]=11;
	    dis[i+1]=11;
	    }
    else if(counter>=2*time) counter=0;
    counter++;
   if(setflag==4) 
        {reset();
		write_onebyte(0xbe);
		write_onebyte(0x00);
		write_onebyte(m_reg);
		write_onebyte(h_reg);
		write_onebyte(0x00);
		write_onebyte(0x00);
		write_onebyte(0x00);
		write_onebyte(0x00);
		write_onebyte(0x00);
		reset();
		setflag++;
        }
   }
}
void display(uchar *dis)
{uchar i=0;
uchar codetable[]={0xc0,0xf9,0xa4,0xb0,0x99,0x92,0x82,0xf8,0x80,0x90,0xbf,0xff};
//                  0    1    2    3     4    5    6    7    8    9   -    熄灭
 P2=0x01;
 for(;i<8;i++)
    {P0=codetable[dis[i]];
	 delay1ms(1);
     P0=0xff;
     P2<<=1;
	 }
}
void main()
{uchar dis[8];//减少全局变量应用，提高子函数独立性
reset();
write_onebyte(0x8e);
write_onebyte(0x00);//写保护关
write_onebyte(0x84);
write_onebyte(0x00);//24小时制
write_onebyte(0x80);
write_onebyte(0x00);//开始计数
reset();
while(1)
  {
  if(setflag==0)
    {readtime();
    } 
    deal(dis);
    keywork(dis);
    statetrans(dis);
    display(dis);
  }
}
void delay1ms(int t)  
{unsigned char a,b;
for(;t>0;t--)
    for(b=2;b>0;b--)
        for(a=250;a>0;a--);
}
