#pragma once

#include "Interactive.h"
#include "AppConfig.h"


class CheckBox: public Interactive
{
public:
    CheckBox();
    ~CheckBox();
    CheckBox(const CheckBox& d);
    CheckBox(int _id, int _x, int _y, int _w, int _h);
    void setup(AppConfig* config, int _id);
    void render();
    void setFont(ofTrueTypeFont* _font);
    void setLabelString(string str);
    void onClicked(ClickArgs& args);

    bool bActivate;
    bool isActive;
    AppConfig* config;

protected:
    ofPath cross;

    bool bDrawLabel;
    string	labelString;
    ofTrueTypeFont* labelFont;

};
