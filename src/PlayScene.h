#pragma once

#include "Interactive.h"
#include "AppConfig.h"

class PlayScene: public Interactive
{
public:
    PlayScene();
    ~PlayScene();
    PlayScene(const PlayScene& d);
    PlayScene(int _id, int _x, int _y, int _w, int _h);
    void setup(AppConfig* conf);
    void render();
    void update();
    void onClicked(int& args);

    int id;
    bool doPlay;
    bool isPlaying;

    AppConfig* config;
};
