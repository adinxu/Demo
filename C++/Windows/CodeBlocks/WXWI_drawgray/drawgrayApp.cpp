/***************************************************************
 * Name:      drawgrayApp.cpp
 * Purpose:   Code for Application Class
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-07-14
 * Copyright: xwd ()
 * License:
 **************************************************************/

#include "wx_pch.h"
#include "drawgrayApp.h"

//(*AppHeaders
#include "drawgrayMain.h"
#include <wx/image.h>
//*)

IMPLEMENT_APP(drawgrayApp);

bool drawgrayApp::OnInit()
{
    //(*AppInitialize
    bool wxsOK = true;
    wxInitAllImageHandlers();
    if ( wxsOK )
    {
    	drawgrayFrame* Frame = new drawgrayFrame(0);
    	Frame->Show();
    	SetTopWindow(Frame);
    }
    //*)
    return wxsOK;

}
