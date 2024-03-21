#include "Loader.h"

Loader::Loader()
{
    id = 0;
    doLoad = false;
}

//--------------------------------------------------------------
Loader::~Loader()
{
    ofRemoveListener(Interactive::clickedEvent, this, &Loader::onClicked);
    ofLogVerbose() << "Loader destructor called...";
}

//--------------------------------------------------------------
void Loader::setup()
{

    ofAddListener(this->clickedEvent, this, &Loader::onClicked);
}

//--------------------------------------------------------------
Loader::Loader(int _id, int _x, int _y, int _w, int _h)
{
    id = _id;

    setX(_x);
    setY(_y);
    setWidth(_w);
    setHeight(_h);
}

//--------------------------------------------------------------
Loader::Loader(const Loader& parent) {
    ofLogVerbose() << "Loader copy constructor called";

    id = parent.id;
    setX(parent.x);
    setY(parent.y);
    setWidth(parent.width);
    setHeight(parent.height);
}

//--------------------------------------------------------------
void Loader::onClicked(int& args) {
    ofLogNotice() << "Loader id: " << id << " clicked";
    doLoad = true;
}

//--------------------------------------------------------------
void Loader::render()
{    
    ofPushStyle();

    ofSetColor(64);
    ofFill();
    ofDrawRectRounded(getX(),getY(),getWidth(), getHeight(),10);

    ofSetColor(64);
    ofSetLineWidth(1);
    ofNoFill();
    ofDrawRectRounded(getX(),getY(),getWidth(), getHeight(),10);


    ofPopStyle();
}

//--------------------------------------------------------------
void Loader::update()
{

}
