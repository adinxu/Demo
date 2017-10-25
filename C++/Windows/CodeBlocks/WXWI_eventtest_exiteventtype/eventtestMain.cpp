/***************************************************************
 * Name:      eventtestMain.cpp
 * Purpose:   Code for Application Frame
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-07-04
 * Copyright: xwd ()
 * License:
 **************************************************************/

#include "wx_pch.h"
#include "eventtestMain.h"
#include <wx/msgdlg.h>

//(*InternalHeaders(eventtestFrame)
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

//(*IdInit(eventtestFrame)
const long eventtestFrame::ID_BUTTON1 = wxNewId();
const long eventtestFrame::idMenuQuit = wxNewId();
const long eventtestFrame::idMenuAbout = wxNewId();
const long eventtestFrame::ID_STATUSBAR1 = wxNewId();
//*)
const long eventtestFrame::ID_MY_WINDOW = wxNewId();
#define use 0

BEGIN_EVENT_TABLE(eventtestFrame,wxFrame)
    //(*EventTable(eventtestFrame)
    //*)
    #if use
    EVT_COMMAND(ID_MY_WINDOW,MY_EVENT,eventtestFrame::onMyEvent)
    #endif // use
END_EVENT_TABLE()


wxDEFINE_EVENT(MY_EVENT,wxCommandEvent);//定义事件表
eventtestFrame::eventtestFrame(wxWindow* parent,wxWindowID id)
{
    //(*Initialize(eventtestFrame)
    wxMenuItem* MenuItem2;
    wxMenuItem* MenuItem1;
    wxMenu* Menu1;
    wxMenuBar* MenuBar1;
    wxMenu* Menu2;

    Create(parent, id, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE, _T("id"));
    Button1 = new wxButton(this, ID_BUTTON1, _("测试"), wxPoint(224,272), wxDefaultSize, 0, wxDefaultValidator, _T("ID_BUTTON1"));
    MenuBar1 = new wxMenuBar();
    Menu1 = new wxMenu();
    MenuItem1 = new wxMenuItem(Menu1, idMenuQuit, _("Quit\tAlt-F4"), _("Quit the application"), wxITEM_NORMAL);
    Menu1->Append(MenuItem1);
    MenuBar1->Append(Menu1, _("&File"));
    Menu2 = new wxMenu();
    MenuItem2 = new wxMenuItem(Menu2, idMenuAbout, _("About\tF1"), _("Show info about this application"), wxITEM_NORMAL);
    Menu2->Append(MenuItem2);
    MenuBar1->Append(Menu2, _("Help"));
    SetMenuBar(MenuBar1);
    StatusBar1 = new wxStatusBar(this, ID_STATUSBAR1, 0, _T("ID_STATUSBAR1"));
    int __wxStatusBarWidths_1[1] = { -1 };
    int __wxStatusBarStyles_1[1] = { wxSB_NORMAL };
    StatusBar1->SetFieldsCount(1,__wxStatusBarWidths_1);
    StatusBar1->SetStatusStyles(1,__wxStatusBarStyles_1);
    SetStatusBar(StatusBar1);

    Connect(ID_BUTTON1,wxEVT_COMMAND_BUTTON_CLICKED,(wxObjectEventFunction)&eventtestFrame::OnButton1Click);
    Connect(idMenuQuit,wxEVT_COMMAND_MENU_SELECTED,(wxObjectEventFunction)&eventtestFrame::OnQuit);
    Connect(idMenuAbout,wxEVT_COMMAND_MENU_SELECTED,(wxObjectEventFunction)&eventtestFrame::OnAbout);
    //*)
    #if !use
    Bind(MY_EVENT,eventtestFrame::onMyEvent,this,ID_MY_WINDOW);
    #endif
}

eventtestFrame::~eventtestFrame()
{
    //(*Destroy(eventtestFrame)
    //*)
}
void eventtestFrame::onMyEvent(wxCommandEvent& event)//事件处理函数
{
    wxString text=event.GetString();
    wxMessageBox(text,"提示");
}

void eventtestFrame::OnQuit(wxCommandEvent& event)
{
    Close();
}

void eventtestFrame::OnAbout(wxCommandEvent& event)
{
    wxString msg = wxbuildinfo(long_f);
    wxMessageBox(msg, _("Welcome to..."));
}

void eventtestFrame::OnButton1Click(wxCommandEvent& event)
{
    MyWindows=new MyWindow(this,ID_MY_WINDOW);
    MyWindows->Show();
    MyWindows->SendEvent();
}

MyWindow::MyWindow(wxWindow* parent,wxWindowID id)
        :wxFrame(parent,id,"mywindows",wxDefaultPosition,wxDefaultSize,wxDEFAULT_FRAME_STYLE,"window")
        {

        }
MyWindow::~MyWindow()
        {

        }
void MyWindow::SendEvent()
{
    wxCommandEvent event(MY_EVENT,GetId());
    event.SetEventObject(this);
    event.SetString("test");
    ProcessWindowEvent(event);
}
