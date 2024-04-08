#pragma once

#include "Interactive.h"
#include "AppConfig.h"
#include "SoundPlayer.h"

class PlayBar: public Interactive
{
public:
    PlayBar();
    ~PlayBar();
    PlayBar(const PlayBar& d);
    PlayBar(int _id, int _x, int _y, int _w, int _h);
    void setup(AppConfig* conf);
    //void render(bool isPlayingDelay, float position, float duration);
    void render(SoundPlayer& soundPlayer);
    void update();
    void onClicked(ClickArgs& args);

    //int id;

    bool doScrub;
    float position;

    AppConfig* config;
};
