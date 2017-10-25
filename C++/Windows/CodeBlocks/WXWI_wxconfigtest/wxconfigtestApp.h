/***************************************************************
 * Name:      wxconfigtestApp.h
 * Purpose:   Defines Application Class
 * Author:    xwd (1334585420@qq.com)
 * Created:   2017-07-28
 * Copyright: xwd ()
 * License:
 **************************************************************/

#ifndef WXCONFIGTESTAPP_H
#define WXCONFIGTESTAPP_H

#include <wx/app.h>

class wxconfigtestApp : public wxApp
{
    public:
        virtual bool OnInit();
        int OnExit();
};

#endif // WXCONFIGTESTAPP_H
