#pragma once

#include "Interactive.h"


class Stop: public Interactive
{
public:
    Stop();
    ~Stop();
    Stop(const Stop& d);
    Stop(int _id, int _x, int _y, int _w, int _h);
    void setup();
    void render(bool isPlaying, float position);
    void update();
    void onClicked(int& args);

    int id;
    bool doStop;
    bool isStopped;

};
