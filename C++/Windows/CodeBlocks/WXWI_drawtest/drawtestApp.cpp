/***************************************************************
 * Name:      drawtestApp.cpp
 * Purpose:   Code for Application Class
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-04-14
 * Copyright: xwd ()
 * License:
 **************************************************************/

#include "wx_pch.h"
#include "drawtestApp.h"

//(*AppHeaders
#include "drawtestMain.h"
#include <wx/image.h>
//*)

IMPLEMENT_APP(drawtestApp);

bool drawtestApp::OnInit()
{
    //(*AppInitialize
    bool wxsOK = true;
    wxInitAllImageHandlers();
    if ( wxsOK )
    {
    	drawtestFrame* Frame = new drawtestFrame(0);
    	Frame->Show();
    	SetTopWindow(Frame);
    }
    //*)
    return wxsOK;

}
