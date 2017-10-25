/***************************************************************
 * Name:      statusbarwithbuttonApp.cpp
 * Purpose:   Code for Application Class
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-04-11
 * Copyright: xwd ()
 * License:
 **************************************************************/

#include "wx_pch.h"
#include "statusbarwithbuttonApp.h"

//(*AppHeaders
#include "statusbarwithbuttonMain.h"
#include <wx/image.h>
//*)

IMPLEMENT_APP(statusbarwithbuttonApp);

bool statusbarwithbuttonApp::OnInit()
{
    //(*AppInitialize
    bool wxsOK = true;
    wxInitAllImageHandlers();
    if ( wxsOK )
    {
    	statusbarwithbuttonFrame* Frame = new statusbarwithbuttonFrame(0);
    	Frame->Show();
    	SetTopWindow(Frame);
    }
    //*)
    return wxsOK;

}
