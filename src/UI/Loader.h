#pragma once

#include "Interactive.h"


class Loader: public Interactive
{
public:
    Loader();
    ~Loader();
    Loader(const Loader& d);
    Loader(int _id, int _x, int _y, int _w, int _h);
    void setup();
    void render();
    void update();
    void onClicked(int& args);

    int id;
    bool doLoad;

};
