#include<stdio.h>
#include<windows.h>

bool readMap(char *pBmpName);
bool  saveBmp(char  *bmpName, unsigned  char  *imgBuf, int  Swidth, int  Sheight, int  SbiBitCount);
unsigned char *pBmpbuf;   //���ͼƬ��·��
int bmpWidth;      // ͼ����
int bmpHight;      //ͼ��߶�
RGBQUAD *pColorTable;  //��ɫ����ָ�룬//û�õ�
int biBitCount;   //ͼ�����ͣ�ÿ����λ

int main()
{
	char readname[] = "C:\\Users\\13345\\Desktop\\picture\\lena.bmp";
	char savename[] = "C:\\Users\\13345\\Desktop\\picture\\text.bmp";
	readMap(readname);
	printf("width=%d,height=%d,bi Bit Count=%d\n", bmpWidth, bmpHight, biBitCount);
	int lineBite = (bmpWidth*biBitCount / 8 + 3) / 4 * 4;
	for (int i = 0; i < bmpWidth / 2; i++)
	{
		for (int j = 0; j < bmpHight / 2; j++)
		{
			for (int k = 0; k < 3; k++)
			{
				*(pBmpbuf + i*lineBite + j*3 + k) = 0;
			}
		}
	}

	if(saveBmp(savename, pBmpbuf,512,512,24)==0)printf("ת��ͼƬʧ��");
	delete pBmpbuf;
	getchar();
	return 0;
}

bool readMap(char *pBmpName)
{
	FILE *fp = fopen(pBmpName, "rb+");
	if (fp == 0)return 0;
	fseek(fp, sizeof(BITMAPFILEHEADER), 0);
	BITMAPINFOHEADER   head;
	fread(&head, sizeof(BITMAPINFOHEADER), 1, fp);
	bmpWidth = head.biWidth;
	bmpHight = head.biHeight;
	biBitCount = head.biBitCount;
	int lineBite = (bmpWidth*biBitCount / 8 + 3) / 4 * 4;
	if (biBitCount == 8)
	{
		pColorTable = new RGBQUAD[256];
		fread(pColorTable,sizeof(RGBQUAD),256,fp);
	}
	pBmpbuf = new unsigned char[lineBite*bmpHight];
	fread(pBmpbuf, 1, lineBite*bmpHight, fp);
	fclose(fp);
	return 1;
}

bool  saveBmp(char  *bmpName, unsigned  char  *imgBuf, int  Swidth, int Sheight,int  SbiBitCount)
{
	//���λͼ����ָ��Ϊ0����û�����ݴ��룬��������

	if (!imgBuf)
		return  0;
	//��ɫ���С�����ֽ�Ϊ��λ���Ҷ�ͼ����ɫ��Ϊ1024�ֽڣ���ɫͼ����ɫ���СΪ0
		int  colorTablesize = 0;
	//if (biBit Count == 8)
	//	color Tablesize = 1024;
	//���洢ͼ������ÿ���ֽ���Ϊ4�ı���

		int  lineByte = (Swidth  *  SbiBitCount / 8 + 3) / 4 * 4;
	//�Զ�����д�ķ�ʽ���ļ�
		FILE  *fp = fopen(bmpName, "wb+");
	if (fp == 0)  return  0;
	//����λͼ�ļ�ͷ�ṹ��������д�ļ�ͷ��Ϣ

BITMAPFILEHEADER  fileHead;
fileHead.bfType = 0x4D42;//bmp ����
//bf Size ��ͼ���ļ�4����ɲ���֮��
fileHead.bfSize = sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER)+colorTablesize + lineByte*Sheight;
fileHead.bfReserved1 = 0;
fileHead.bfReserved2 = 0;


//bf Off Bits ��ͼ���ļ�ǰ3����������ռ�֮��
fileHead.bfOffBits = 54 ;
//д�ļ�ͷ���ļ�

fwrite(&fileHead, sizeof(BITMAPFILEHEADER), 1, fp);
//����λͼ��Ϣͷ�ṹ��������д��Ϣͷ��Ϣ

BITMAPINFOHEADER  head;
head.biBitCount = SbiBitCount;
head.biClrImportant = 0;
head.biClrUsed = 0;
head.biCompression = 0;
head.biHeight = Sheight;
head.biPlanes = 1;
head.biSize = 40;
head.biSizeImage = lineByte*Sheight;
head.biWidth = Swidth;
head.biXPelsPerMeter = 0;
head.biYPelsPerMeter = 0;
//дλͼ��Ϣͷ���ڴ�
fwrite(&head, sizeof(BITMAPINFOHEADER), 1, fp);
//����Ҷ�ͼ������ɫ��д���ļ�
//if (biBitCount == 8)
//fwrite(pColorTable, sizeof(RGBQUAD), 256, fp);
//дλͼ���ݽ��ļ�

fwrite(imgBuf, Sheight*lineByte, 1, fp);
//�ر��ļ�

fclose(fp);
return  1;
}


