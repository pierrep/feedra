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
void Player::render(SoundPlayer& soundPlayer)
{    
    ofPushStyle();

    ofFill();
    if(isLoaded) {
        ofSetHexColor(0xd08331);
    } else {
        ofSetHexColor(0x998c84);
    }

    if(soundPlayer.isPlaying()) {
        //pause button
        ofDrawRectangle(getX(),getY(),getWidth()/3, getHeight());
        ofDrawRectangle(getX()+getWidth()*2.0f/3.0f,getY(),getWidth()/3, getHeight());
    } else {
        //play button
        ofDrawTriangle(getX(),getY(),getX(),getY()+getHeight(),getX()+getWidth(),getY()+getHeight()/2);
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
