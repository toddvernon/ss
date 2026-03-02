//-------------------------------------------------------------------------------------------------
//
//  MessageLineView.h
//  ss (spreadsheet)
//
//  Copyright 2022-2025 Todd Vernon. All rights reserved.
//  Licensed under the Apache License, Version 2.0
//  See LICENSE file for details.
//
//  MessageLineView - displays command result messages at the bottom of the screen.
//
//-------------------------------------------------------------------------------------------------

#include <stdio.h>

#include <cx/base/string.h>
#include <cx/base/utfstring.h>
#include <cx/screen/screen.h>

#ifndef _MessageLineView_h_
#define _MessageLineView_h_


//-------------------------------------------------------------------------------------------------
//
// MessageLineView
//
// Displays command result messages on the final line of the terminal.
//
//-------------------------------------------------------------------------------------------------

class MessageLineView {

  public:

    MessageLineView(CxScreen *screen, int screenRow);
    ~MessageLineView(void);

    void updateScreen(void);
    // redraw the message line

    void recalcForResize(int screenRow);
    // recalculate position after terminal resize

    void setText(CxString text);
    CxString getText(void);

  private:

    CxScreen *_screen;
    int _screenRow;
    CxUTFString _text;
};


#endif
