#include"myPicture.h"
namespace pic
{
    myPicture::myPicture(int col,int row,char eos)
{
    cols=col;rows=row;
    readbytenum=cols*rows;
    eosstring=eos;
}
myPicture::~myPicture()
{

}
void myPicture::getonepic()
{
    getrawdata();


}
void myPicture::getrawdata()
{
    databuf=serialport->ReadBetweenEos(readbytenum,eosstring);
}
Mat myPicture::convertType(const Mat& srcImg, int toType, double alpha, double beta)//ת����ָ����ʽMAT
{
    Mat dstImg;
    srcImg.convertTo(dstImg, toType, alpha, beta);
    return dstImg;
}
void myPicture::Mat2wxImage(Mat &mat, wxImage  &image)
{
    // data dimension
    int w = mat.cols, h = mat.rows;//��ȡMAT��ȣ��߶�
    int size = w*h*3*sizeof(unsigned char);//ΪWxImage����ռ�������Ϊ��Ϊ24bit������*3

    // allocate memory for internal wxImage data
    unsigned char * wxData = (unsigned char*) malloc(size);//����ռ�

    // the matrix stores BGR image for conversion
    Mat cvRGBImg = Mat(h, w, CV_8UC3, wxData);//ת���洢��MAT

    switch (mat.channels())//����MATͨ��������ת��
    {
    case 1: // 1-channel case: expand and copy�Ҷȵ�RGB
    {
      // convert type if source is not an integer matrix
      if (mat.depth() != CV_8U)//����unsigned char��ת��
      {
        cvtColor(convertType(mat, CV_8U, 255,0), cvRGBImg, CV_GRAY2RGB);//��ͼƬ��һ����ɫ�ռ�ת��Ϊ��һ��
      }
      else
      {
        cvtColor(mat, cvRGBImg, CV_GRAY2RGB);
      }
    } break;

    case 3: // 3-channel case: swap R&B channels
    {
      int mapping[] = {0,2,1,1,2,0}; // CV(BGR) to WX(RGB)
      // bgra[0] -> bgr[2], bgra[1] -> bgr[1], bgra[2] -> bgr[0]����ת��RGB������alphaͨ��
      mixChannels(&mat, 1, &cvRGBImg, 1, mapping, 3);//һ���������һ���������maping������������
    } break;

    default://ͨ����������
    {
      wxLogError(wxT("Cv2WxImage : input image (#channel=%d) should be either 1- or 3-channel"), mat.channels());
    }
    image.Destroy(); // free existing data if there's any
    image = wxImage(w, h, wxData);
    }
}
}
