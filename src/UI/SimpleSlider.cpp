/*
 *  SimpleSlider.cpp
 *  Created by Golan Levin on 2/24/12.
 *
 */

#include "SimpleSlider.h"

//----------------------------------------------------
SimpleSlider::SimpleSlider(){
    bEventsEnabled = false;
    labelFont = nullptr;
}

//----------------------------------------------------
SimpleSlider::~SimpleSlider(){
	clear();
}

//----------------------------------------------------
void SimpleSlider::setScale(float _x, float _y){
    x_scale = _x;
    y_scale = _y;
}

//-----------------------------------------------------------------------------------------------------------------------
void SimpleSlider::setup(int _id,float _x, float _y, float _w, float _h, float loVal, float hiVal, float initialVal, bool bVert, bool bDrawNum){
    id = _id;
    x = _x;
    y = _y;
    width = _w;
    height = _h;
    box.set(x,y, width, height);
	numberDisplayPrecision = 2;
	
	bVertical = bVert;
	bDrawNumber = bDrawNum;
	bHasFocus = false;

	lowValue = loVal;
	highValue = hiVal;
    percent = ofMap(initialVal, lowValue, highValue, 0.0, 1.0f);
    percent = ofClamp(percent, 0.0f, 1.0f);
	
    labelString = "";
    bDrawLabel = false;
	
    if(!bEventsEnabled){
        enableEvents();
	}
}

void SimpleSlider::enableEvents()
{
    //ofLogNotice() << " Enable slider events, id = " << id;
    //ofAddListener(ofEvents().draw, this, &SimpleSlider::draw);
    ofAddListener(ofEvents().mouseMoved, this, &SimpleSlider::mouseMoved);
    ofAddListener(ofEvents().mousePressed, this, &SimpleSlider::mousePressed);
    ofAddListener(ofEvents().mouseReleased, this, &SimpleSlider::mouseReleased);
    ofAddListener(ofEvents().mouseDragged, this, &SimpleSlider::mouseDragged);
    bEventsEnabled = true;
}

void SimpleSlider::disableEvents()
{
    //ofLogNotice() << " Disable slider events, id = " << id;
    //ofRemoveListener(ofEvents().draw, this, &SimpleSlider::draw);
    ofRemoveListener(ofEvents().mouseMoved, this, &SimpleSlider::mouseMoved);
    ofRemoveListener(ofEvents().mousePressed, this, &SimpleSlider::mousePressed);
    ofRemoveListener(ofEvents().mouseReleased, this, &SimpleSlider::mouseReleased);
    ofRemoveListener(ofEvents().mouseDragged, this, &SimpleSlider::mouseDragged);
    bEventsEnabled = false;
}

//----------------------------------------------------
void SimpleSlider::clear(){
    if(bEventsEnabled){
        disableEvents();
	}
}

//----------------------------------------------------
void SimpleSlider::setFont(ofTrueTypeFont* _font){
    labelFont = _font;
    bDrawLabel = true;
}

//----------------------------------------------------
void SimpleSlider::setLabelString(string str){
	labelString = str;
    bDrawLabel = true;
}

