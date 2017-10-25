/***************************************************************
 * Name:      statusbarwithbuttonMain.h
 * Purpose:   Defines Application Frame
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-04-11
 * Copyright: xwd ()
 * License:
 **************************************************************/

#ifndef STATUSBARWITHBUTTONMAIN_H
#define STATUSBARWITHBUTTONMAIN_H

//(*Headers(statusbarwithbuttonFrame)
#include <wx/sizer.h>
#include <wx/panel.h>
#include <wx/button.h>
#include <wx/frame.h>
//*)

class myStatusbar: public wxStatusBar
{
public:
    myStatusbar(wxWindow* parent);
    ~myStatusbar();

    void SetValue(int val);

    void onButtonClick(wxCommandEvent& event);
    void onSize(wxSizeEvent& event);
private:
    wxButton* button;
    static const long ID_BUTTON;
};


class statusbarwithbuttonFrame: public wxFrame
{
    public:

        statusbarwithbuttonFrame(wxWindow* parent,wxWindowID id = -1);
        virtual ~statusbarwithbuttonFrame();

    private:

        //(*Handlers(statusbarwithbuttonFrame)
        void OnButton1Click(wxCommandEvent& event);
        //*)

        //(*Identifiers(statusbarwithbuttonFrame)
        static const long ID_BUTTON1;
        static const long ID_PANEL1;
        //*)

        //(*Declarations(statusbarwithbuttonFrame)
        wxButton* Button1;
        wxPanel* Panel1;
        //*)
        myStatusbar* statusbar;

        DECLARE_EVENT_TABLE()
};


#endif // STATUSBARWITHBUTTONMAIN_H
