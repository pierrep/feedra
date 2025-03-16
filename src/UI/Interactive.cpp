#include "Interactive.h"

Interactive::Interactive()
{
    id = 0;
    bClicked = false;
    bEventsEnabled = false;

    enableEvents();
    ofAddListener(ofEvents().mousePressed, this, &Interactive::mousePressed);
    ofAddListener(ofEvents().mouseMoved, this, &Interactive::mouseMoved);
    ofAddListener(ofEvents().mouseDragged, this, &Interactive::mouseDragged);
    ofAddListener(ofEvents().mouseReleased, this, &Interactive::mouseReleased);
}

Interactive::~Interactive()
{
    ofLogVerbose() << "Interactive destructor called...";

    disableEvents();
    ofRemoveListener(ofEvents().mousePressed, this, &Interactive::mousePressed);
    ofRemoveListener(ofEvents().mouseMoved, this, &Interactive::mouseMoved);
    ofRemoveListener(ofEvents().mouseDragged, this, &Interactive::mouseDragged);
    ofRemoveListener(ofEvents().mouseReleased, this, &Interactive::mouseReleased);
}

void Interactive::enableEvents()
{
    //enable events
    if(!bEventsEnabled) {
        bEventsEnabled = true;
    }
}

void Interactive::disableEvents()
{
    if(bEventsEnabled) {
        bEventsEnabled = false;
    }
}

Interactive::Interactive(const Interactive &parent)
{
    ofLogVerbose() << "Interactive copy constructor called";

    bClicked = parent.bClicked;
    x = parent.x;
    y = parent.y;
    width = parent.width;
    height = parent.height;

    enableEvents();
}

void Interactive::mouseMoved(ofMouseEventArgs &args) {
    if(!bEventsEnabled) {
        return;
    }
//    int x = args.x;
//    int y = args.y;
//    int button = args.button;
}

void Interactive::mousePressed(ofMouseEventArgs &args) {
    if(!bEventsEnabled) {
        return;
    }

    if(inside(args.x, args.y)) {
        bClicked = true;
//        offsetx = x - args.x;
//        offsety = y - args.y;
        ClickArgs ca;
        ca.id = id;
        ca.mouseButton = args;
        ofNotifyEvent(clickedEvent, ca);
    }
}

void Interactive::mouseDragged(ofMouseEventArgs &args) {
    if(!bEventsEnabled) {
        return;
    }
    if(inside(args.x, args.y)) {
        ClickArgs ca;
        ca.id = id;
        ca.mouseButton = args;
        ofNotifyEvent(draggedEvent, ca);
    }
}

void Interactive::mouseReleased(ofMouseEventArgs &args) {
    if(!bEventsEnabled) return;
    if(inside(args.x, args.y)) {
        ClickArgs ca;
        ca.id = id;
        ca.mouseButton = args;
        ofNotifyEvent(releasedEvent, ca);
    }
    bClicked = false;
}
