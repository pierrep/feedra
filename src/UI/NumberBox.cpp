#include "NumberBox.h"

NumberBox::NumberBox()
{
    id = 0;
    labelFont = nullptr;
}

//--------------------------------------------------------------
NumberBox::~NumberBox()
{
    ofLogVerbose() << "Stop destructor called...";
    ofRemoveListener(decButton.clickedEvent, this, &NumberBox::onClicked);
    ofRemoveListener(incButton.clickedEvent, this, &NumberBox::onClicked);
}

//--------------------------------------------------------------
void NumberBox::setup(AppConfig* _config, int _id, int _x, int _y)
{    
    config = _config;
    id = _id;
    setX(_x);
    setY(_y);
    float tfw = 80; // text field width
    float tfh = 30; // text field height
    textBox.setup();
    textBox.text = "0";
    textBox.setFont(config->f1());
    textBox.bounds = ofRectangle(_x* config->x_scale, _y* config->y_scale, tfw*config->x_scale, tfh*config->y_scale);
    textBox.setUseListeners(true);
    textBox.setStringLimit(6);
    textBox.setTextAlignment(TextInputField::TextAlignment::CENTRE);
    textBox.enable();
    textBox.setNumeric(true);
    textBox.allowNegative(false);

    labelString = "";
    bDrawLabel = false;

    ofAddListener(textBox.onTextChange, this, &NumberBox::onTextChanged);

    ofAddListener(decButton.clickedEvent, this, &NumberBox::onClicked);
    decButton.id = 1;
    decButton.setConfig(config);
    decButton.buttonType = ButtonType::MINUS;
    decButton.width = 30*config->x_scale;
    decButton.height = 30*config->y_scale;
    decButton.x = _x* config->x_scale - decButton.width - 5*config->x_scale;
    decButton.y = _y* config->y_scale;

    ofAddListener(incButton.clickedEvent, this, &NumberBox::onClicked);
    incButton.id = 2;
    incButton.setConfig(config);
    incButton.buttonType = ButtonType::PLUS;
    incButton.width = 30*config->x_scale;
    incButton.height = 30*config->y_scale;
    incButton.x = _x* config->x_scale + textBox.bounds.getWidth() + 5*config->x_scale;
    incButton.y = _y* config->y_scale;
}

//--------------------------------------------------------------
NumberBox::NumberBox(int _id, int _x, int _y, int _w, int _h)
{
    id = _id;
    labelFont = nullptr;
    bDrawLabel = false;

    setX(_x);
    setY(_y);
    setWidth(_w);
    setHeight(_h);
}

//--------------------------------------------------------------
NumberBox::NumberBox(const NumberBox& parent) {
    ofLogVerbose() << "NumberBox copy constructor called";

    id = parent.id;
    labelFont = parent.labelFont;
    bDrawLabel = parent.bDrawLabel;

    setX(parent.x);
    setY(parent.y);
    setWidth(parent.width);
    setHeight(parent.height);
}

//----------------------------------------------------
void NumberBox::setFont(ofTrueTypeFont* _font){
    labelFont = _font;
    bDrawLabel = true;
}

//----------------------------------------------------
void NumberBox::setLabelString(string str){
    labelString = str;
    bDrawLabel = true;
}

//--------------------------------------------------------------
void NumberBox::onTextChanged(string& args) {
    NumberBoxData d;
    d.id = id;
    d.value = getValue();
    ofNotifyEvent(numberChangedEvent, d);
}

//--------------------------------------------------------------
void NumberBox::onClicked(ClickArgs& args) {
    //ofLogNotice() << "NumberBox id: " << args.id << " clicked";
    clickTime = ofGetElapsedTimeMillis();

    if((args.id == 1) && (args.mouseButton.type == ofMouseEventArgs::Pressed))
    {
        decrementValue();        
    }
    if((args.id == 2) && (args.mouseButton.type == ofMouseEventArgs::Pressed))
    {
        incrementValue();
    }
}

void NumberBox::decrementValue()
{
    int val;
    std::size_t num;
    try {
        val = stoi(textBox.text,&num);
    } catch (...) {
        val = 0;
    }
    // check if numerical result same length as input string
    if(num != textBox.text.length())
    {
        val = 0;
    } else {
        if((val == 0) || (val == 1)) {
            val = 0;
        } else {
            val = val -1;
        }
    }
    textBox.text = std::to_string(val);

    NumberBoxData d;
    d.id = id;
    d.value = getValue();
    ofNotifyEvent(numberChangedEvent, d);
}

void NumberBox::incrementValue()
{
    int val;
    std::size_t num;
    try {
        val = stoi(textBox.text,&num);
    } catch (...) {
        val = 0;
    }
    // check if numerical result same length as input string
    if(num != textBox.text.length())
    {
        val = 0;
    } else {
        val = val + 1;
    }
    textBox.text = std::to_string(val);

    NumberBoxData d;
    d.id = id;
    d.value = getValue();
    ofNotifyEvent(numberChangedEvent, d);
}

//--------------------------------------------------------------
int NumberBox::getValue()
{
    int val;
    std::size_t num;
    try {
        val = stoi(textBox.text,&num);
    } catch (...) {
        val = 0;
    }

    return val;
}

//--------------------------------------------------------------
void NumberBox::setValue(int val)
{
    textBox.text = std::to_string(val);
}

//--------------------------------------------------------------
void NumberBox::render()
{
    if(incButton.bClicked)
    {
        if(ofGetElapsedTimeMillis() - clickTime > clickDelay) {
            incrementValue();
        }
    } else
    if(decButton.bClicked)
    {
        if(ofGetElapsedTimeMillis() - clickTime > clickDelay) {
            decrementValue();
        }
    }

    decButton.draw();

    ofPushStyle();

    if(bDrawLabel)
    {
        float labelStringHeight = 4*config->y_scale;
        if(labelFont != nullptr) {
            ofSetColor(255);
            labelFont->drawString( labelString, getX(), getY()-labelStringHeight);
        } else {
            ofDrawBitmapString( labelString, getX(), getY()-labelStringHeight);
        }
    }

    if(textBox.isEditing()) {
        ofSetHexColor(0x81bbe1);
    } else {
        ofSetColor(255);
    }

    //Text box fill
    ofSetHexColor(0xfbe9d8);
    ofFill();
    ofDrawRectRounded(textBox.bounds,5*config->x_scale);

    //Text box Border
    ofSetLineWidth(2);
    ofNoFill();
    ofDrawRectRounded(textBox.bounds,5*config->x_scale);
    ofPopStyle();

    ofPushStyle();
    ofSetColor(0);
    textBox.draw();
    ofPopStyle();

    incButton.draw();
}
