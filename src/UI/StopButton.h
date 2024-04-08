#pragma once

#include "Interactive.h"


class StopButton: public Interactive
{
public:
    StopButton();
    ~StopButton();
    StopButton(const StopButton& d);
    StopButton(int _id, int _x, int _y, int _w, int _h);
    void setup();
    void render(bool isPlaying, float position);
    void onClicked(ClickArgs& args);

    //int id;
    bool doStop;
    bool isStopped;

};
