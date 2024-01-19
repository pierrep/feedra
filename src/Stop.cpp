#include "Stop.h"

Stop::Stop()
{
    id = 0;
    doStop = false;
    isStopped = true;
}

//--------------------------------------------------------------
Stop::~Stop()
{
    ofRemoveListener(Interactive::clickedEvent, this, &Stop::onClicked);
    ofLogVerbose() << "Stop destructor called...";
}

//--------------------------------------------------------------
void Stop::setup()
{

    ofAddListener(this->clickedEvent, this, &Stop::onClicked);
}

//--------------------------------------------------------------
Stop::Stop(int _id, int _x, int _y, int _w, int _h)
{
    id = _id;

    setX(_x);
    setY(_y);
    setWidth(_w);
    setHeight(_h);
}

//--------------------------------------------------------------
Stop::Stop(const Stop& parent) {
    ofLogVerbose() << "Stop copy constructor called";

    id = parent.id;
    setX(parent.x);
    setY(parent.y);
    setWidth(parent.width);
    setHeight(parent.height);
}

//--------------------------------------------------------------
void Stop::onClicked(int& args) {
    //ofLogNotice() << "Stop id: " << id << " clicked";
    doStop = true;
}

//--------------------------------------------------------------
void Stop::render(bool isPlaying, float _position)
{    
    ofPushStyle();

    if(isPlaying || (_position > 0.0f)) {
        ofSetColor(250,160,120);
        ofFill();
        ofDrawRectangle(getX(),getY(),getWidth(), getHeight());
    }


    ofPopStyle();
}

//--------------------------------------------------------------
void Stop::update()
{

}
