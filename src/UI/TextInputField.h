//
//  textInput.h
//  textInput
//
//  Created by Elliot Woods on 09/12/2011.
//  modified by James George 12/2/2011
//  modified by Momo the Monster 7/10/2012
//  swappable fonts added by James George 9/11/2012
//	refactoring and modifications by Elliot Woods on 30/11/2014
//
//	MIT license
//	http://www.opensource.org/licenses/mit-license.php
//

// jg : TODO: text wrapping

#pragma once

#include "ofMain.h"

//For lack of a type abstraction, this lets you #define a font renderer so
//(like ofxFTGL or ofxFont)
//to use ofxFTGL use somethinglike this:
//#define TEXTFIELD_FONT_RENDERER ofxFTGLFont
//#define TEXTFIELD_FONT_INCLUDE "ofxFTGLFont.h"

#ifndef TEXTFIELD_FONT_RENDERER
#define TEXTFIELD_FONT_RENDERER ofTrueTypeFont
#endif 

#ifdef TEXTFIELD_FONT_INCLUDE
#include TEXTFIELD_FONT_INCLUDE
#endif

#define TEXTFIELD_IS_ACTIVE "textfieldIsActive"
#define TEXTFIELD_IS_INACTIVE "textfieldIsInactive"

#include "TextInputFieldFontRenderer.h"

class TextInputField {
  public:
	TextInputField();
	virtual ~TextInputField();

	/// Always call this first
    void setup(bool enableListeners = true);

	/// Change the font used to draw the text
    void setFont(TEXTFIELD_FONT_RENDERER & font);
    TextInput::FontRenderer * getFontRenderer();

	void enable();
	void disable();
    bool isEnabled() const;
	
	/// Whether the text box is focused and capturing keys
	void beginEditing();
	void endEditing();
	bool isEditing() const;

	void setUseListeners(bool);
	bool getUseListeners() const;

    enum TextAlignment {LEFT,RIGHT,CENTRE};

    void setTextAlignment(TextAlignment align);
    void setNumeric(bool val) {bUseNumeric = val;}
    void allowNegative(bool val) {bAllowNegatives = val;};

	/// Draw inside this->bounds
	void draw();

	/// Clear text
	void clear();

    //can be set manually or otherwise is controlled by enable/disable
    bool drawCursor;
	
	string text;

	ofRectangle bounds;
	int cursorPosition;
	int selectionBegin;
	int selectionEnd;
	bool selecting;
	
	ofEvent<string> onTextChange;
	ofEvent<string> onHitReturn;

	void keyPressed(ofKeyEventArgs &a);
    void keyReleased(ofKeyEventArgs &a);
	void mousePressed(ofMouseEventArgs& args);
	void mouseDragged(ofMouseEventArgs& args);
	void mouseReleased(ofMouseEventArgs& args);

	bool autoClear;
	bool autoTab;
	
	bool multiline;
    
	float getVerticalPadding() const;
	float getHorizontalPadding() const;
    
    void setVerticalPadding(float vp);
    void setHorizontalPadding(float hp);
    
	#if defined(USE_GLFW_CLIPBOARD) && defined(TARGET_OSX)
    void setClipboard(string clippy);
    string getClipboard();
	#endif

    void setStringLimit(size_t max) {
        string_limit = max;
    }
    void setDoubleClick(bool val) {bUseDoubleClick = val;}
	
  protected:
	float lastTimeCursorMoved;
	
	float verticalPadding;
    float horizontalPadding;
    TextAlignment textAlignment;

	TextInput::FontRenderer* fontRef;
	
    bool enabled;
	bool editing;
    bool useListeners;
    bool bUseNumeric;

	bool mouseDownInRect;
	
	void notifyTextChange();
	void notifyHitReturn();
	void getCursorCoords(int pos, int &cursorX, int &cursorY);
	int getCursorPositionFromMouse(int x, int y);
    
	void addListeners();
	void removeListeners();
	bool hasListeners;

    bool bUseDoubleClick;
    float prevClick;
    float currentClick;
    float doubleClickTime;

    bool shiftHeld, commandHeld;
    map<int, char> shiftMap;

    size_t string_limit;
    bool bAllowNegatives;
};

