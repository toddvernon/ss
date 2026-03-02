//-------------------------------------------------------------------------------------------------
//
//  CommandLineView.h
//  ss (spreadsheet)
//
//  Copyright 2022-2025 Todd Vernon. All rights reserved.
//  Licensed under the Apache License, Version 2.0
//  See LICENSE file for details.
//
//  Command line view - displays prompts, messages, and command input.
//
//-------------------------------------------------------------------------------------------------

#include <stdio.h>

#include <cx/base/string.h>
#include <cx/base/utfstring.h>
#include <cx/screen/screen.h>

class SpreadsheetDefaults;  // forward declaration

#ifndef _CommandLineView_h_
#define _CommandLineView_h_


//-------------------------------------------------------------------------------------------------
//
// CommandLineView
//
// Simple command line display at the bottom of the screen. Shows the current cell position
// when idle, command input when in command mode, and messages.
//
//-------------------------------------------------------------------------------------------------

class CommandLineView {

  public:

    CommandLineView(CxScreen *screen, SpreadsheetDefaults *defaults, int screenRow);
    ~CommandLineView(void);

    void updateScreen(void);
    // redraw the command line

    void recalcForResize(int screenRow);
    // recalculate position after terminal resize

    void setText(CxString text);
    // set the text to display

    CxString getText(void);
    // get current text

    void placeCursor(void);
    // place cursor at end of text

    void placeCursorAt(int col);
    // place cursor at a specific column offset

  private:

    CxScreen *_screen;
    SpreadsheetDefaults *_defaults;
    int _screenRow;
    CxUTFString _text;
};


#endif
