#include "Scene.h"

Scene::Scene()
{   
}

//--------------------------------------------------------------
Scene::~Scene()
{
    for(size_t i=0;i < sounds.size();i++) {
        delete sounds[i];
    }
    sounds.clear();

    ofRemoveListener(Interactive::clickedEvent, this, &Scene::onClicked);
    //ofLogNotice() << "Scene destructor for id: " << id << " called...";
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
        sounds[i]->load(0);
    }

    play_button.setup(config);
    delete_scene.setup(config);
    textfield.setUseListeners(true);
}

//--------------------------------------------------------------
Scene::Scene(AppConfig* _config, string name, int _id, int _activeSound, int _x, int _y, int _w, int _h)
{
    id = _id;

    config = _config;
    activeSound = _activeSound;

    setWidth(_w);
    setHeight(_h);

    updatePosition(_x,_y);

    play_button.id = id;
    play_button.setWidth(20 * config->x_scale);
    play_button.setHeight(20 * config->y_scale);

    delete_scene.id = id;
    delete_scene.setWidth(20 * config->x_scale);
    delete_scene.setHeight(20 * config->y_scale);

    scene_name = "Scene "+ofToString(id);
    if(strcmp(name.c_str(),"")== 0) {
        textfield.text = scene_name;
    } else {
        textfield.text = name;
    }

    textfield.setFont(config->f2());
    textfield.disable();

    selectScene = false;
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

//---------------------------------------------------------
void Scene::updatePosition(int _x, int _y)
{
    setX(_x);
    setY(_y);

    play_button.setX(_x + getWidth()- 80 * config->x_scale);
    play_button.setY(_y + 10 * config->y_scale);

    delete_scene.setX(_x + getWidth()- 40 * config->x_scale);
    delete_scene.setY(_y + 10 * config->y_scale);

    float txt_height = 16;
    textfield.bounds = ofRectangle(getX()+10*config->x_scale, getY() + getHeight()/2 - txt_height/3*config->y_scale, 100*config->x_scale, txt_height*config->y_scale);
}

//---------------------------------------------------------
void Scene::onClicked(int& args) {
    //ofLogNotice() << "Scene id: " << id << " clicked";
    selectScene = true;
    config->activeScene =  id;
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
    if(config->activeScene == id) {
        ofSetColor(0);
    } else {
        ofSetColor(64);
    }
    ofDrawRectangle(getX(),getY(),getWidth(), getHeight());

    if(config->activeScene == id) {
        play_button.render();
        delete_scene.render();
    }    

    if(config->activeScene == id) {
        ofSetColor(0);
    } else {
        ofSetColor(128);
    }
    //config->smallfont.drawString(scene_name,getX()+10*config->x_scale, getY() + getHeight()/2);
    textfield.draw();
    if(textfield.isEditing()) {
        ofDrawRectangle(textfield.bounds);
    }
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
    activeSound = config->activeSound;

    if(play_button.doPlay) {
        play_button.doPlay = false;
        play();
    }

    for(size_t i=0; i < sounds.size();i++) {
         sounds[i]->update();
    }
}

//--------------------------------------------------------------
void Scene::enable()
{
    play_button.enableEvents();
    delete_scene.enableEvents();
    textfield.enable();
}

//--------------------------------------------------------------
void Scene::disable()
{
    play_button.disableEvents();
    delete_scene.disableEvents();
    textfield.disable();
}

