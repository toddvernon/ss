//-------------------------------------------------------------------------------------------------
//
//  SheetView.cpp
//  ss (spreadsheet)
//
//  Copyright 2022-2025 Todd Vernon. All rights reserved.
//  Licensed under the Apache License, Version 2.0
//  See LICENSE file for details.
//
//  SheetView implementation - grid display and cell rendering.
//
//-------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cx/json/json_utf_object.h>
#include <cx/json/json_utf_member.h>
#include <cx/json/json_utf_number.h>

#include "SheetView.h"
#include "SpreadsheetDefaults.h"


//-------------------------------------------------------------------------------------------------
// SheetView::SheetView
//
// Constructor
//-------------------------------------------------------------------------------------------------
SheetView::SheetView(CxScreen *scr, CxSheetModel *model, SpreadsheetDefaults *defaults,
                     int startRow, int endRow)
: screen(scr)
, sheetModel(model)
, _defaults(defaults)
, _startRow(startRow)
, _endRow(endRow)
, _rowHeaderWidth(5)        // "9999 " - room for 4-digit row numbers
, _colHeaderHeight(1)
, _defaultColWidth(10)
, _scrollRowOffset(0)
, _scrollColOffset(0)
, _inCellHuntMode(0)
, _huntRangeActive(0)
, _rangeSelectActive(0)
{
    // Initialize all column widths to 0 (meaning use default)
    for (int i = 0; i < MAX_COLUMNS; i++) {
        _colWidths[i] = 0;
    }
}


//-------------------------------------------------------------------------------------------------
// SheetView::~SheetView
//
// Destructor
//-------------------------------------------------------------------------------------------------
SheetView::~SheetView(void)
{
}


//-------------------------------------------------------------------------------------------------
// SheetView::recalcForResize
//
// Recalculate layout after terminal resize.
//-------------------------------------------------------------------------------------------------
void
SheetView::recalcForResize(int startRow, int endRow)
{
    _startRow = startRow;
    _endRow = endRow;

    // Ensure cursor is still visible after resize
    ensureCursorVisible();
}


//-------------------------------------------------------------------------------------------------
// SheetView::updateScreen
//
// Redraw the entire sheet view.
//-------------------------------------------------------------------------------------------------
void
SheetView::updateScreen(void)
{
    // Ensure cursor is visible before drawing
    ensureCursorVisible();

    // Clear the sheet area first (rows _startRow to _endRow)
    for (int r = _startRow; r <= _endRow; r++) {
        CxScreen::placeCursor(r, 0);
        CxScreen::clearScreenFromCursorToEndOfLine();
    }

    // Draw components
    drawColumnHeaders();
    drawRowNumbers();
    drawCells();

    // Draw status/divider line at bottom of sheet area
    updateStatusLine();

    fflush(stdout);
}


//-------------------------------------------------------------------------------------------------
// SheetView::updateCells
//
// Redraw only the specified cells. Used for optimized updates after data changes.
// Only redraws cells that are currently visible on screen.
//-------------------------------------------------------------------------------------------------
void
SheetView::updateCells(CxSList<CxSheetCellCoordinate> cells)
{
    int visRows = visibleDataRows();
    int screenWidth = screen->cols();

    for (int i = 0; i < (int)cells.entries(); i++) {
        CxSheetCellCoordinate coord = cells.at(i);
        int dataRow = coord.getRow();
        int dataCol = coord.getCol();

        // Check if row is visible
        if (dataRow < _scrollRowOffset || dataRow >= _scrollRowOffset + visRows) {
            continue;  // row not visible
        }

        // Check if column is visible (must be >= scroll offset)
        if (dataCol < _scrollColOffset) {
            continue;  // column not visible (scrolled past)
        }

        // Calculate screen position
        int screenRow = _startRow + _colHeaderHeight + (dataRow - _scrollRowOffset);
        int screenCol = getColumnScreenX(dataCol);

        // Check if column is visible (on screen)
        if (screenCol >= screenWidth) {
            continue;  // column not visible (off right edge)
        }

        HighlightType highlightType = getHighlightTypeForCell(dataRow, dataCol);
        drawCell(screenRow, screenCol, dataRow, dataCol, highlightType);
    }

    fflush(stdout);
}


//-------------------------------------------------------------------------------------------------
// SheetView::updateCursorMove
//
// Optimized update for cursor movement. Checks if scrolling is needed and does a
// full redraw if so, otherwise just redraws the old and new cell positions.
//-------------------------------------------------------------------------------------------------
void
SheetView::updateCursorMove(CxSheetCellCoordinate oldPos, CxSheetCellCoordinate newPos)
{
    int newRow = newPos.getRow();
    int newCol = newPos.getCol();

    int visRows = visibleDataRows();
    int visCols = visibleDataCols();

    // Check if new position is outside visible area (scrolling needed)
    int needsScroll = 0;
    if (newRow < _scrollRowOffset || newRow >= _scrollRowOffset + visRows) {
        needsScroll = 1;
    }
    if (newCol < _scrollColOffset || newCol >= _scrollColOffset + visCols) {
        needsScroll = 1;
    }

    if (needsScroll) {
        // Full redraw - ensureCursorVisible will adjust scroll offsets
        updateScreen();
    } else {
        // Just redraw the two affected cells
        CxSList<CxSheetCellCoordinate> affectedCells;
        affectedCells.append(oldPos);
        if (!(oldPos == newPos)) {
            affectedCells.append(newPos);
        }
        updateCells(affectedCells);
    }
}


