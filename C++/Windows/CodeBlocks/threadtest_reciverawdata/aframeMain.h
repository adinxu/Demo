/***************************************************************
 * Name:      aframeMain.h
 * Purpose:   Defines Application Frame
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-04-21
 * Copyright: xwd ()
 * License:
 **************************************************************/

#ifndef AFRAMEMAIN_H
#define AFRAMEMAIN_H

#include "mySerialPort.h"
//(*Headers(aframeFrame)
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/panel.h>
#include <wx/button.h>
#include <wx/frame.h>
//*)
typedef struct FrameInfo
{
    char sof;
    char eof;
    int col;
    int row;
}
class aframeFrame: public wxFrame
{
    public:

        aframeFrame(wxWindow* parent,wxWindowID id = -1);
        virtual ~aframeFrame();
        void Notify(char*& rawdata);
    protected:
        ctb::mySerialPort* serialport;
        FrameInfo frameinfo;
        friend class CommThread;
    private:
        int portnum;
        int baudrate;
        //(*Handlers(aframeFrame)
        void OnButton1Click(wxCommandEvent& event);
        //*)

        //(*Identifiers(aframeFrame)
        static const long ID_TEXTCTRL1;
        static const long ID_BUTTON1;
        static const long ID_BUTTON3;
        static const long ID_BUTTON2;
        static const long ID_BUTTON4;
        static const long ID_PANEL1;
        //*)

        //(*Declarations(aframeFrame)
        wxButton* Button4;
        wxButton* Button1;
        wxPanel* Panel1;
        wxButton* Button2;
        wxButton* Button3;
        wxTextCtrl* TextCtrl1;
        //*)

        DECLARE_EVENT_TABLE()
};

#endif // AFRAMEMAIN_H
