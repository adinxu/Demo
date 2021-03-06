/***************************************************************
 * Name:      aframeApp.cpp
 * Purpose:   Code for Application Class
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-04-20
 * Copyright: xwd ()
 * License:
 **************************************************************/

#include "wx_pch.h"
#include "aframeApp.h"
#include "mySerialPort.h"
//(*AppHeaders
#include "aframeMain.h"
#include <wx/image.h>
//*)
char buff[300]={//15*20
2,	4,	6,	8,	10,	12,	14,	16,	18,	20,	22,	24,	26,	28,	30,
3,	5,	7,	9,	11,	13,	15,	17,	19,	21,	23,	25,	27,	29,	31,
4,	6,	8,	10,	12,	14,	16,	18,	20,	22,	24,	26,	28,	30,	32,
5,	7,	9,	11,	13,	15,	17,	19,	21,	23,	25,	27,	29,	31,	33,
6,	8,	10,	12,	14,	16,	18,	20,	22,	24,	26,	28,	30,	32,	34,
7,	9,	11,	13,	15,	17,	19,	21,	23,	25,	27,	29,	31,	33,	35,
8,	10,	12,	14,	16,	18,	20,	22,	24,	26,	28,	30,	32,	34,	36,
9,	11,	13,	15,	17,	19,	21,	23,	25,	27,	29,	31,	33,	35,	37,
10,	12,	14,	16,	18,	20,	22,	24,	26,	28,	30,	32,	34,	36,	38,
11,	13,	15,	17,	19,	21,	23,	25,	27,	29,	31,	33,	35,	37,	39,
12,	14,	16,	18,	20,	22,	24,	26,	28,	30,	32,	34,	36,	38,	40,
13,	15,	17,	19,	21,	23,	25,	27,	29,	31,	33,	35,	37,	39,	41,
14,	16,	18,	20,	22,	24,	26,	28,	30,	32,	34,	36,	38,	40,	42,
15,	17,	19,	21,	23,	25,	27,	29,	31,	33,	35,	37,	39,	41,	43,
16,	18,	20,	22,	24,	26,	28,	30,	32,	34,	36,	38,	40,	42,	44,
17,	19,	21,	23,	25,	27,	29,	31,	33,	35,	37,	39,	41,	43,	45,
18,	20,	22,	24,	26,	28,	30,	32,	34,	36,	38,	40,	42,	44,	46,
19,	21,	23,	25,	27,	29,	31,	33,	35,	37,	39,	41,	43,	45,	47,
20,	22,	24,	26,	28,	30,	32,	34,	36,	38,	40,	42,	44,	46,	48,
20,	22,	24,	26,	28,	30,	32,	34,	36,	38,	40,	42,	44,	46,	48,
};
IMPLEMENT_APP(aframeApp);

bool aframeApp::OnInit()
{
    bool wxsOK = true;
    wxInitAllImageHandlers();

    	aframeFrame* Frame = new aframeFrame(0);
    	Frame->Show();
    	SetTopWindow(Frame);
//    MyThread* thread=new MyThread(Frame,buff,15,20);
//    if(wxTHREAD_NO_ERROR != thread->Create())
//    {
//        wxLogError("Can't create the thread!");
//        return false;
//    }
//    if(wxTHREAD_NO_ERROR != thread->Run())
//    {
//        wxLogError("Can't create the thread!");
//        return false;
//    }
    return wxsOK;

}