//-------------------------------------------------------------------------------------------------
// SheetView::drawColumnHeaders
//
// Draw the column header row (A, B, C, ...)
//-------------------------------------------------------------------------------------------------
void
SheetView::drawColumnHeaders(void)
{
    CxScreen::placeCursor(_startRow, 0);

    // Apply header colors
    _defaults->applyHeaderColors(screen);

    // Row header spacer
    for (int i = 0; i < _rowHeaderWidth; i++) {
        printf(" ");
    }

    // Column headers - iterate until we run out of screen width
    int screenX = _rowHeaderWidth;
    int screenWidth = screen->cols();
    CxSheetCellCoordinate tempCoord;

    for (int dataCol = _scrollColOffset; screenX < screenWidth; dataCol++) {
        int colWidth = getColumnWidth(dataCol);
        CxString colName = tempCoord.colToLetters(dataCol);

        // Center the column name in the column width
        int padding = (colWidth - colName.length()) / 2;
        for (int p = 0; p < padding && screenX < screenWidth; p++) {
            printf(" ");
            screenX++;
        }
        // Print column name (truncate if needed)
        for (int i = 0; i < (int)colName.length() && screenX < screenWidth; i++) {
            printf("%c", colName.data()[i]);
            screenX++;
        }
        int remaining = colWidth - padding - colName.length();
        for (int p = 0; p < remaining && screenX < screenWidth; p++) {
            printf(" ");
            screenX++;
        }
    }

    // Reset colors
    _defaults->resetColors(screen);
}


//-------------------------------------------------------------------------------------------------
// SheetView::drawRowNumbers
//
// Draw the row numbers in the left column.
//-------------------------------------------------------------------------------------------------
void
SheetView::drawRowNumbers(void)
{
    int numRows = visibleDataRows();

    // Apply header colors (same as column headers for visual consistency)
    _defaults->applyHeaderColors(screen);

    for (int r = 0; r < numRows; r++) {
        int screenRow = _startRow + _colHeaderHeight + r;
        int dataRow = r + _scrollRowOffset;

        CxScreen::placeCursor(screenRow, 0);

        // Row number (1-based display)
        char rowNum[16];
        snprintf(rowNum, sizeof(rowNum), "%4d ", dataRow + 1);
        printf("%s", rowNum);
    }

    // Reset colors
    _defaults->resetColors(screen);
}


//-------------------------------------------------------------------------------------------------
// SheetView::drawCells
//
// Draw all visible cells.
//-------------------------------------------------------------------------------------------------
void
SheetView::drawCells(void)
{
    int numRows = visibleDataRows();
    int screenWidth = screen->cols();

    for (int r = 0; r < numRows; r++) {
        int screenRow = _startRow + _colHeaderHeight + r;
        int dataRow = r + _scrollRowOffset;

        // Draw columns until we run out of screen width
        int screenCol = _rowHeaderWidth;
        for (int dataCol = _scrollColOffset; screenCol < screenWidth; dataCol++) {
            int colWidth = getColumnWidth(dataCol);

            HighlightType highlightType = getHighlightTypeForCell(dataRow, dataCol);
            drawCell(screenRow, screenCol, dataRow, dataCol, highlightType);

            screenCol += colWidth;
        }
    }
}


//-------------------------------------------------------------------------------------------------
// SheetView::drawCell
//
// Draw a single cell with the specified highlight type.
//-------------------------------------------------------------------------------------------------
void
SheetView::drawCell(int screenRow, int screenCol, int dataRow, int dataCol,
                    HighlightType highlightType)
{
    CxScreen::placeCursor(screenRow, screenCol);

    // Get cell from model
    CxSheetCellCoordinate coord(dataRow, dataCol);
    CxSheetCell *cell = sheetModel->getCellPtr(coord);

    // Format cell content using per-column width
    int colWidth = getColumnWidth(dataCol);
    CxString content = formatCellValue(cell, colWidth);

    // Draw with appropriate colors based on highlight type
    switch (highlightType) {
        case HIGHLIGHT_CURSOR:
            _defaults->applySelectedCellColors(screen);
            printf("%s", content.data());
            _defaults->resetColors(screen);
            break;

        case HIGHLIGHT_HUNT:
            _defaults->applyCellHuntColors(screen);
            printf("%s", content.data());
            _defaults->resetColors(screen);
            break;

        case HIGHLIGHT_HUNT_RANGE:
            _defaults->applyCellHuntRangeColors(screen);
            printf("%s", content.data());
            _defaults->resetColors(screen);
            break;

        case HIGHLIGHT_RANGE:
            _defaults->applyRangeSelectColors(screen);
            printf("%s", content.data());
            _defaults->resetColors(screen);
            break;

        case HIGHLIGHT_NONE:
        default:
            _defaults->applyCellColors(screen);
            printf("%s", content.data());
            _defaults->resetColors(screen);
            break;
    }
}


