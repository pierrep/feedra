#include "PlayScene.h"

PlayScene::PlayScene()
{
    id = 0;
    isPlaying =false;
    doPlay = false;
}

//--------------------------------------------------------------
PlayScene::~PlayScene()
{
    ofRemoveListener(Interactive::clickedEvent, this, &PlayScene::onClicked);
    //ofLogNotice() << "PlayScene destructor for id: " << id << " called...";
}

//--------------------------------------------------------------
void PlayScene::setup(AppConfig* _config)
{
    ofAddListener(this->clickedEvent, this, &PlayScene::onClicked);
    config = _config;
}

//--------------------------------------------------------------
PlayScene::PlayScene(int _id, int _x, int _y, int _w, int _h)
{
    id = _id;

    setX(_x);
    setY(_y);
    setWidth(_w);
    setHeight(_h);
}

//--------------------------------------------------------------
PlayScene::PlayScene(const PlayScene& parent) {
    ofLog() << "PlayScene copy constructor called";

    id = parent.id;
    setX(parent.x);
    setY(parent.y);
    setWidth(parent.width);
    setHeight(parent.height);

    doPlay = parent.doPlay;
}

//--------------------------------------------------------------
void PlayScene::onClicked(ClickArgs& args) {
    //ofLogNotice() << "PlayScene id: " << id;
    doPlay = true;
    isPlaying = !isPlaying;
}

//--------------------------------------------------------------
void PlayScene::render()
{    
    ofPushStyle();

    ofFill();
    ofSetHexColor(0x7b2829);

    if(isPlaying) {
        ofDrawRectangle(getX(),getY(),getWidth()/3, getHeight());
        ofDrawRectangle(getX()+getWidth()*2.0f/3.0f,getY(),getWidth()/3, getHeight());
    } else {
        ofDrawTriangle(getX(),getY(),getX(),getY()+getHeight(),getX()+getWidth(),getY()+getHeight()/2);
    }

    ofPopStyle();
}

//--------------------------------------------------------------
void PlayScene::update()
{

}
