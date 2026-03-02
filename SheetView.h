//-------------------------------------------------------------------------------------------------
//
//  SheetView.h
//  ss (spreadsheet)
//
//  Copyright 2022-2025 Todd Vernon. All rights reserved.
//  Licensed under the Apache License, Version 2.0
//  See LICENSE file for details.
//
//  SheetView - displays the spreadsheet grid and handles cell rendering.
//
//-------------------------------------------------------------------------------------------------

#include <stdio.h>

#include <cx/base/string.h>
#include <cx/base/slist.h>
#include <cx/base/utfstring.h>
#include <cx/screen/screen.h>
#include <cx/sheetModel/sheetModel.h>
#include <cx/sheetModel/sheetCellCoordinate.h>

class SpreadsheetDefaults;  // forward declaration

#ifndef _SheetView_h_
#define _SheetView_h_


//-------------------------------------------------------------------------------------------------
//
// SheetView
//
// Displays the spreadsheet grid. Cells are rendered as rectangular regions without borders.
// The current cell is highlighted (inverse video). Column headers (A, B, C...) and row
// numbers (1, 2, 3...) are displayed. Scrolling occurs when the cursor moves off-screen.
//
//-------------------------------------------------------------------------------------------------

class SheetView {

  public:

    SheetView(CxScreen *scr, CxSheetModel *model, SpreadsheetDefaults *defaults,
              int startRow, int endRow);
    ~SheetView(void);

    void updateScreen(void);
    // redraw the entire sheet view

    void updateCells(CxSList<CxSheetCellCoordinate> cells);
    // redraw only the specified cells (if visible)
    // used for optimized updates after data changes

    void updateCursorMove(CxSheetCellCoordinate oldPos, CxSheetCellCoordinate newPos);
    // optimized update for cursor movement - handles scrolling if needed

    void recalcForResize(int startRow, int endRow);
    // recalculate layout after terminal resize

    void placeCursor(void);
    // place terminal cursor at current cell position

    int getDefaultColumnWidth(void);
    void setDefaultColumnWidth(int width);

  private:

    CxScreen *screen;
    CxSheetModel *sheetModel;
    SpreadsheetDefaults *_defaults;

    int _startRow;          // first screen row for sheet area
    int _endRow;            // last screen row for sheet area

    int _rowHeaderWidth;    // width of row number column (e.g., 4 for "999 ")
    int _colHeaderHeight;   // height of column header row (1)

    int _defaultColWidth;   // default column width in characters

    int _scrollRowOffset;   // first visible data row (0-based)
    int _scrollColOffset;   // first visible data column (0-based)

    // internal helpers
    void drawColumnHeaders(void);
    void drawRowNumbers(void);
    void drawCells(void);
    void drawCell(int screenRow, int screenCol, int dataRow, int dataCol, int isHighlighted);

    int visibleDataRows(void);
    int visibleDataCols(void);

    void ensureCursorVisible(void);
    // adjust scroll offsets if cursor is off-screen

    CxString formatCellValue(CxSheetCell *cell, int width);
    // format cell contents for display
};


#endif
