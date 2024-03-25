#include "Interactive.h"

Interactive::Interactive()
{
    clicked = false;
    bEventsEnabled = false;

    enableEvents();
}

Interactive::~Interactive()
{
    ofLogVerbose() << "Interactive destructor called...";

    disableEvents();
}

void Interactive::enableEvents()
{
    //enable events
    if(!bEventsEnabled) {
        ofAddListener(ofEvents().mousePressed, this, &Interactive::mousePressed);
        ofAddListener(ofEvents().mouseMoved, this, &Interactive::mouseMoved);
        ofAddListener(ofEvents().mouseDragged, this, &Interactive::mouseDragged);
        ofAddListener(ofEvents().mouseReleased, this, &Interactive::mouseReleased);
        bEventsEnabled = true;
    }
}

void Interactive::disableEvents()
{
    if(bEventsEnabled) {
        ofRemoveListener(ofEvents().mousePressed, this, &Interactive::mousePressed);
        ofRemoveListener(ofEvents().mouseMoved, this, &Interactive::mouseMoved);
        ofRemoveListener(ofEvents().mouseDragged, this, &Interactive::mouseDragged);
        ofRemoveListener(ofEvents().mouseReleased, this, &Interactive::mouseReleased);
        bEventsEnabled = false;
    }
}

Interactive::Interactive(const Interactive &parent)
{
    ofLogVerbose() << "Interactive copy constructor called";

    clicked = parent.clicked;
    x = parent.x;
    y = parent.y;
    width = parent.width;
    height = parent.height;

    enableEvents();
}

void Interactive::mouseMoved(ofMouseEventArgs &args) {
//    int x = args.x;
//    int y = args.y;
//    int button = args.button;
}

void Interactive::mousePressed(ofMouseEventArgs &args) {
    if(inside(args.x, args.y)) {
        clicked = true;
        offsetx = x - args.x;
        offsety = y - args.y;
        ofNotifyEvent(clickedEvent, args.button);
    }
}

void Interactive::mouseDragged(ofMouseEventArgs &args) {
    if(clicked) {
        //setX(args.x+offsetx);
        //setY(args.y+offsety);
    }
}

void Interactive::mouseReleased(ofMouseEventArgs &args) {
    clicked = false;
}
