#include "PlayBar.h"

PlayBar::PlayBar()
{
    id = 0;

    doScrub = false;
}

//--------------------------------------------------------------
PlayBar::~PlayBar()
{
    ofRemoveListener(Interactive::clickedEvent, this, &PlayBar::onClicked);
    ofLogVerbose() << "PlayBar destructor called...";
}

//--------------------------------------------------------------
void PlayBar::setup(AppConfig* _config)
{
    ofAddListener(this->clickedEvent, this, &PlayBar::onClicked);
    config = _config;
}

//--------------------------------------------------------------
PlayBar::PlayBar(int _id, int _x, int _y, int _w, int _h)
{
    id = _id;

    setX(_x);
    setY(_y);
    setWidth(_w);
    setHeight(_h);
}

//--------------------------------------------------------------
PlayBar::PlayBar(const PlayBar& parent) {
    ofLog() << "PlayBar copy constructor called";

    id = parent.id;
    setX(parent.x);
    setY(parent.y);
    setWidth(parent.width);
    setHeight(parent.height);

    doScrub = parent.doScrub;
}

//--------------------------------------------------------------
void PlayBar::onClicked(int& args) {
    position = (ofGetMouseX() - getX()) / getWidth();
    //ofLogNotice() << "PlayBar id: " << id << " clicked at position: "<< pos << " ofGetMouseX(): "<< ofGetMouseX() << " getX(): " << getX();
    doScrub = true;
}

//--------------------------------------------------------------
void PlayBar::render(float position)
{    
    ofPushStyle();

    //if(isPlaying || (isLoaded && (position > 0.0f))) {
    ofFill();
    ofSetHexColor(0x7b2829);
    ofDrawRectangle(getX(), getY(), getWidth()*position, getHeight());
    ofNoFill();
    ofSetLineWidth(1);
    ofDrawRectangle(getX(), getY(), getWidth(), getHeight());

    ofPopStyle();
}

//--------------------------------------------------------------
void PlayBar::update()
{

}
