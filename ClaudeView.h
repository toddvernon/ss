//-------------------------------------------------------------------------------------------------
//
//  ClaudeView.h
//  ss (spreadsheet)
//
//  Copyright 2022-2025 Todd Vernon. All rights reserved.
//  Licensed under the Apache License, Version 2.0
//  See LICENSE file for details.
//
//  ClaudeView - displays the Claude AI chat area at the bottom of the screen.
//  6-line scrolling chat display + input line, with a divider line above.
//
//-------------------------------------------------------------------------------------------------

#include <stdio.h>

#include <cx/base/string.h>
#include <cx/base/utfstring.h>
#include <cx/base/slist.h>
#include <cx/screen/screen.h>

class SpreadsheetDefaults;  // forward declaration

#ifndef _ClaudeView_h_
#define _ClaudeView_h_


//-------------------------------------------------------------------------------------------------
//
// ClaudeView
//
// Displays Claude AI chat area: divider line, scrolling chat lines, and input line.
// Total height = 25% of terminal height (minimum 8 rows).
//
//-------------------------------------------------------------------------------------------------

class ClaudeView {

  public:

    ClaudeView(CxScreen *screen, SpreadsheetDefaults *defaults, int startRow, int endRow);
    ~ClaudeView(void);

    void updateScreen(void);
    // redraw divider + chat lines + input line

    void recalcForResize(int startRow, int endRow);
    // recalculate position after terminal resize

    void placeCursor(void);
    // place terminal cursor at input line

    int getHeight(void);
    // returns total height (25% of terminal, minimum 8)

    void setDisplayLines(CxSList<CxString*> *lines, int count);
    // set the display buffer (borrowed from ClaudeHandler)

    void setInputText(CxString text);
    // set the input line text

    void setInputCursorPos(int pos);
    // set cursor position within input line

    void scrollUp(void);
    // scroll chat area up

    void scrollDown(void);
    // scroll chat area down

    void scrollToBottom(void);
    // scroll to show most recent content

  private:

    void drawDivider(void);
    // draw the "─── Claude ───" divider line

    void drawChatLines(void);
    // draw the 6 scrolling chat lines

    void drawInputLine(void);
    // draw the "claude> " input line

    CxScreen *_screen;
    SpreadsheetDefaults *_defaults;
    int _startRow;          // first screen row (divider line)
    int _endRow;            // last screen row (input line)

    CxSList<CxString*> *_displayLines;  // borrowed from ClaudeHandler
    int _displayLineCount;

    int _scrollOffset;      // scroll position in display buffer

    CxString _inputText;    // current input text
    int _inputCursorPos;    // cursor position in input
};


#endif
