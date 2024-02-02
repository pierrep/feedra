#pragma once

#include "Interactive.h"
#include "AppConfig.h"
#include "SoundObject.h"
#include "PlayScene.h"
#include "DeleteScene.h"

class Scene: public Interactive
{
public:
    Scene();
    ~Scene();
    Scene(const Scene& d);
    Scene(AppConfig* config, string name,int _id, int _x, int _y, int _w, int _h);
    void setup();
    void render();
    void update();
    void play();
    void updatePosition(int x,int y);
    void onClicked(int& args);
    void enable();
    void disable();

    int id;
    bool selectScene;
    AppConfig* config;

    string scene_name;
    PlayScene play_button;
    DeleteScene delete_scene;
    vector<SoundObject*> sounds;
    TextInputField textfield;
};

