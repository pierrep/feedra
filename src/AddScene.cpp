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

    ofDrawRectangle(getX()+getWidth()/4 - getWidth()/16,getY(),getWidth()/8, getHeight()/2);
    ofDrawRectangle(getX(),getY()+getHeight()/4 - getHeight()/16, getWidth()/2, getHeight()/8);

    ofNoFill();
    ofSetLineWidth(1);
    ofDrawRectangle(getX(),getY(),getWidth()/2,getHeight()/2);

    ofPopStyle();
}

//--------------------------------------------------------------
void AddScene::update()
{

}
