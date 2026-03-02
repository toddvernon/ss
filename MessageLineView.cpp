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


//-------------------------------------------------------------------------------------------------
// MessageLineView::MessageLineView
//
// Constructor
//-------------------------------------------------------------------------------------------------
MessageLineView::MessageLineView(CxScreen *screen, int screenRow)
: _screen(screen)
, _screenRow(screenRow)
, _text("")
{
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

    // Output text
    printf("%s", _text.data());

    fflush(stdout);
}


//-------------------------------------------------------------------------------------------------
// MessageLineView::setText
//
// Set the text to display.
//-------------------------------------------------------------------------------------------------
void
MessageLineView::setText(CxString text)
{
    _text = text;
}


//-------------------------------------------------------------------------------------------------
// MessageLineView::getText
//
// Get current text.
//-------------------------------------------------------------------------------------------------
CxString
MessageLineView::getText(void)
{
    return _text;
}
