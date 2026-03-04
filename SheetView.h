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
// HighlightType - enumeration for different cell highlight states
//
//-------------------------------------------------------------------------------------------------

enum HighlightType {
    HIGHLIGHT_NONE = 0,         // no highlight (normal cell)
    HIGHLIGHT_CURSOR = 1,       // current cursor position (blue)
    HIGHLIGHT_HUNT = 2,         // cell hunt cursor (green)
    HIGHLIGHT_HUNT_RANGE = 3,   // cell hunt range fill (light green)
    HIGHLIGHT_RANGE = 4         // EDIT mode range selection (light blue)
};


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

    int getColumnWidth(int col);
    // get width for specific column (returns default if not set)

    void setColumnWidth(int col, int width);
    // set width for specific column (0 clears custom width)

    int getColumnScreenX(int col);
    // get screen X position for a column (accounting for varying widths)

    // Cell hunt mode support
    void setCellHuntMode(int active, CxSheetCellCoordinate formulaCell,
                         CxSheetCellCoordinate huntCell);
    // enable/disable cell hunt mode with formula and hunt cell positions

    void setHuntRange(int active, CxSheetCellCoordinate anchor,
                      CxSheetCellCoordinate current);
    // set range selection (after SPACE pressed)

    void updateCellHuntMove(CxSheetCellCoordinate oldPos,
                            CxSheetCellCoordinate newPos);
    // optimized redraw for cell hunt cursor movement

    // Range selection support (EDIT mode multi-cell selection)
    void setRangeSelection(int active, CxSheetCellCoordinate anchor,
                           CxSheetCellCoordinate current);
    // enable/disable range selection with anchor and current positions

    void updateRangeSelectionMove(CxSheetCellCoordinate anchor,
                                  CxSheetCellCoordinate oldCurrent,
                                  CxSheetCellCoordinate newCurrent);
    // optimized redraw for range selection extension - only redraws changed cells

    void setFilePath(CxString path);
    // set the file path for status line display

    void updateStatusLine(void);
    // update the status/divider line (shows filename and cell position)

  private:

    CxScreen *screen;
    CxSheetModel *sheetModel;
    SpreadsheetDefaults *_defaults;

    CxString _filePath;     // current file path for status line display

    int _startRow;          // first screen row for sheet area
    int _endRow;            // last screen row for sheet area

    int _rowHeaderWidth;    // width of row number column (e.g., 4 for "999 ")
    int _colHeaderHeight;   // height of column header row (1)

    int _defaultColWidth;   // default column width in characters

    static const int MAX_COLUMNS = 702;  // A-ZZ (26 + 26*26)
    int _colWidths[MAX_COLUMNS];         // per-column widths (0 = use default)

    int _scrollRowOffset;   // first visible data row (0-based)
    int _scrollColOffset;   // first visible data column (0-based)

    // Cell hunt mode state
    int _inCellHuntMode;                    // 1 if cell hunt mode is active
    int _huntRangeActive;                   // 1 if selecting a range (after SPACE)
    CxSheetCellCoordinate _huntFormulaCell; // cell where formula entry started
    CxSheetCellCoordinate _huntAnchorCell;  // range start (after SPACE pressed)
    CxSheetCellCoordinate _huntCurrentCell; // current selection / range end

    // Range selection state (EDIT mode multi-cell selection)
    int _rangeSelectActive;                 // 1 if range selection is active
    CxSheetCellCoordinate _rangeAnchor;     // where selection started
    CxSheetCellCoordinate _rangeCurrent;    // current end of selection

    // internal helpers
    void drawColumnHeaders(void);
    void drawRowNumbers(void);
    void drawCells(void);
    void drawCell(int screenRow, int screenCol, int dataRow, int dataCol,
                  HighlightType highlightType);

    int visibleDataRows(void);
    int visibleDataCols(void);

    void ensureCursorVisible(void);
    // adjust scroll offsets if cursor is off-screen

    HighlightType getHighlightTypeForCell(int dataRow, int dataCol);
    // determine appropriate highlight type for a cell

    int isCellInHuntRange(int row, int col);
    // check if cell is within the hunt range (anchor to current)

    int isCellInSelectionRange(int row, int col);
    // check if cell is within the selection range (EDIT mode)

    CxString formatCellValue(CxSheetCell *cell, int width);
    // format cell contents for display

    CxString formatSymbolFill(CxString symbolType, int width);
    // format symbol fill cells (box drawing)
};


#endif
