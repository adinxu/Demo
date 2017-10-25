/***************************************************************
 * Name:      eventtestMain.h
 * Purpose:   Defines Application Frame
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-07-04
 * Copyright: xwd ()
 * License:
 **************************************************************/

#ifndef EVENTTESTMAIN_H
#define EVENTTESTMAIN_H

//(*Headers(eventtestFrame)
#include <wx/menu.h>
#include <wx/button.h>
#include <wx/frame.h>
#include <wx/statusbr.h>
//*)

class MyEventClass: public wxEvent//定义事件类
{
public:
    MyEventClass(wxEventType eventType,int winid,const wxString& strings);
    virtual ~MyEventClass();

    virtual wxEvent *Clone() const {return new MyEventClass(*this);}

    wxString GetString() const {return str;}
private:
    const wxString str;
};

class eventtestFrame: public wxFrame
{
    public:

        eventtestFrame(wxWindow* parent,wxWindowID id = -1);
        virtual ~eventtestFrame();

        void OnEvent(MyEventClass& event);

    private:

        //(*Handlers(eventtestFrame)
        void OnQuit(wxCommandEvent& event);
        void OnAbout(wxCommandEvent& event);
        void OnButton1Click(wxCommandEvent& event);
        //*)

        //(*Identifiers(eventtestFrame)
        static const long ID_BUTTON1;
        static const long idMenuQuit;
        static const long idMenuAbout;
        static const long ID_STATUSBAR1;
        //*)

        //(*Declarations(eventtestFrame)
        wxButton* Button1;
        wxStatusBar* StatusBar1;
        //*)

        DECLARE_EVENT_TABLE()
};



#endif // EVENTTESTMAIN_H