//-------------------------------------------------------------------------------------------------
// SheetView::formatCellValue
//
// Format cell contents for display, truncating or padding as needed.
// Uses CxUTFString for proper UTF-8 character handling.
// Handles symbol fill cells (box drawing) via appAttribute "symbolFill".
//-------------------------------------------------------------------------------------------------
CxString
SheetView::formatCellValue(CxSheetCell *cell, int width)
{
    CxString result;

    if (cell == NULL) {
        // Empty cell - just spaces
        for (int i = 0; i < width; i++) {
            result = result + " ";
        }
        return result;
    }

    // Check for symbol fill cells
    if (cell->hasAppAttribute("symbolFill")) {
        CxString symbolType = cell->getAppAttributeString("symbolFill");
        return formatSymbolFill(symbolType, width);
    }

    CxString rawText;

    switch (cell->getType()) {
        case CxSheetCell::TEXT:
            rawText = cell->getText();
            break;

        case CxSheetCell::DOUBLE:
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "%g", cell->getDouble().value);
                rawText = CxString(buf);
            }
            break;

        case CxSheetCell::FORMULA:
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "%g", cell->getEvaluatedValue().value);
                rawText = CxString(buf);
            }
            break;

        case CxSheetCell::EMPTY:
        default:
            // Empty - just spaces
            for (int i = 0; i < width; i++) {
                result = result + " ";
            }
            return result;
    }

    // Convert to UTF string for proper character counting
    CxUTFString utfText;
    utfText.fromCxString(rawText, 8);  // 8-space tabs

    int displayLen = utfText.displayWidth();

    if (displayLen >= width) {
        // Truncate by characters (not bytes) to fit width
        CxUTFString truncated;
        int currentWidth = 0;
        for (int i = 0; i < utfText.charCount() && currentWidth < width; i++) {
            const CxUTFCharacter *ch = utfText.at(i);
            if (ch != NULL && currentWidth + ch->displayWidth() <= width) {
                truncated.append(*ch);
                currentWidth += ch->displayWidth();
            } else {
                break;
            }
        }
        result = truncated.toBytes();
        // Pad with spaces if truncation left room
        for (int i = currentWidth; i < width; i++) {
            result = result + " ";
        }
    } else {
        // Right-align numbers, left-align text
        if (cell->getType() == CxSheetCell::TEXT) {
            result = rawText;
            for (int i = displayLen; i < width; i++) {
                result = result + " ";
            }
        } else {
            // Right-align numbers - pad on left
            result = "";
            for (int i = displayLen; i < width; i++) {
                result = result + " ";
            }
            result = result + rawText;
        }
    }

    return result;
}


