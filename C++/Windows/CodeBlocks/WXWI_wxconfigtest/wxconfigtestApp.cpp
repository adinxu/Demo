/***************************************************************
 * Name:      wxconfigtestApp.cpp
 * Purpose:   Code for Application Class
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-07-28
 * Copyright: xwd ()
 * License:
 **************************************************************/

#include "wx_pch.h"
#include "wxconfigtestApp.h"

//(*AppHeaders
#include "wxconfigtestMain.h"
#include <wx/image.h>
//*)
#include "wx/config.h"

IMPLEMENT_APP(wxconfigtestApp);
wxConfig* config=new wxConfig("myapp");
bool wxconfigtestApp::OnInit()
{
    //(*AppInitialize
    bool wxsOK = true;
    wxInitAllImageHandlers();
    if ( wxsOK )
    {
    	wxconfigtestFrame* Frame = new wxconfigtestFrame(0);
    	Frame->Show();
    	SetTopWindow(Frame);
    }
    //*)
    return wxsOK;

}
int wxconfigtestApp::OnExit()
{
    delete config;
    return 0;
}
