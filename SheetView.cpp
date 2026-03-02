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

#include "SheetView.h"


//-------------------------------------------------------------------------------------------------
// SheetView::SheetView
//
// Constructor
//-------------------------------------------------------------------------------------------------
SheetView::SheetView(CxScreen *scr, CxSheetModel *model, int startRow, int endRow)
: screen(scr)
, sheetModel(model)
, _startRow(startRow)
, _endRow(endRow)
, _rowHeaderWidth(5)        // "9999 " - room for 4-digit row numbers
, _colHeaderHeight(1)
, _defaultColWidth(10)
, _scrollRowOffset(0)
, _scrollColOffset(0)
{
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

    // Draw divider line at bottom of sheet area (last row of our region)
    CxScreen::placeCursor(_endRow, 0);
    for (int i = 0; i < screen->cols(); i++) {
        printf("-");
    }

    fflush(stdout);
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

    // Row header spacer
    for (int i = 0; i < _rowHeaderWidth; i++) {
        printf(" ");
    }

    // Column headers
    int numCols = visibleDataCols();
    CxSheetCellCoordinate tempCoord;

    for (int c = 0; c < numCols; c++) {
        int dataCol = c + _scrollColOffset;
        CxString colName = tempCoord.colToLetters(dataCol);

        // Center the column name in the column width
        int padding = (_defaultColWidth - colName.length()) / 2;
        for (int p = 0; p < padding; p++) {
            printf(" ");
        }
        printf("%s", colName.data());
        int remaining = _defaultColWidth - padding - colName.length();
        for (int p = 0; p < remaining; p++) {
            printf(" ");
        }
    }
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

    for (int r = 0; r < numRows; r++) {
        int screenRow = _startRow + _colHeaderHeight + r;
        int dataRow = r + _scrollRowOffset;

        CxScreen::placeCursor(screenRow, 0);

        // Row number (1-based display)
        char rowNum[16];
        snprintf(rowNum, sizeof(rowNum), "%4d ", dataRow + 1);
        printf("%s", rowNum);
    }
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
    int numCols = visibleDataCols();

    CxSheetCellCoordinate cursorPos = sheetModel->getCurrentPosition();

    for (int r = 0; r < numRows; r++) {
        int screenRow = _startRow + _colHeaderHeight + r;
        int dataRow = r + _scrollRowOffset;

        for (int c = 0; c < numCols; c++) {
            int screenCol = _rowHeaderWidth + (c * _defaultColWidth);
            int dataCol = c + _scrollColOffset;

            int isHighlighted = (dataRow == (int)cursorPos.getRow() &&
                                 dataCol == (int)cursorPos.getCol());

            drawCell(screenRow, screenCol, dataRow, dataCol, isHighlighted);
        }
    }
}


//-------------------------------------------------------------------------------------------------
// SheetView::drawCell
//
// Draw a single cell.
//-------------------------------------------------------------------------------------------------
void
SheetView::drawCell(int screenRow, int screenCol, int dataRow, int dataCol, int isHighlighted)
{
    CxScreen::placeCursor(screenRow, screenCol);

    // Get cell from model
    CxSheetCellCoordinate coord(dataRow, dataCol);
    CxSheetCell *cell = sheetModel->getCellPtr(coord);

    // Format cell content
    CxString content = formatCellValue(cell, _defaultColWidth);

    // Draw with highlight if current cell
    if (isHighlighted) {
        // Inverse video for highlight
        printf("\033[7m%s\033[0m", content.data());
    } else {
        printf("%s", content.data());
    }
}


//-------------------------------------------------------------------------------------------------
// SheetView::formatCellValue
//
// Format cell contents for display, truncating or padding as needed.
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

    CxString text;

    switch (cell->getType()) {
        case CxSheetCell::TEXT:
            text = cell->getText();
            break;

        case CxSheetCell::DOUBLE:
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "%g", cell->getDouble().value);
                text = CxString(buf);
            }
            break;

        case CxSheetCell::FORMULA:
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "%g", cell->getEvaluatedValue().value);
                text = CxString(buf);
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

    // Truncate or pad to width
    int len = text.length();
    if (len >= width) {
        // Truncate
        result = text.subString(0, width);
    } else {
        // Right-align numbers, left-align text
        if (cell->getType() == CxSheetCell::TEXT) {
            result = text;
            for (int i = len; i < width; i++) {
                result = result + " ";
            }
        } else {
            // Right-align numbers - pad on left
            result = "";
            for (int i = len; i < width; i++) {
                result = result + " ";
            }
            result = result + text;
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
    int availableWidth = screen->cols() - _rowHeaderWidth;
    int cols = availableWidth / _defaultColWidth;
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
    int visCols = visibleDataCols();

    // Vertical scrolling
    if (row < _scrollRowOffset) {
        _scrollRowOffset = row;
    } else if (row >= _scrollRowOffset + visRows) {
        _scrollRowOffset = row - visRows + 1;
    }

    // Horizontal scrolling
    if (col < _scrollColOffset) {
        _scrollColOffset = col;
    } else if (col >= _scrollColOffset + visCols) {
        _scrollColOffset = col - visCols + 1;
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
    int col = pos.getCol() - _scrollColOffset;

    int screenRow = _startRow + _colHeaderHeight + row;
    int screenCol = _rowHeaderWidth + (col * _defaultColWidth);

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
