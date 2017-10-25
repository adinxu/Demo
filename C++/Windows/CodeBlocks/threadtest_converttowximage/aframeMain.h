/***************************************************************
 * Name:      aframeMain.h
 * Purpose:   Defines Application Frame
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-04-20
 * Copyright: xwd ()
 * License:
 **************************************************************/

#ifndef AFRAMEMAIN_H
#define AFRAMEMAIN_H

//(*Headers(aframeFrame)
#include <wx/frame.h>
//*)
#include <wx/image.h>

class aframeFrame: public wxFrame
{
    public:

        aframeFrame(wxWindow* parent,wxWindowID id = -1);
        virtual ~aframeFrame();
        void OnPaint(wxPaintEvent &evt);//绘制位图
//        void Notify(wxImage &image);//获得一个通过image调整的位图，绘制前景，不擦除背景
    private:

        //(*Handlers(aframeFrame)
        //*)

        //(*Identifiers(aframeFrame)
        //*)

        //(*Declarations(aframeFrame)
        //*)
        wxBitmap* bitmap;
        DECLARE_EVENT_TABLE()
};

#endif // AFRAMEMAIN_H