//-------------------------------------------------------------------------------------------------
// SheetView::formatSymbolFill
//
// Format symbol fill cells (box drawing). Symbol types:
//   horizontal:   ────────── (fill with ─)
//   vertical:     │          (single │, pad with spaces)
//   upper-left:   ┌──────── (corner then ─ fill)
//   upper-right:  ────────┐ (─ fill then corner)
//   lower-left:   └──────── (corner then ─ fill)
//   lower-right:  ────────┘ (─ fill then corner)
//   left-tee:     ├          (single char)
//   right-tee:    ┤          (single char)
//   upper-tee:    ────┬──── (centered T with ─ fill)
//   lower-tee:    ────┴──── (centered T with ─ fill)
//-------------------------------------------------------------------------------------------------
CxString
SheetView::formatSymbolFill(CxString symbolType, int width)
{
    CxString result;

    // Box drawing characters (UTF-8)
    const char* horizLine = "\xe2\x94\x80";     // ─
    const char* vertLine = "\xe2\x94\x82";      // │
    const char* upperLeft = "\xe2\x94\x8c";     // ┌
    const char* upperRight = "\xe2\x94\x90";    // ┐
    const char* lowerLeft = "\xe2\x94\x94";     // └
    const char* lowerRight = "\xe2\x94\x98";    // ┘
    const char* leftTee = "\xe2\x94\x9c";       // ├
    const char* rightTee = "\xe2\x94\xa4";      // ┤
    const char* upperTee = "\xe2\x94\xac";      // ┬
    const char* lowerTee = "\xe2\x94\xb4";      // ┴

    if (symbolType == "horizontal") {
        // Fill entire width with horizontal line
        for (int i = 0; i < width; i++) {
            result = result + horizLine;
        }
    }
    else if (symbolType == "vertical") {
        // Single vertical line, pad rest with spaces
        result = vertLine;
        for (int i = 1; i < width; i++) {
            result = result + " ";
        }
    }
    else if (symbolType == "upper-left") {
        // Corner then horizontal fill
        if (width >= 1) {
            result = upperLeft;
            for (int i = 1; i < width; i++) {
                result = result + horizLine;
            }
        }
    }
    else if (symbolType == "upper-right") {
        // Horizontal fill then corner
        if (width >= 1) {
            for (int i = 0; i < width - 1; i++) {
                result = result + horizLine;
            }
            result = result + upperRight;
        }
    }
    else if (symbolType == "lower-left") {
        // Corner then horizontal fill
        if (width >= 1) {
            result = lowerLeft;
            for (int i = 1; i < width; i++) {
                result = result + horizLine;
            }
        }
    }
    else if (symbolType == "lower-right") {
        // Horizontal fill then corner
        if (width >= 1) {
            for (int i = 0; i < width - 1; i++) {
                result = result + horizLine;
            }
            result = result + lowerRight;
        }
    }
    else if (symbolType == "left-tee") {
        // Single left tee, pad rest with spaces
        result = leftTee;
        for (int i = 1; i < width; i++) {
            result = result + " ";
        }
    }
    else if (symbolType == "right-tee") {
        // Single right tee, pad rest with spaces
        result = rightTee;
        for (int i = 1; i < width; i++) {
            result = result + " ";
        }
    }
    else if (symbolType == "upper-tee") {
        // Centered T with horizontal fill on both sides
        if (width == 1) {
            result = upperTee;
        } else {
            int leftCount = (width - 1) / 2;
            int rightCount = width - 1 - leftCount;
            for (int i = 0; i < leftCount; i++) {
                result = result + horizLine;
            }
            result = result + upperTee;
            for (int i = 0; i < rightCount; i++) {
                result = result + horizLine;
            }
        }
    }
    else if (symbolType == "lower-tee") {
        // Centered T with horizontal fill on both sides
        if (width == 1) {
            result = lowerTee;
        } else {
            int leftCount = (width - 1) / 2;
            int rightCount = width - 1 - leftCount;
            for (int i = 0; i < leftCount; i++) {
                result = result + horizLine;
            }
            result = result + lowerTee;
            for (int i = 0; i < rightCount; i++) {
                result = result + horizLine;
            }
        }
    }
    else {
        // Unknown symbol type - fill with spaces
        for (int i = 0; i < width; i++) {
            result = result + " ";
        }
    }

    return result;
}


//-------------------------------------------------------------------------------------------------
// SheetView::visibleDataRows
//
// Return number of data rows that fit on screen.
// The sheet area is _startRow to _endRow inclusive. Subtract 1 for column header.
// The divider is at _endRow, so data rows fill from _startRow+1 to _endRow-1.
//-------------------------------------------------------------------------------------------------
int
SheetView::visibleDataRows(void)
{
    int availableRows = _endRow - _startRow - _colHeaderHeight;
    return availableRows > 0 ? availableRows : 0;
}


//-------------------------------------------------------------------------------------------------
// SheetView::visibleDataCols
//
// Return number of data columns that fit on screen.
//-------------------------------------------------------------------------------------------------
int
SheetView::visibleDataCols(void)
{
    int screenWidth = screen->cols();
    int usedWidth = _rowHeaderWidth;
    int cols = 0;

    // Count how many columns fit starting from scroll offset
    for (int dataCol = _scrollColOffset; usedWidth < screenWidth && dataCol < MAX_COLUMNS; dataCol++) {
        usedWidth += getColumnWidth(dataCol);
        cols++;
    }

    return cols > 0 ? cols : 1;
}


//-------------------------------------------------------------------------------------------------
// SheetView::ensureCursorVisible
//
// Adjust scroll offsets if the current cell is off-screen.
//-------------------------------------------------------------------------------------------------
void
SheetView::ensureCursorVisible(void)
{
    CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
    int row = pos.getRow();
    int col = pos.getCol();

    int visRows = visibleDataRows();

    // Vertical scrolling
    if (row < _scrollRowOffset) {
        _scrollRowOffset = row;
    } else if (row >= _scrollRowOffset + visRows) {
        _scrollRowOffset = row - visRows + 1;
    }

    // Horizontal scrolling - check if column is visible
    if (col < _scrollColOffset) {
        // Scrolled too far right - scroll left to show this column
        _scrollColOffset = col;
    } else {
        // Check if column extends past right edge of screen
        int screenWidth = screen->cols();
        int colScreenX = getColumnScreenX(col);
        int colWidth = getColumnWidth(col);

        if (colScreenX + colWidth > screenWidth) {
            // Need to scroll right - find the minimum scroll offset that shows this column
            // Try scrolling right one column at a time until the column is visible
            while (colScreenX + colWidth > screenWidth && _scrollColOffset < col) {
                _scrollColOffset++;
                colScreenX = getColumnScreenX(col);
            }
        }
    }
}


