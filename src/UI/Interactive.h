#pragma once

#include "ofMain.h"

class Interactive : public ofRectangle {
public:
    Interactive();
    virtual ~Interactive();

    Interactive(const Interactive &p);
    void mouseMoved(ofMouseEventArgs &args);
    void mousePressed(ofMouseEventArgs &args);
    void mouseDragged(ofMouseEventArgs &args);
    void mouseReleased(ofMouseEventArgs &args);
    void enableEvents();
    void disableEvents();

    bool clicked;
    int offsetx, offsety;
    bool bEventsEnabled;

    ofEvent<int> clickedEvent;
};
