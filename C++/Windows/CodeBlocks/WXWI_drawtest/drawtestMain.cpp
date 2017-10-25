/***************************************************************
 * Name:      drawtestMain.cpp
 * Purpose:   Code for Application Frame
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-04-14
 * Copyright: xwd ()
 * License:
 **************************************************************/

#include "wx_pch.h"
#include "drawtestMain.h"
#include <wx/msgdlg.h>

//(*InternalHeaders(drawtestFrame)
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

//(*IdInit(drawtestFrame)
const long drawtestFrame::ID_PANEL1 = wxNewId();
//*)

BEGIN_EVENT_TABLE(drawtestFrame,wxFrame)
    //(*EventTable(drawtestFrame)
    //*)
    EVT_MOTION(drawtestFrame::OnMotion)
    EVT_ERASE_BACKGROUND(drawtestFrame::OnErase)
    EVT_PAINT(drawtestFrame::OnPaint)
END_EVENT_TABLE()


drawtestFrame::drawtestFrame(wxWindow* parent,wxWindowID id)
{
    //(*Initialize(drawtestFrame)
    Create(parent, id, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE, _T("id"));
    Panel1 = new wxPanel(this, ID_PANEL1, wxPoint(256,168), wxDefaultSize, 0, _T("ID_PANEL1"));
    //*)


}

drawtestFrame::~drawtestFrame()
{
    //(*Destroy(drawtestFrame)
    //*)
}
void drawtestFrame::OnMotion(wxMouseEvent& event)
{
    if(event.Dragging())
    {
       wxClientDC dc(this);
//    wxPen pen(*wxRED,1);
//    dc.SetPen(pen);
    wxColour colour(130,130,130);
    wxBrush brush(colour,wxBDIAGONAL_HATCH);
    dc.SetBrush(brush);
    dc.DrawPoint(event.GetPosition());
    dc.SetPen(wxNullPen);
    }


}
void drawtestFrame::OnPaint(wxPaintEvent &event)
{

    wxPaintDC* adc=new wxPaintDC(Panel1);
    wxDC* clientDC=adc;
    wxSize sz(100,100);
    wxPen pen(*wxRED,1);
    clientDC->SetPen(pen);
    clientDC->DrawRectangle(wxPoint(0,0),sz);
}
void drawtestFrame::OnErase(wxEraseEvent& event)
{
    event.Skip();
}

