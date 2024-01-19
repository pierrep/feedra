#pragma once

#include "Interactive.h"
#include "AppConfig.h"
#include "SoundObject.h"
#include "PlayScene.h"

class Scene: public Interactive
{
public:
    Scene();
    ~Scene();
    Scene(const Scene& d);
    Scene(AppConfig* config,int _id, int _x, int _y, int _w, int _h);
    void setup();
    void render();
    void update();
    void play();
    void onClicked(int& args);

    int id;
    bool playScene;
    AppConfig* config;

    PlayScene play_button;
    vector<SoundObject*> sounds;
};
