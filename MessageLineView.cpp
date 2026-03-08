//-------------------------------------------------------------------------------------------------
//
//  MessageLineView.cpp
//  ss (spreadsheet)
//
//  Copyright 2022-2025 Todd Vernon. All rights reserved.
//  Licensed under the Apache License, Version 2.0
//  See LICENSE file for details.
//
//  Message line view implementation.
//
//-------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>

#include "MessageLineView.h"
#include "SpreadsheetDefaults.h"


//-------------------------------------------------------------------------------------------------
// MessageLineView::MessageLineView
//
// Constructor
//-------------------------------------------------------------------------------------------------
MessageLineView::MessageLineView(CxScreen *screen, SpreadsheetDefaults *defaults, int screenRow)
: _screen(screen)
, _defaults(defaults)
, _screenRow(screenRow)
{
    // _text is default-constructed as empty CxUTFString
}


//-------------------------------------------------------------------------------------------------
// MessageLineView::~MessageLineView
//
// Destructor
//-------------------------------------------------------------------------------------------------
MessageLineView::~MessageLineView(void)
{
}


//-------------------------------------------------------------------------------------------------
// MessageLineView::recalcForResize
//
// Recalculate position after terminal resize.
//-------------------------------------------------------------------------------------------------
void
MessageLineView::recalcForResize(int screenRow)
{
    _screenRow = screenRow;
}


//-------------------------------------------------------------------------------------------------
// MessageLineView::updateScreen
//
// Redraw the message line.
//-------------------------------------------------------------------------------------------------
void
MessageLineView::updateScreen(void)
{
    CxScreen::placeCursor(_screenRow, 0);

    // Clear the line
    CxScreen::clearScreenFromCursorToEndOfLine();

    // Apply message line color
    _screen->setForegroundColor(_defaults->messageLineTextColor());

    // Output text (convert UTF string to bytes for output)
    CxString bytes = _text.toBytes();
    printf("%s", bytes.data());

    // Reset color
    _screen->resetForegroundColor();
}


//-------------------------------------------------------------------------------------------------
// MessageLineView::setText
//
// Set the text to display.
//-------------------------------------------------------------------------------------------------
void
MessageLineView::setText(CxString text)
{
    _text.fromCxString(text, 8);  // Convert to UTF with 8-space tabs
}


//-------------------------------------------------------------------------------------------------
// MessageLineView::getText
//
// Get current text (as bytes).
//-------------------------------------------------------------------------------------------------
CxString
MessageLineView::getText(void)
{
    return _text.toBytes();
}
