/***************************************************************
 * Name:      aframeApp.h
 * Purpose:   Defines Application Class
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-04-21
 * Copyright: xwd ()
 * License:
 **************************************************************/

#ifndef AFRAMEAPP_H
#define AFRAMEAPP_H

#include <wx/app.h>
#include"commthread.h"

class aframeApp : public wxApp
{
    public:
        virtual bool OnInit();
};

#endif // AFRAMEAPP_H
