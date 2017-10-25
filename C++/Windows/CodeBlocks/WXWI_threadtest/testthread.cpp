#include"testthread.h"
#define detec 1
testthread::testthread(threadtestFrame* frame)
#if detec
:wxThread()
#else
:wxThread(wxTHREAD_JOINABLE)
#endif

{
    myFrame=frame;
    counts=0;
}
testthread::~testthread()
{

}
void* testthread::Entry()
{
    while(!TestDestroy())
    {
        counts++;
        if(counts<=100)
        {
            wxQueueEvent(myFrame,new wxThreadEvent(wxEVT_COMMAND_MYTHREAD_UPDATE));
        }
        else break;
        Sleep(100);
    }
    #if detec
    wxQueueEvent(myFrame,new wxThreadEvent(wxEVT_COMMAND_MYTHREAD_COMPLETED));
    #endif // detec
    return (wxThread::ExitCode)0;
}
