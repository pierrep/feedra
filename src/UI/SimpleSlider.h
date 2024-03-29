/*
 *  SimpleSlider.h
 *  Created by Golan Levin on 2/24/12.
 *
 */


#pragma once

#include "ofMain.h"

struct SliderData {
    float value;
    int id;
};

class SimpleSlider {

	public:
			
		SimpleSlider();
		~SimpleSlider();

        void	setup (int id, float x, float y, float w, float h, float loVal, float hiVal, float initialPercent, bool bVert, bool bDrawNum);
		void	clear();

        void	render();
		void	mouseMoved(ofMouseEventArgs& event);
		void	mouseDragged(ofMouseEventArgs& event);
		void	mousePressed(ofMouseEventArgs& event);
		void	mouseReleased(ofMouseEventArgs& event);
	
		float	getValue();
		float	getLowValue();
		float	getHighValue();
		float	getPercent();
		
		void	setLowValue(float lv);
		void	setHighValue(float hv);
		void	setPercent(float p);
		void	setNumberDisplayPrecision(int prec);
		void	setLabelString (string str);
        void	setFont (ofTrueTypeFont* font);
		void	updatePercentFromMouse(int mx, int my); 
        void    setScale(float x, float y);
        void    showLabel(bool bShow) {bDrawLabel = bShow;}

        void    disableEvents();
        void    enableEvents();

        ofEvent<SliderData> clickedEvent;
	
	protected:
		
        int     id;
		float	x;
		float	y; 
		float	width; 
		float	height;
		ofRectangle box; 
		int		numberDisplayPrecision;
	
		bool	bVertical;
		bool	bDrawNumber;
		bool	bHasFocus; 
		
		float	lowValue;
		float	highValue;
		float	percent;
	
		string	labelString; 
        ofTrueTypeFont* labelFont;

        float   x_scale;
        float   y_scale;
	
        bool    bDrawLabel;
        bool	bEventsEnabled;
};
