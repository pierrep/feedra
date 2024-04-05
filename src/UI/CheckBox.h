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
    void setup(AppConfig* config);
    void render();
    void onClicked(int& args);

    int id;
    bool bActivate;
    bool isActive;
    AppConfig* config;

private:
    ofPath cross;

};
