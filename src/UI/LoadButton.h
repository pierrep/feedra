#pragma once

#include "Interactive.h"


class LoadButton: public Interactive
{
public:
    LoadButton();
    ~LoadButton();
    LoadButton(const LoadButton& d);
    LoadButton(int _id, int _x, int _y, int _w, int _h);
    void setup();
    void render();
    void update();
    void onClicked(int& args);

    int id;
    bool doLoad;

};
