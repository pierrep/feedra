#include "CheckBox.h"

CheckBox::CheckBox()
{
    id = 0;
    bActivate = false;
    isActive = false;
}

//--------------------------------------------------------------
CheckBox::~CheckBox()
{
    ofRemoveListener(Interactive::clickedEvent, this, &CheckBox::onClicked);
    ofLogVerbose() << "Stop destructor called...";
}

//--------------------------------------------------------------
void CheckBox::setup(AppConfig* _config)
{
    config = _config;
    ofAddListener(this->clickedEvent, this, &CheckBox::onClicked);

    cross.clear();
    cross.setStrokeColor(ofColor(0));
    cross.setStrokeWidth(2*config->x_scale);
    cross.setFilled(false);
    int x = 4*config->x_scale;
    int y = 4*config->y_scale;
    cross.moveTo(this->getTopLeft() + glm::vec3(x,y,0));
    cross.lineTo(this->getBottomRight() + glm::vec3(-x,-y,0));
    cross.moveTo(this->getTopRight() + glm::vec3(-x,y,0));
    cross.lineTo(this->getBottomLeft() + glm::vec3(x,-y,0));
}

//--------------------------------------------------------------
CheckBox::CheckBox(int _id, int _x, int _y, int _w, int _h)
{
    id = _id;

    setX(_x);
    setY(_y);
    setWidth(_w);
    setHeight(_h);
}

//--------------------------------------------------------------
CheckBox::CheckBox(const CheckBox& parent) {
    ofLogVerbose() << "CheckBox copy constructor called";

    id = parent.id;
    setX(parent.x);
    setY(parent.y);
    setWidth(parent.width);
    setHeight(parent.height);
}

//--------------------------------------------------------------
void CheckBox::onClicked(ClickArgs& args) {
    //ofLogNotice() << "Stop id: " << id << " clicked";
    bActivate = true;
}

//--------------------------------------------------------------
void CheckBox::render()
{    
    ofPushStyle();

    ofSetColor(66);
    ofSetLineWidth(3);
    ofNoFill();
	ofDrawRectangle(getX(),getY(),getWidth(), getHeight());
//cout << "x = " << getX() << " y = " << getY() << " width = " << getWidth() << " height =" << getHeight() << endl;
    if(isActive) {
        cross.draw();
    }

    ofPopStyle();
}