//-------------------------------------------------------------------------------------------------
// SheetView::placeCursor
//
// Place the terminal cursor at the current cell position.
//-------------------------------------------------------------------------------------------------
void
SheetView::placeCursor(void)
{
    CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();

    int row = pos.getRow() - _scrollRowOffset;
    int dataCol = pos.getCol();

    int screenRow = _startRow + _colHeaderHeight + row;
    int screenCol = getColumnScreenX(dataCol);

    CxScreen::placeCursor(screenRow, screenCol);
}


//-------------------------------------------------------------------------------------------------
// SheetView::getDefaultColumnWidth
//-------------------------------------------------------------------------------------------------
int
SheetView::getDefaultColumnWidth(void)
{
    return _defaultColWidth;
}


//-------------------------------------------------------------------------------------------------
// SheetView::setDefaultColumnWidth
//-------------------------------------------------------------------------------------------------
void
SheetView::setDefaultColumnWidth(int width)
{
    if (width >= 3 && width <= 50) {
        _defaultColWidth = width;
    }
}


//-------------------------------------------------------------------------------------------------
// SheetView::getColumnWidth
//
// Get width for specific column. Returns default if no custom width is set.
//-------------------------------------------------------------------------------------------------
int
SheetView::getColumnWidth(int col)
{
    if (col < 0 || col >= MAX_COLUMNS) {
        return _defaultColWidth;
    }
    if (_colWidths[col] == 0) {
        return _defaultColWidth;
    }
    return _colWidths[col];
}


//-------------------------------------------------------------------------------------------------
// SheetView::setColumnWidth
//
// Set width for specific column. Use 0 to clear custom width.
//-------------------------------------------------------------------------------------------------
void
SheetView::setColumnWidth(int col, int width)
{
    if (col < 0 || col >= MAX_COLUMNS) {
        return;
    }
    if (width < 0) {
        width = 0;
    }
    if (width > 100) {
        width = 100;
    }
    _colWidths[col] = width;
}


//-------------------------------------------------------------------------------------------------
// SheetView::getColumnScreenX
//
// Get screen X position for a column, accounting for varying column widths.
// This sums up widths of all columns from _scrollColOffset to col-1.
//-------------------------------------------------------------------------------------------------
int
SheetView::getColumnScreenX(int col)
{
    int screenX = _rowHeaderWidth;

    // Sum widths of visible columns before this one
    for (int c = _scrollColOffset; c < col; c++) {
        screenX += getColumnWidth(c);
    }

    return screenX;
}


//-------------------------------------------------------------------------------------------------
// SheetView::setCellHuntMode
//
// Enable or disable cell hunt mode. When active, the formula cell is highlighted blue
// and the hunt cell is highlighted green.
//-------------------------------------------------------------------------------------------------
void
SheetView::setCellHuntMode(int active, CxSheetCellCoordinate formulaCell,
                           CxSheetCellCoordinate huntCell)
{
    _inCellHuntMode = active;
    _huntFormulaCell = formulaCell;
    _huntCurrentCell = huntCell;

    if (!active) {
        _huntRangeActive = 0;
    }
}


//-------------------------------------------------------------------------------------------------
// SheetView::setHuntRange
//
// Set range selection state. When active is true, anchor marks the start of the range
// and current marks the end. Cells between are highlighted with range color.
//-------------------------------------------------------------------------------------------------
void
SheetView::setHuntRange(int active, CxSheetCellCoordinate anchor,
                        CxSheetCellCoordinate current)
{
    _huntRangeActive = active;
    _huntAnchorCell = anchor;
    _huntCurrentCell = current;
}


//-------------------------------------------------------------------------------------------------
// SheetView::setRangeSelection
//
// Enable or disable range selection (EDIT mode multi-cell selection).
// When active, anchor marks the start of the range and current marks the end.
//-------------------------------------------------------------------------------------------------
void
SheetView::setRangeSelection(int active, CxSheetCellCoordinate anchor,
                             CxSheetCellCoordinate current)
{
    _rangeSelectActive = active;
    _rangeAnchor = anchor;
    _rangeCurrent = current;
}


