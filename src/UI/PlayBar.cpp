#include "PlayBar.h"

PlayBar::PlayBar()
{
    id = 0;

    doScrub = false;
}

//--------------------------------------------------------------
PlayBar::~PlayBar()
{
    ofRemoveListener(Interactive::clickedEvent, this, &PlayBar::onClicked);
    ofLogVerbose() << "PlayBar destructor called...";
}

//--------------------------------------------------------------
void PlayBar::setup(AppConfig* _config)
{
    ofAddListener(this->clickedEvent, this, &PlayBar::onClicked);
    config = _config;
}

//--------------------------------------------------------------
PlayBar::PlayBar(int _id, int _x, int _y, int _w, int _h)
{
    id = _id;

    setX(_x);
    setY(_y);
    setWidth(_w);
    setHeight(_h);
}

//--------------------------------------------------------------
PlayBar::PlayBar(const PlayBar& parent) {
    ofLog() << "PlayBar copy constructor called";

    id = parent.id;
    setX(parent.x);
    setY(parent.y);
    setWidth(parent.width);
    setHeight(parent.height);

    doScrub = parent.doScrub;
}

//--------------------------------------------------------------
void PlayBar::onClicked(int& args) {
    position = (ofGetMouseX() - getX()) / getWidth();
    //ofLogNotice() << "PlayBar id: " << id << " clicked at position: "<< pos << " ofGetMouseX(): "<< ofGetMouseX() << " getX(): " << getX();
    doScrub = true;
}

//--------------------------------------------------------------
void PlayBar::render(SoundPlayer& soundPlayer)
{    
    ofPushStyle();

    ofFill();
    if(soundPlayer.isPlayingDelay()) {
        ofSetColor(128);
    } else {
        //ofSetColor(100,250,100);
        ofSetHexColor(0xbfe2ac);
    }
    ofDrawRectangle(getX(), getY(), getWidth()*soundPlayer.getPosition(), getHeight());
    ofNoFill();
    ofSetLineWidth(1);
    ofSetColor(64);
    ofDrawRectangle(getX(), getY(), getWidth(), getHeight());

    ofSetColor(0);
    float timeleft;
    if(soundPlayer.isPlayingDelay()) {
        timeleft = (1.0f - soundPlayer.getPosition()) * soundPlayer.getTotalDelay()/1000.0f;
    } else {
        timeleft = (1.0f - soundPlayer.getPosition()) * soundPlayer.getDuration();
    }
    int minutes = timeleft / 60;
    int seconds = (int)timeleft % 60;
    int hours = minutes / 60;
    minutes = minutes % 60;

    std::ostringstream min;
    min << std::setw(2) << std::setfill('0') << minutes;

    std::ostringstream sec;
    sec << std::setw(2) << std::setfill('0') << seconds;

    std::stringstream tl;
    tl << (hours ? ofToString(hours)+":" :"") << min.str()+":" << sec.str();
    //tl << std::fixed << std::setprecision(2) << timeleft;
    int w = config->f3().stringWidth(tl.str());
    int h = config->f3().stringHeight(tl.str());
    config->f3().drawString(tl.str(),getX()+getWidth()/2-(float)w/2.0f,getY()+getHeight()/2 + (float)h/2.0f);

    ofPopStyle();
}

//--------------------------------------------------------------
void PlayBar::update()
{

}
