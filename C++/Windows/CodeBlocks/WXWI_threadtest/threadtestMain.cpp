/***************************************************************
 * Name:      threadtestMain.cpp
 * Purpose:   Code for Application Frame
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-06-19
 * Copyright: xwd ()
 * License:
 **************************************************************/

#include "wx_pch.h"
#include "threadtestMain.h"
#include <wx/msgdlg.h>
#define detec 1
//(*InternalHeaders(threadtestFrame)
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

#include"testthread.h"

//(*IdInit(threadtestFrame)
const long threadtestFrame::ID_GAUGE1 = wxNewId();
const long threadtestFrame::ID_BUTTON1 = wxNewId();
const long threadtestFrame::ID_BUTTON2 = wxNewId();
const long threadtestFrame::ID_PANEL1 = wxNewId();
const long threadtestFrame::idMenuQuit = wxNewId();
const long threadtestFrame::idMenuAbout = wxNewId();
const long threadtestFrame::ID_STATUSBAR1 = wxNewId();
//*)

wxDEFINE_EVENT(wxEVT_COMMAND_MYTHREAD_COMPLETED, wxThreadEvent);//定义事件种类
wxDEFINE_EVENT(wxEVT_COMMAND_MYTHREAD_UPDATE, wxThreadEvent);

BEGIN_EVENT_TABLE(threadtestFrame,wxFrame)
    //(*EventTable(threadtestFrame)
    //*)
//    EVT_COMMAND(wxID_ANY,wxEVT_COMMAND_MYTHREAD_UPDATE,threadtestFrame::OnThreadUpdate)
//    EVT_COMMAND(wxID_ANY,wxEVT_COMMAND_MYTHREAD_COMPLETED,threadtestFrame::OnThreadCompleted)
END_EVENT_TABLE()

threadtestFrame::threadtestFrame(wxWindow* parent,wxWindowID id)
{
    //(*Initialize(threadtestFrame)
    wxMenuItem* MenuItem2;
    wxMenuItem* MenuItem1;
    wxBoxSizer* BoxSizer2;
    wxMenu* Menu1;
    wxBoxSizer* BoxSizer1;
    wxMenuBar* MenuBar1;
    wxStaticBoxSizer* StaticBoxSizer1;
    wxMenu* Menu2;

    Create(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE, _T("wxID_ANY"));
    BoxSizer1 = new wxBoxSizer(wxHORIZONTAL);
    Panel1 = new wxPanel(this, ID_PANEL1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL, _T("ID_PANEL1"));
    BoxSizer2 = new wxBoxSizer(wxVERTICAL);
    StaticBoxSizer1 = new wxStaticBoxSizer(wxHORIZONTAL, Panel1, _("进度"));
    Gauge1 = new wxGauge(Panel1, ID_GAUGE1, 100, wxDefaultPosition, wxSize(241,28), 0, wxDefaultValidator, _T("ID_GAUGE1"));
    StaticBoxSizer1->Add(Gauge1, 1, wxALL|wxALIGN_CENTER_HORIZONTAL|wxALIGN_CENTER_VERTICAL, 5);
    BoxSizer2->Add(StaticBoxSizer1, 1, wxALL|wxALIGN_CENTER_HORIZONTAL|wxALIGN_CENTER_VERTICAL, 5);
    Button1 = new wxButton(Panel1, ID_BUTTON1, _("test"), wxDefaultPosition, wxSize(182,30), 0, wxDefaultValidator, _T("ID_BUTTON1"));
    BoxSizer2->Add(Button1, 1, wxALL|wxALIGN_CENTER_HORIZONTAL|wxALIGN_CENTER_VERTICAL, 5);
    Button2 = new wxButton(Panel1, ID_BUTTON2, _("stop"), wxDefaultPosition, wxSize(178,61), 0, wxDefaultValidator, _T("ID_BUTTON2"));
    BoxSizer2->Add(Button2, 1, wxALL|wxALIGN_CENTER_HORIZONTAL|wxALIGN_CENTER_VERTICAL, 5);
    Panel1->SetSizer(BoxSizer2);
    BoxSizer2->Fit(Panel1);
    BoxSizer2->SetSizeHints(Panel1);
    BoxSizer1->Add(Panel1, 1, wxALL|wxALIGN_CENTER_HORIZONTAL|wxALIGN_CENTER_VERTICAL, 0);
    SetSizer(BoxSizer1);
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
    BoxSizer1->Fit(this);
    BoxSizer1->SetSizeHints(this);

    Connect(ID_BUTTON1,wxEVT_COMMAND_BUTTON_CLICKED,(wxObjectEventFunction)&threadtestFrame::OnButton1Click);
    Connect(ID_BUTTON2,wxEVT_COMMAND_BUTTON_CLICKED,(wxObjectEventFunction)&threadtestFrame::OnButton2Click);
    Connect(idMenuQuit,wxEVT_COMMAND_MENU_SELECTED,(wxObjectEventFunction)&threadtestFrame::OnQuit);
    Connect(idMenuAbout,wxEVT_COMMAND_MENU_SELECTED,(wxObjectEventFunction)&threadtestFrame::OnAbout);
    //*)
    Bind(wxEVT_COMMAND_MYTHREAD_UPDATE,threadtestFrame::OnThreadUpdate,this,wxID_ANY);
    Bind(wxEVT_COMMAND_MYTHREAD_COMPLETED,threadtestFrame::OnThreadCompleted,this,wxID_ANY);
    Center();
}

threadtestFrame::~threadtestFrame()
{
    //(*Destroy(threadtestFrame)
    //*)
}

void threadtestFrame::OnQuit(wxCommandEvent& event)
{
    Close();
}

void threadtestFrame::OnAbout(wxCommandEvent& event)
{
    wxString msg = wxbuildinfo(long_f);
    wxMessageBox(msg, _("Welcome to..."));
}

void threadtestFrame::OnThreadUpdate(wxThreadEvent& event)
{
    Gauge1->SetValue(++counts);

}
void threadtestFrame::OnThreadCompleted(wxThreadEvent& event)
{
    #if detec
    myThread=NULL;
    Button1->SetLabel("test");
    Button1->Enable();
    #endif // detec
}

void threadtestFrame::OnButton1Click(wxCommandEvent& event)
{
    Gauge1->SetValue(0);
    counts=0;
    Button1->SetLabel("testing...");
    Button1->Disable();
    myThread=new testthread(this);
    if(myThread->Run()!=wxTHREAD_NO_ERROR)
    {
        wxLogError("Can't create the thread!");
        delete myThread;
        myThread = NULL;
    }
    #if !detec
    myThread->Wait();
    delete myThread;
    myThread=NULL;
    Button1->SetLabel("test");
    Button1->Enable();
    #endif

}

void threadtestFrame::OnButton2Click(wxCommandEvent& event)
{
    #if detec
    if(myThread)
    myThread->Delete();
    #endif // detec
}
