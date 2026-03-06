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
#include "SpreadsheetDefaults.h"


//-------------------------------------------------------------------------------------------------
// CommandLineView::CommandLineView
//
// Constructor
//-------------------------------------------------------------------------------------------------
CommandLineView::CommandLineView(CxScreen *screen, SpreadsheetDefaults *defaults, int screenRow)
: _screen(screen)
, _defaults(defaults)
, _screenRow(screenRow)
, _dimMode(1)
{
    // _text is default-constructed as empty CxUTFString
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

    // Apply colors based on mode: dim (browsing) or bright (editing)
    if (_dimMode) {
        _defaults->applyCommandLineDimColors(_screen);
    } else {
        _defaults->applyCommandLineEditColors(_screen);
    }

    // Clear the line
    CxScreen::clearScreenFromCursorToEndOfLine();

    // Output text (convert UTF string to bytes for output)
    CxString bytes = _text.toBytes();
    printf("%s", bytes.data());

    // Reset colors
    _defaults->resetColors(_screen);

    if (_dimMode) {
        _screen->hideCursor();
    } else {
        _screen->showCursor();
    }

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
    _text.fromCxString(text, 8);  // Convert to UTF with 8-space tabs
}


//-------------------------------------------------------------------------------------------------
// CommandLineView::getText
//
// Get current text (as bytes).
//-------------------------------------------------------------------------------------------------
CxString
CommandLineView::getText(void)
{
    return _text.toBytes();
}


//-------------------------------------------------------------------------------------------------
// CommandLineView::placeCursor
//
// Place cursor at end of text (using display width for proper UTF-8 positioning).
//-------------------------------------------------------------------------------------------------
void
CommandLineView::placeCursor(void)
{
    CxScreen::placeCursor(_screenRow, _text.displayWidth());
}


//-------------------------------------------------------------------------------------------------
// CommandLineView::setDimMode
//
// Set dim mode. 1 = dark gray (browsing), 0 = white (editing).
//-------------------------------------------------------------------------------------------------
void
CommandLineView::setDimMode(int dim)
{
    _dimMode = dim;
}


//-------------------------------------------------------------------------------------------------
// CommandLineView::placeCursorAt
//
// Place cursor at a specific column offset. Used to position cursor after typed text
// but before the hint display.
//-------------------------------------------------------------------------------------------------
void
CommandLineView::placeCursorAt(int col)
{
    CxScreen::placeCursor(_screenRow, col);
    fflush(stdout);
}
