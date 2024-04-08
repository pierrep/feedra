#include "StopButton.h"

StopButton::StopButton()
{
    id = 0;
    doStop = false;
    isStopped = true;
}

//--------------------------------------------------------------
StopButton::~StopButton()
{
    ofRemoveListener(Interactive::clickedEvent, this, &StopButton::onClicked);
    ofLogVerbose() << "Stop destructor called...";
}

//--------------------------------------------------------------
void StopButton::setup()
{

    ofAddListener(this->clickedEvent, this, &StopButton::onClicked);
}

//--------------------------------------------------------------
StopButton::StopButton(int _id, int _x, int _y, int _w, int _h)
{
    id = _id;

    setX(_x);
    setY(_y);
    setWidth(_w);
    setHeight(_h);
}

//--------------------------------------------------------------
StopButton::StopButton(const StopButton& parent) {
    ofLogVerbose() << "StopButton copy constructor called";

    id = parent.id;
    setX(parent.x);
    setY(parent.y);
    setWidth(parent.width);
    setHeight(parent.height);
}

//--------------------------------------------------------------
void StopButton::onClicked(ClickArgs& args) {
    //ofLogNotice() << "Stop id: " << id << " clicked";
    doStop = true;
}

//--------------------------------------------------------------
void StopButton::render(bool isPlaying, float _position)
{    
    ofPushStyle();

    if(isPlaying || (_position > 0.0f)) {
    //if(isPlaying) {
        ofSetColor(250,160,120);
        ofFill();
        ofDrawRectangle(getX(),getY(),getWidth(), getHeight());
    }


    ofPopStyle();
}
