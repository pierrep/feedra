#include "LoadButton.h"

LoadButton::LoadButton()
{
    id = 0;
    doLoad = false;
}

//--------------------------------------------------------------
LoadButton::~LoadButton()
{
    ofRemoveListener(Interactive::clickedEvent, this, &LoadButton::onClicked);
    ofLogVerbose() << "LoadButton destructor called...";
}

//--------------------------------------------------------------
void LoadButton::setup()
{

    ofAddListener(this->clickedEvent, this, &LoadButton::onClicked);
}

//--------------------------------------------------------------
LoadButton::LoadButton(int _id, int _x, int _y, int _w, int _h)
{
    id = _id;

    setX(_x);
    setY(_y);
    setWidth(_w);
    setHeight(_h);
}

//--------------------------------------------------------------
LoadButton::LoadButton(const LoadButton& parent) {
    ofLogVerbose() << "LoadButton copy constructor called";

    id = parent.id;
    setX(parent.x);
    setY(parent.y);
    setWidth(parent.width);
    setHeight(parent.height);
}

//--------------------------------------------------------------
void LoadButton::onClicked(int& args) {
    ofLogNotice() << "LoadButton id: " << id << " clicked";
    doLoad = true;
}

//--------------------------------------------------------------
void LoadButton::render()
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
void LoadButton::update()
{

}
