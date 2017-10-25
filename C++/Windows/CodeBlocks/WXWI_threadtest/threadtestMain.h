/***************************************************************
 * Name:      threadtestMain.h
 * Purpose:   Defines Application Frame
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-06-19
 * Copyright: xwd ()
 * License:
 **************************************************************/

#ifndef THREADTESTMAIN_H
#define THREADTESTMAIN_H

//(*Headers(threadtestFrame)
#include <wx/sizer.h>
#include <wx/menu.h>
#include <wx/panel.h>
#include <wx/button.h>
#include <wx/frame.h>
#include <wx/gauge.h>
#include <wx/statusbr.h>
//*)
class testthread;
class threadtestFrame: public wxFrame
{
    public:

        threadtestFrame(wxWindow* parent,wxWindowID id = -1);
        virtual ~threadtestFrame();
        void OnThreadUpdate(wxThreadEvent& event);
        void OnThreadCompleted(wxThreadEvent& event);

    private:
        testthread* myThread;
        //(*Handlers(threadtestFrame)
        void OnQuit(wxCommandEvent& event);
        void OnAbout(wxCommandEvent& event);
        void OnButton1Click(wxCommandEvent& event);
        void OnButton2Click(wxCommandEvent& event);
        //*)

        //(*Identifiers(threadtestFrame)
        static const long ID_GAUGE1;
        static const long ID_BUTTON1;
        static const long ID_BUTTON2;
        static const long ID_PANEL1;
        static const long idMenuQuit;
        static const long idMenuAbout;
        static const long ID_STATUSBAR1;
        //*)

        //(*Declarations(threadtestFrame)
        wxButton* Button1;
        wxGauge* Gauge1;
        wxPanel* Panel1;
        wxButton* Button2;
        wxStatusBar* StatusBar1;
        //*)

        int counts;

        DECLARE_EVENT_TABLE()
};

wxDECLARE_EVENT(wxEVT_COMMAND_MYTHREAD_COMPLETED, wxThreadEvent);
wxDECLARE_EVENT(wxEVT_COMMAND_MYTHREAD_UPDATE, wxThreadEvent);
#endif // THREADTESTMAIN_H
