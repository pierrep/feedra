#include "Looper.h"

Looper::Looper()
{
    id = 0;
    doLooper = false;
    isLooping = false;
}

//--------------------------------------------------------------
Looper::~Looper()
{
    ofRemoveListener(Interactive::clickedEvent, this, &Looper::onClicked);
    ofLogVerbose() << "Looper destructor called...";
}

//--------------------------------------------------------------
void Looper::setup(AppConfig* _config)
{
    config = _config;
    isLooping = config->loopByDefault;
    ofAddListener(this->clickedEvent, this, &Looper::onClicked);
}

//--------------------------------------------------------------
Looper::Looper(int _id, int _x, int _y, int _w, int _h)
{
    id = _id;

    setX(_x);
    setY(_y);
    setWidth(_w);
    setHeight(_h);
}

//--------------------------------------------------------------
Looper::Looper(const Looper& parent) {
    ofLogVerbose() << "Looper copy constructor called";

    id = parent.id;
    setX(parent.x);
    setY(parent.y);
    setWidth(parent.width);
    setHeight(parent.height);
}

//--------------------------------------------------------------
void Looper::onClicked(ClickArgs& args) {
    //ofLogNotice() << "Looper id: " << id << " clicked";
    doLooper = true;
    isLooping = !isLooping;
}

//--------------------------------------------------------------
void Looper::render()
{    
    ofPushStyle();

    if(isLooping)
    {
        //ofSetHexColor(0xfe700e);
        ofSetColor(255.0f,50,50);
    } else {
        ofSetHexColor(0x9b6f42);
    }

    //ofFill();
    config->loopicon.draw(getX(),getY(),getWidth(),getHeight());
    //ofDrawRectRounded(getX(),getY(),getWidth(), getHeight(),10);

    ofPopStyle();
}

//--------------------------------------------------------------
void Looper::update()
{
}
