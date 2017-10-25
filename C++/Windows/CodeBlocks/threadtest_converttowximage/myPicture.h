#ifndef MYPICTURE_H_INCLUDED
#define MYPICTURE_H_INCLUDED
#include"mySerialPort.h"
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/objdetect/objdetect.hpp>
#include<wx/image.h>
namespace pic
{
class myPicture
{
public:
    myPicture(int col,int row,char eos);
    ~myPicture();
    void getonepic();

private:
    int cols,rows;
    DWORD readbytenum;
    char* databuf;
    mySerialPort* serialport;
    char eosstring;

    void getrawdata();
    Mat convertType(const Mat& srcImg, int toType, double alpha, double beta);//转换到指定格式MAT
    void Mat2wxImage(Mat &mat, wxImage  &image);
};
}

#endif // MYPICTURE_H_INCLUDED
