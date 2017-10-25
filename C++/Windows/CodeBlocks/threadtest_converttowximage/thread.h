#ifndef THREAD_H_INCLUDED
#define THREAD_H_INCLUDED
#include "aframeMain.h"
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/objdetect/objdetect.hpp>
#include<wx/image.h>
using namespace cv;
class MyThread : public wxThread
{
public:
    MyThread(aframeFrame* frame,char* rawdata,int col,int row);
    virtual void* Entry();
private:
    int width;
    int heigth;
    char* data=NULL;
    aframeFrame* gui;
    Mat convertType(const Mat& srcImg, int toType, double alpha, double beta);//转换到指定格式MAT
    void Mat2wxImage(Mat &mat, wxImage  &image);
    void ToFrame(wxImage &image);
};


#endif // THREAD_H_INCLUDED
