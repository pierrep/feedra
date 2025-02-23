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

    int id;
    bool bClicked;
    //int offsetx, offsety;
    bool bEventsEnabled;

    struct ClickArgs
    {
        int id;
        ofMouseEventArgs mouseButton;
    };

    ofEvent<ClickArgs> clickedEvent;
    ofEvent<ClickArgs> releasedEvent;
    ofEvent<ClickArgs> draggedEvent;
};
