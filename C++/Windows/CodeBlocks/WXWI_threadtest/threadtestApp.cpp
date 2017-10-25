/***************************************************************
 * Name:      threadtestApp.cpp
 * Purpose:   Code for Application Class
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-06-19
 * Copyright: xwd ()
 * License:
 **************************************************************/

#include "wx_pch.h"
#include "threadtestApp.h"

//(*AppHeaders
#include "threadtestMain.h"
#include <wx/image.h>
//*)

IMPLEMENT_APP(threadtestApp);

bool threadtestApp::OnInit()
{
    //(*AppInitialize
    bool wxsOK = true;
    wxInitAllImageHandlers();
    if ( wxsOK )
    {
    	threadtestFrame* Frame = new threadtestFrame(0);
    	Frame->Show();
    	SetTopWindow(Frame);
    }
    //*)
    return wxsOK;

}
