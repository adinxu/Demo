/***************************************************************
 * Name:      drawgrayMain.h
 * Purpose:   Defines Application Frame
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-07-14
 * Copyright: xwd ()
 * License:
 **************************************************************/

#ifndef DRAWGRAYMAIN_H
#define DRAWGRAYMAIN_H

//(*Headers(drawgrayFrame)
#include <wx/menu.h>
#include <wx/panel.h>
#include <wx/frame.h>
#include <wx/statusbr.h>
//*)

class drawgrayFrame: public wxFrame
{
    public:

        drawgrayFrame(wxWindow* parent,wxWindowID id = -1);
        virtual ~drawgrayFrame();

    private:

        //(*Handlers(drawgrayFrame)
        void OnQuit(wxCommandEvent& event);
        void OnAbout(wxCommandEvent& event);
        void OnPanel1Paint(wxPaintEvent& event);
        //*)

        //(*Identifiers(drawgrayFrame)
        static const long ID_PANEL1;
        static const long idMenuQuit;
        static const long idMenuAbout;
        static const long ID_STATUSBAR1;
        //*)

        //(*Declarations(drawgrayFrame)
        wxPanel* Panel1;
        wxStatusBar* StatusBar1;
        //*)
        DECLARE_EVENT_TABLE()
};

#endif // DRAWGRAYMAIN_H
