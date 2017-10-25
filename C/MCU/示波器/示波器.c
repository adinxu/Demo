#include ".\lib\drwawave.h"
uchar xdata wavedat[256];//���建��ռ�
uchar point=0;//ʾ��������Ԫ�ر���
uchar trg=0,hold=0;//����ɨ�� ���� �������� ��־
uchar	KeyFuncIndex=0;
typedef struct //�ṹ����������
{ uchar   KeyStateIndex; //��ǰ״̬������
  uchar   KeyDnState;    //���¡����¡���ʱת���������
  uchar   KeyUpState;    //���¡����ϡ���ʱת���������
  uchar   KeyCrState;    //���¡��س�����ʱת���������
  uchar   KeyBackState;  //���¡��˻ء���ʱת���������
}  KbdTabStruct;//�ṹ���������
KbdTabStruct code KeyTab[sizeofkeybdmenu]={
{0,0,0,1,0},
{1,2,1,6,0},//ʾ����
{2,3,1,7,0},//��ͼ
{3,4,2,8,0},//������ֱ��
{4,5,3,9,0},//������
{5,5,4,10,0},//������ʾ
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
TH0=0x3c;//50ms ����ʱtrg��50msΪ1
TL0=0xb0;
EA=1;
ET0=1;
TR0=1;//����ʱ�ж�
while(1)
	{ GetKeylnput();
	    perform();
      while(trg==0);
     }
}
//--------------------------------����ָ���----------------------------------
 void GetKeylnput()//��ѯ�˵����
{
	switch(trg)
	{
		case 0x01:
		KeyFuncIndex = KeyTab[KeyFuncIndex].KeyCrState;//�س������ҳ��µĲ˵�״̬���
		break;
	 case 0x02:
	  KeyFuncIndex = KeyTab[KeyFuncIndex].KeyDnState;	//���¼����ҳ��µĲ˵�״̬���
		break;
   case 0x04:
		KeyFuncIndex = KeyTab[KeyFuncIndex].KeyUpState;//���ϼ����ҳ��µĲ˵�״̬���
		break;
   case 0x08:
    KeyFuncIndex = KeyTab[KeyFuncIndex].KeyBackState;//���˼����ҳ��µĲ˵�״̬���
	  break;
		 }
}
//--------------------------------����ָ���----------------------------------
	void perform()
	{if(KeyFuncIndex<5&&KeyFuncIndex)
		{	write_12864(0x80,16,menu1);
			write_12864(0x90,16,menu2);
			write_12864(0x88,16,menu3);
			write_12864(0x98,16,menu4);	//�˵�����
		}
		switch(KeyFuncIndex)
		{ case 0:
			cleargd_12864();
		  write_12864(0x80,16,disdata1);
      write_12864(0x90,16,disdata2);
      write_12864(0x88,16,disdata3);
      write_12864(0x98,16,disdata4);//������ʾ����
			break;
		  case 1: cleargd_12864();  drawplane_12864(0,0,127,15,2);break;//�ƶ�������
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
//--------------------------------����ָ���----------------------------------
void drwawave()
{ uchar res[2];//����ת��������λ
	uchar x_t=0;//�������ڿ���
	uchar x;
  wcom_12864(0x34);
  wcom_12864(0x36);//����ͼ��ʾ
wavedat[0]=getone_adc();//��һ��дADC_CONTRҪ���ĸ�ʱ������
EADC=1;//��AD�ж�
    while(trg!=0x08)
        {
        for(x=0;x<2;x++)
				{point=point+128;
				if(!wavedat[point])
				res[x]=63;
				else res[x]=trans(wavedat[point]);
			  }//�����0��ֱ�Ӹ�ֵ��������
	      ADC_CONTR=contr;
	      drawpoint_12864(point%128,res[0],0);
        drawpoint_12864(point%128,res[1],1);
				if(trg&0x06)
				{x_t=x_t+(trg&0x02)*1+(trg&0x04)*(-1);
				if(x_t>10) x_t=0;}
				if(x_t)
				delay1ms(x_t);//����ɨ�����ڵ���
		    point++;
        }
wcom_12864(0x30);
ADC_CONTR=0;//ֹͣת��
P1ASF=0X00;//�ر�P1.0ģ�⹦��
EADC=0;//�ر�AD�ж�
}
//--------------------------------����ָ���----------------------------------
void int_t0(void) interrupt 1
{ static uchar check=0;
	register uchar readdata=PINB^0xff;//��ȡ��ֵ�������߱���1��
	if(hold==readdata) trg=check;//����                   ���䲻��������ֵ��������ֵ ��ÿ��ֻ�ܰ���һ������Ȼ���ܳ��� eg������һ��֮����һ�������ٰ���
  check=readdata&(readdata^hold);//��֤trgֻ������һ��    ��¼����� ��ֵ�ȶ�ʱΪ0�����֤������׼ȷ��¼ �� Ϊ�˸��ŵ��������ֻ��¼������һ���ڵ�����
  hold =readdata;//״̬����
TH0=0x3c;
TL0=0xb0;}
/*
readata hold
0         0��Ч����/ƽ��״̬
1         0�����/���ţ��ٵİ��£�
1         1��Ч����/ƽ��״̬
0         1�����/���ţ��ٵ��ɼ���
*/
//--------------------------------����ָ���----------------------------------
void int_adc() interrupt 5
{ADC_CONTR&=~ADC_FLAG;//ת��ֹͣ
point=point+1;
wavedat[point]=ADC_RES;
point=point-1;
}
