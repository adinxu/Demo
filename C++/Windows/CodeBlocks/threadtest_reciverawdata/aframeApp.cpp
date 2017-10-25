/***************************************************************
 * Name:      aframeApp.cpp
 * Purpose:   Code for Application Class
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-04-21
 * Copyright: xwd ()
 * License:
 **************************************************************/

#include "wx_pch.h"
#include "aframeApp.h"

//(*AppHeaders
#include "aframeMain.h"
#include <wx/image.h>
//*)

IMPLEMENT_APP(aframeApp);

bool aframeApp::OnInit()
{

    bool wxsOK = true;
    wxInitAllImageHandlers();

    aframeFrame* Frame = new aframeFrame(0);
    Frame->Show();
    SetTopWindow(Frame);

    CommThread* commthread=new CommThread(Frame,10,1,'s');
    if(wxTHREAD_NO_ERROR != commthread->Create())
    {
        wxLogError("Can't create the thread!");
        return false;
    }
    if(wxTHREAD_NO_ERROR != commthread->Run())
    {
        wxLogError("Can't create the thread!");
        return false;
    }

    return wxsOK;

}
