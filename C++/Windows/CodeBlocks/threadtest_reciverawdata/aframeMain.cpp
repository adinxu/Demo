/***************************************************************
 * Name:      aframeMain.cpp
 * Purpose:   Code for Application Frame
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-04-21
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
const long aframeFrame::ID_TEXTCTRL1 = wxNewId();
const long aframeFrame::ID_BUTTON1 = wxNewId();
const long aframeFrame::ID_BUTTON3 = wxNewId();
const long aframeFrame::ID_BUTTON2 = wxNewId();
const long aframeFrame::ID_BUTTON4 = wxNewId();
const long aframeFrame::ID_PANEL1 = wxNewId();
//*)

BEGIN_EVENT_TABLE(aframeFrame,wxFrame)
    //(*EventTable(aframeFrame)
    //*)
END_EVENT_TABLE()

aframeFrame::aframeFrame(wxWindow* parent,wxWindowID id)
{
    portnum=1;
    baudrate = 9600;
    //(*Initialize(aframeFrame)
    wxBoxSizer* BoxSizer2;
    wxBoxSizer* BoxSizer1;
    wxStaticBoxSizer* StaticBoxSizer1;
    wxFlexGridSizer* FlexGridSizer1;

    Create(parent, id, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE, _T("id"));
    BoxSizer1 = new wxBoxSizer(wxHORIZONTAL);
    Panel1 = new wxPanel(this, ID_PANEL1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL, _T("ID_PANEL1"));
    BoxSizer2 = new wxBoxSizer(wxVERTICAL);
    StaticBoxSizer1 = new wxStaticBoxSizer(wxHORIZONTAL, Panel1, _("数据接收"));
    TextCtrl1 = new wxTextCtrl(Panel1, ID_TEXTCTRL1, wxEmptyString, wxDefaultPosition, wxSize(537,201), 0, wxDefaultValidator, _T("ID_TEXTCTRL1"));
    StaticBoxSizer1->Add(TextCtrl1, 1, wxALL|wxEXPAND, 5);
    BoxSizer2->Add(StaticBoxSizer1, 1, wxALL|wxEXPAND, 5);
    FlexGridSizer1 = new wxFlexGridSizer(2, 2, 0, 0);
    Button1 = new wxButton(Panel1, ID_BUTTON1, _("打开串口"), wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator, _T("ID_BUTTON1"));
    FlexGridSizer1->Add(Button1, 1, wxALL|wxALIGN_CENTER_HORIZONTAL|wxALIGN_CENTER_VERTICAL, 5);
    Button3 = new wxButton(Panel1, ID_BUTTON3, _("参数设置"), wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator, _T("ID_BUTTON3"));
    FlexGridSizer1->Add(Button3, 1, wxALL|wxALIGN_CENTER_HORIZONTAL|wxALIGN_CENTER_VERTICAL, 5);
    Button2 = new wxButton(Panel1, ID_BUTTON2, _("接收数据帧"), wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator, _T("ID_BUTTON2"));
    FlexGridSizer1->Add(Button2, 1, wxALL|wxALIGN_CENTER_HORIZONTAL|wxALIGN_CENTER_VERTICAL, 5);
    Button4 = new wxButton(Panel1, ID_BUTTON4, _("帧参数设置"), wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator, _T("ID_BUTTON4"));
    FlexGridSizer1->Add(Button4, 1, wxALL|wxALIGN_CENTER_HORIZONTAL|wxALIGN_CENTER_VERTICAL, 5);
    BoxSizer2->Add(FlexGridSizer1, 0, wxALL|wxALIGN_CENTER_HORIZONTAL|wxALIGN_CENTER_VERTICAL, 5);
    Panel1->SetSizer(BoxSizer2);
    BoxSizer2->Fit(Panel1);
    BoxSizer2->SetSizeHints(Panel1);
    BoxSizer1->Add(Panel1, 1, wxALL|wxEXPAND, 0);
    SetSizer(BoxSizer1);
    BoxSizer1->Fit(this);
    BoxSizer1->SetSizeHints(this);

    Connect(ID_BUTTON1,wxEVT_COMMAND_BUTTON_CLICKED,(wxObjectEventFunction)&aframeFrame::OnButton1Click);
    //*)
    using ctb::mySerialPort;
    serialport=new mySerialPort();
}

aframeFrame::~aframeFrame()
{
    //(*Destroy(aframeFrame)
    //*)
}
void aframeFrame::Notify(char*& rawdata)
{
    *TextCtrl1<<rawdata;
    delete[] rawdata;
}

void aframeFrame::OnButton1Click(wxCommandEvent& event)
{
    serialport->Open(portnum,baudrate);//打开串口
}
