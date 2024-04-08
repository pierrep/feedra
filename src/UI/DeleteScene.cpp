#include "DeleteScene.h"

DeleteScene::DeleteScene()
{
    id = 0;
    doDeleteScene = false;
}

//--------------------------------------------------------------
DeleteScene::~DeleteScene()
{
    ofRemoveListener(Interactive::clickedEvent, this, &DeleteScene::onClicked);
    ofLogVerbose() << "DeleteScene destructor called...";
}

//--------------------------------------------------------------
void DeleteScene::setup(AppConfig* _config)
{
    ofAddListener(this->clickedEvent, this, &DeleteScene::onClicked);    
    config = _config;
}

//--------------------------------------------------------------
DeleteScene::DeleteScene(int _x, int _y, int _w, int _h)
{

    setX(_x);
    setY(_y);
    setWidth(_w);
    setHeight(_h);
}

//--------------------------------------------------------------
DeleteScene::DeleteScene(const DeleteScene& parent) {
    ofLog() << "DeleteScene copy constructor called";

    id = parent.id;
    setX(parent.x);
    setY(parent.y);
    setWidth(parent.width);
    setHeight(parent.height);

    doDeleteScene = parent.doDeleteScene;
}

//--------------------------------------------------------------
void DeleteScene::onClicked(ClickArgs& args) {
    //ofLogNotice() << "DeleteScene id: " << id;   
    doDeleteScene = true;
}

//--------------------------------------------------------------
void DeleteScene::render()
{    
    ofPushStyle();

    ofFill();
    ofSetHexColor(0x7b2800);

    //ofDrawRectangle(getX()+getWidth()/2 - getWidth()/8,getY(),getWidth()/4, getHeight());
    ofDrawRectangle(getX(),getY()+getHeight()/2 - getHeight()/8, getWidth(), getHeight()/4);

    ofNoFill();
    ofSetLineWidth(1);
    ofDrawRectangle(getX(),getY(),getWidth(),getHeight());

    ofPopStyle();
}

//--------------------------------------------------------------
void DeleteScene::update()
{

}