//-------------------------------------------------------------------------------------------------
// SheetView::updateRangeSelectionMove
//
// Optimized redraw for range selection extension. Only redraws cells that changed
// highlight state between the old selection and new selection.
// If scrolling is needed, falls back to full redraw.
//-------------------------------------------------------------------------------------------------
void
SheetView::updateRangeSelectionMove(CxSheetCellCoordinate anchor,
                                    CxSheetCellCoordinate oldCurrent,
                                    CxSheetCellCoordinate newCurrent)
{
    // Update range state - ensure selection is marked active
    _rangeSelectActive = 1;
    _rangeAnchor = anchor;
    _rangeCurrent = newCurrent;

    // Check if new position requires scrolling
    int newRow = newCurrent.getRow();
    int newCol = newCurrent.getCol();
    int visRows = visibleDataRows();
    int visCols = visibleDataCols();

    int needsScroll = 0;
    if (newRow < _scrollRowOffset || newRow >= _scrollRowOffset + visRows) {
        needsScroll = 1;
    }
    if (newCol < _scrollColOffset || newCol >= _scrollColOffset + visCols) {
        needsScroll = 1;
    }

    if (needsScroll) {
        // Full redraw needed for scrolling
        updateScreen();
        return;
    }

    // Calculate old range bounds (normalized)
    int oldMinRow = (anchor.getRow() < oldCurrent.getRow()) ? anchor.getRow() : oldCurrent.getRow();
    int oldMaxRow = (anchor.getRow() > oldCurrent.getRow()) ? anchor.getRow() : oldCurrent.getRow();
    int oldMinCol = (anchor.getCol() < oldCurrent.getCol()) ? anchor.getCol() : oldCurrent.getCol();
    int oldMaxCol = (anchor.getCol() > oldCurrent.getCol()) ? anchor.getCol() : oldCurrent.getCol();

    // Calculate new range bounds (normalized)
    int newMinRow = (anchor.getRow() < newCurrent.getRow()) ? anchor.getRow() : newCurrent.getRow();
    int newMaxRow = (anchor.getRow() > newCurrent.getRow()) ? anchor.getRow() : newCurrent.getRow();
    int newMinCol = (anchor.getCol() < newCurrent.getCol()) ? anchor.getCol() : newCurrent.getCol();
    int newMaxCol = (anchor.getCol() > newCurrent.getCol()) ? anchor.getCol() : newCurrent.getCol();

    // Collect cells that changed highlight state
    CxSList<CxSheetCellCoordinate> changedCells;

    // Calculate union of old and new ranges to find all potentially changed cells
    int unionMinRow = (oldMinRow < newMinRow) ? oldMinRow : newMinRow;
    int unionMaxRow = (oldMaxRow > newMaxRow) ? oldMaxRow : newMaxRow;
    int unionMinCol = (oldMinCol < newMinCol) ? oldMinCol : newMinCol;
    int unionMaxCol = (oldMaxCol > newMaxCol) ? oldMaxCol : newMaxCol;

    // Check each cell in the union - add to list if its state changed
    for (int r = unionMinRow; r <= unionMaxRow; r++) {
        for (int c = unionMinCol; c <= unionMaxCol; c++) {
            int wasInOld = (r >= oldMinRow && r <= oldMaxRow &&
                            c >= oldMinCol && c <= oldMaxCol);
            int isInNew = (r >= newMinRow && r <= newMaxRow &&
                           c >= newMinCol && c <= newMaxCol);

            // Only redraw if the cell's selection state changed
            if (wasInOld != isInNew) {
                changedCells.append(CxSheetCellCoordinate(r, c));
            }
        }
    }

    // Always redraw the old and new cursor positions (cursor highlight may change)
    changedCells.append(oldCurrent);
    if (!(oldCurrent == newCurrent)) {
        changedCells.append(newCurrent);
    }

    // Redraw only the changed cells
    updateCells(changedCells);
}


//-------------------------------------------------------------------------------------------------
// SheetView::updateCellHuntMove
//
// Optimized redraw for cell hunt cursor movement. Redraws old and new positions.
//-------------------------------------------------------------------------------------------------
void
SheetView::updateCellHuntMove(CxSheetCellCoordinate oldPos, CxSheetCellCoordinate newPos)
{
    // Update the hunt current position
    _huntCurrentCell = newPos;

    // Collect cells to redraw
    CxSList<CxSheetCellCoordinate> cells;
    cells.append(oldPos);

    if (!(oldPos == newPos)) {
        cells.append(newPos);
    }

    // If range is active, we may need a full redraw for range highlighting
    if (_huntRangeActive) {
        // Full redraw to properly show range
        updateScreen();
    } else {
        updateCells(cells);
    }
}


//-------------------------------------------------------------------------------------------------
// SheetView::getHighlightTypeForCell
//
// Determine the appropriate highlight type for a cell based on current mode and position.
//-------------------------------------------------------------------------------------------------
HighlightType
SheetView::getHighlightTypeForCell(int dataRow, int dataCol)
{
    CxSheetCellCoordinate cursorPos = sheetModel->getCurrentPosition();

    // If in cell hunt mode, check hunt positions first
    if (_inCellHuntMode) {
        // Check if this is the hunt cursor position
        if (dataRow == (int)_huntCurrentCell.getRow() &&
            dataCol == (int)_huntCurrentCell.getCol()) {
            return HIGHLIGHT_HUNT;
        }

        // Check if this is within the hunt range (when range is active)
        if (_huntRangeActive && isCellInHuntRange(dataRow, dataCol)) {
            return HIGHLIGHT_HUNT_RANGE;
        }

        // Check if this is the formula cell (where we started)
        if (dataRow == (int)_huntFormulaCell.getRow() &&
            dataCol == (int)_huntFormulaCell.getCol()) {
            return HIGHLIGHT_CURSOR;
        }
    } else {
        // Normal mode - check if this is the cursor position
        if (dataRow == (int)cursorPos.getRow() &&
            dataCol == (int)cursorPos.getCol()) {
            return HIGHLIGHT_CURSOR;
        }

        // Check if this is within the selection range (EDIT mode multi-cell selection)
        if (_rangeSelectActive && isCellInSelectionRange(dataRow, dataCol)) {
            return HIGHLIGHT_RANGE;
        }
    }

    return HIGHLIGHT_NONE;
}


