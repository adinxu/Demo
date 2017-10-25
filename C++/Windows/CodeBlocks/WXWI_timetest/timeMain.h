/***************************************************************
 * Name:      timeMain.h
 * Purpose:   Defines Application Frame
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-08-14
 * Copyright: xwd ()
 * License:
 **************************************************************/

#ifndef TIMEMAIN_H
#define TIMEMAIN_H

//(*Headers(timeFrame)
#include <wx/sizer.h>
#include <wx/menu.h>
#include <wx/panel.h>
#include <wx/button.h>
#include <wx/frame.h>
#include <wx/statusbr.h>
//*)

class timeFrame: public wxFrame
{
    public:

        timeFrame(wxWindow* parent,wxWindowID id = -1);
        virtual ~timeFrame();

    private:

        //(*Handlers(timeFrame)
        void OnQuit(wxCommandEvent& event);
        void OnAbout(wxCommandEvent& event);
        void OnButton1Click(wxCommandEvent& event);
        //*)

        //(*Identifiers(timeFrame)
        static const long ID_BUTTON1;
        static const long ID_PANEL1;
        static const long idMenuQuit;
        static const long idMenuAbout;
        static const long ID_STATUSBAR1;
        //*)

        //(*Declarations(timeFrame)
        wxButton* Button1;
        wxPanel* Panel1;
        wxStatusBar* StatusBar1;
        //*)

        DECLARE_EVENT_TABLE()
};

#endif // TIMEMAIN_H
