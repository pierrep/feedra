#include "Player.h"

Player::Player()
{
    id = 0;
    isPlaying = false;
    isLoaded = false;
    doPlay = false;
}

//--------------------------------------------------------------
Player::~Player()
{
    ofRemoveListener(Interactive::clickedEvent, this, &Player::onClicked);
    ofLogVerbose() << "Player destructor called...";
}

//--------------------------------------------------------------
void Player::setup(AppConfig* _config)
{
    ofAddListener(this->clickedEvent, this, &Player::onClicked);
    config = _config;
}

//--------------------------------------------------------------
Player::Player(int _id, int _x, int _y, int _w, int _h)
{
    id = _id;

    setX(_x);
    setY(_y);
    setWidth(_w);
    setHeight(_h);
}

//--------------------------------------------------------------
Player::Player(const Player& parent) {
    ofLogVerbose() << "Player copy constructor called";

    id = parent.id;
    setX(parent.x);
    setY(parent.y);
    setWidth(parent.width);
    setHeight(parent.height);

    isPlaying = parent.isPlaying;
    doPlay = parent.doPlay;
}

//--------------------------------------------------------------
void Player::onClicked(int& args) {
    //ofLogVerbose() << "Player id: " << id << " clicked";
    doPlay = true;
}

//--------------------------------------------------------------
void Player::render(float position)
{    
    ofPushStyle();

    ofFill();
    if(isLoaded) {
        ofSetHexColor(0xd08331);
    } else {
        ofSetHexColor(0x998c84);
    }

    if(isPlaying) {
        ofDrawRectangle(getX(),getY(),getWidth()/3, getHeight());
        ofDrawRectangle(getX()+getWidth()*2.0f/3.0f,getY(),getWidth()/3, getHeight());
    } else {
        ofDrawTriangle(getX(),getY(),getX(),getY()+getHeight(),getX()+getWidth(),getY()+getHeight()/2);
    }

    if(isPlaying || (isLoaded && (position > 0.0f))) {
        ofSetHexColor(0x7b2829);
        ofDrawRectangle(getX(),getY()+getHeight()+1,getWidth()*position, 10*config->y_scale);
        ofNoFill();
        ofSetLineWidth(1);
        ofDrawRectangle(getX(),getY()+getHeight()+1,getWidth(), 10*config->y_scale);
    }

    ofSetColor(192);
    ofSetLineWidth(1);
    ofNoFill();
    ofDrawRectangle(getX(),getY(),getWidth(), getHeight());


    ofPopStyle();
}

//--------------------------------------------------------------
void Player::update()
{

}
