#include "Button.h"

Button::Button()
{
    id = 0;
    bActivate = false;
    bIsActive = false;
    bBorder = false;
}

//--------------------------------------------------------------
Button::~Button()
{
    ofRemoveListener(Interactive::clickedEvent, this, &Button::onClicked);
    ofLogVerbose() << "Stop destructor called...";
}

//--------------------------------------------------------------
Button::Button(AppConfig* _config, int _id, int _x, int _y, int _w, int _h, ButtonType _type)
{
    config = _config;
    id = _id;
    buttonType = _type;

    setX(_x);
    setY(_y);
    setWidth(_w);
    setHeight(_h);
    ofAddListener(this->clickedEvent, this, &Button::onClicked);

    bActivate = false;
    bIsActive = false;
}

//--------------------------------------------------------------
Button::Button(const Button& parent) {
    ofLogWarning() << "Button copy constructor called";

    config = parent.config;
    id = parent.id;
    buttonType = parent.buttonType;


    setX(parent.x);
    setY(parent.y);
    setWidth(parent.width);
    setHeight(parent.height);

    bActivate = parent.bActivate;
    bIsActive = parent.bIsActive;

    ofAddListener(this->clickedEvent, this, &Button::onClicked);
}

//--------------------------------------------------------------
void Button::setup(AppConfig* _config, int _id, int _x, int _y, int _w, int _h, ButtonType _type)
{
    config = _config;
    id = _id;
    buttonType = _type;

    setX(_x);
    setY(_y);
    setWidth(_w);
    setHeight(_h);
    ofAddListener(this->clickedEvent, this, &Button::onClicked);

    bActivate = false;
    bIsActive = false;
}

//--------------------------------------------------------------
void Button::onClicked(ClickArgs& args) {
    //ofLogNotice() << "Button id: " << id << " clicked";
    bActivate = true;
    bIsActive = !bIsActive;
}

//--------------------------------------------------------------
void Button::draw()
{
    switch(buttonType){
        case ButtonType::MINUS:
        {
            ofPushStyle();
            ofSetHexColor(0x81bbe1);
            ofFill();
            ofDrawRectRounded(getX(),getY(),getWidth(), getHeight(),5*config->x_scale);
            ofSetColor(64);
            float x = getX() + width/2.0f - config->f1().stringWidth("-")/2.0f;
            float y = getY() + getHeight()/2.0f + config->f1().stringHeight("+")/2.0f;
            config->f1().drawString("-",x,y);
            ofPopStyle();
            break;
        }
        case ButtonType::PLUS:
        {
            ofPushStyle();
            ofSetHexColor(0x81bbe1);
            ofFill();
            ofDrawRectRounded(getX(),getY(),getWidth(), getHeight(),5*config->x_scale);
            ofSetColor(64);
            float x = getX()+width/2.0f - config->f1().stringWidth("+")/2.0f;
            float y = getY() + getHeight()/2.0f + config->f1().stringHeight("+")/2.0f;
            config->f1().drawString("+",x,y);
            ofPopStyle();
            break;
        }
    case ButtonType::ADD:
    {
        ofPushStyle();
        ofFill();
        ofSetHexColor(colour1);

        ofDrawRectangle(getX()+getWidth()/4 - getWidth()/16,getY(),getWidth()/8, getHeight()/2);
        ofDrawRectangle(getX(),getY()+getHeight()/4 - getHeight()/16, getWidth()/2, getHeight()/8);

        if(bBorder) {
            ofNoFill();
            ofSetLineWidth(1);
            ofDrawRectangle(getX(),getY(),getWidth()/2,getHeight()/2);
        }

        ofPopStyle();
        break;
    }
    case ButtonType::PLAY_SCENE:
    {
        ofPushStyle();

        ofFill();
        ofSetHexColor(0x7b2829);

        if(bIsActive) {
            ofDrawRectangle(getX(),getY(),getWidth()/3, getHeight());
            ofDrawRectangle(getX()+getWidth()*2.0f/3.0f,getY(),getWidth()/3, getHeight());
        } else {
            ofDrawTriangle(getX(),getY(),getX(),getY()+getHeight(),getX()+getWidth(),getY()+getHeight()/2);
        }

        ofPopStyle();
        break;
    }
    case ButtonType::STOP_SCENE:
    {
        ofPushStyle();
        ofSetColor(250,160,120);
        ofFill();
        ofDrawRectangle(getX(),getY(),getWidth(), getHeight());
        ofPopStyle();
        break;
    }
    case ButtonType::DELETE_SCENE:
    {
        ofPushStyle();
        ofFill();
        ofSetHexColor(0x7b2800);
        ofDrawRectangle(getX(),getY()+getHeight()/2 - getHeight()/8, getWidth(), getHeight()/4);
        ofNoFill();
        ofSetLineWidth(1);
        ofDrawRectangle(getX(),getY(),getWidth(),getHeight());
        ofPopStyle();
        break;
    }
    default:
            return;

    };

}
