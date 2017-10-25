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
class MyWindow: public wxFrame
{
public:
    MyWindow(wxWindow* parent,wxWindowID id);
    virtual ~MyWindow();
    void SendEvent();
};
class eventtestFrame: public wxFrame
{
    public:

        eventtestFrame(wxWindow* parent,wxWindowID id = -1);
        virtual ~eventtestFrame();
        void onMyEvent(wxCommandEvent& event);

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
        static const long ID_MY_WINDOW;//定义事件标识

        //(*Declarations(eventtestFrame)
        wxButton* Button1;
        wxStatusBar* StatusBar1;
        //*)
        MyWindow* MyWindows;

        DECLARE_EVENT_TABLE()
};
wxDECLARE_EVENT(MY_EVENT,wxCommandEvent);//声明事件类型
#endif // EVENTTESTMAIN_H
