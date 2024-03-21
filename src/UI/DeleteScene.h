#pragma once

#include "Interactive.h"
#include "AppConfig.h"

class DeleteScene: public Interactive
{
public:
    DeleteScene();
    ~DeleteScene();
    DeleteScene(const DeleteScene& d);
    DeleteScene(int _x, int _y, int _w, int _h);
    void setup(AppConfig* config);
    void render();
    void update();
    void onClicked(int& args);

    int id;
    bool doDeleteScene;

    AppConfig* config;
};
