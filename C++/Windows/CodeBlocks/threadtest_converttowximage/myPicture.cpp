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
Mat myPicture::convertType(const Mat& srcImg, int toType, double alpha, double beta)//转换到指定格式MAT
{
    Mat dstImg;
    srcImg.convertTo(dstImg, toType, alpha, beta);
    return dstImg;
}
void myPicture::Mat2wxImage(Mat &mat, wxImage  &image)
{
    // data dimension
    int w = mat.cols, h = mat.rows;//获取MAT宽度，高度
    int size = w*h*3*sizeof(unsigned char);//为WxImage分配空间数，因为其为24bit，所以*3

    // allocate memory for internal wxImage data
    unsigned char * wxData = (unsigned char*) malloc(size);//分配空间

    // the matrix stores BGR image for conversion
    Mat cvRGBImg = Mat(h, w, CV_8UC3, wxData);//转换存储用MAT

    switch (mat.channels())//根据MAT通道数进行转换
    {
    case 1: // 1-channel case: expand and copy灰度到RGB
    {
      // convert type if source is not an integer matrix
      if (mat.depth() != CV_8U)//不是unsigned char先转化
      {
        cvtColor(convertType(mat, CV_8U, 255,0), cvRGBImg, CV_GRAY2RGB);//把图片从一个颜色空间转换为另一个
      }
      else
      {
        cvtColor(mat, cvRGBImg, CV_GRAY2RGB);
      }
    } break;

    case 3: // 3-channel case: swap R&B channels
    {
      int mapping[] = {0,2,1,1,2,0}; // CV(BGR) to WX(RGB)
      // bgra[0] -> bgr[2], bgra[1] -> bgr[1], bgra[2] -> bgr[0]，即转成RGB，舍弃alpha通道
      mixChannels(&mat, 1, &cvRGBImg, 1, mapping, 3);//一个输入矩阵，一个输出矩阵，maping中三个索引对
    } break;

    default://通道数量不对
    {
      wxLogError(wxT("Cv2WxImage : input image (#channel=%d) should be either 1- or 3-channel"), mat.channels());
    }
    image.Destroy(); // free existing data if there's any
    image = wxImage(w, h, wxData);
    }
}
}
