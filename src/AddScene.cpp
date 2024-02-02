#include "AddScene.h"

AddScene::AddScene()
{    
}

//--------------------------------------------------------------
AddScene::~AddScene()
{
    ofRemoveListener(Interactive::clickedEvent, this, &AddScene::onClicked);
    ofLogVerbose() << "AddScene destructor called...";
}

//--------------------------------------------------------------
void AddScene::setup()
{
    ofAddListener(this->clickedEvent, this, &AddScene::onClicked);
}

//--------------------------------------------------------------
AddScene::AddScene(AppConfig* _config, int _x, int _y, int _w, int _h)
{
    config = _config;

    setX(_x);
    setY(_y);
    setWidth(_w);
    setHeight(_h);

    doAddScene = false;
}

//--------------------------------------------------------------
AddScene::AddScene(const AddScene& parent) {
    ofLog() << "AddScene copy constructor called";

    setX(parent.x);
    setY(parent.y);
    setWidth(parent.width);
    setHeight(parent.height);

    doAddScene = parent.doAddScene;
}

//--------------------------------------------------------------
void AddScene::onClicked(int& args) {
    ofLogNotice() << "AddScene clicked";
    doAddScene = true;
}

//--------------------------------------------------------------
void AddScene::render(int hexcol)
{    
    ofPushStyle();

    ofFill();
    ofSetHexColor(hexcol);

    ofDrawRectangle(getX()+getWidth()/2 - getWidth()/8,getY(),getWidth()/4, getHeight());
    ofDrawRectangle(getX(),getY()+getHeight()/2 - getHeight()/8, getWidth(), getHeight()/4);

    ofNoFill();
    ofSetLineWidth(1);
    ofDrawRectangle(getX(),getY(),getWidth(),getHeight());

    ofPopStyle();
}

//--------------------------------------------------------------
void AddScene::update()
{

}
