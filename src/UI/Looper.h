#pragma once

#include "Interactive.h"
#include "AppConfig.h"


class Looper: public Interactive
{
public:
    Looper();
    ~Looper();
    Looper(const Looper& d);
    Looper(int _id, int _x, int _y, int _w, int _h);
    void setup(AppConfig* config);
    void render();
    void update();
    void onClicked(ClickArgs& args);

    //int id;
    bool doLooper;
    bool isLooping;

    AppConfig* config;

};
