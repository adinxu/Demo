#ifndef COMMTHREAD_H_INCLUDED
#define COMMTHREAD_H_INCLUDED
#include"mySerialPort.h"
#include"aframeMain.h"
using namespace ctb;
class CommThread : public wxThread
{
public:
    CommThread(aframeFrame* frame);
    virtual void* Entry();
    void ToGui(char*& databuf);
private:
    aframeFrame* gui;
    int cols,rows;
    char* databuf;
    mySerialPort* serialport;
    char eosstring;

};


#endif // COMMTHREAD_H_INCLUDED
