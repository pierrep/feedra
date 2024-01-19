#include "Scene.h"

Scene::Scene()
{
    id = 0;
    playScene = false;
}

//--------------------------------------------------------------
Scene::~Scene()
{
    for(size_t i=0;i < sounds.size();i++) {
        delete sounds[i];
    }
    sounds.clear();

    ofRemoveListener(Interactive::clickedEvent, this, &Scene::onClicked);
    ofLogVerbose() << "Scene destructor called...";
}

//--------------------------------------------------------------
void Scene::setup()
{
    ofAddListener(this->clickedEvent, this, &Scene::onClicked);

    // create grid of sound players
    int sound_id = 0;
    for(size_t i=0;i < config->gridWidth*config->gridHeight;i++) {
        int x = i%config->gridWidth*config->spacing + config->xoffset;
        int y = (i/config->gridWidth)*config->spacing + config->yoffset;
        sound_id++;
        SoundObject* s = new SoundObject(config,id,sound_id,x,y,config->size,config->size);
        sounds.push_back(s);
    }

    // setup sound objects
    for(size_t i=0;i < sounds.size();i++) {
        sounds[i]->setup();
        sounds[i]->load();
    }

    play_button.setup(config);
}

//--------------------------------------------------------------
Scene::Scene(AppConfig* _config, int _id, int _x, int _y, int _w, int _h)
{
    id = _id;

    config = _config;
    setX(_x);
    setY(_y);
    setWidth(_w);
    setHeight(_h);

    play_button.id = _id;
    play_button.setX(_x + 50 * config->x_scale);
    play_button.setY(_y + 10 * config->y_scale);
    play_button.setWidth(30 * config->x_scale);
    play_button.setHeight(30 * config->y_scale);

    playScene = false;
}

//--------------------------------------------------------------
Scene::Scene(const Scene& parent) {
    ofLog() << "Scene copy constructor called";

    id = parent.id;
    setX(parent.x);
    setY(parent.y);
    setWidth(parent.width);
    setHeight(parent.height);
}

//--------------------------------------------------------------
void Scene::onClicked(int& args) {
    ofLogNotice() << "Scene id: " << id << " clicked";
    playScene = true;
    config->activeScene = id;
}

//--------------------------------------------------------------
void Scene::render()
{    
    if(config->activeScene == id) {
        for(size_t i=0; i < sounds.size();i++) {
             sounds[i]->render();
        }
    }

    ofPushStyle();
    if(config->activeScene == id) {
        ofSetColor(255.0f,100,100);
    } else {
        ofSetColor(64);
    }
    ofDrawRectangle(getX(),getY(),getWidth(), getHeight());

    ofNoFill();
    ofSetColor(64);
    ofDrawRectangle(getX(),getY(),getWidth(), getHeight());

    if(config->activeScene == id) {
        play_button.render();
    }

    ofSetColor(0);
    config->smallfont.drawString("Scene"+ofToString(id),getX(),getY()-5);
    ofPopStyle();   
}

//--------------------------------------------------------------
void Scene::play()
{
    for(size_t i=0; i < sounds.size();i++) {
         sounds[i]->play();
    }
}

//--------------------------------------------------------------
void Scene::update()
{
    if(play_button.doPlay) {
        play_button.doPlay = false;
        play();
    }

    for(size_t i=0; i < sounds.size();i++) {
         sounds[i]->update();
    }
}
