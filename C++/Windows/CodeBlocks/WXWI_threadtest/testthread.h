#ifndef TESTTHREAD_H_INCLUDED
#define TESTTHREAD_H_INCLUDED
#include "threadtestMain.h"
class testthread: public wxThread
{
public:
    testthread(threadtestFrame* frame);
    ~testthread();
    virtual void *Entry();
private:
    threadtestFrame* myFrame;
    int counts;

};

#endif // TESTTHREAD_H_INCLUDED
