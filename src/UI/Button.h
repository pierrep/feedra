#pragma once

#include "Interactive.h"


class Button: public Interactive
{
public:
    Button();
    ~Button();
    Button(const Button& d);
    Button(int _id, int _x, int _y, int _w, int _h);
    void setup(std::function<void()>& render);
    void render();
    void onClicked(ClickArgs& args);

    bool bActivate;
    bool bIsActive;

    std::function<void()> renderFunc;
};
