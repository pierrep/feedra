#pragma once

#include "Interactive.h"
#include "AppConfig.h"

class AddScene: public Interactive
{
public:
    AddScene();
    ~AddScene();
    AddScene(const AddScene& d);
    AddScene(AppConfig* config, int _x, int _y, int _w, int _h);
    void setup();
    void render(int hexcol);
    void update();
    void onClicked(int& args);

    bool doAddScene;

    AppConfig* config;
};
