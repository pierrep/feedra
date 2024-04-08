#pragma once

#include "Interactive.h"
#include "AppConfig.h"
#include "SoundPlayer.h"

class PlayButton: public Interactive
{
public:
    PlayButton();
    ~PlayButton();
    PlayButton(const PlayButton& d);
    PlayButton(int _id, int _x, int _y, int _w, int _h);
    void setup(AppConfig* conf);
    void render(SoundPlayer& soundplayer);
    void onClicked(ClickArgs& args);

    //int id;
    bool isPlaying;
    bool isLoaded;
    bool doPlay;

    AppConfig* config;
};
