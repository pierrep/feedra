#pragma once

#include "Interactive.h"
#include "AppConfig.h"

class NewScene: public Interactive
{
public:
    NewScene();
    ~NewScene();
    NewScene(const NewScene& d);
    NewScene(AppConfig* config, int _x, int _y, int _w, int _h);
    void setup();
    void render();
    void update();
    void onClicked(int& args);

    bool doNewScene;

    AppConfig* config;
};
