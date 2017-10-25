#include"commthread.h"
CommThread::CommThread(aframeFrame* frame)
            :wxThread()
{
    gui=frame;

    serialport = new ctb::mySerialPort();
    serialport->Open("com1" , baudrate,
					    "8N1",
					    ctb::SerialPort::NoFlowControl );
    cols=col;rows=row;
    eosstring=eos;
}
void* CommThread::Entry()
{
    DWORD readbytenum=cols*rows;
    databuf=serialport->ReadBetweenEos(readbytenum,eosstring);
    gui->Notify(databuf);
    return NULL;
}
void CommThread::ToGui(char*& databuf)
{

}
