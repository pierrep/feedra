#include "NewScene.h"

NewScene::NewScene()
{
    doNewScene = false;
}

//--------------------------------------------------------------
NewScene::~NewScene()
{
    ofRemoveListener(Interactive::clickedEvent, this, &NewScene::onClicked);
    ofLogVerbose() << "NewScene destructor called...";
}

//--------------------------------------------------------------
void NewScene::setup()
{
    ofAddListener(this->clickedEvent, this, &NewScene::onClicked);    
}

//--------------------------------------------------------------
NewScene::NewScene(AppConfig* _config, int _x, int _y, int _w, int _h)
{
    config = _config;

    setX(_x);
    setY(_y);
    setWidth(_w);
    setHeight(_h);
}

//--------------------------------------------------------------
NewScene::NewScene(const NewScene& parent) {
    ofLog() << "NewScene copy constructor called";

    setX(parent.x);
    setY(parent.y);
    setWidth(parent.width);
    setHeight(parent.height);

    doNewScene = parent.doNewScene;
}

//--------------------------------------------------------------
void NewScene::onClicked(int& args) {
    ofLogNotice() << "NewScene clicked";
    doNewScene = true;
}

//--------------------------------------------------------------
void NewScene::render()
{    
    ofPushStyle();

    ofFill();
    ofSetHexColor(0x7b2800);

    ofDrawRectangle(getX()+getWidth()/2 - getWidth()/8,getY(),getWidth()/4, getHeight());
    ofDrawRectangle(getX(),getY()+getHeight()/2 - getHeight()/8, getWidth(), getHeight()/4);

    ofNoFill();
    ofSetLineWidth(1);
    ofDrawRectangle(getX(),getY(),getWidth(),getHeight());

    ofPopStyle();
}

//--------------------------------------------------------------
void NewScene::update()
{

}
