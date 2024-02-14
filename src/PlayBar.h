#pragma once

#include "Interactive.h"
#include "AppConfig.h"

class PlayBar: public Interactive
{
public:
    PlayBar();
    ~PlayBar();
    PlayBar(const PlayBar& d);
    PlayBar(int _id, int _x, int _y, int _w, int _h);
    void setup(AppConfig* conf);
    void render(bool isPlayingDelay, float position);
    void update();
    void onClicked(int& args);

    int id;

    bool doScrub;
    float position;

    AppConfig* config;
};
