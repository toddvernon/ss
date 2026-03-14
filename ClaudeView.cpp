//-------------------------------------------------------------------------------------------------
//
//  ClaudeView.cpp
//  ss (spreadsheet)
//
//  Copyright 2022-2025 Todd Vernon. All rights reserved.
//  Licensed under the Apache License, Version 2.0
//  See LICENSE file for details.
//
//  ClaudeView implementation - Claude AI chat display area.
//
//-------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>

#include "ClaudeView.h"
#include "SpreadsheetDefaults.h"


//-------------------------------------------------------------------------------------------------
// ClaudeView::ClaudeView
//-------------------------------------------------------------------------------------------------
ClaudeView::ClaudeView(CxScreen *screen, SpreadsheetDefaults *defaults,
                       int startRow, int endRow)
: _screen(screen)
, _defaults(defaults)
, _startRow(startRow)
, _endRow(endRow)
, _displayLines(NULL)
, _displayLineCount(0)
, _scrollOffset(0)
, _inputCursorPos(0)
{
}


//-------------------------------------------------------------------------------------------------
// ClaudeView::~ClaudeView
//-------------------------------------------------------------------------------------------------
ClaudeView::~ClaudeView(void)
{
}


//-------------------------------------------------------------------------------------------------
// ClaudeView::getHeight
//
// Returns total height: 25% of terminal height (minimum 8, divider + chat + input).
//-------------------------------------------------------------------------------------------------
int
ClaudeView::getHeight(void)
{
    int totalRows = _screen->rows();
    int height = totalRows / 4;
    if (height < 8) height = 8;
    return height;
}


//-------------------------------------------------------------------------------------------------
// ClaudeView::recalcForResize
//-------------------------------------------------------------------------------------------------
void
ClaudeView::recalcForResize(int startRow, int endRow)
{
    _startRow = startRow;
    _endRow = endRow;
}


//-------------------------------------------------------------------------------------------------
// ClaudeView::setDisplayLines
//-------------------------------------------------------------------------------------------------
void
ClaudeView::setDisplayLines(CxSList<CxString*> *lines, int count)
{
    _displayLines = lines;
    _displayLineCount = count;
}


//-------------------------------------------------------------------------------------------------
// ClaudeView::setInputText
//-------------------------------------------------------------------------------------------------
void
ClaudeView::setInputText(CxString text)
{
    _inputText = text;
}


//-------------------------------------------------------------------------------------------------
// ClaudeView::setInputCursorPos
//-------------------------------------------------------------------------------------------------
void
ClaudeView::setInputCursorPos(int pos)
{
    _inputCursorPos = pos;
}


//-------------------------------------------------------------------------------------------------
// ClaudeView::scrollUp
//-------------------------------------------------------------------------------------------------
void
ClaudeView::scrollUp(void)
{
    if (_scrollOffset > 0) {
        _scrollOffset--;
    }
}


//-------------------------------------------------------------------------------------------------
// ClaudeView::scrollDown
//-------------------------------------------------------------------------------------------------
void
ClaudeView::scrollDown(void)
{
    // 6 visible chat lines
    int chatLines = (_endRow - _startRow) - 1;  // subtract divider and input
    if (_displayLineCount > chatLines) {
        int maxOffset = _displayLineCount - chatLines;
        if (_scrollOffset < maxOffset) {
            _scrollOffset++;
        }
    }
}


//-------------------------------------------------------------------------------------------------
// ClaudeView::scrollToBottom
//-------------------------------------------------------------------------------------------------
void
ClaudeView::scrollToBottom(void)
{
    int chatLines = (_endRow - _startRow) - 1;
    if (_displayLineCount > chatLines) {
        _scrollOffset = _displayLineCount - chatLines;
    } else {
        _scrollOffset = 0;
    }
}


//-------------------------------------------------------------------------------------------------
// ClaudeView::updateScreen
//
// Draw the complete Claude view: divider, chat lines, input line.
//-------------------------------------------------------------------------------------------------
void
ClaudeView::updateScreen(void)
{
    drawDivider();
    drawChatLines();
    drawInputLine();
}


//-------------------------------------------------------------------------------------------------
// ClaudeView::drawDivider
//
// Draw "─── Claude ───────..." divider line across the full width.
//-------------------------------------------------------------------------------------------------
void
ClaudeView::drawDivider(void)
{
    int cols = _screen->cols();
    CxScreen::placeCursor(_startRow, 0);

    // Use divider color
    _screen->setForegroundColor(_defaults->dividerColor());

    // Build divider: "─── Claude " then fill with ─
    printf("\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80 Claude ");
    int labelDisplayWidth = 11;  // 3 box chars + space + "Claude" + space

    // Fill rest with ─
    for (int i = labelDisplayWidth; i < cols; i++) {
        printf("\xe2\x94\x80");
    }

    _defaults->resetColors(_screen);
}


//-------------------------------------------------------------------------------------------------
// ClaudeView::drawChatLines
//
// Draw the 6 scrolling chat lines from the display buffer.
//-------------------------------------------------------------------------------------------------
void
ClaudeView::drawChatLines(void)
{
    int cols = _screen->cols();
    int chatStartRow = _startRow + 1;
    int chatEndRow = _endRow - 1;      // leave last row for input

    for (int screenRow = chatStartRow; screenRow <= chatEndRow; screenRow++) {
        CxScreen::placeCursor(screenRow, 0);

        int lineIndex = _scrollOffset + (screenRow - chatStartRow);

        if (_displayLines != NULL && lineIndex < _displayLineCount) {
            CxString *line = _displayLines->at(lineIndex);
            if (line != NULL) {
                // Check prefix for coloring
                CxString text = *line;

                if (text.length() >= 2 && text.data()[0] == '>' && text.data()[1] == ' ') {
                    // User message - use command line edit color (white)
                    _defaults->applyCommandLineEditColors(_screen);
                } else if (text.length() >= 1 && text.data()[0] == '[') {
                    // Tool activity - use dim color
                    _defaults->applyCommandLineDimColors(_screen);
                } else {
                    // Claude response - use message line color (orange-ish)
                    _screen->setForegroundColor(_defaults->messageLineTextColor());
                }

                // Truncate to terminal width
                if ((int)text.length() > cols) {
                    text = text.subString(0, cols);
                }
                printf("%s", text.data());

                _defaults->resetColors(_screen);
            }
        }

        // Clear rest of line
        CxScreen::clearScreenFromCursorToEndOfLine();
    }
}


//-------------------------------------------------------------------------------------------------
// ClaudeView::drawInputLine
//
// Draw "claude> " prompt with current input text.
//-------------------------------------------------------------------------------------------------
void
ClaudeView::drawInputLine(void)
{
    CxScreen::placeCursor(_endRow, 0);

    // Prompt in dim color
    _defaults->applyCommandLineDimColors(_screen);
    printf("claude> ");
    _defaults->resetColors(_screen);

    // Input text in edit color
    _defaults->applyCommandLineEditColors(_screen);
    printf("%s", _inputText.data());
    _defaults->resetColors(_screen);

    CxScreen::clearScreenFromCursorToEndOfLine();
}


//-------------------------------------------------------------------------------------------------
// ClaudeView::placeCursor
//
// Place terminal cursor at the input position.
//-------------------------------------------------------------------------------------------------
void
ClaudeView::placeCursor(void)
{
    int promptLen = 8;  // "claude> "
    CxScreen::placeCursor(_endRow, promptLen + _inputCursorPos);
    _screen->showCursor();
}
