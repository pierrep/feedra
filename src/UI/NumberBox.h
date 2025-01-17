#pragma once

#include "Interactive.h"
#include "TextInputField.h"
#include "Button.h"
#include "AppConfig.h"

class NumberBox: public Interactive
{
public:
    NumberBox();
    ~NumberBox();
    NumberBox(const NumberBox& d);
    NumberBox(int _id, int _x, int _y, int _w, int _h);
    void setup(AppConfig* config, int _id, int x, int y);
    void render();
    void onClicked(ClickArgs& args);
    void onTextChanged(string& args);
    void incrementValue();
    void decrementValue();
    int getValue();

protected:
    int id;
    AppConfig* config;
    TextInputField textBox;
    Button incButton;
    Button decButton;

    float clickTime;
    const int clickDelay = 500;

};
