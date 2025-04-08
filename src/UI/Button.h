#pragma once

#include "Interactive.h"
#include "AppConfig.h"

enum ButtonType {
    NONE = 0,
    MINUS,
    PLUS,
    ADD,
    NEW_SCENE,
    PLAY_SCENE,
    STOP_SCENE,
    DELETE_SCENE,
    MAX_BUTTONS
};

class Button: public Interactive
{
public:
    Button();
    ~Button();
    Button(const Button& d);
    Button(AppConfig* config, int _id, int _x, int _y, int _w, int _h, ButtonType _type);
    void setup(AppConfig* config, int _id, int _x, int _y, int _w, int _h, ButtonType _type);
    void setConfig(AppConfig* _config) {config = _config;}
    void setBorder(bool value) {bBorder = value;}
    void setPrimaryColour(int hexColour) {colour1 = hexColour;}
    void setSecondaryColour(int hexColour) {colour2 = hexColour;}
    void setName(string _name) {name = _name;}
    string getName() const {return name;}
    void draw();
    void onClicked(ClickArgs& args);

    bool bActivate;
    bool bIsActive;
    ButtonType buttonType;

 protected:
    AppConfig* config;
    bool bBorder;
    int colour1;
    int colour2;
    string name;

};
