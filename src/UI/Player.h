#pragma once

#include "Interactive.h"
#include "AppConfig.h"
#include "SoundPlayer.h"

class Player: public Interactive
{
public:
    Player();
    ~Player();
    Player(const Player& d);
    Player(int _id, int _x, int _y, int _w, int _h);
    void setup(AppConfig* conf);
    void render(SoundPlayer& soundplayer);
    void update();
    void onClicked(int& args);

    int id;
    bool isPlaying;
    bool isLoaded;
    bool doPlay;

    AppConfig* config;
};
