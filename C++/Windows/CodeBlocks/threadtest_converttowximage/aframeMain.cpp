/***************************************************************
 * Name:      aframeMain.cpp
 * Purpose:   Code for Application Frame
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-04-20
 * Copyright: xwd ()
 * License:
 **************************************************************/

#include "wx_pch.h"
#include "aframeMain.h"
#include <wx/msgdlg.h>

//(*InternalHeaders(aframeFrame)
#include <wx/intl.h>
#include <wx/string.h>
//*)

//helper functions
enum wxbuildinfoformat {
    short_f, long_f };

wxString wxbuildinfo(wxbuildinfoformat format)
{
    wxString wxbuild(wxVERSION_STRING);

    if (format == long_f )
    {
#if defined(__WXMSW__)
        wxbuild << _T("-Windows");
#elif defined(__UNIX__)
        wxbuild << _T("-Linux");
#endif

#if wxUSE_UNICODE
        wxbuild << _T("-Unicode build");
#else
        wxbuild << _T("-ANSI build");
#endif // wxUSE_UNICODE
    }

    return wxbuild;
}

//(*IdInit(aframeFrame)
//*)

BEGIN_EVENT_TABLE(aframeFrame,wxFrame)
    //(*EventTable(aframeFrame)
    //*)
        EVT_PAINT(aframeFrame::OnPaint)
END_EVENT_TABLE()

extern char buff[300];
aframeFrame::aframeFrame(wxWindow* parent,wxWindowID id)
{
    //(*Initialize(aframeFrame)
    Create(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE, _T("wxID_ANY"));
    //*)
//    wxImage img("lena.jpg");
//    bitmap = wxBitmap(img);
    bitmap=new wxBitmap(buff,15,20);
    bitmap->SetDepth(8);
    SetBackgroundColour(*wxBLACK);

}

aframeFrame::~aframeFrame()
{
    //(*Destroy(aframeFrame)
    //*)
}
//void aframeFrame::Notify(wxImage &image)
//{
//    bitmap = wxBitmap(image.Scale(800,600));
//    Refresh(false);
//}
void aframeFrame::OnPaint(wxPaintEvent &evt)
{
    wxPaintDC dc(this);
    dc.DrawBitmap(*bitmap,wxPoint(-1,-1));
}
