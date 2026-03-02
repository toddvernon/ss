//-------------------------------------------------------------------------------------------------
//
//  CommandLineView.cpp
//  ss (spreadsheet)
//
//  Copyright 2022-2025 Todd Vernon. All rights reserved.
//  Licensed under the Apache License, Version 2.0
//  See LICENSE file for details.
//
//  Command line view implementation.
//
//-------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>

#include "CommandLineView.h"


//-------------------------------------------------------------------------------------------------
// CommandLineView::CommandLineView
//
// Constructor
//-------------------------------------------------------------------------------------------------
CommandLineView::CommandLineView(CxScreen *screen, int screenRow)
: _screen(screen)
, _screenRow(screenRow)
, _text("")
{
}


//-------------------------------------------------------------------------------------------------
// CommandLineView::~CommandLineView
//
// Destructor
//-------------------------------------------------------------------------------------------------
CommandLineView::~CommandLineView(void)
{
}


//-------------------------------------------------------------------------------------------------
// CommandLineView::recalcForResize
//
// Recalculate position after terminal resize.
//-------------------------------------------------------------------------------------------------
void
CommandLineView::recalcForResize(int screenRow)
{
    _screenRow = screenRow;
}


//-------------------------------------------------------------------------------------------------
// CommandLineView::updateScreen
//
// Redraw the command line.
//-------------------------------------------------------------------------------------------------
void
CommandLineView::updateScreen(void)
{
    CxScreen::placeCursor(_screenRow, 0);

    // Clear the line
    CxScreen::clearScreenFromCursorToEndOfLine();

    // Output text
    printf("%s", _text.data());

    fflush(stdout);
}


//-------------------------------------------------------------------------------------------------
// CommandLineView::setText
//
// Set the text to display.
//-------------------------------------------------------------------------------------------------
void
CommandLineView::setText(CxString text)
{
    _text = text;
}


//-------------------------------------------------------------------------------------------------
// CommandLineView::getText
//
// Get current text.
//-------------------------------------------------------------------------------------------------
CxString
CommandLineView::getText(void)
{
    return _text;
}


//-------------------------------------------------------------------------------------------------
// CommandLineView::placeCursor
//
// Place cursor at end of text.
//-------------------------------------------------------------------------------------------------
void
CommandLineView::placeCursor(void)
{
    CxScreen::placeCursor(_screenRow, _text.length());
}