//-------------------------------------------------------------------------------------------------
// SheetView::isCellInHuntRange
//
// Check if a cell is within the hunt range (from anchor to current, inclusive).
// The range is normalized so it works regardless of selection direction.
//-------------------------------------------------------------------------------------------------
int
SheetView::isCellInHuntRange(int row, int col)
{
    if (!_huntRangeActive) {
        return 0;
    }

    int anchorRow = _huntAnchorCell.getRow();
    int anchorCol = _huntAnchorCell.getCol();
    int currentRow = _huntCurrentCell.getRow();
    int currentCol = _huntCurrentCell.getCol();

    // Normalize the range (min/max for each dimension)
    int minRow = (anchorRow < currentRow) ? anchorRow : currentRow;
    int maxRow = (anchorRow > currentRow) ? anchorRow : currentRow;
    int minCol = (anchorCol < currentCol) ? anchorCol : currentCol;
    int maxCol = (anchorCol > currentCol) ? anchorCol : currentCol;

    return (row >= minRow && row <= maxRow && col >= minCol && col <= maxCol);
}


//-------------------------------------------------------------------------------------------------
// SheetView::isCellInSelectionRange
//
// Check if a cell is within the selection range (EDIT mode multi-cell selection).
// The range is normalized so it works regardless of selection direction.
//-------------------------------------------------------------------------------------------------
int
SheetView::isCellInSelectionRange(int row, int col)
{
    if (!_rangeSelectActive) {
        return 0;
    }

    int anchorRow = _rangeAnchor.getRow();
    int anchorCol = _rangeAnchor.getCol();
    int currentRow = _rangeCurrent.getRow();
    int currentCol = _rangeCurrent.getCol();

    // Normalize the range (min/max for each dimension)
    int minRow = (anchorRow < currentRow) ? anchorRow : currentRow;
    int maxRow = (anchorRow > currentRow) ? anchorRow : currentRow;
    int minCol = (anchorCol < currentCol) ? anchorCol : currentCol;
    int maxCol = (anchorCol > currentCol) ? anchorCol : currentCol;

    return (row >= minRow && row <= maxRow && col >= minCol && col <= maxCol);
}


//-------------------------------------------------------------------------------------------------
// SheetView::setFilePath
//
// Set the file path for status line display.
//-------------------------------------------------------------------------------------------------
void
SheetView::setFilePath(CxString path)
{
    _filePath = path;
}


//-------------------------------------------------------------------------------------------------
// SheetView::updateStatusLine
//
// Draw the status/divider line at the bottom of the sheet area.
// Shows: ── ss: Editing [ <filename> ] ──────────────────── cell(<address>) ──
// Similar to cm's status line format.
//-------------------------------------------------------------------------------------------------
void
SheetView::updateStatusLine(void)
{
    // Status fill character - UTF-8 box drawing horizontal line
    static const char *STATUS_FILL = "\xe2\x94\x80";  // ─ (U+2500)

    CxScreen::placeCursor(_endRow, 0);
    screen->setForegroundColor(_defaults->headerTextColor());
    screen->setBackgroundColor(_defaults->headerBackgroundColor());

    // Build left part: ── ss: Editing [ <filename> ]
    CxString leftPart;
    leftPart += STATUS_FILL;
    leftPart += STATUS_FILL;
    leftPart += " ss: Editing [ ";

    if (_filePath.length() > 0) {
        leftPart += _filePath;
    } else {
        leftPart += "(untitled)";
    }
    leftPart += " ] ";

    // Calculate display width for left part (2 fill chars + text)
    int leftDisplayWidth = 2;  // two fill chars
    leftDisplayWidth += 15;    // " ss: Editing [ "
    if (_filePath.length() > 0) {
        leftDisplayWidth += _filePath.length();
    } else {
        leftDisplayWidth += 10;  // "(untitled)"
    }
    leftDisplayWidth += 3;     // " ] "

    // Build right part: cell(<address>) or cell(<anchor>:<current>) for range
    CxString cellAddress;
    if (_rangeSelectActive) {
        // Show range: cell(A1:C3)
        cellAddress = _rangeAnchor.toAddress();
        cellAddress += ":";
        cellAddress += _rangeCurrent.toAddress();
    } else {
        // Show single cell
        CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
        cellAddress = pos.toAddress();
    }

    CxString rightPart;
    rightPart += "cell(";
    rightPart += cellAddress;
    rightPart += ") ";
    rightPart += STATUS_FILL;
    rightPart += STATUS_FILL;

    // Right part display width: "cell(" + address + ") " + 2 fill chars
    int rightDisplayWidth = 5 + cellAddress.length() + 2 + 2;

    // Calculate fill characters needed between left and right
    int totalWidth = screen->cols();
    int fillNeeded = totalWidth - leftDisplayWidth - rightDisplayWidth;
    if (fillNeeded < 0) fillNeeded = 0;

    // Build and output the full status line
    CxString statusLine = leftPart;
    for (int i = 0; i < fillNeeded; i++) {
        statusLine += STATUS_FILL;
    }
    statusLine += rightPart;

    // Write the status line
    printf("%s", statusLine.data());

    screen->resetColors();
    fflush(stdout);
}


