#include "PlayButton.h"

PlayButton::PlayButton()
{
    id = 0;
    isPlaying = false;
    isLoaded = false;
    doPlay = false;
}

//--------------------------------------------------------------
PlayButton::~PlayButton()
{
    ofRemoveListener(Interactive::clickedEvent, this, &PlayButton::onClicked);
    ofLogVerbose() << "PlayButton destructor called...";
}

//--------------------------------------------------------------
void PlayButton::setup(AppConfig* _config)
{
    ofAddListener(this->clickedEvent, this, &PlayButton::onClicked);
    config = _config;
}

//--------------------------------------------------------------
PlayButton::PlayButton(int _id, int _x, int _y, int _w, int _h)
{
    id = _id;

    setX(_x);
    setY(_y);
    setWidth(_w);
    setHeight(_h);
}

//--------------------------------------------------------------
PlayButton::PlayButton(const PlayButton& parent) {
    ofLogVerbose() << "PlayButton copy constructor called";

    id = parent.id;
    setX(parent.x);
    setY(parent.y);
    setWidth(parent.width);
    setHeight(parent.height);

    isPlaying = parent.isPlaying;
    doPlay = parent.doPlay;
}

//--------------------------------------------------------------
void PlayButton::onClicked(int& args) {
    //ofLogVerbose() << "PlayButton id: " << id << " clicked";
    doPlay = true;
}

//--------------------------------------------------------------
void PlayButton::render(SoundPlayer& soundPlayer)
{    
    ofPushStyle();

    ofFill();
    if(isLoaded) {
        ofSetHexColor(0xd08331);
    } else {
        ofSetHexColor(0x998c84);
    }

    if(soundPlayer.isPlaying()) {
        //pause button
        ofDrawRectangle(getX(),getY(),getWidth()/3, getHeight());
        ofDrawRectangle(getX()+getWidth()*2.0f/3.0f,getY(),getWidth()/3, getHeight());
    } else {
        //play button
        ofDrawTriangle(getX(),getY(),getX(),getY()+getHeight(),getX()+getWidth(),getY()+getHeight()/2);
    }

    ofSetColor(192);
    ofSetLineWidth(1);
    ofNoFill();
    ofDrawRectangle(getX(),getY(),getWidth(), getHeight());

    ofPopStyle();
}
