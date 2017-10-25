/***************************************************************
 * Name:      timeApp.cpp
 * Purpose:   Code for Application Class
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-08-14
 * Copyright: xwd ()
 * License:
 **************************************************************/

#include "wx_pch.h"
#include "timeApp.h"

//(*AppHeaders
#include "timeMain.h"
#include <wx/image.h>
//*)

IMPLEMENT_APP(timeApp);

bool timeApp::OnInit()
{
    //(*AppInitialize
    bool wxsOK = true;
    wxInitAllImageHandlers();
    if ( wxsOK )
    {
    	timeFrame* Frame = new timeFrame(0);
    	Frame->Show();
    	SetTopWindow(Frame);
    }
    //*)
    return wxsOK;

}