//----------------------------------------------------
void SimpleSlider::render(){
    ofPushStyle();
	ofEnableAlphaBlending();
	ofDisableSmoothing();
	ofPushMatrix();
	ofTranslate(x,y,0);
	
    // Use different alphas if we're actively manipulating me.
	float sliderAlpha = (bHasFocus) ? 128:64;
	float spineAlpha  = (bHasFocus) ? 192:128;
	float thumbAlpha  = (bHasFocus) ? 255:160;
	
	// draw box outline
    ofFill();
	ofSetLineWidth(1.0);
	ofSetColor(64,64,64, sliderAlpha); 
	ofDrawRectangle(0,0, width,height); 
	
	// draw spine
	ofSetLineWidth(1.0);
	ofSetColor(0,0,0, spineAlpha); 
	if (bVertical){
		ofDrawLine(width/2,0, width/2,height); 
	} else {
		ofDrawLine(0,height/2, width,height/2); 
	}
	
	// draw thumb
	ofSetLineWidth(5.0);
    //ofSetHexColor(0xd08331);
    ofSetColor(0,0,0, thumbAlpha);
	if (bVertical){
		float thumbY = ofMap(percent, 0,1, height,0, true);
		ofDrawLine(0,thumbY, width,thumbY); 
	} else {
		float thumbX = ofMap(percent, 0,1, 0,width, true);
		ofDrawLine(thumbX,0, thumbX,height); 
	}
	
	// draw numeric value 
	if (bHasFocus){
		ofSetColor(0); 
	} else {
		ofSetColor(128); 
	}

    ofDisableAlphaBlending();

    ofSetColor(0);

    if(labelFont != nullptr) {
        if (bVertical){
            labelFont->drawString( ofToString(getValue(),numberDisplayPrecision), width+5*x_scale,height);
        } else {
            labelFont->drawString( ofToString(getValue(),numberDisplayPrecision), width+5*x_scale,height/2 + 4*y_scale);
        }
    } else {
        if (bVertical){
            ofDrawBitmapString( ofToString(getValue(),numberDisplayPrecision), width+5*x_scale,height);
        } else {
            ofDrawBitmapString( ofToString(getValue(),numberDisplayPrecision), width+5*x_scale,height/2 + 4*y_scale);
        }
    }

    if(bDrawLabel && !bVertical)
    {
        float labelStringHeight = 4*y_scale;
        if(labelFont != nullptr) {
            labelFont->drawString( labelString, 0, -labelStringHeight);
        } else {
            ofDrawBitmapString( labelString, 0, -labelStringHeight);
        }
    }
	
	ofPopMatrix();
    ofPopStyle();
}

//----------------------------------------------------
float SimpleSlider::getValue(){
	// THIS IS THE MAIN WAY YOU GET THE VALUE FROM THE SLIDER!
	float out = ofMap(percent, 0,1, lowValue,highValue, true); 
	return out;
}

//----------------------------------------------------
// Probably not used very much. 
float SimpleSlider::getLowValue(){
	return lowValue;
}
float SimpleSlider::getHighValue(){
	return highValue;
}
float SimpleSlider::getPercent(){
	return percent;
}

//----------------------------------------------------
// Probably not used very much. 
void SimpleSlider::setLowValue(float lv){
	lowValue = lv;
}
void SimpleSlider::setHighValue(float hv){
	highValue = hv; 
}
void SimpleSlider::setPercent (float p){
	// Set the slider's percentage from the outside. 
	p = ofClamp(p, 0,1);
	percent	= p;
}
void SimpleSlider::setNumberDisplayPrecision(int prec){
	numberDisplayPrecision = prec;
}
		
//----------------------------------------------------
void SimpleSlider::mouseMoved(ofMouseEventArgs& event){
	bHasFocus = false;
}
void SimpleSlider::mouseDragged(ofMouseEventArgs& event){
	if (bHasFocus){
		updatePercentFromMouse (event.x, event.y); 
        SliderData d;
        d.id = id;
        d.value = getValue();
        ofNotifyEvent(clickedEvent, d);
	}
}
void SimpleSlider::mousePressed(ofMouseEventArgs& event){
	bHasFocus = false;
	if (box.inside(event.x, event.y)){
		bHasFocus = true;
		updatePercentFromMouse (event.x, event.y); 
        SliderData d;
        d.id = id;
        d.value = getValue();
        ofNotifyEvent(clickedEvent, d);
	}
}
void SimpleSlider::mouseReleased(ofMouseEventArgs& event){
	if (bHasFocus){
		if (box.inside(event.x, event.y)){
			updatePercentFromMouse (event.x, event.y); 
		}
	}
	bHasFocus = false;
}

//----------------------------------------------------
void SimpleSlider::updatePercentFromMouse (int mx, int my){
	// Given the mouse value, compute the percentage.
	if (bVertical){
		percent = ofMap(my, y, y+height, 1,0, true);
	} else {
		percent = ofMap(mx, x, x+width,  0,1, true);
	}
}
		

