#include "Button.h"

Button::Button()
{
    id = 0;
    bActivate = false;
    bIsActive = false;
}

//--------------------------------------------------------------
Button::~Button()
{
    ofRemoveListener(Interactive::clickedEvent, this, &Button::onClicked);
    ofLogVerbose() << "Stop destructor called...";
}

//--------------------------------------------------------------
void Button::setup(std::function<void()>& render)
{
    renderFunc = render;
    ofAddListener(this->clickedEvent, this, &Button::onClicked);
}

//--------------------------------------------------------------
Button::Button(int _id, int _x, int _y, int _w, int _h)
{
    id = _id;

    setX(_x);
    setY(_y);
    setWidth(_w);
    setHeight(_h);
}

//--------------------------------------------------------------
Button::Button(const Button& parent) {
    ofLogVerbose() << "Button copy constructor called";

    id = parent.id;
    setX(parent.x);
    setY(parent.y);
    setWidth(parent.width);
    setHeight(parent.height);
}

//--------------------------------------------------------------
void Button::onClicked(ClickArgs& args) {
    //ofLogNotice() << "Button id: " << id << " clicked";
    bActivate = true;
}

//--------------------------------------------------------------
void Button::render()
{    
    ofPushStyle();
    renderFunc();
    ofPopStyle();
}
