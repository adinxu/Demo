/***************************************************************
 * Name:      drawtestMain.h
 * Purpose:   Defines Application Frame
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-04-14
 * Copyright: xwd ()
 * License:
 **************************************************************/

#ifndef DRAWTESTMAIN_H
#define DRAWTESTMAIN_H

//(*Headers(drawtestFrame)
#include <wx/panel.h>
#include <wx/frame.h>
//*)

class drawtestFrame: public wxFrame
{
    public:

        drawtestFrame(wxWindow* parent,wxWindowID id = -1);
        virtual ~drawtestFrame();

    private:

        //(*Handlers(drawtestFrame)
        //*)

        //(*Identifiers(drawtestFrame)
        static const long ID_PANEL1;
        //*)

        //(*Declarations(drawtestFrame)
        wxPanel* Panel1;
        //*)
        void OnMotion(wxMouseEvent& event);
        void OnErase(wxEraseEvent& event);
        void OnPaint(wxPaintEvent &event);

        DECLARE_EVENT_TABLE()
};

#endif // DRAWTESTMAIN_H
