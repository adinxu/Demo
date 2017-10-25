/***************************************************************
 * Name:      wxconfigtestMain.h
 * Purpose:   Defines Application Frame
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-07-28
 * Copyright: xwd ()
 * License:
 **************************************************************/

#ifndef WXCONFIGTESTMAIN_H
#define WXCONFIGTESTMAIN_H

//(*Headers(wxconfigtestFrame)
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/menu.h>
#include <wx/textctrl.h>
#include <wx/panel.h>
#include <wx/button.h>
#include <wx/frame.h>
#include <wx/statusbr.h>
//*)


class wxconfigtestFrame: public wxFrame
{
    public:

        wxconfigtestFrame(wxWindow* parent,wxWindowID id = -1);
        virtual ~wxconfigtestFrame();

    private:

        //(*Handlers(wxconfigtestFrame)
        void OnQuit(wxCommandEvent& event);
        void OnAbout(wxCommandEvent& event);
        void OnButton1Click(wxCommandEvent& event);
        void OnButton1Click1(wxCommandEvent& event);
        //*)

        //(*Identifiers(wxconfigtestFrame)
        static const long ID_STATICTEXT1;
        static const long ID_STATICTEXT2;
        static const long ID_TEXTCTRL1;
        static const long ID_TEXTCTRL2;
        static const long ID_BUTTON1;
        static const long ID_PANEL1;
        static const long idMenuQuit;
        static const long idMenuAbout;
        static const long ID_STATUSBAR1;
        //*)

        //(*Declarations(wxconfigtestFrame)
        wxStaticText* StaticText2;
        wxButton* Button1;
        wxPanel* Panel1;
        wxStaticText* StaticText1;
        wxStatusBar* StatusBar1;
        wxTextCtrl* TextCtrl2;
        wxTextCtrl* TextCtrl1;
        //*)
        wxString str;
        long val;

        DECLARE_EVENT_TABLE()
};

#endif // WXCONFIGTESTMAIN_H
