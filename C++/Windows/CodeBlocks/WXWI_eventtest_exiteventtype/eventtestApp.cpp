/***************************************************************
 * Name:      eventtestApp.cpp
 * Purpose:   Code for Application Class
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-07-04
 * Copyright: xwd ()
 * License:
 **************************************************************/

#include "wx_pch.h"
#include "eventtestApp.h"

//(*AppHeaders
#include "eventtestMain.h"
#include <wx/image.h>
//*)

IMPLEMENT_APP(eventtestApp);

bool eventtestApp::OnInit()
{
    //(*AppInitialize
    bool wxsOK = true;
    wxInitAllImageHandlers();
    if ( wxsOK )
    {
    	eventtestFrame* Frame = new eventtestFrame(0);
    	Frame->Show();
    	SetTopWindow(Frame);
    }
    //*)
    return wxsOK;

}
