/***************************************************************
 * Name:      statusbarwithbuttonMain.cpp
 * Purpose:   Code for Application Frame
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-04-11
 * Copyright: xwd ()
 * License:
 **************************************************************/

#include "wx_pch.h"
#include "statusbarwithbuttonMain.h"
#include <wx/msgdlg.h>

//(*InternalHeaders(statusbarwithbuttonFrame)
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

//(*IdInit(statusbarwithbuttonFrame)
const long statusbarwithbuttonFrame::ID_BUTTON1 = wxNewId();
const long statusbarwithbuttonFrame::ID_PANEL1 = wxNewId();
//*)


BEGIN_EVENT_TABLE(statusbarwithbuttonFrame,wxFrame)
    //(*EventTable(statusbarwithbuttonFrame)
    //*)
END_EVENT_TABLE()

statusbarwithbuttonFrame::statusbarwithbuttonFrame(wxWindow* parent,wxWindowID id)
{
    //(*Initialize(statusbarwithbuttonFrame)
    wxBoxSizer* BoxSizer2;
    wxBoxSizer* BoxSizer1;

    Create(parent, id, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE, _T("id"));
    BoxSizer1 = new wxBoxSizer(wxHORIZONTAL);
    Panel1 = new wxPanel(this, ID_PANEL1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL, _T("ID_PANEL1"));
    BoxSizer2 = new wxBoxSizer(wxVERTICAL);
    Button1 = new wxButton(Panel1, ID_BUTTON1, _("²âÊÔ"), wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator, _T("ID_BUTTON1"));
    BoxSizer2->Add(Button1, 1, wxALL|wxALIGN_CENTER_HORIZONTAL|wxALIGN_CENTER_VERTICAL, 5);
    Panel1->SetSizer(BoxSizer2);
    BoxSizer2->Fit(Panel1);
    BoxSizer2->SetSizeHints(Panel1);
    BoxSizer1->Add(Panel1, 1, wxALL|wxEXPAND, 0);
    SetSizer(BoxSizer1);
    BoxSizer1->Fit(this);
    BoxSizer1->SetSizeHints(this);

    Connect(ID_BUTTON1,wxEVT_COMMAND_BUTTON_CLICKED,(wxObjectEventFunction)&statusbarwithbuttonFrame::OnButton1Click);
    //*)
    statusbar=new myStatusbar(this);
    SetStatusBar(statusbar);
}

statusbarwithbuttonFrame::~statusbarwithbuttonFrame()
{
    //(*Destroy(statusbarwithbuttonFrame)
    //*)
}
myStatusbar::myStatusbar(wxWindow* parent)
            :wxStatusBar(parent)
{
    SetFieldsCount(3);
    int patch[3]={-1,-1,100};
    SetStatusWidths(3,patch);
    SetStatusText("RX:",0);
    SetStatusText("0",1);
    button=new wxButton(this,ID_BUTTON,"²âÊÔ");
    button->SetLabel("Çå¿Õ¼ÆÊý");
    Bind(wxEVT_COMMAND_BUTTON_CLICKED, &myStatusbar::onButtonClick, this, ID_BUTTON);
    Bind(wxEVT_SIZE,&myStatusbar::onSize,this);

}
myStatusbar::~myStatusbar()
{
    delete button;
}

void myStatusbar::SetValue(int val)
{
    if(val>=0)
    SetStatusText(wxString::Format("%i",val),1);
    else
    wxLogError("an error occur,the err val is %d",val);
}

void myStatusbar::onButtonClick(wxCommandEvent& event)
{
    SetStatusText("0",1);
}

void myStatusbar::onSize(wxSizeEvent& event)
{
    wxRect rect;
    GetFieldRect(2,rect);
    button->SetPosition(wxPoint(rect.x+1,rect.y+1));
    button->SetSize(wxSize(rect.width-4,rect.height-4));
}

void statusbarwithbuttonFrame::OnButton1Click(wxCommandEvent& event)
{
    statusbar->SetValue(10);
}