//-------------------------------------------------------------------------------------------------
// SheetView::loadColumnWidthsFromAppData
//
// Load column widths from app data (after sheet load).
// Expected format: { "columns": { "A": 12, "B": 25, ... } }
//-------------------------------------------------------------------------------------------------
void
SheetView::loadColumnWidthsFromAppData(CxJSONUTFObject* appData)
{
    if (appData == NULL) {
        return;
    }

    // Look for "columns" object
    CxJSONUTFMember *columnsMember = appData->find("columns");
    if (columnsMember == NULL) {
        return;
    }

    CxJSONUTFBase *columnsBase = columnsMember->object();
    if (columnsBase == NULL || columnsBase->type() != CxJSONUTFBase::OBJECT) {
        return;
    }

    CxJSONUTFObject *columns = (CxJSONUTFObject *)columnsBase;

    // Iterate through all column width entries
    int numEntries = columns->entries();
    for (int i = 0; i < numEntries; i++) {
        CxJSONUTFMember *member = columns->at(i);
        if (member == NULL) {
            continue;
        }

        // Get the column letter (e.g., "A", "B", "AA")
        CxUTFString colName = member->var();
        CxString colNameStr = colName.toBytes();

        // Get the width value
        CxJSONUTFBase *widthBase = member->object();
        if (widthBase == NULL || widthBase->type() != CxJSONUTFBase::NUMBER) {
            continue;
        }

        CxJSONUTFNumber *widthNum = (CxJSONUTFNumber *)widthBase;
        int width = (int)widthNum->get();

        // Convert column letter to column index
        CxSheetCellCoordinate coord;
        int col = coord.lettersToCol(colNameStr);

        // Set the column width
        if (col >= 0 && col < MAX_COLUMNS) {
            _colWidths[col] = width;
        }
    }
}


//-------------------------------------------------------------------------------------------------
// SheetView::saveColumnWidthsToAppData
//
// Save column widths to app data (before sheet save).
// Writes format: { "columns": { "A": 12, "B": 25, ... } }
//-------------------------------------------------------------------------------------------------
void
SheetView::saveColumnWidthsToAppData(CxJSONUTFObject* appData)
{
    if (appData == NULL) {
        return;
    }

    // Check if any columns have custom widths
    int hasCustomWidths = 0;
    for (int col = 0; col < MAX_COLUMNS; col++) {
        if (_colWidths[col] != 0) {
            hasCustomWidths = 1;
            break;
        }
    }

    if (!hasCustomWidths) {
        // No custom widths - remove existing columns entry if present
        CxJSONUTFMember *existing = appData->find("columns");
        if (existing != NULL) {
            // Find and remove it
            int numEntries = appData->entries();
            for (int i = 0; i < numEntries; i++) {
                CxJSONUTFMember *m = appData->at(i);
                CxUTFString mname = m->var();
                if (mname.toBytes() == "columns") {
                    appData->removeAt(i);
                    delete existing;
                    break;
                }
            }
        }
        return;
    }

    // Create new columns object
    CxJSONUTFObject *columns = new CxJSONUTFObject();

    // Add entries for each column with custom width
    CxSheetCellCoordinate coord;  // for colToLetters helper
    for (int col = 0; col < MAX_COLUMNS; col++) {
        if (_colWidths[col] != 0) {
            CxString colName = coord.colToLetters(col);
            CxJSONUTFNumber *widthNum = new CxJSONUTFNumber((double)_colWidths[col]);
            CxJSONUTFMember *member = new CxJSONUTFMember(colName.data(), widthNum);
            columns->append(member);
        }
    }

    // Remove existing "columns" entry if present
    CxJSONUTFMember *existing = appData->find("columns");
    if (existing != NULL) {
        int numEntries = appData->entries();
        for (int i = 0; i < numEntries; i++) {
            CxJSONUTFMember *m = appData->at(i);
            CxUTFString mname = m->var();
            if (mname.toBytes() == "columns") {
                appData->removeAt(i);
                delete existing;
                break;
            }
        }
    }

    // Add the new columns object
    CxJSONUTFMember *columnsMember = new CxJSONUTFMember("columns", columns);
    appData->append(columnsMember);
}
