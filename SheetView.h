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
class CxJSONUTFObject;      // forward declaration

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
    HIGHLIGHT_RANGE = 4,        // EDIT mode range selection (light blue)
    HIGHLIGHT_FORMULA_REF = 5   // formula reference highlight (yellow)
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

    void updateScreenForColumnChange(void);
    // optimized redraw for column insert/delete - skips row numbers

    void updateCells(CxSList<CxSheetCellCoordinate> cells);
    // redraw only the specified cells (if visible)
    // used for optimized updates after data changes

    void updateCursorMove(CxSheetCellCoordinate oldPos, CxSheetCellCoordinate newPos);
    // optimized update for cursor movement - handles scrolling if needed

    int terminalInsertRow(int dataRow);
    // optimized row insert using terminal line operations
    // returns 1 if optimization was used, 0 if caller should do full redraw

    int terminalDeleteRow(int dataRow);
    // optimized row delete using terminal line operations
    // returns 1 if optimization was used, 0 if caller should do full redraw

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

    void setFormulaRefHighlight(int active, CxSList<CxSheetCellCoordinate> cells);
    // enable/disable formula reference highlighting with list of cells to highlight

    int isFormulaRefHighlightActive(void);
    // check if formula ref highlighting is currently active

    void setFilePath(CxString path);
    // set the file path for status line display

    void updateStatusLine(void);
    // update the status/divider line (shows filename and cell position)

    void loadColumnWidthsFromAppData(CxJSONUTFObject* appData);
    // load column widths from app data (after sheet load)

    void saveColumnWidthsToAppData(CxJSONUTFObject* appData);
    // save column widths to app data (before sheet save)

    void shiftColumnWidths(int col, int direction);
    // shift column widths for insert (+1) or delete (-1) column operations

    void shiftColumnFormats(int col, int direction);
    // shift column format defaults for insert (+1) or delete (-1) column operations

    // Hidden row/column support
    int isColumnHidden(int col);
    void setColumnHidden(int col, int hidden);
    int isRowHidden(int row);
    void setRowHidden(int row, int hidden);
    void showAllRows(void);
    void showAllColumns(void);
    int nextVisibleRow(int row, int direction);
    // find the next visible row in the given direction (+1 or -1), returns row if visible
    int nextVisibleCol(int col, int direction);
    // find the next visible column in the given direction (+1 or -1), returns col if visible

    void shiftHiddenRows(int row, int direction);
    // shift hidden row indices for insert (+1) or delete (-1) row operations

    void updateVisibleTextmapCells(void);
    // redraw visible cells that have textmap rules (display-layer dependencies)

    // Freeze panes support
    void setFreeze(int freezeRow, int freezeCol);
    // set freeze pane counts (0 = no freeze)
    void getFreeze(int *freezeRow, int *freezeCol);
    // get current freeze pane counts

    CxString formatNumber(double value, int col, CxSheetCell *cell);
    // format number with currency, decimals, percent, thousands based on column/cell attributes

    // Column format default getters/setters
    int getColAlign(int col);              // 0=unset, 1=left, 2=center, 3=right
    void setColAlign(int col, int align);
    int getColDecimalPlaces(int col);      // -1=unset
    void setColDecimalPlaces(int col, int places);
    int getColCurrency(int col);           // 0=unset, 1=on, 2=off
    void setColCurrency(int col, int val);
    int getColPercent(int col);            // 0=unset, 1=on, 2=off
    void setColPercent(int col, int val);
    int getColThousands(int col);          // 0=unset, 1=on, 2=off
    void setColThousands(int col, int val);

    // Cascaded attribute getters (cell overrides column, column overrides type-default)
    CxString getEffectiveAlign(int col, CxSheetCell *cell);
    int getEffectiveCurrency(int col, CxSheetCell *cell);
    int getEffectivePercent(int col, CxSheetCell *cell);
    int getEffectiveThousands(int col, CxSheetCell *cell);
    int getEffectiveDecimalPlaces(int col, CxSheetCell *cell);  // -1 = auto

    // Column color getters/setters (color string like "RGB:255,0,0" or "" for none)
    CxString getColFgColorString(int col);
    void setColFgColor(int col, CxString colorStr);
    CxString getColBgColorString(int col);
    void setColBgColor(int col, CxString colorStr);

    // Cascaded color getters (cell overrides column) - returns color string or ""
    CxString getEffectiveFgColor(int col, CxSheetCell *cell);
    CxString getEffectiveBgColor(int col, CxSheetCell *cell);

    // Mouse coordinate translation methods
    int terminalToCell(int termRow, int termCol, int *dataRow, int *dataCol);
    // Convert terminal coordinates to cell coordinates
    // Returns 1 if valid cell, 0 if outside data area (header, divider, etc.)

    int scrollViewport(int rowDelta, int colDelta);
    // Scroll the viewport by the given number of rows/columns
    // Returns 1 if scrolling occurred, 0 if already at boundary

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

    // Column-level formatting defaults (cell attributes override these)
    // Values: 0 = unset (use type-based default)
    int _colAlign[MAX_COLUMNS];          // 0=unset, 1=left, 2=center, 3=right
    int _colDecimalPlaces[MAX_COLUMNS];  // -1=unset, 0-10 = fixed decimal places
    int _colCurrency[MAX_COLUMNS];       // 0=unset, 1=on, 2=off
    int _colPercent[MAX_COLUMNS];        // 0=unset, 1=on, 2=off
    int _colThousands[MAX_COLUMNS];      // 0=unset, 1=on, 2=off
    CxString _colFgColor[MAX_COLUMNS];   // "" = unset (terminal default), else color string
    CxString _colBgColor[MAX_COLUMNS];   // "" = unset (terminal default), else color string
    int _colHidden[MAX_COLUMNS];         // 0 = visible, 1 = hidden

    // Hidden rows (sparse - stored as sorted list since rows are unbounded)
    static const int MAX_HIDDEN_ROWS = 1000;
    int _hiddenRows[MAX_HIDDEN_ROWS];    // sorted list of hidden row indices
    int _hiddenRowCount;                 // number of entries in _hiddenRows

    int _freezeRow;         // count of frozen rows (0=none, 2=rows 0-1 frozen)
    int _freezeCol;         // count of frozen columns (0=none, 1=col A frozen)

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

    // Formula reference highlight state
    int _formulaRefHighlightActive;         // 1 if formula ref highlighting is active
    CxSList<CxSheetCellCoordinate> _formulaRefCells;  // cells referenced by formula

    // internal helpers
    void drawColumnHeaders(void);
    void drawColumnHeader(int dataCol);
    void drawRowNumbers(void);
    void drawCells(void);
    void drawRow(int screenRow, int dataRow);
    void drawCell(int screenRow, int screenCol, int dataRow, int dataCol,
                  HighlightType highlightType);

    int visibleDataRows(void);
    int visibleDataCols(void);

    void ensureCursorVisible(void);
    // adjust scroll offsets if cursor is off-screen

    void deltaScroll(int rowDelta, CxSheetCellCoordinate oldPos, CxSheetCellCoordinate newPos);
    // use terminal scroll regions for efficient vertical scrolling

    void drawRowNumber(int screenRow, int dataRow);
    // draw a single row number (used by deltaScroll)

    HighlightType getHighlightTypeForCell(int dataRow, int dataCol);
    // determine appropriate highlight type for a cell

    int isCellInHuntRange(int row, int col);
    // check if cell is within the hunt range (anchor to current)

    int isCellInSelectionRange(int row, int col);
    // check if cell is within the selection range (EDIT mode)

    CxString formatCellValue(CxSheetCell *cell, int col, int width);
    // format cell contents for display (col needed for column format defaults)

    CxString formatSymbolFill(CxString symbolType, int width);
    // format symbol fill cells (box drawing)

    CxString evaluateTextmapRule(CxString ruleStr);
    // evaluate a textmap rule string and return the matched text label

    int isCellOccupied(int dataRow, int dataCol);
    // check if a cell blocks text overflow (has content, symbolFill, or textmap)

    int getCellContentWidth(CxSheetCell *cell, int col);
    // get the untruncated display width of a cell's formatted content

    CxString getCellAlignment(CxSheetCell *cell);
    // get alignment string for a cell ("left", "right", or "center")

    int isCellTextType(CxSheetCell *cell);
    // check if cell content is text (TEXT type or textmap) — only text overflows

    int dataRowToScreenRow(int dataRow);
    // convert data row to screen row, accounting for hidden rows

    int isDataRowVisible(int dataRow);
    // check if a data row is within the visible screen area (not just not-hidden)

    // Freeze pane helpers
    int frozenRowsScreenHeight(void);
    // count of visible (non-hidden) rows in the frozen region
    int frozenColsScreenWidth(void);
    // total screen width of frozen columns (non-hidden)
    int freezeDividerRowHeight(void);
    // returns 1 if freeze row divider active, 0 otherwise
    int freezeDividerColWidth(void);
    // returns 1 if freeze col divider active, 0 otherwise
    void drawFreezeDividers(void);
    // draw the freeze pane divider lines (═ ║ ╬)
    void drawRowSegment(int screenRow, int dataRow, int colStart, int colEnd,
                        int screenXStart, int screenXEnd);
    // draw a segment of columns for one row (used by drawRow for freeze split)
};


#endif
