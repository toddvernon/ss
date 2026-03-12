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
#include <math.h>

#include <cx/json/json_utf_array.h>
#include <cx/json/json_utf_object.h>
#include <cx/json/json_utf_member.h>
#include <cx/json/json_utf_number.h>
#include <cx/json/json_utf_string.h>
#include <cx/json/json_utf_boolean.h>
#include <cx/sheetModel/sheetInputParser.h>

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
, _freezeRow(0)
, _freezeCol(0)
, _scrollRowOffset(0)
, _scrollColOffset(0)
, _inCellHuntMode(0)
, _huntRangeActive(0)
, _rangeSelectActive(0)
, _formulaRefHighlightActive(0)
{
    // Initialize all column widths to 0 (meaning use default)
    for (int i = 0; i < MAX_COLUMNS; i++) {
        _colWidths[i] = 0;
    }

    // Initialize column format defaults to unset
    for (int i = 0; i < MAX_COLUMNS; i++) {
        _colAlign[i] = 0;           // 0 = unset
        _colDecimalPlaces[i] = -1;  // -1 = unset
        _colCurrency[i] = 0;        // 0 = unset
        _colPercent[i] = 0;         // 0 = unset
        _colThousands[i] = 0;       // 0 = unset
        _colFgColor[i] = "";        // "" = unset (terminal default)
        _colBgColor[i] = "";        // "" = unset (terminal default)
        _colHidden[i] = 0;          // 0 = visible
    }

    // Initialize hidden rows list
    _hiddenRowCount = 0;
    for (int i = 0; i < MAX_HIDDEN_ROWS; i++) {
        _hiddenRows[i] = -1;
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
    CxScreen::beginSyncUpdate();

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
    drawFreezeDividers();

    // Draw status/divider line at bottom of sheet area
    updateStatusLine();

    CxScreen::endSyncUpdate();
}


//-------------------------------------------------------------------------------------------------
// SheetView::updateScreenForColumnChange
//
// Optimized redraw for column insert/delete operations.
// Skips row number redrawing since row numbers don't change for column operations.
// Clears only from the row header width onwards to preserve existing row numbers.
//-------------------------------------------------------------------------------------------------
void
SheetView::updateScreenForColumnChange(void)
{
    CxScreen::beginSyncUpdate();

    // Ensure cursor is visible before drawing
    ensureCursorVisible();

    // Clear only from row header width onwards (preserve row numbers)
    for (int r = _startRow; r <= _endRow; r++) {
        CxScreen::placeCursor(r, _rowHeaderWidth);
        CxScreen::clearScreenFromCursorToEndOfLine();
    }

    // Draw components (skip row numbers - they don't change for column ops)
    drawColumnHeaders();
    drawCells();
    drawFreezeDividers();

    // Draw status/divider line at bottom of sheet area
    updateStatusLine();

    CxScreen::endSyncUpdate();
}


//-------------------------------------------------------------------------------------------------
// SheetView::updateCells
//
// Redraw affected rows for the specified cells. Redraws entire rows to correctly
// handle text overflow (a cell change may affect overflow from/into neighbors).
//-------------------------------------------------------------------------------------------------
void
SheetView::updateCells(CxSList<CxSheetCellCoordinate> cells)
{
    int visRows = visibleDataRows();

    // Collect unique visible rows that need redrawing
    static const int MAX_ROWS = 128;
    int affectedRows[MAX_ROWS];
    int numAffectedRows = 0;

    for (int i = 0; i < (int)cells.entries(); i++) {
        int dataRow = cells.at(i).getRow();

        // Skip hidden rows
        if (isRowHidden(dataRow)) continue;

        // Check if row is visible on screen
        if (!isDataRowVisible(dataRow)) continue;

        // Check if already in the affected list
        int found = 0;
        for (int j = 0; j < numAffectedRows; j++) {
            if (affectedRows[j] == dataRow) {
                found = 1;
                break;
            }
        }
        if (!found && numAffectedRows < MAX_ROWS) {
            affectedRows[numAffectedRows] = dataRow;
            numAffectedRows++;
        }
    }

    // Redraw each affected row
    for (int i = 0; i < numAffectedRows; i++) {
        int dataRow = affectedRows[i];
        int screenRow = dataRowToScreenRow(dataRow);
        if (screenRow < 0) continue;

        if (_freezeRow > 0 || _freezeCol > 0) {
            // Use segment-based drawing to respect freeze boundaries
            int screenWidth = screen->cols();
            int frozenWidth = frozenColsScreenWidth();
            int divColW = freezeDividerColWidth();
            int scrollableColStart = _rowHeaderWidth + frozenWidth + divColW;

            if (_freezeCol > 0) {
                drawRowSegment(screenRow, dataRow, 0, _freezeCol,
                               _rowHeaderWidth, _rowHeaderWidth + frozenWidth);
            }
            drawRowSegment(screenRow, dataRow, _scrollColOffset, MAX_COLUMNS,
                           scrollableColStart, screenWidth);
        } else {
            drawRow(screenRow, dataRow);
        }
    }
}


//-------------------------------------------------------------------------------------------------
// SheetView::updateVisibleTextmapCells
//
// Scan visible cells for textmap appAttributes and redraw their rows.
// Textmap cells depend on other cells for their display text but aren't in
// sheetModel's dependency graph, so they need explicit redrawing after data changes.
// Redraws entire rows to correctly handle text overflow.
//-------------------------------------------------------------------------------------------------
void
SheetView::updateVisibleTextmapCells(void)
{
    int screenWidth = screen->cols();
    int redrew = 0;

    // Helper: check if a row has textmap cells and redraw if so
    // Check frozen rows first, then scrollable rows
    // For frozen rows:
    if (_freezeRow > 0) {
        int screenRow = _startRow + _colHeaderHeight;
        for (int dr = 0; dr < _freezeRow; dr++) {
            if (isRowHidden(dr)) continue;
            int rowHasTextmap = 0;
            int screenX = _rowHeaderWidth;
            for (int c = 0; screenX < screenWidth; c++) {
                if (c >= MAX_COLUMNS) break;
                if (_colHidden[c]) continue;
                screenX += getColumnWidth(c);
                CxSheetCell cell = sheetModel->getCell(CxSheetCellCoordinate(dr, c));
                if (cell.appAttributes != NULL) {
                    CxString textmap = cell.getAppAttributeString("textmap");
                    if (textmap.length() > 0) { rowHasTextmap = 1; break; }
                }
            }
            if (rowHasTextmap) {
                drawRow(screenRow, dr);
                redrew = 1;
            }
            screenRow++;
        }
    }

    // Scrollable rows
    int visRows = visibleDataRows();
    for (int r = 0; r < visRows; r++) {
        int dataRow = _scrollRowOffset + r;
        int screenX = _rowHeaderWidth;
        int rowHasTextmap = 0;

        for (int c = _scrollColOffset; screenX < screenWidth; c++) {
            if (c >= MAX_COLUMNS) break;
            int colWidth = getColumnWidth(c);
            screenX += colWidth;

            CxSheetCell cell = sheetModel->getCell(CxSheetCellCoordinate(dataRow, c));
            if (cell.appAttributes != NULL) {
                CxString textmap = cell.getAppAttributeString("textmap");
                if (textmap.length() > 0) {
                    rowHasTextmap = 1;
                    break;
                }
            }
        }

        if (rowHasTextmap) {
            int screenRow = dataRowToScreenRow(dataRow);
            drawRow(screenRow, dataRow);
            redrew = 1;
        }
    }

    (void)redrew;
}


//-------------------------------------------------------------------------------------------------
// SheetView::updateCursorMove
//
// Optimized update for cursor movement. Checks if scrolling is needed and uses
// terminal scroll regions for small vertical scrolls, falls back to full redraw
// for large jumps or horizontal scrolling.
//-------------------------------------------------------------------------------------------------
void
SheetView::updateCursorMove(CxSheetCellCoordinate oldPos, CxSheetCellCoordinate newPos)
{
    int newRow = newPos.getRow();
    int newCol = newPos.getCol();

    int visRows = visibleDataRows();
    int visCols = visibleDataCols();

    // Check if new position is outside visible area (scrolling needed)
    int needsRowScroll = 0;
    int needsColScroll = 0;

    if (!isDataRowVisible(newRow)) {
        needsRowScroll = 1;
    }
    if (newCol < _scrollColOffset || newCol >= _scrollColOffset + visCols) {
        needsColScroll = 1;
    }

    if (needsColScroll) {
        // Horizontal scroll always requires full redraw (column headers change)
        updateScreen();
    } else if (needsRowScroll) {
        // When hidden rows or freeze panes exist, use full redraw
        if (_hiddenRowCount > 0 || _freezeRow > 0 || _freezeCol > 0) {
            updateScreen();
        } else {
            // Calculate how many rows we need to scroll
            int rowDelta = 0;
            if (newRow < _scrollRowOffset) {
                // Scrolling up - new row is above visible area
                rowDelta = newRow - _scrollRowOffset;  // negative
            } else if (newRow >= _scrollRowOffset + visRows) {
                // Scrolling down - new row is below visible area
                rowDelta = newRow - (_scrollRowOffset + visRows - 1);  // positive
            }

            // Use delta scroll for small scrolls (1-3 rows), full redraw for large jumps
            int absRowDelta = rowDelta < 0 ? -rowDelta : rowDelta;
            if (absRowDelta <= 3 && absRowDelta > 0) {
                deltaScroll(rowDelta, oldPos, newPos);
            } else {
                // Large jump - full redraw
                updateScreen();
            }
        }
    } else {
        // No scrolling needed - just redraw the two affected cells
        CxSList<CxSheetCellCoordinate> affectedCells;
        affectedCells.append(oldPos);
        if (!(oldPos == newPos)) {
            affectedCells.append(newPos);
        }
        updateCells(affectedCells);

        // Update column headers if column changed
        if (oldPos.getCol() != newPos.getCol()) {
            drawColumnHeader(oldPos.getCol());
            drawColumnHeader(newPos.getCol());
        }

        // Update row numbers if row changed
        if (oldPos.getRow() != newPos.getRow()) {
            int oldScreenRow = dataRowToScreenRow(oldPos.getRow());
            int newScreenRow = dataRowToScreenRow(newPos.getRow());
            drawRowNumber(oldScreenRow, oldPos.getRow());
            drawRowNumber(newScreenRow, newPos.getRow());
        }
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
    int cursorCol = sheetModel->getCurrentPosition().getCol();

    CxScreen::placeCursor(_startRow, 0);

    // Apply header colors
    _defaults->applyHeaderColors(screen);

    // Row header spacer
    for (int i = 0; i < _rowHeaderWidth; i++) {
        printf(" ");
    }

    int screenX = _rowHeaderWidth;
    int screenWidth = screen->cols();
    CxSheetCellCoordinate tempCoord;

    // Pass 1: Frozen columns (0 to _freezeCol-1)
    if (_freezeCol > 0) {
        for (int dataCol = 0; screenX < screenWidth && dataCol < _freezeCol; dataCol++) {
            if (_colHidden[dataCol]) continue;

            int colWidth = getColumnWidth(dataCol);
            CxString colName = tempCoord.colToLetters(dataCol);

            if (dataCol == cursorCol) {
                _defaults->applyHeaderHighlightColors(screen);
            }

            int padding = (colWidth - colName.length()) / 2;
            for (int p = 0; p < padding && screenX < screenWidth; p++) {
                printf(" "); screenX++;
            }
            for (int i = 0; i < (int)colName.length() && screenX < screenWidth; i++) {
                printf("%c", colName.data()[i]); screenX++;
            }
            int remaining = colWidth - padding - colName.length();
            for (int p = 0; p < remaining && screenX < screenWidth; p++) {
                printf(" "); screenX++;
            }

            if (dataCol == cursorCol) {
                _defaults->applyHeaderColors(screen);
            }
        }

        // Skip the divider column (drawn by drawFreezeDividers)
        if (screenX < screenWidth) {
            printf(" ");
            screenX++;
        }
    }

    // Pass 2: Scrollable columns (from _scrollColOffset)
    for (int dataCol = _scrollColOffset; screenX < screenWidth; dataCol++) {
        if (dataCol >= MAX_COLUMNS) break;
        if (_colHidden[dataCol]) continue;

        int colWidth = getColumnWidth(dataCol);
        CxString colName = tempCoord.colToLetters(dataCol);

        if (dataCol == cursorCol) {
            _defaults->applyHeaderHighlightColors(screen);
        }

        int padding = (colWidth - colName.length()) / 2;
        for (int p = 0; p < padding && screenX < screenWidth; p++) {
            printf(" "); screenX++;
        }
        for (int i = 0; i < (int)colName.length() && screenX < screenWidth; i++) {
            printf("%c", colName.data()[i]); screenX++;
        }
        int remaining = colWidth - padding - colName.length();
        for (int p = 0; p < remaining && screenX < screenWidth; p++) {
            printf(" "); screenX++;
        }

        if (dataCol == cursorCol) {
            _defaults->applyHeaderColors(screen);
        }
    }

    // Reset colors
    _defaults->resetColors(screen);
}


//-------------------------------------------------------------------------------------------------
// SheetView::drawColumnHeader
//
// Draw a single column header at its position (used for incremental updates).
//-------------------------------------------------------------------------------------------------
void
SheetView::drawColumnHeader(int dataCol)
{
    // Skip hidden columns
    if (dataCol >= 0 && dataCol < MAX_COLUMNS && _colHidden[dataCol]) return;

    int cursorCol = sheetModel->getCurrentPosition().getCol();
    int screenWidth = screen->cols();
    CxSheetCellCoordinate tempCoord;

    // Calculate screen X using freeze-aware getColumnScreenX
    int screenX = getColumnScreenX(dataCol);

    // Off-screen check
    if (screenX >= screenWidth) return;

    int colWidth = getColumnWidth(dataCol);
    CxString colName = tempCoord.colToLetters(dataCol);

    CxScreen::placeCursor(_startRow, screenX);

    if (dataCol == cursorCol) {
        _defaults->applyHeaderHighlightColors(screen);
    } else {
        _defaults->applyHeaderColors(screen);
    }

    int padding = (colWidth - colName.length()) / 2;
    for (int p = 0; p < padding && screenX < screenWidth; p++) {
        printf(" ");
        screenX++;
    }
    for (int i = 0; i < (int)colName.length() && screenX < screenWidth; i++) {
        printf("%c", colName.data()[i]);
        screenX++;
    }
    int remaining = colWidth - padding - colName.length();
    for (int p = 0; p < remaining && screenX < screenWidth; p++) {
        printf(" ");
        screenX++;
    }

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
    int cursorRow = sheetModel->getCurrentPosition().getRow();
    int screenRow = _startRow + _colHeaderHeight;

    // Pass 1: Frozen rows (0 to _freezeRow-1)
    if (_freezeRow > 0) {
        for (int dataRow = 0; dataRow < _freezeRow; dataRow++) {
            if (isRowHidden(dataRow)) continue;

            CxScreen::placeCursor(screenRow, 0);

            if (dataRow == cursorRow) {
                _defaults->applyHeaderHighlightColors(screen);
            } else {
                _defaults->applyHeaderColors(screen);
            }

            char rowNum[16];
            snprintf(rowNum, sizeof(rowNum), "%4d ", dataRow + 1);
            printf("%s", rowNum);

            screenRow++;
        }

        // Skip divider row
        screenRow += freezeDividerRowHeight();
    }

    // Pass 2: Scrollable rows
    int numRows = visibleDataRows();
    int dataRow = _scrollRowOffset;
    int drawnRows = 0;

    while (drawnRows < numRows) {
        if (isRowHidden(dataRow)) {
            dataRow++;
            continue;
        }

        CxScreen::placeCursor(screenRow, 0);

        if (dataRow == cursorRow) {
            _defaults->applyHeaderHighlightColors(screen);
        } else {
            _defaults->applyHeaderColors(screen);
        }

        char rowNum[16];
        snprintf(rowNum, sizeof(rowNum), "%4d ", dataRow + 1);
        printf("%s", rowNum);

        screenRow++;
        dataRow++;
        drawnRows++;
    }

    // Reset colors
    _defaults->resetColors(screen);
}


//-------------------------------------------------------------------------------------------------
// SheetView::drawRowNumber
//
// Draw a single row number at the specified screen row.
//-------------------------------------------------------------------------------------------------
void
SheetView::drawRowNumber(int screenRow, int dataRow)
{
    int cursorRow = sheetModel->getCurrentPosition().getRow();

    if (dataRow == cursorRow) {
        _defaults->applyHeaderHighlightColors(screen);
    } else {
        _defaults->applyHeaderColors(screen);
    }

    CxScreen::placeCursor(screenRow, 0);

    char rowNum[16];
    snprintf(rowNum, sizeof(rowNum), "%4d ", dataRow + 1);
    printf("%s", rowNum);

    _defaults->resetColors(screen);
}


//-------------------------------------------------------------------------------------------------
// SheetView::deltaScroll
//
// Use terminal scroll regions for efficient vertical scrolling.
// Only redraws the new rows that appear at the edge, rather than the entire screen.
//-------------------------------------------------------------------------------------------------
void
SheetView::deltaScroll(int rowDelta, CxSheetCellCoordinate oldPos, CxSheetCellCoordinate newPos)
{
    // Freeze active - fall back to full redraw (scroll regions get complex)
    if (_freezeRow > 0 || _freezeCol > 0) {
        updateScreen();
        return;
    }

    CxScreen::beginSyncUpdate();

    // Calculate the data area boundaries (excluding column header and status line)
    int dataStartRow = _startRow + _colHeaderHeight;
    int dataEndRow = _endRow - 1;  // Exclude status line
    int visRows = visibleDataRows();

    // Set scroll region to data area only
    CxScreen::setScrollRegion(dataStartRow, dataEndRow);

    if (rowDelta > 0) {
        // Scrolling down - content moves up, new rows appear at bottom
        CxScreen::scrollUp(rowDelta);
        _scrollRowOffset += rowDelta;

        // Redraw the new rows at the bottom
        for (int i = 0; i < rowDelta; i++) {
            int screenRow = dataEndRow - rowDelta + 1 + i;
            int dataRow = _scrollRowOffset + visRows - rowDelta + i;
            drawRowNumber(screenRow, dataRow);
            drawRow(screenRow, dataRow);
        }
    } else if (rowDelta < 0) {
        // Scrolling up - content moves down, new rows appear at top
        int scrollAmount = -rowDelta;
        CxScreen::scrollDown(scrollAmount);
        _scrollRowOffset += rowDelta;  // rowDelta is negative

        // Redraw the new rows at the top
        for (int i = 0; i < scrollAmount; i++) {
            int screenRow = dataStartRow + i;
            int dataRow = _scrollRowOffset + i;
            drawRowNumber(screenRow, dataRow);
            drawRow(screenRow, dataRow);
        }
    }

    // Reset scroll region to full screen
    CxScreen::resetScrollRegion();

    // Redraw all row numbers (highlight moved with scroll, need to fix)
    drawRowNumbers();

    // Redraw column headers (cursor column highlight may need updating)
    drawColumnHeaders();

    // Redraw cursor cells (old and new positions)
    CxSList<CxSheetCellCoordinate> cells;
    cells.append(oldPos);
    if (!(oldPos == newPos)) {
        cells.append(newPos);
    }
    updateCells(cells);

    // Update status line to reflect new position
    updateStatusLine();

    CxScreen::endSyncUpdate();
}


//-------------------------------------------------------------------------------------------------
// SheetView::terminalInsertRow
//
// Optimized row insert using terminal line operations.
// Uses insertLines() to push content down, then redraws only affected elements:
//   - Row numbers from inserted row to bottom (they shifted incorrectly)
//   - The new empty row's cells
//
// Returns 1 if optimization was used, 0 if caller should do full redraw.
//-------------------------------------------------------------------------------------------------
int
SheetView::terminalInsertRow(int dataRow)
{
    // Freeze active - fall back to full redraw
    if (_freezeRow > 0 || _freezeCol > 0) return 0;

    int visRows = visibleDataRows();

    // Check if inserted row is visible
    if (dataRow < _scrollRowOffset || dataRow >= _scrollRowOffset + visRows) {
        return 0;  // Not visible, caller should do full redraw
    }

    // Calculate screen coordinates
    int dataStartRow = _startRow + _colHeaderHeight;
    int dataEndRow = _endRow - 1;  // Exclude status line
    int screenRow = dataStartRow + (dataRow - _scrollRowOffset);

    CxScreen::beginSyncUpdate();

    // Set scroll region to data area
    CxScreen::setScrollRegion(dataStartRow, dataEndRow);

    // Position cursor at the row where we're inserting
    CxScreen::placeCursor(screenRow, 0);

    // Insert one blank line - pushes all content from here down
    CxScreen::insertLines(1);

    // Reset scroll region
    CxScreen::resetScrollRegion();

    // Redraw row numbers from inserted row to visible bottom
    // (they shifted with content but the numbers are now wrong)
    for (int r = dataRow; r < _scrollRowOffset + visRows; r++) {
        int sr = dataStartRow + (r - _scrollRowOffset);
        drawRowNumber(sr, r);
    }

    // Draw the new empty row's cells
    drawRow(screenRow, dataRow);

    // Update status line
    updateStatusLine();

    CxScreen::endSyncUpdate();

    return 1;  // Optimization was used
}


//-------------------------------------------------------------------------------------------------
// SheetView::terminalDeleteRow
//
// Optimized row delete using terminal line operations.
// Uses deleteLines() to pull content up, then redraws only affected elements:
//   - Row numbers from deleted row to bottom (they shifted incorrectly)
//   - The bottom row (new content scrolled into view)
//
// Returns 1 if optimization was used, 0 if caller should do full redraw.
//-------------------------------------------------------------------------------------------------
int
SheetView::terminalDeleteRow(int dataRow)
{
    // Freeze active - fall back to full redraw
    if (_freezeRow > 0 || _freezeCol > 0) return 0;

    int visRows = visibleDataRows();

    // Check if deleted row is visible
    if (dataRow < _scrollRowOffset || dataRow >= _scrollRowOffset + visRows) {
        return 0;  // Not visible, caller should do full redraw
    }

    // Calculate screen coordinates
    int dataStartRow = _startRow + _colHeaderHeight;
    int dataEndRow = _endRow - 1;  // Exclude status line
    int screenRow = dataStartRow + (dataRow - _scrollRowOffset);

    CxScreen::beginSyncUpdate();

    // Set scroll region to data area
    CxScreen::setScrollRegion(dataStartRow, dataEndRow);

    // Position cursor at the row being deleted
    CxScreen::placeCursor(screenRow, 0);

    // Delete one line - pulls all content below up
    CxScreen::deleteLines(1);

    // Reset scroll region
    CxScreen::resetScrollRegion();

    // Redraw row numbers from deleted row to visible bottom
    // (they shifted with content but the numbers are now wrong)
    for (int r = dataRow; r < _scrollRowOffset + visRows; r++) {
        int sr = dataStartRow + (r - _scrollRowOffset);
        drawRowNumber(sr, r);
    }

    // Draw the bottom row (new content scrolled into view from below)
    int bottomDataRow = _scrollRowOffset + visRows - 1;
    int bottomScreenRow = dataEndRow;
    drawRow(bottomScreenRow, bottomDataRow);

    // Update status line
    updateStatusLine();

    CxScreen::endSyncUpdate();

    return 1;  // Optimization was used
}


//-------------------------------------------------------------------------------------------------
// SheetView::drawCells
//
// Draw all visible cells with text overflow support via drawRow().
//-------------------------------------------------------------------------------------------------
void
SheetView::drawCells(void)
{
    int screenWidth = screen->cols();
    int screenRow;

    // When freeze is active, use drawRowSegment for four-quadrant rendering
    if (_freezeRow > 0 || _freezeCol > 0) {
        int frozenWidth = frozenColsScreenWidth();
        int divColW = freezeDividerColWidth();
        int scrollableColStart = _rowHeaderWidth + frozenWidth + divColW;

        // Q1 + Q2: Frozen rows
        if (_freezeRow > 0) {
            screenRow = _startRow + _colHeaderHeight;
            for (int dr = 0; dr < _freezeRow; dr++) {
                if (isRowHidden(dr)) continue;

                // Q1: frozen rows × frozen cols
                if (_freezeCol > 0) {
                    drawRowSegment(screenRow, dr, 0, _freezeCol,
                                   _rowHeaderWidth, _rowHeaderWidth + frozenWidth);
                }
                // Q2: frozen rows × scrollable cols
                drawRowSegment(screenRow, dr, _scrollColOffset, MAX_COLUMNS,
                               scrollableColStart, screenWidth);
                screenRow++;
            }
        }

        // Skip divider row
        screenRow = _startRow + _colHeaderHeight + frozenRowsScreenHeight()
                    + freezeDividerRowHeight();

        // Q3 + Q4: Scrollable rows
        int numRows = visibleDataRows();
        int dataRow = _scrollRowOffset;
        int drawnRows = 0;

        while (drawnRows < numRows) {
            if (isRowHidden(dataRow)) {
                dataRow++;
                continue;
            }

            // Q3: scrollable rows × frozen cols
            if (_freezeCol > 0) {
                drawRowSegment(screenRow, dataRow, 0, _freezeCol,
                               _rowHeaderWidth, _rowHeaderWidth + frozenWidth);
            }
            // Q4: scrollable rows × scrollable cols
            drawRowSegment(screenRow, dataRow, _scrollColOffset, MAX_COLUMNS,
                           scrollableColStart, screenWidth);

            screenRow++;
            dataRow++;
            drawnRows++;
        }
    } else {
        // No freeze - original path
        int numRows = visibleDataRows();
        screenRow = _startRow + _colHeaderHeight;
        int dataRow = _scrollRowOffset;
        int drawnRows = 0;

        while (drawnRows < numRows) {
            if (isRowHidden(dataRow)) {
                dataRow++;
                continue;
            }
            drawRow(screenRow, dataRow);
            screenRow++;
            dataRow++;
            drawnRows++;
        }
    }
}


//-------------------------------------------------------------------------------------------------
// SheetView::drawRow
//
// Draw a single row of visible cells with text overflow support.
//
// Two-pass approach:
// Pass 1 (left-to-right): Left-aligned text cells claim rightward overflow into
//   unoccupied, unhighlighted neighbor cells.
// Pass 2 (right-to-left): Right-aligned text cells claim leftward overflow into
//   unclaimed, unoccupied, unhighlighted neighbor cells.
// Then draw each cell using its effective width (own width + claimed neighbors).
//-------------------------------------------------------------------------------------------------
void
SheetView::drawRow(int screenRow, int dataRow)
{
    int screenWidth = screen->cols();

    // Max visible columns we'll track per row
    static const int MAX_VIS_COLS = 128;

    // Collect visible column info for this row
    int dataCols[MAX_VIS_COLS];
    int screenXs[MAX_VIS_COLS];
    int colWidths[MAX_VIS_COLS];
    int numCols = 0;

    int sx = _rowHeaderWidth;
    for (int dc = _scrollColOffset; sx < screenWidth && numCols < MAX_VIS_COLS; dc++) {
        if (dc >= MAX_COLUMNS) break;
        if (_colHidden[dc]) continue;
        dataCols[numCols] = dc;
        screenXs[numCols] = sx;
        colWidths[numCols] = getColumnWidth(dc);
        numCols++;
        sx += getColumnWidth(dc);
    }

    if (numCols == 0) {
        return;
    }

    // effectiveWidth[i] = total screen columns this cell will draw
    // claimedBy[i] = index of the cell that claimed this slot (-1 = self)
    int effectiveWidth[MAX_VIS_COLS];
    int claimedBy[MAX_VIS_COLS];
    for (int i = 0; i < numCols; i++) {
        effectiveWidth[i] = colWidths[i];
        claimedBy[i] = -1;
    }

    // Pass 1: left-aligned text overflow (left-to-right)
    for (int i = 0; i < numCols; i++) {
        CxSheetCell *cell = sheetModel->getCellPtr(
            CxSheetCellCoordinate(dataRow, dataCols[i]));
        if (cell == NULL || !isCellTextType(cell)) {
            continue;
        }
        CxString align = getCellAlignment(cell);
        if (align != "left") {
            continue;
        }
        int contentWidth = getCellContentWidth(cell, dataCols[i]);
        if (contentWidth <= colWidths[i]) {
            continue;  // fits in cell, no overflow needed
        }

        // Try to claim columns to the right
        int totalWidth = colWidths[i];
        for (int j = i + 1; j < numCols && totalWidth < contentWidth; j++) {
            if (claimedBy[j] != -1) {
                break;  // already claimed by another overflow
            }
            if (isCellOccupied(dataRow, dataCols[j])) {
                break;  // occupied cell stops overflow
            }
            HighlightType ht = getHighlightTypeForCell(dataRow, dataCols[j]);
            if (ht != HIGHLIGHT_NONE) {
                break;  // highlighted cell stops overflow (cursor reveals boundary)
            }
            claimedBy[j] = i;
            totalWidth += colWidths[j];
        }
        effectiveWidth[i] = totalWidth;
    }

    // Pass 2: right-aligned text overflow (right-to-left)
    for (int i = numCols - 1; i >= 0; i--) {
        CxSheetCell *cell = sheetModel->getCellPtr(
            CxSheetCellCoordinate(dataRow, dataCols[i]));
        if (cell == NULL || !isCellTextType(cell)) {
            continue;
        }
        CxString align = getCellAlignment(cell);
        if (align != "right") {
            continue;
        }
        int contentWidth = getCellContentWidth(cell, dataCols[i]);
        if (contentWidth <= colWidths[i]) {
            continue;
        }

        // Try to claim columns to the left
        int totalWidth = colWidths[i];
        for (int j = i - 1; j >= 0 && totalWidth < contentWidth; j--) {
            if (claimedBy[j] != -1) {
                break;
            }
            if (isCellOccupied(dataRow, dataCols[j])) {
                break;
            }
            HighlightType ht = getHighlightTypeForCell(dataRow, dataCols[j]);
            if (ht != HIGHLIGHT_NONE) {
                break;
            }
            claimedBy[j] = i;
            totalWidth += colWidths[j];
        }
        effectiveWidth[i] = totalWidth;
    }

    // Draw pass: render each cell
    for (int i = 0; i < numCols; i++) {
        if (claimedBy[i] != -1) {
            continue;  // this cell's space is consumed by an overflowing neighbor
        }

        // Clip effective width to screen edge
        int drawWidth = effectiveWidth[i];
        int availableWidth = screenWidth - screenXs[i];
        if (drawWidth > availableWidth) {
            drawWidth = availableWidth;
        }

        // For right-aligned overflow, the draw starts at an earlier screen column
        int drawScreenCol = screenXs[i];
        CxSheetCell *cell = sheetModel->getCellPtr(
            CxSheetCellCoordinate(dataRow, dataCols[i]));
        if (drawWidth > colWidths[i] && cell != NULL) {
            CxString align = getCellAlignment(cell);
            if (align == "right") {
                // Shift draw start leftward by the extra claimed width
                drawScreenCol = screenXs[i] - (drawWidth - colWidths[i]);
                // Clip to screen edge on left
                int availLeft = screenWidth - drawScreenCol;
                if (availLeft < drawWidth) {
                    drawWidth = availLeft;
                }
            }
        }

        HighlightType ht = getHighlightTypeForCell(dataRow, dataCols[i]);

        CxScreen::placeCursor(screenRow, drawScreenCol);
        CxSheetCellCoordinate coord(dataRow, dataCols[i]);

        CxString content = formatCellValue(
            sheetModel->getCellPtr(coord), dataCols[i], drawWidth);

        switch (ht) {
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
            case HIGHLIGHT_FORMULA_REF:
                _defaults->applyFormulaRefColors(screen);
                printf("%s", content.data());
                _defaults->resetColors(screen);
                break;
            case HIGHLIGHT_NONE:
            default:
            {
                // Apply cell/column colors (cascade: cell > column > default)
                CxSheetCell *cellPtr = sheetModel->getCellPtr(coord);
                CxString fgColorStr = getEffectiveFgColor(dataCols[i], cellPtr);
                CxString bgColorStr = getEffectiveBgColor(dataCols[i], cellPtr);

                // Apply foreground color if set
                if (fgColorStr.length() > 0 && fgColorStr.index("NONE") < 0) {
                    CxColor *fgColor = SpreadsheetDefaults::parseColor(fgColorStr, 0);
                    if (fgColor != NULL) {
                        screen->setForegroundColor(fgColor);
                        delete fgColor;
                    }
                } else {
                    _defaults->applyCellColors(screen);
                }

                // Apply background color if set
                if (bgColorStr.length() > 0 && bgColorStr.index("NONE") < 0) {
                    CxColor *bgColor = SpreadsheetDefaults::parseColor(bgColorStr, 1);
                    if (bgColor != NULL) {
                        screen->setBackgroundColor(bgColor);
                        delete bgColor;
                    }
                }

                printf("%s", content.data());
                _defaults->resetColors(screen);
            }
            break;
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
    int screenWidth = screen->cols();

    // Clip to available screen width to prevent wrapping onto next line
    int availableWidth = screenWidth - screenCol;
    if (availableWidth < colWidth) {
        colWidth = availableWidth;
    }

    CxString content = formatCellValue(cell, dataCol, colWidth);

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

        case HIGHLIGHT_FORMULA_REF:
            _defaults->applyFormulaRefColors(screen);
            printf("%s", content.data());
            _defaults->resetColors(screen);
            break;

        case HIGHLIGHT_NONE:
        default:
        {
            // Apply cell/column colors (cascade: cell > column > default)
            CxString fgColorStr = getEffectiveFgColor(dataCol, cell);
            CxString bgColorStr = getEffectiveBgColor(dataCol, cell);

            // Apply foreground color if set
            if (fgColorStr.length() > 0 && fgColorStr.index("NONE") < 0) {
                CxColor *fgColor = SpreadsheetDefaults::parseColor(fgColorStr, 0);
                if (fgColor != NULL) {
                    screen->setForegroundColor(fgColor);
                    delete fgColor;
                }
            } else {
                _defaults->applyCellColors(screen);
            }

            // Apply background color if set
            if (bgColorStr.length() > 0 && bgColorStr.index("NONE") < 0) {
                CxColor *bgColor = SpreadsheetDefaults::parseColor(bgColorStr, 1);
                if (bgColor != NULL) {
                    screen->setBackgroundColor(bgColor);
                    delete bgColor;
                }
            }

            printf("%s", content.data());
            _defaults->resetColors(screen);
        }
        break;
    }
}


//-------------------------------------------------------------------------------------------------
// SheetView::formatNumber
//
// Format a numeric value based on column defaults and cell attributes:
//   - "currency" (bool): 2 decimal places, negatives in parentheses
//   - "decimalPlaces" (int): fixed decimal places (default: auto)
//   - "percent" (bool): multiply by 100 and suffix with %
//   - "thousands" (bool): add comma separators, at least 2 decimals if fractional
// Uses cascaded getters: cell attribute > column default > type-based default.
//-------------------------------------------------------------------------------------------------
CxString
SheetView::formatNumber(double value, int col, CxSheetCell *cell)
{
    int isCurrency = getEffectiveCurrency(col, cell);
    int isPercent = getEffectivePercent(col, cell);
    int hasThousands = getEffectiveThousands(col, cell);
    int decimalPlaces = getEffectiveDecimalPlaces(col, cell);
    int hasDecimalPlaces = (decimalPlaces >= 0) ? 1 : 0;
    if (decimalPlaces < 0) decimalPlaces = 2;  // default for calculations below

    // Track if value is negative (for currency parentheses)
    int isNegative = (value < 0) ? 1 : 0;
    double absValue = isNegative ? -value : value;

    // Apply percent transformation first
    if (isPercent) {
        absValue = absValue * 100.0;
    }

    // Determine decimal places
    int useDecimalPlaces = decimalPlaces;
    if (isCurrency && !hasDecimalPlaces) {
        // Currency defaults to 0 decimal places if not explicitly set
        useDecimalPlaces = 0;
    } else if (hasThousands && !hasDecimalPlaces) {
        // Thousands with fractional part: ensure at least 2 decimal places
        double intPart;
        double fracPart = modf(absValue, &intPart);
        if (fracPart > 0.00001) {  // Has a fractional component
            useDecimalPlaces = 2;
        }
    }

    // Format the number
    char buf[128];
    if (hasDecimalPlaces || isCurrency || (hasThousands && useDecimalPlaces == 2)) {
        // Fixed decimal places
        snprintf(buf, sizeof(buf), "%.*f", useDecimalPlaces, absValue);
    } else {
        // Auto format (use %g for compact representation)
        snprintf(buf, sizeof(buf), "%g", absValue);
    }

    CxString numStr(buf);

    // Add thousands separators if requested (or always for currency)
    if (hasThousands || isCurrency) {
        // Find the decimal point (if any)
        int decimalPos = -1;
        for (int i = 0; i < (int)numStr.length(); i++) {
            if (numStr.data()[i] == '.') {
                decimalPos = i;
                break;
            }
        }

        // Get integer part
        CxString intPart = (decimalPos >= 0) ? numStr.subString(0, decimalPos) : numStr;
        CxString fracPart = (decimalPos >= 0) ? numStr.subString(decimalPos, numStr.length() - decimalPos) : "";

        // Insert commas every 3 digits from the right
        CxString withCommas = "";
        int len = intPart.length();
        for (int i = 0; i < len; i++) {
            if (i > 0 && (len - i) % 3 == 0) {
                withCommas = withCommas + ",";
            }
            withCommas = withCommas + CxString(intPart.data()[i]);
        }

        numStr = withCommas + fracPart;
    }

    // Build final result
    CxString result;

    if (isNegative && !isCurrency) {
        // Non-currency: standard minus sign
        result = CxString("-") + numStr;
    } else {
        result = numStr;
    }

    if (isPercent) {
        result = result + CxString("%");
    }

    return result;
}


//-------------------------------------------------------------------------------------------------
// SheetView::formatCellValue
//
// Format cell contents for display, truncating or padding as needed.
// Uses CxUTFString for proper UTF-8 character handling.
// Handles symbol fill cells (box drawing) via appAttribute "symbolFill".
// The col parameter is used for column format defaults (cascaded attributes).
//-------------------------------------------------------------------------------------------------
CxString
SheetView::formatCellValue(CxSheetCell *cell, int col, int width)
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

    // Check for textmap cells (display-layer feature)
    if (cell->hasAppAttribute("textmap")) {
        CxString ruleStr = cell->getAppAttributeString("textmap");
        CxString rawText = evaluateTextmapRule(ruleStr);
        if (rawText.length() == 0) {
            // No match - fill with spaces
            for (int i = 0; i < width; i++) {
                result = result + " ";
            }
            return result;
        }
        // Left-align the textmap result
        CxUTFString utfText;
        utfText.fromCxString(rawText, 8);
        int displayLen = utfText.displayWidth();
        if (displayLen >= width) {
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
            for (int i = currentWidth; i < width; i++) {
                result = result + " ";
            }
        } else {
            result = rawText;
            for (int i = displayLen; i < width; i++) {
                result = result + " ";
            }
        }
        return result;
    }

    CxString rawText;

    switch (cell->getType()) {
        case CxSheetCell::TEXT:
            rawText = cell->getText();
            break;

        case CxSheetCell::DOUBLE:
            {
                double value = cell->getDouble().value;
                // Check for date format first
                if (cell->hasAppAttribute("dateFormat")) {
                    CxString dateFormat = cell->getAppAttributeString("dateFormat");
                    rawText = CxSheetInputParser::formatDate(value, dateFormat);
                }
                // Check if any number formatting is active (cell or column level)
                else if (getEffectiveCurrency(col, cell) ||
                    getEffectivePercent(col, cell) ||
                    getEffectiveThousands(col, cell) ||
                    getEffectiveDecimalPlaces(col, cell) >= 0) {
                    rawText = formatNumber(value, col, cell);
                } else {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%g", value);
                    rawText = CxString(buf);
                }
            }
            break;

        case CxSheetCell::FORMULA:
            {
                double value = cell->getEvaluatedValue().value;
                // Check for date format first
                if (cell->hasAppAttribute("dateFormat")) {
                    CxString dateFormat = cell->getAppAttributeString("dateFormat");
                    rawText = CxSheetInputParser::formatDate(value, dateFormat);
                }
                // Check if any number formatting is active (cell or column level)
                else if (getEffectiveCurrency(col, cell) ||
                    getEffectivePercent(col, cell) ||
                    getEffectiveThousands(col, cell) ||
                    getEffectiveDecimalPlaces(col, cell) >= 0) {
                    rawText = formatNumber(value, col, cell);
                } else {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%g", value);
                    rawText = CxString(buf);
                }
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

    // Apply wide text spacing for TEXT cells with the wideText attribute
    // "FUND IV" becomes "F U N D   I V" (spaces between chars, double space for existing spaces)
    if (cell->getType() == CxSheetCell::TEXT &&
        cell->getAppAttributeBool("wideText", false)) {
        CxUTFString utfRaw;
        utfRaw.fromCxString(rawText, 8);
        CxString wideResult;
        for (int i = 0; i < utfRaw.charCount(); i++) {
            const CxUTFCharacter *ch = utfRaw.at(i);
            if (ch != NULL) {
                if (i > 0) {
                    wideResult = wideResult + " ";
                }
                // Append the character bytes
                const unsigned char *b = ch->bytes();
                int bc = ch->byteCount();
                for (int j = 0; j < bc; j++) {
                    char tmp[2];
                    tmp[0] = (char)b[j];
                    tmp[1] = 0;
                    wideResult = wideResult + tmp;
                }
            }
        }
        rawText = wideResult;
    }

    // Convert to UTF string for proper character counting
    CxUTFString utfText;
    utfText.fromCxString(rawText, 8);  // 8-space tabs

    int displayLen = utfText.displayWidth();

    // Determine alignment using cascaded getter: cell > column > type-default
    CxString alignment = getEffectiveAlign(col, cell);

    // Check for currency using cascaded getter - needs special handling
    // Format:  $   123.45 (positive) or  $ (123.45) (negative)
    // Space + $ on left, number (with optional parens) is right-aligned
    int isCurrency = getEffectiveCurrency(col, cell);

    if (isCurrency && width > 2) {
        // Check if value is negative (for parentheses display)
        double cellValue = 0.0;
        if (cell->getType() == CxSheetCell::DOUBLE) {
            cellValue = cell->getDouble().value;
        } else if (cell->getType() == CxSheetCell::FORMULA) {
            cellValue = cell->getEvaluatedValue().value;
        }
        int isNegative = (cellValue < 0) ? 1 : 0;

        // Build the number portion (with parens if negative)
        CxString numberPart;
        if (isNegative) {
            numberPart = CxString("(") + rawText + CxString(")");
        } else {
            numberPart = rawText;
        }

        // Convert number part to UTF for proper width calculation
        CxUTFString utfNumber;
        utfNumber.fromCxString(numberPart, 8);
        int numberWidth = utfNumber.displayWidth();

        // Available width after " $" prefix
        int availWidth = width - 2;  // 2 chars for space + $

        if (availWidth < 1) {
            // Not enough room - just fill with #
            for (int i = 0; i < width; i++) {
                result = result + "#";
            }
        } else if (numberWidth >= availWidth) {
            // Truncate number to fit
            CxUTFString truncated;
            int currentWidth = 0;
            for (int i = 0; i < utfNumber.charCount() && currentWidth < availWidth; i++) {
                const CxUTFCharacter *ch = utfNumber.at(i);
                if (ch != NULL && currentWidth + ch->displayWidth() <= availWidth) {
                    truncated.append(*ch);
                    currentWidth += ch->displayWidth();
                } else {
                    break;
                }
            }
            result = CxString(" $") + truncated.toBytes();
            // Pad with spaces if truncation left room
            for (int i = currentWidth; i < availWidth; i++) {
                result = result + " ";
            }
        } else {
            int padding = availWidth - numberWidth;

            if (alignment == "center") {
                int leftPad = padding / 2;
                int rightPad = padding - leftPad;
                result = CxString(" $");
                for (int i = 0; i < leftPad; i++) {
                    result = result + " ";
                }
                result = result + numberPart;
                for (int i = 0; i < rightPad; i++) {
                    result = result + " ";
                }
            } else if (alignment == "right") {
                // space+$ on left, spaces, then number (with parens) on right
                result = CxString(" $");
                for (int i = 0; i < padding; i++) {
                    result = result + " ";
                }
                result = result + numberPart;
            } else {
                // Left-align: space+$ then number then padding
                result = CxString(" $") + numberPart;
                for (int i = 0; i < padding; i++) {
                    result = result + " ";
                }
            }
        }
    } else if (displayLen >= width) {
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
        int padding = width - displayLen;

        if (alignment == "center") {
            // Center: split padding between left and right
            int leftPad = padding / 2;
            int rightPad = padding - leftPad;
            result = "";
            for (int i = 0; i < leftPad; i++) {
                result = result + " ";
            }
            result = result + rawText;
            for (int i = 0; i < rightPad; i++) {
                result = result + " ";
            }
        } else if (alignment == "right") {
            // Right-align: pad on left
            result = "";
            for (int i = 0; i < padding; i++) {
                result = result + " ";
            }
            result = result + rawText;
        } else {
            // Left-align (default): pad on right
            result = rawText;
            for (int i = 0; i < padding; i++) {
                result = result + " ";
            }
        }
    }

    return result;
}


//-------------------------------------------------------------------------------------------------
// SheetView::isCellOccupied
//
// Returns 1 if the cell blocks text overflow. A cell is occupied if it has
// actual content (TEXT, DOUBLE, FORMULA), symbolFill (borders), or textmap
// (derived display text). Empty cells with only formatting attrs are NOT occupied.
//-------------------------------------------------------------------------------------------------
int
SheetView::isCellOccupied(int dataRow, int dataCol)
{
    CxSheetCell *cell = sheetModel->getCellPtr(CxSheetCellCoordinate(dataRow, dataCol));
    if (cell == NULL) {
        return 0;  // never touched — not occupied
    }
    if (cell->getType() != CxSheetCell::EMPTY) {
        return 1;  // has content
    }
    if (cell->hasAppAttribute("symbolFill")) {
        return 1;  // border cell
    }
    if (cell->hasAppAttribute("textmap")) {
        return 1;  // derived display text
    }
    return 0;  // empty cell (may have formatting attrs)
}


//-------------------------------------------------------------------------------------------------
// SheetView::isCellTextType
//
// Returns 1 if the cell's content is text-like (only text overflows, not numbers).
// TEXT cells and textmap cells return 1. Everything else returns 0.
//-------------------------------------------------------------------------------------------------
int
SheetView::isCellTextType(CxSheetCell *cell)
{
    if (cell == NULL) {
        return 0;
    }
    if (cell->getType() == CxSheetCell::TEXT) {
        return 1;
    }
    if (cell->hasAppAttribute("textmap")) {
        return 1;
    }
    return 0;
}


//-------------------------------------------------------------------------------------------------
// SheetView::getCellAlignment
//
// Get alignment for a cell. Returns "left", "right", or "center".
//-------------------------------------------------------------------------------------------------
CxString
SheetView::getCellAlignment(CxSheetCell *cell)
{
    if (cell == NULL) {
        return "left";
    }
    if (cell->hasAppAttribute("align")) {
        return cell->getAppAttributeString("align");
    }
    if (cell->getType() != CxSheetCell::TEXT) {
        return "right";
    }
    return "left";
}


//-------------------------------------------------------------------------------------------------
// SheetView::getCellContentWidth
//
// Get the untruncated display width of a cell's formatted content.
// Uses formatCellValue with a very large width to get the full content,
// then measures its display width (excluding leading AND trailing padding spaces).
//-------------------------------------------------------------------------------------------------
int
SheetView::getCellContentWidth(CxSheetCell *cell, int col)
{
    if (cell == NULL) {
        return 0;
    }

    // Format with large width to avoid truncation
    CxString content = formatCellValue(cell, col, 1000);

    // Find first and last non-space to get actual content bounds
    // (excludes leading padding for right-aligned and trailing padding for left-aligned)
    CxUTFString utfContent;
    utfContent.fromCxString(content, 8);

    CxUTFCharacter spaceChar = CxUTFCharacter::fromASCII(' ');
    int firstNonSpace = -1;
    int lastNonSpace = -1;
    for (int i = 0; i < utfContent.charCount(); i++) {
        const CxUTFCharacter *ch = utfContent.at(i);
        if (ch != NULL && !(*ch == spaceChar)) {
            if (firstNonSpace < 0) {
                firstNonSpace = i;
            }
            lastNonSpace = i;
        }
    }

    if (firstNonSpace < 0 || lastNonSpace < 0) {
        return 0;
    }

    // Measure display width from first to last non-space (actual content only)
    int width = 0;
    for (int i = firstNonSpace; i <= lastNonSpace; i++) {
        const CxUTFCharacter *ch = utfContent.at(i);
        if (ch != NULL) {
            width += ch->displayWidth();
        }
    }
    return width;
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
    int availableRows = _endRow - _startRow - _colHeaderHeight
                        - frozenRowsScreenHeight() - freezeDividerRowHeight();
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
    int usedWidth = _rowHeaderWidth + frozenColsScreenWidth() + freezeDividerColWidth();
    int cols = 0;

    // Count how many scrollable columns fit starting from scroll offset (skip hidden)
    for (int dataCol = _scrollColOffset; usedWidth < screenWidth && dataCol < MAX_COLUMNS; dataCol++) {
        if (_colHidden[dataCol]) continue;
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

    // Enforce scroll offset minimums for freeze panes
    if (_scrollRowOffset < _freezeRow) {
        _scrollRowOffset = _freezeRow;
    }
    if (_scrollColOffset < _freezeCol) {
        _scrollColOffset = _freezeCol;
    }

    // Vertical scrolling - frozen rows are always visible
    if (_freezeRow > 0 && row < _freezeRow) {
        // Row is in frozen area - always visible, no vertical scroll needed
    } else {
        int visRows = visibleDataRows();

        if (row < _scrollRowOffset) {
            _scrollRowOffset = row;
            if (_scrollRowOffset < _freezeRow) _scrollRowOffset = _freezeRow;
        } else {
            // Count visible rows from _scrollRowOffset to see if cursor is visible
            int visibleCount = 0;
            int r = _scrollRowOffset;
            while (visibleCount < visRows && r < row) {
                if (!isRowHidden(r)) visibleCount++;
                r++;
            }
            if (visibleCount >= visRows) {
                // Cursor is below visible area - adjust scroll offset
                visibleCount = 0;
                r = row;
                while (visibleCount < visRows && r > 0) {
                    r--;
                    if (!isRowHidden(r)) visibleCount++;
                }
                _scrollRowOffset = r;
                if (_scrollRowOffset < _freezeRow) _scrollRowOffset = _freezeRow;
            }
        }
    }

    // Horizontal scrolling - frozen cols are always visible
    if (_freezeCol > 0 && col < _freezeCol) {
        // Column is in frozen area - always visible, no horizontal scroll needed
    } else {
        if (col < _scrollColOffset) {
            _scrollColOffset = col;
            if (_scrollColOffset < _freezeCol) _scrollColOffset = _freezeCol;
        } else {
            // Check if column extends past right edge of screen
            int screenWidth = screen->cols();
            int colScreenX = getColumnScreenX(col);
            int colWidth = getColumnWidth(col);

            if (colScreenX + colWidth > screenWidth) {
                // Need to scroll right - skip hidden columns when scrolling
                while (colScreenX + colWidth > screenWidth && _scrollColOffset < col) {
                    _scrollColOffset++;
                    while (_scrollColOffset < col && _colHidden[_scrollColOffset]) {
                        _scrollColOffset++;
                    }
                    colScreenX = getColumnScreenX(col);
                }
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

    int screenRow = dataRowToScreenRow(pos.getRow());
    int screenCol = getColumnScreenX(pos.getCol());

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
    // Frozen columns: positioned starting at _rowHeaderWidth
    if (_freezeCol > 0 && col < _freezeCol) {
        int screenX = _rowHeaderWidth;
        for (int c = 0; c < col; c++) {
            if (!_colHidden[c]) screenX += getColumnWidth(c);
        }
        return screenX;
    }

    // Scrollable columns: positioned after frozen cols + divider
    int screenX = _rowHeaderWidth + frozenColsScreenWidth() + freezeDividerColWidth();

    for (int c = _scrollColOffset; c < col; c++) {
        if (!_colHidden[c]) {
            screenX += getColumnWidth(c);
        }
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
// SheetView::setFormulaRefHighlight
//
// Enable or disable formula reference highlighting.
// When active, the provided list of cells will be highlighted with HIGHLIGHT_FORMULA_REF.
//-------------------------------------------------------------------------------------------------
void
SheetView::setFormulaRefHighlight(int active, CxSList<CxSheetCellCoordinate> cells)
{
    _formulaRefHighlightActive = active;
    _formulaRefCells = cells;
}


//-------------------------------------------------------------------------------------------------
// SheetView::isFormulaRefHighlightActive
//
// Check if formula reference highlighting is currently active.
//-------------------------------------------------------------------------------------------------
int
SheetView::isFormulaRefHighlightActive(void)
{
    return _formulaRefHighlightActive;
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

        // Check if this cell is in the formula reference highlight list
        if (_formulaRefHighlightActive) {
            CxSListIterator<CxSheetCellCoordinate> iter = _formulaRefCells.begin();
            CxSListIterator<CxSheetCellCoordinate> endIter = _formulaRefCells.end();
            while (iter != endIter) {
                CxSheetCellCoordinate coord = *iter;
                ++iter;
                if ((int)coord.getRow() == dataRow && (int)coord.getCol() == dataCol) {
                    return HIGHLIGHT_FORMULA_REF;
                }
            }
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

    // Show freeze indicator
    if (_freezeRow > 0 || _freezeCol > 0) {
        leftPart += "[Frozen] ";
    }

    // Calculate display width for left part (2 fill chars + text)
    int leftDisplayWidth = 2;  // two fill chars
    leftDisplayWidth += 15;    // " ss: Editing [ "
    if (_filePath.length() > 0) {
        leftDisplayWidth += _filePath.length();
    } else {
        leftDisplayWidth += 10;  // "(untitled)"
    }
    leftDisplayWidth += 3;     // " ] "
    if (_freezeRow > 0 || _freezeCol > 0) {
        leftDisplayWidth += 9;  // "[Frozen] "
    }

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
}


//-------------------------------------------------------------------------------------------------
// SheetView::loadColumnWidthsFromAppData
//
// Load column widths and format defaults from app data (after sheet load).
// Supports two formats:
//   Old: { "columns": { "A": 12, "B": 25 } }              (number = width)
//   New: { "columns": { "A": { "width": 12, "align": "right" } } }  (object)
//-------------------------------------------------------------------------------------------------
void
SheetView::loadColumnWidthsFromAppData(CxJSONUTFObject* appData)
{
    if (appData == NULL) {
        return;
    }

    // Load freeze pane state: { "freeze": { "row": N, "col": N } }
    CxJSONUTFMember *freezeMember = appData->find("freeze");
    if (freezeMember != NULL) {
        CxJSONUTFBase *fBase = freezeMember->object();
        if (fBase != NULL && fBase->type() == CxJSONUTFBase::OBJECT) {
            CxJSONUTFObject *freezeObj = (CxJSONUTFObject *)fBase;
            CxJSONUTFMember *rowM = freezeObj->find("row");
            if (rowM != NULL) {
                CxJSONUTFBase *rv = rowM->object();
                if (rv != NULL && rv->type() == CxJSONUTFBase::NUMBER) {
                    _freezeRow = (int)((CxJSONUTFNumber *)rv)->get();
                }
            }
            CxJSONUTFMember *colM = freezeObj->find("col");
            if (colM != NULL) {
                CxJSONUTFBase *cv = colM->object();
                if (cv != NULL && cv->type() == CxJSONUTFBase::NUMBER) {
                    _freezeCol = (int)((CxJSONUTFNumber *)cv)->get();
                }
            }
        }
    }
    if (_scrollRowOffset < _freezeRow) _scrollRowOffset = _freezeRow;
    if (_scrollColOffset < _freezeCol) _scrollColOffset = _freezeCol;

    // Load hidden rows (also before columns check)
    CxJSONUTFMember *hiddenRowsMemberEarly = appData->find("hiddenRows");
    if (hiddenRowsMemberEarly != NULL) {
        CxJSONUTFBase *hrBase = hiddenRowsMemberEarly->object();
        if (hrBase != NULL && hrBase->type() == CxJSONUTFBase::ARRAY) {
            CxJSONUTFArray *hrArray = (CxJSONUTFArray *)hrBase;
            _hiddenRowCount = 0;
            for (int i = 0; i < hrArray->entries() && _hiddenRowCount < MAX_HIDDEN_ROWS; i++) {
                CxJSONUTFBase *elem = hrArray->at(i);
                if (elem != NULL && elem->type() == CxJSONUTFBase::NUMBER) {
                    _hiddenRows[_hiddenRowCount] = (int)((CxJSONUTFNumber *)elem)->get();
                    _hiddenRowCount++;
                }
            }
        }
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

    // Iterate through all column entries
    int numEntries = columns->entries();
    for (int i = 0; i < numEntries; i++) {
        CxJSONUTFMember *member = columns->at(i);
        if (member == NULL) {
            continue;
        }

        // Get the column letter (e.g., "A", "B", "AA")
        CxUTFString colName = member->var();
        CxString colNameStr = colName.toBytes();

        // Convert column letter to column index
        CxSheetCellCoordinate coord;
        int col = coord.lettersToCol(colNameStr);
        if (col < 0 || col >= MAX_COLUMNS) {
            continue;
        }

        CxJSONUTFBase *valueBase = member->object();
        if (valueBase == NULL) {
            continue;
        }

        // Old format: value is a number (just width)
        if (valueBase->type() == CxJSONUTFBase::NUMBER) {
            CxJSONUTFNumber *widthNum = (CxJSONUTFNumber *)valueBase;
            _colWidths[col] = (int)widthNum->get();
        }
        // New format: value is an object with width and format attributes
        else if (valueBase->type() == CxJSONUTFBase::OBJECT) {
            CxJSONUTFObject *colObj = (CxJSONUTFObject *)valueBase;

            // Load width
            CxJSONUTFMember *widthMember = colObj->find("width");
            if (widthMember != NULL) {
                CxJSONUTFBase *wb = widthMember->object();
                if (wb != NULL && wb->type() == CxJSONUTFBase::NUMBER) {
                    _colWidths[col] = (int)((CxJSONUTFNumber *)wb)->get();
                }
            }

            // Load align (1=left, 2=center, 3=right)
            CxJSONUTFMember *alignMember = colObj->find("align");
            if (alignMember != NULL) {
                CxJSONUTFBase *ab = alignMember->object();
                if (ab != NULL && ab->type() == CxJSONUTFBase::STRING) {
                    CxString alignStr = ((CxJSONUTFString *)ab)->get().toBytes();
                    if (alignStr == "left") _colAlign[col] = 1;
                    else if (alignStr == "center") _colAlign[col] = 2;
                    else if (alignStr == "right") _colAlign[col] = 3;
                }
            }

            // Load decimalPlaces
            CxJSONUTFMember *decMember = colObj->find("decimalPlaces");
            if (decMember != NULL) {
                CxJSONUTFBase *db = decMember->object();
                if (db != NULL && db->type() == CxJSONUTFBase::NUMBER) {
                    _colDecimalPlaces[col] = (int)((CxJSONUTFNumber *)db)->get();
                }
            }

            // Load currency (true/false -> 1/2)
            CxJSONUTFMember *currMember = colObj->find("currency");
            if (currMember != NULL) {
                CxJSONUTFBase *cb = currMember->object();
                if (cb != NULL && cb->type() == CxJSONUTFBase::BOOLEAN) {
                    _colCurrency[col] = ((CxJSONUTFBoolean *)cb)->get() ? 1 : 2;
                }
            }

            // Load percent (true/false -> 1/2)
            CxJSONUTFMember *pctMember = colObj->find("percent");
            if (pctMember != NULL) {
                CxJSONUTFBase *pb = pctMember->object();
                if (pb != NULL && pb->type() == CxJSONUTFBase::BOOLEAN) {
                    _colPercent[col] = ((CxJSONUTFBoolean *)pb)->get() ? 1 : 2;
                }
            }

            // Load thousands (true/false -> 1/2)
            CxJSONUTFMember *thouMember = colObj->find("thousands");
            if (thouMember != NULL) {
                CxJSONUTFBase *tb = thouMember->object();
                if (tb != NULL && tb->type() == CxJSONUTFBase::BOOLEAN) {
                    _colThousands[col] = ((CxJSONUTFBoolean *)tb)->get() ? 1 : 2;
                }
            }

            // Load fgColor (color string)
            CxJSONUTFMember *fgMember = colObj->find("fgColor");
            if (fgMember != NULL) {
                CxJSONUTFBase *fgb = fgMember->object();
                if (fgb != NULL && fgb->type() == CxJSONUTFBase::STRING) {
                    _colFgColor[col] = ((CxJSONUTFString *)fgb)->get().toBytes();
                }
            }

            // Load bgColor (color string)
            CxJSONUTFMember *bgMember = colObj->find("bgColor");
            if (bgMember != NULL) {
                CxJSONUTFBase *bgb = bgMember->object();
                if (bgb != NULL && bgb->type() == CxJSONUTFBase::STRING) {
                    _colBgColor[col] = ((CxJSONUTFString *)bgb)->get().toBytes();
                }
            }

            // Load hidden (boolean)
            CxJSONUTFMember *hiddenMember = colObj->find("hidden");
            if (hiddenMember != NULL) {
                CxJSONUTFBase *hb = hiddenMember->object();
                if (hb != NULL && hb->type() == CxJSONUTFBase::BOOLEAN) {
                    _colHidden[col] = ((CxJSONUTFBoolean *)hb)->get() ? 1 : 0;
                }
            }
        }
    }

}


//-------------------------------------------------------------------------------------------------
// SheetView::saveColumnWidthsToAppData
//
// Save column widths and format defaults to app data (before sheet save).
// Writes format: { "columns": { "A": { "width": 12, "align": "right" }, ... } }
// Columns with only width and no format defaults are saved as objects anyway for consistency.
//-------------------------------------------------------------------------------------------------
void
SheetView::saveColumnWidthsToAppData(CxJSONUTFObject* appData)
{
    if (appData == NULL) {
        return;
    }

    // Check if any columns have custom settings (width, format, or color)
    int hasCustomSettings = 0;
    for (int col = 0; col < MAX_COLUMNS; col++) {
        if (_colWidths[col] != 0 || _colAlign[col] != 0 ||
            _colDecimalPlaces[col] >= 0 || _colCurrency[col] != 0 ||
            _colPercent[col] != 0 || _colThousands[col] != 0 ||
            _colFgColor[col].length() > 0 || _colBgColor[col].length() > 0 ||
            _colHidden[col] != 0) {
            hasCustomSettings = 1;
            break;
        }
    }

    if (!hasCustomSettings) {
        // No custom settings - remove existing columns entry if present
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
        return;
    }

    // Create new columns object
    CxJSONUTFObject *columns = new CxJSONUTFObject();

    // Add entries for each column with custom settings
    CxSheetCellCoordinate coord;  // for colToLetters helper
    for (int col = 0; col < MAX_COLUMNS; col++) {
        // Check if this column has any custom settings
        int hasWidth = (_colWidths[col] != 0);
        int hasAlign = (_colAlign[col] != 0);
        int hasDecimal = (_colDecimalPlaces[col] >= 0);
        int hasCurrency = (_colCurrency[col] != 0);
        int hasPercent = (_colPercent[col] != 0);
        int hasThousands = (_colThousands[col] != 0);
        int hasFgColor = (_colFgColor[col].length() > 0);
        int hasBgColor = (_colBgColor[col].length() > 0);
        int hasHidden = (_colHidden[col] != 0);

        if (!hasWidth && !hasAlign && !hasDecimal && !hasCurrency &&
            !hasPercent && !hasThousands && !hasFgColor && !hasBgColor && !hasHidden) {
            continue;  // skip columns with no custom settings
        }

        CxString colName = coord.colToLetters(col);
        CxJSONUTFObject *colObj = new CxJSONUTFObject();

        // Add width
        if (hasWidth) {
            CxJSONUTFNumber *widthNum = new CxJSONUTFNumber((double)_colWidths[col]);
            colObj->append(new CxJSONUTFMember("width", widthNum));
        }

        // Add align
        if (hasAlign) {
            const char *alignStr = "left";
            if (_colAlign[col] == 2) alignStr = "center";
            else if (_colAlign[col] == 3) alignStr = "right";
            CxJSONUTFString *alignVal = new CxJSONUTFString(alignStr);
            colObj->append(new CxJSONUTFMember("align", alignVal));
        }

        // Add decimalPlaces
        if (hasDecimal) {
            CxJSONUTFNumber *decNum = new CxJSONUTFNumber((double)_colDecimalPlaces[col]);
            colObj->append(new CxJSONUTFMember("decimalPlaces", decNum));
        }

        // Add currency (store as bool, 1=true, 2=false)
        if (hasCurrency) {
            CxJSONUTFBoolean *currVal = new CxJSONUTFBoolean(_colCurrency[col] == 1);
            colObj->append(new CxJSONUTFMember("currency", currVal));
        }

        // Add percent
        if (hasPercent) {
            CxJSONUTFBoolean *pctVal = new CxJSONUTFBoolean(_colPercent[col] == 1);
            colObj->append(new CxJSONUTFMember("percent", pctVal));
        }

        // Add thousands
        if (hasThousands) {
            CxJSONUTFBoolean *thouVal = new CxJSONUTFBoolean(_colThousands[col] == 1);
            colObj->append(new CxJSONUTFMember("thousands", thouVal));
        }

        // Add fgColor
        if (hasFgColor) {
            CxJSONUTFString *fgStr = new CxJSONUTFString(_colFgColor[col].data());
            colObj->append(new CxJSONUTFMember("fgColor", fgStr));
        }

        // Add bgColor
        if (hasBgColor) {
            CxJSONUTFString *bgStr = new CxJSONUTFString(_colBgColor[col].data());
            colObj->append(new CxJSONUTFMember("bgColor", bgStr));
        }

        // Add hidden
        if (hasHidden) {
            CxJSONUTFBoolean *hiddenVal = new CxJSONUTFBoolean(1);
            colObj->append(new CxJSONUTFMember("hidden", hiddenVal));
        }

        CxJSONUTFMember *member = new CxJSONUTFMember(colName.data(), colObj);
        columns->append(member);
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

    // Save hidden rows
    // Remove existing hiddenRows entry if present
    CxJSONUTFMember *existingHR = appData->find("hiddenRows");
    if (existingHR != NULL) {
        int numEntries2 = appData->entries();
        for (int i = 0; i < numEntries2; i++) {
            CxJSONUTFMember *m = appData->at(i);
            CxUTFString mname = m->var();
            if (mname.toBytes() == "hiddenRows") {
                appData->removeAt(i);
                delete existingHR;
                break;
            }
        }
    }

    if (_hiddenRowCount > 0) {
        CxJSONUTFArray *hrArray = new CxJSONUTFArray();
        for (int i = 0; i < _hiddenRowCount; i++) {
            hrArray->append(new CxJSONUTFNumber((double)_hiddenRows[i]));
        }
        appData->append(new CxJSONUTFMember("hiddenRows", hrArray));
    }

    // Save freeze pane state as object: { "freeze": { "row": N, "col": N } }
    // (appData merge in sheetModel only copies OBJECT-type members)
    CxJSONUTFMember *existingFreeze = appData->find("freeze");
    if (existingFreeze != NULL) {
        int numEntriesF = appData->entries();
        for (int i = 0; i < numEntriesF; i++) {
            CxJSONUTFMember *m = appData->at(i);
            CxUTFString mname = m->var();
            if (mname.toBytes() == "freeze") {
                appData->removeAt(i);
                delete existingFreeze;
                break;
            }
        }
    }

    if (_freezeRow > 0 || _freezeCol > 0) {
        CxJSONUTFObject *freezeObj = new CxJSONUTFObject();
        freezeObj->append(new CxJSONUTFMember("row",
            new CxJSONUTFNumber((double)_freezeRow)));
        freezeObj->append(new CxJSONUTFMember("col",
            new CxJSONUTFNumber((double)_freezeCol)));
        appData->append(new CxJSONUTFMember("freeze", freezeObj));
    }
}


//-------------------------------------------------------------------------------------------------
// SheetView::evaluateTextmapRule
//
// Evaluate a textmap rule string and return the matched text label.
// Rule format: "Q4=1: Too Early; Q4<1.3: On Track; Healthy"
//   - Semicolons separate rules
//   - Each rule: <cellRef><op><value>: <label>
//   - Operators: =, <, >, <=, >=, !=
//   - Last entry without ':' is the default label
//   - Returns the first matching label, or default, or empty string
//-------------------------------------------------------------------------------------------------
CxString
SheetView::evaluateTextmapRule(CxString ruleStr)
{
    CxString defaultLabel;
    int hasDefault = 0;

    // Split on ';' to get individual rules
    const char *p = ruleStr.data();
    int len = ruleStr.length();
    int ruleStart = 0;

    for (int pos = 0; pos <= len; pos++) {
        // At end of string or semicolon, we have a rule
        if (pos == len || p[pos] == ';') {
            // Extract the rule substring
            CxString rule = ruleStr.subString(ruleStart, pos - ruleStart);

            // Trim whitespace
            rule.stripLeading(" \t");
            rule.stripTrailing(" \t");

            if (rule.length() == 0) {
                ruleStart = pos + 1;
                continue;
            }

            // Check if this rule has a ':' (condition: label)
            int colonPos = -1;
            for (int j = 0; j < (int)rule.length(); j++) {
                if (rule.data()[j] == ':') {
                    colonPos = j;
                    break;
                }
            }

            if (colonPos < 0) {
                // No colon - this is the default label
                defaultLabel = rule;
                hasDefault = 1;
                ruleStart = pos + 1;
                continue;
            }

            // Split on first ':' into condition and label
            CxString condition = rule.subString(0, colonPos);
            CxString label = rule.subString(colonPos + 1, rule.length() - colonPos - 1);
            condition.stripLeading(" \t");
            condition.stripTrailing(" \t");
            label.stripLeading(" \t");
            label.stripTrailing(" \t");

            // Parse condition: cellRef operator value
            // Find where letters+digits of cell ref end and operator begins
            const char *cp = condition.data();
            int clen = condition.length();
            int opStart = -1;

            // Cell ref is letters followed by digits (with optional $)
            int i = 0;
            // Skip optional $
            if (i < clen && cp[i] == '$') i++;
            // Skip letters
            while (i < clen && ((cp[i] >= 'A' && cp[i] <= 'Z') || (cp[i] >= 'a' && cp[i] <= 'z'))) i++;
            // Skip optional $
            if (i < clen && cp[i] == '$') i++;
            // Skip digits
            while (i < clen && cp[i] >= '0' && cp[i] <= '9') i++;
            opStart = i;

            if (opStart <= 0 || opStart >= clen) {
                ruleStart = pos + 1;
                continue;  // malformed condition
            }

            CxString cellRef = condition.subString(0, opStart);

            // Parse operator (=, <, >, <=, >=, !=)
            CxString op;
            int valStart = opStart;
            if (valStart < clen - 1 && (cp[valStart] == '<' || cp[valStart] == '>' || cp[valStart] == '!') && cp[valStart + 1] == '=') {
                op = condition.subString(valStart, 2);
                valStart += 2;
            } else if (valStart < clen && (cp[valStart] == '=' || cp[valStart] == '<' || cp[valStart] == '>')) {
                op = condition.subString(valStart, 1);
                valStart += 1;
            } else {
                ruleStart = pos + 1;
                continue;  // no operator found
            }

            // Parse the comparison value
            CxString valStr = condition.subString(valStart, clen - valStart);
            valStr.stripLeading(" \t");
            double compareValue = atof(valStr.data());

            // Look up the referenced cell value
            CxSheetCellCoordinate refCoord;
            if (!refCoord.parseAddress(cellRef)) {
                ruleStart = pos + 1;
                continue;  // invalid cell ref
            }

            CxSheetCell *refCell = sheetModel->getCellPtr(refCoord);
            double cellValue = 0.0;
            if (refCell != NULL) {
                if (refCell->getType() == CxSheetCell::DOUBLE) {
                    cellValue = refCell->getDouble().value;
                } else if (refCell->getType() == CxSheetCell::FORMULA) {
                    cellValue = refCell->getEvaluatedValue().value;
                }
            }

            // Evaluate the condition
            int matched = 0;
            if (op == "=") {
                matched = (cellValue == compareValue);
            } else if (op == "<") {
                matched = (cellValue < compareValue);
            } else if (op == ">") {
                matched = (cellValue > compareValue);
            } else if (op == "<=") {
                matched = (cellValue <= compareValue);
            } else if (op == ">=") {
                matched = (cellValue >= compareValue);
            } else if (op == "!=") {
                matched = (cellValue != compareValue);
            }

            if (matched) {
                return label;
            }

            ruleStart = pos + 1;
        }
    }

    // No condition matched - return default if available
    if (hasDefault) {
        return defaultLabel;
    }

    return "";
}


//-------------------------------------------------------------------------------------------------
// SheetView::shiftColumnWidths
//
// Shift column widths for insert (+1) or delete (-1) column operations.
// For insert: widths from col onward shift right, new column gets default (0).
// For delete: widths from col onward shift left, last column gets default (0).
//-------------------------------------------------------------------------------------------------
void
SheetView::shiftColumnWidths(int col, int direction)
{
    if (col < 0 || col >= MAX_COLUMNS) {
        return;
    }

    if (direction > 0) {
        // Insert: shift right from end to col
        for (int i = MAX_COLUMNS - 1; i > col; i--) {
            _colWidths[i] = _colWidths[i - 1];
            _colHidden[i] = _colHidden[i - 1];
        }
        _colWidths[col] = 0;  // new column gets default width
        _colHidden[col] = 0;  // new column is visible
    } else if (direction < 0) {
        // Delete: shift left from col to end
        for (int i = col; i < MAX_COLUMNS - 1; i++) {
            _colWidths[i] = _colWidths[i + 1];
            _colHidden[i] = _colHidden[i + 1];
        }
        _colWidths[MAX_COLUMNS - 1] = 0;
        _colHidden[MAX_COLUMNS - 1] = 0;
    }
}


//-------------------------------------------------------------------------------------------------
// SheetView::shiftColumnFormats
//
// Shift column format defaults for insert (+1) or delete (-1) column operations.
// For insert: formats from col onward shift right, new column gets unset values.
// For delete: formats from col onward shift left, last column gets unset values.
//-------------------------------------------------------------------------------------------------
void
SheetView::shiftColumnFormats(int col, int direction)
{
    if (col < 0 || col >= MAX_COLUMNS) {
        return;
    }

    if (direction > 0) {
        // Insert: shift right from end to col
        for (int i = MAX_COLUMNS - 1; i > col; i--) {
            _colAlign[i] = _colAlign[i - 1];
            _colDecimalPlaces[i] = _colDecimalPlaces[i - 1];
            _colCurrency[i] = _colCurrency[i - 1];
            _colPercent[i] = _colPercent[i - 1];
            _colThousands[i] = _colThousands[i - 1];
            _colFgColor[i] = _colFgColor[i - 1];
            _colBgColor[i] = _colBgColor[i - 1];
        }
        // New column gets unset values
        _colAlign[col] = 0;
        _colDecimalPlaces[col] = -1;
        _colCurrency[col] = 0;
        _colPercent[col] = 0;
        _colThousands[col] = 0;
        _colFgColor[col] = "";
        _colBgColor[col] = "";
    } else if (direction < 0) {
        // Delete: shift left from col to end
        for (int i = col; i < MAX_COLUMNS - 1; i++) {
            _colAlign[i] = _colAlign[i + 1];
            _colDecimalPlaces[i] = _colDecimalPlaces[i + 1];
            _colCurrency[i] = _colCurrency[i + 1];
            _colPercent[i] = _colPercent[i + 1];
            _colThousands[i] = _colThousands[i + 1];
            _colFgColor[i] = _colFgColor[i + 1];
            _colBgColor[i] = _colBgColor[i + 1];
        }
        // Last column gets unset values
        _colAlign[MAX_COLUMNS - 1] = 0;
        _colDecimalPlaces[MAX_COLUMNS - 1] = -1;
        _colCurrency[MAX_COLUMNS - 1] = 0;
        _colPercent[MAX_COLUMNS - 1] = 0;
        _colThousands[MAX_COLUMNS - 1] = 0;
        _colFgColor[MAX_COLUMNS - 1] = "";
        _colBgColor[MAX_COLUMNS - 1] = "";
    }
}


//-------------------------------------------------------------------------------------------------
// Hidden column/row support
//-------------------------------------------------------------------------------------------------

int
SheetView::isColumnHidden(int col)
{
    if (col < 0 || col >= MAX_COLUMNS) return 0;
    return _colHidden[col];
}

void
SheetView::setColumnHidden(int col, int hidden)
{
    if (col < 0 || col >= MAX_COLUMNS) return;
    _colHidden[col] = hidden ? 1 : 0;
}

int
SheetView::isRowHidden(int row)
{
    if (row < 0) return 0;
    for (int i = 0; i < _hiddenRowCount; i++) {
        if (_hiddenRows[i] == row) return 1;
        if (_hiddenRows[i] > row) return 0;  // sorted, no need to continue
    }
    return 0;
}

void
SheetView::setRowHidden(int row, int hidden)
{
    if (row < 0) return;

    if (hidden) {
        // Check if already hidden
        if (isRowHidden(row)) return;
        if (_hiddenRowCount >= MAX_HIDDEN_ROWS) return;

        // Insert in sorted order
        int insertPos = _hiddenRowCount;
        for (int i = 0; i < _hiddenRowCount; i++) {
            if (_hiddenRows[i] > row) {
                insertPos = i;
                break;
            }
        }

        // Shift entries right
        for (int i = _hiddenRowCount; i > insertPos; i--) {
            _hiddenRows[i] = _hiddenRows[i - 1];
        }
        _hiddenRows[insertPos] = row;
        _hiddenRowCount++;
    } else {
        // Remove from sorted list
        for (int i = 0; i < _hiddenRowCount; i++) {
            if (_hiddenRows[i] == row) {
                for (int j = i; j < _hiddenRowCount - 1; j++) {
                    _hiddenRows[j] = _hiddenRows[j + 1];
                }
                _hiddenRowCount--;
                return;
            }
        }
    }
}

void
SheetView::showAllRows(void)
{
    _hiddenRowCount = 0;
}

void
SheetView::showAllColumns(void)
{
    for (int i = 0; i < MAX_COLUMNS; i++) {
        _colHidden[i] = 0;
    }
}


//-------------------------------------------------------------------------------------------------
// SheetView::shiftHiddenRows
//
// Shift hidden row indices when a row is inserted (+1) or deleted (-1).
// For insert: all hidden rows at or after 'row' shift up by 1.
// For delete: remove 'row' from hidden list, shift rows after 'row' down by 1.
//-------------------------------------------------------------------------------------------------
void
SheetView::shiftHiddenRows(int row, int direction)
{
    if (direction > 0) {
        // Insert: shift hidden rows at or after 'row' up by 1
        for (int i = 0; i < _hiddenRowCount; i++) {
            if (_hiddenRows[i] >= row) {
                _hiddenRows[i]++;
            }
        }
    } else {
        // Delete: remove 'row' if hidden, shift rows after 'row' down by 1
        int removed = 0;
        for (int i = 0; i < _hiddenRowCount; i++) {
            if (_hiddenRows[i] == row) {
                removed = 1;
                for (int j = i; j < _hiddenRowCount - 1; j++) {
                    _hiddenRows[j] = _hiddenRows[j + 1];
                }
                _hiddenRowCount--;
                i--;  // re-check this index
            } else if (_hiddenRows[i] > row) {
                _hiddenRows[i]--;
            }
        }
        (void)removed;
    }
}


int
SheetView::nextVisibleRow(int row, int direction)
{
    // direction is +1 or -1
    int r = row;
    int limit = (direction > 0) ? 100000 : 0;

    while (isRowHidden(r)) {
        if ((direction > 0 && r >= limit) || (direction < 0 && r <= limit)) {
            return row;  // no visible row found, return original
        }
        r += direction;
    }
    return r;
}

int
SheetView::nextVisibleCol(int col, int direction)
{
    int c = col;
    while (c >= 0 && c < MAX_COLUMNS && isColumnHidden(c)) {
        c += direction;
    }
    if (c < 0 || c >= MAX_COLUMNS) {
        return col;  // no visible column found, return original
    }
    return c;
}


//-------------------------------------------------------------------------------------------------
// Column format getters/setters
//-------------------------------------------------------------------------------------------------
int SheetView::getColAlign(int col) {
    if (col < 0 || col >= MAX_COLUMNS) return 0;
    return _colAlign[col];
}

void SheetView::setColAlign(int col, int align) {
    if (col < 0 || col >= MAX_COLUMNS) return;
    _colAlign[col] = align;
}

int SheetView::getColDecimalPlaces(int col) {
    if (col < 0 || col >= MAX_COLUMNS) return -1;
    return _colDecimalPlaces[col];
}

void SheetView::setColDecimalPlaces(int col, int places) {
    if (col < 0 || col >= MAX_COLUMNS) return;
    _colDecimalPlaces[col] = places;
}

int SheetView::getColCurrency(int col) {
    if (col < 0 || col >= MAX_COLUMNS) return 0;
    return _colCurrency[col];
}

void SheetView::setColCurrency(int col, int val) {
    if (col < 0 || col >= MAX_COLUMNS) return;
    _colCurrency[col] = val;
}

int SheetView::getColPercent(int col) {
    if (col < 0 || col >= MAX_COLUMNS) return 0;
    return _colPercent[col];
}

void SheetView::setColPercent(int col, int val) {
    if (col < 0 || col >= MAX_COLUMNS) return;
    _colPercent[col] = val;
}

int SheetView::getColThousands(int col) {
    if (col < 0 || col >= MAX_COLUMNS) return 0;
    return _colThousands[col];
}

void SheetView::setColThousands(int col, int val) {
    if (col < 0 || col >= MAX_COLUMNS) return;
    _colThousands[col] = val;
}

CxString SheetView::getColFgColorString(int col) {
    if (col < 0 || col >= MAX_COLUMNS) return "";
    return _colFgColor[col];
}

void SheetView::setColFgColor(int col, CxString colorStr) {
    if (col < 0 || col >= MAX_COLUMNS) return;
    _colFgColor[col] = colorStr;
}

CxString SheetView::getColBgColorString(int col) {
    if (col < 0 || col >= MAX_COLUMNS) return "";
    return _colBgColor[col];
}

void SheetView::setColBgColor(int col, CxString colorStr) {
    if (col < 0 || col >= MAX_COLUMNS) return;
    _colBgColor[col] = colorStr;
}


//-------------------------------------------------------------------------------------------------
// SheetView::getEffectiveAlign
//
// Get effective alignment for a cell: cell attribute > column default > type-based default.
// Returns "left", "center", or "right".
//-------------------------------------------------------------------------------------------------
CxString
SheetView::getEffectiveAlign(int col, CxSheetCell *cell)
{
    // 1. Cell has align attribute - use it
    if (cell != NULL && cell->hasAppAttribute("align")) {
        return cell->getAppAttributeString("align");
    }

    // 2. Column has default alignment
    int colAlign = getColAlign(col);
    if (colAlign == 1) return "left";
    if (colAlign == 2) return "center";
    if (colAlign == 3) return "right";

    // 3. Type-based default: text = left, numbers = right
    if (cell == NULL) {
        return "left";
    }
    if (cell->getType() != CxSheetCell::TEXT) {
        return "right";
    }
    return "left";
}


//-------------------------------------------------------------------------------------------------
// SheetView::getEffectiveCurrency
//
// Get effective currency setting: cell attribute > column default > false.
// Returns 1 if currency enabled, 0 otherwise.
//-------------------------------------------------------------------------------------------------
int
SheetView::getEffectiveCurrency(int col, CxSheetCell *cell)
{
    // 1. Cell has currency attribute - use it
    if (cell != NULL && cell->hasAppAttribute("currency")) {
        return cell->getAppAttributeBool("currency", false) ? 1 : 0;
    }

    // 2. Column has currency default
    int colCurrency = getColCurrency(col);
    if (colCurrency == 1) return 1;  // on
    if (colCurrency == 2) return 0;  // off

    // 3. Default: no currency
    return 0;
}


//-------------------------------------------------------------------------------------------------
// SheetView::getEffectivePercent
//
// Get effective percent setting: cell attribute > column default > false.
// Returns 1 if percent enabled, 0 otherwise.
//-------------------------------------------------------------------------------------------------
int
SheetView::getEffectivePercent(int col, CxSheetCell *cell)
{
    // 1. Cell has percent attribute - use it
    if (cell != NULL && cell->hasAppAttribute("percent")) {
        return cell->getAppAttributeBool("percent", false) ? 1 : 0;
    }

    // 2. Column has percent default
    int colPercent = getColPercent(col);
    if (colPercent == 1) return 1;  // on
    if (colPercent == 2) return 0;  // off

    // 3. Default: no percent
    return 0;
}


//-------------------------------------------------------------------------------------------------
// SheetView::getEffectiveThousands
//
// Get effective thousands separator setting: cell attribute > column default > false.
// Returns 1 if thousands enabled, 0 otherwise.
//-------------------------------------------------------------------------------------------------
int
SheetView::getEffectiveThousands(int col, CxSheetCell *cell)
{
    // 1. Cell has thousands attribute - use it
    if (cell != NULL && cell->hasAppAttribute("thousands")) {
        return cell->getAppAttributeBool("thousands", false) ? 1 : 0;
    }

    // 2. Column has thousands default
    int colThousands = getColThousands(col);
    if (colThousands == 1) return 1;  // on
    if (colThousands == 2) return 0;  // off

    // 3. Default: no thousands separator
    return 0;
}


//-------------------------------------------------------------------------------------------------
// SheetView::getEffectiveDecimalPlaces
//
// Get effective decimal places: cell attribute > column default > -1 (auto).
// Returns -1 for auto (use %g), 0-10 for fixed decimal places.
//-------------------------------------------------------------------------------------------------
int
SheetView::getEffectiveDecimalPlaces(int col, CxSheetCell *cell)
{
    // 1. Cell has decimalPlaces attribute - use it
    if (cell != NULL && cell->hasAppAttribute("decimalPlaces")) {
        return cell->getAppAttributeInt("decimalPlaces", -1);
    }

    // 2. Column has decimal places default
    int colDecimal = getColDecimalPlaces(col);
    if (colDecimal >= 0) return colDecimal;

    // 3. Default: auto
    return -1;
}


//-------------------------------------------------------------------------------------------------
// SheetView::getEffectiveFgColor
//
// Get effective foreground color: cell attribute > column default > "" (terminal default).
// Returns "" for terminal default, else color string like "RGB:255,0,0".
//-------------------------------------------------------------------------------------------------
CxString
SheetView::getEffectiveFgColor(int col, CxSheetCell *cell)
{
    // 1. Cell has fgColor attribute - use it
    if (cell != NULL && cell->hasAppAttribute("fgColor")) {
        return cell->getAppAttributeString("fgColor");
    }

    // 2. Column has fgColor default
    CxString colFg = getColFgColorString(col);
    if (colFg.length() > 0) return colFg;

    // 3. Default: terminal default
    return "";
}


//-------------------------------------------------------------------------------------------------
// SheetView::getEffectiveBgColor
//
// Get effective background color: cell attribute > column default > "" (terminal default).
// Returns "" for terminal default, else color string like "RGB:255,0,0".
//-------------------------------------------------------------------------------------------------
CxString
SheetView::getEffectiveBgColor(int col, CxSheetCell *cell)
{
    // 1. Cell has bgColor attribute - use it
    if (cell != NULL && cell->hasAppAttribute("bgColor")) {
        return cell->getAppAttributeString("bgColor");
    }

    // 2. Column has bgColor default
    CxString colBg = getColBgColorString(col);
    if (colBg.length() > 0) return colBg;

    // 3. Default: terminal default
    return "";
}


//-------------------------------------------------------------------------------------------------
// SheetView::setFreeze
//
// Set freeze pane counts. Enforce scroll offset minimums.
//-------------------------------------------------------------------------------------------------
void
SheetView::setFreeze(int freezeRow, int freezeCol)
{
    _freezeRow = freezeRow;
    _freezeCol = freezeCol;

    // Enforce scroll offset minimums so scrollable area starts after frozen
    if (_scrollRowOffset < _freezeRow) {
        _scrollRowOffset = _freezeRow;
    }
    if (_scrollColOffset < _freezeCol) {
        _scrollColOffset = _freezeCol;
    }

    ensureCursorVisible();
}


//-------------------------------------------------------------------------------------------------
// SheetView::getFreeze
//
// Get current freeze pane counts.
//-------------------------------------------------------------------------------------------------
void
SheetView::getFreeze(int *freezeRow, int *freezeCol)
{
    if (freezeRow != NULL) *freezeRow = _freezeRow;
    if (freezeCol != NULL) *freezeCol = _freezeCol;
}


//-------------------------------------------------------------------------------------------------
// SheetView::frozenRowsScreenHeight
//
// Count of visible (non-hidden) rows in the frozen region (0 to _freezeRow-1).
//-------------------------------------------------------------------------------------------------
int
SheetView::frozenRowsScreenHeight(void)
{
    int count = 0;
    for (int r = 0; r < _freezeRow; r++) {
        if (!isRowHidden(r)) count++;
    }
    return count;
}


//-------------------------------------------------------------------------------------------------
// SheetView::frozenColsScreenWidth
//
// Total screen width of frozen columns (non-hidden, cols 0 to _freezeCol-1).
//-------------------------------------------------------------------------------------------------
int
SheetView::frozenColsScreenWidth(void)
{
    int width = 0;
    for (int c = 0; c < _freezeCol; c++) {
        if (!_colHidden[c]) width += getColumnWidth(c);
    }
    return width;
}


//-------------------------------------------------------------------------------------------------
// SheetView::freezeDividerRowHeight
//
// Returns 1 if freeze row divider is active, 0 otherwise.
//-------------------------------------------------------------------------------------------------
int
SheetView::freezeDividerRowHeight(void)
{
    return (_freezeRow > 0) ? 1 : 0;
}


//-------------------------------------------------------------------------------------------------
// SheetView::freezeDividerColWidth
//
// Returns 1 if freeze col divider is active, 0 otherwise.
//-------------------------------------------------------------------------------------------------
int
SheetView::freezeDividerColWidth(void)
{
    return (_freezeCol > 0) ? 1 : 0;
}


//-------------------------------------------------------------------------------------------------
// SheetView::drawFreezeDividers
//
// Draw the freeze pane divider lines using double-line box drawing characters.
// Horizontal: ═ (U+2550), Vertical: ║ (U+2551), Cross: ╬ (U+256C)
//-------------------------------------------------------------------------------------------------
void
SheetView::drawFreezeDividers(void)
{
    if (_freezeRow == 0 && _freezeCol == 0) return;

    static const char *HORIZ = "\xe2\x95\x90";   // ═
    static const char *VERT  = "\xe2\x95\x91";    // ║
    static const char *CROSS = "\xe2\x95\xac";    // ╬

    int screenWidth = screen->cols();
    int frozenHeight = frozenRowsScreenHeight();

    // Divider row screen position (right after frozen rows)
    int dividerScreenRow = _startRow + _colHeaderHeight + frozenHeight;

    // Divider col screen position (right after frozen cols + row header)
    int frozenWidth = frozenColsScreenWidth();
    int dividerScreenCol = _rowHeaderWidth + frozenWidth;

    _defaults->applyHeaderColors(screen);

    // Draw horizontal divider (═) across the row
    if (_freezeRow > 0) {
        CxScreen::placeCursor(dividerScreenRow, 0);

        // Fill row header area with ═
        for (int x = 0; x < _rowHeaderWidth; x++) {
            printf("%s", HORIZ);
        }

        // Fill data columns area with ═
        for (int x = _rowHeaderWidth; x < screenWidth; x++) {
            if (_freezeCol > 0 && x == dividerScreenCol) {
                printf("%s", CROSS);
            } else {
                printf("%s", HORIZ);
            }
        }
    }

    // Draw vertical divider (║) down the column
    if (_freezeCol > 0) {
        int dataStartScreenRow = _startRow + _colHeaderHeight;
        int dataEndScreenRow = _endRow - 1;

        // Draw the column header portion of the vertical divider
        CxScreen::placeCursor(_startRow, dividerScreenCol);
        printf("%s", VERT);

        for (int sr = dataStartScreenRow; sr <= dataEndScreenRow; sr++) {
            if (_freezeRow > 0 && sr == dividerScreenRow) {
                continue;  // already drew cross at intersection
            }
            CxScreen::placeCursor(sr, dividerScreenCol);
            printf("%s", VERT);
        }
    }

    _defaults->resetColors(screen);
}


//-------------------------------------------------------------------------------------------------
// SheetView::drawRowSegment
//
// Draw a segment of columns for one row, constrained to [colStart, colEnd) data columns
// and clipped to [screenXStart, screenXEnd) screen X range.
// Handles overflow within the segment boundaries — overflow does NOT cross segment boundaries.
//-------------------------------------------------------------------------------------------------
void
SheetView::drawRowSegment(int screenRow, int dataRow, int colStart, int colEnd,
                          int screenXStart, int screenXEnd)
{
    static const int MAX_VIS_COLS = 128;

    // Collect visible columns in this segment
    int dataCols[MAX_VIS_COLS];
    int screenXs[MAX_VIS_COLS];
    int colWidths_s[MAX_VIS_COLS];
    int numCols = 0;

    int sx = screenXStart;
    for (int dc = colStart; sx < screenXEnd && numCols < MAX_VIS_COLS; dc++) {
        if (dc >= colEnd || dc >= MAX_COLUMNS) break;
        if (_colHidden[dc]) continue;
        int cw = getColumnWidth(dc);
        dataCols[numCols] = dc;
        screenXs[numCols] = sx;
        colWidths_s[numCols] = cw;
        numCols++;
        sx += cw;
    }

    if (numCols == 0) return;

    // Overflow calculation (same logic as drawRow, but confined to segment)
    int effectiveWidth[MAX_VIS_COLS];
    int claimedBy[MAX_VIS_COLS];
    for (int i = 0; i < numCols; i++) {
        effectiveWidth[i] = colWidths_s[i];
        claimedBy[i] = -1;
    }

    // Pass 1: left-aligned text overflow (left-to-right)
    for (int i = 0; i < numCols; i++) {
        CxSheetCell *cell = sheetModel->getCellPtr(
            CxSheetCellCoordinate(dataRow, dataCols[i]));
        if (cell == NULL || !isCellTextType(cell)) continue;
        CxString align = getCellAlignment(cell);
        if (align != "left") continue;
        int contentWidth = getCellContentWidth(cell, dataCols[i]);
        if (contentWidth <= colWidths_s[i]) continue;

        int totalWidth = colWidths_s[i];
        for (int j = i + 1; j < numCols && totalWidth < contentWidth; j++) {
            if (claimedBy[j] != -1) break;
            if (isCellOccupied(dataRow, dataCols[j])) break;
            HighlightType ht = getHighlightTypeForCell(dataRow, dataCols[j]);
            if (ht != HIGHLIGHT_NONE) break;
            claimedBy[j] = i;
            totalWidth += colWidths_s[j];
        }
        effectiveWidth[i] = totalWidth;
    }

    // Pass 2: right-aligned text overflow (right-to-left)
    for (int i = numCols - 1; i >= 0; i--) {
        CxSheetCell *cell = sheetModel->getCellPtr(
            CxSheetCellCoordinate(dataRow, dataCols[i]));
        if (cell == NULL || !isCellTextType(cell)) continue;
        CxString align = getCellAlignment(cell);
        if (align != "right") continue;
        int contentWidth = getCellContentWidth(cell, dataCols[i]);
        if (contentWidth <= colWidths_s[i]) continue;

        int totalWidth = colWidths_s[i];
        for (int j = i - 1; j >= 0 && totalWidth < contentWidth; j--) {
            if (claimedBy[j] != -1) break;
            if (isCellOccupied(dataRow, dataCols[j])) break;
            HighlightType ht = getHighlightTypeForCell(dataRow, dataCols[j]);
            if (ht != HIGHLIGHT_NONE) break;
            claimedBy[j] = i;
            totalWidth += colWidths_s[j];
        }
        effectiveWidth[i] = totalWidth;
    }

    // Draw pass
    for (int i = 0; i < numCols; i++) {
        if (claimedBy[i] != -1) continue;

        int drawWidth = effectiveWidth[i];
        int availableWidth = screenXEnd - screenXs[i];
        if (drawWidth > availableWidth) drawWidth = availableWidth;

        int drawScreenCol = screenXs[i];
        CxSheetCell *cell = sheetModel->getCellPtr(
            CxSheetCellCoordinate(dataRow, dataCols[i]));
        if (drawWidth > colWidths_s[i] && cell != NULL) {
            CxString align = getCellAlignment(cell);
            if (align == "right") {
                drawScreenCol = screenXs[i] - (drawWidth - colWidths_s[i]);
                if (drawScreenCol < screenXStart) {
                    drawWidth -= (screenXStart - drawScreenCol);
                    drawScreenCol = screenXStart;
                }
            }
        }

        HighlightType ht = getHighlightTypeForCell(dataRow, dataCols[i]);
        CxScreen::placeCursor(screenRow, drawScreenCol);
        CxSheetCellCoordinate coord(dataRow, dataCols[i]);
        CxString content = formatCellValue(
            sheetModel->getCellPtr(coord), dataCols[i], drawWidth);

        switch (ht) {
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
            case HIGHLIGHT_FORMULA_REF:
                _defaults->applyFormulaRefColors(screen);
                printf("%s", content.data());
                _defaults->resetColors(screen);
                break;
            case HIGHLIGHT_NONE:
            default:
            {
                CxSheetCell *cellPtr = sheetModel->getCellPtr(coord);
                CxString fgColorStr = getEffectiveFgColor(dataCols[i], cellPtr);
                CxString bgColorStr = getEffectiveBgColor(dataCols[i], cellPtr);

                if (fgColorStr.length() > 0 && fgColorStr.index("NONE") < 0) {
                    CxColor *fgColor = SpreadsheetDefaults::parseColor(fgColorStr, 0);
                    if (fgColor != NULL) {
                        screen->setForegroundColor(fgColor);
                        delete fgColor;
                    }
                } else {
                    _defaults->applyCellColors(screen);
                }

                if (bgColorStr.length() > 0 && bgColorStr.index("NONE") < 0) {
                    CxColor *bgColor = SpreadsheetDefaults::parseColor(bgColorStr, 1);
                    if (bgColor != NULL) {
                        screen->setBackgroundColor(bgColor);
                        delete bgColor;
                    }
                }

                printf("%s", content.data());
                _defaults->resetColors(screen);
            }
            break;
        }
    }
}


//-------------------------------------------------------------------------------------------------
// SheetView::dataRowToScreenRow
//
// Convert a data row to a screen row, accounting for hidden rows and freeze panes.
//-------------------------------------------------------------------------------------------------
int
SheetView::dataRowToScreenRow(int dataRow)
{
    // Frozen rows: mapped from row 0 to _freezeRow-1
    if (_freezeRow > 0 && dataRow < _freezeRow) {
        int screenRow = _startRow + _colHeaderHeight;
        for (int r = 0; r < dataRow; r++) {
            if (!isRowHidden(r)) screenRow++;
        }
        return screenRow;
    }

    // Scrollable rows: start after frozen rows + divider
    int screenRow = _startRow + _colHeaderHeight + frozenRowsScreenHeight()
                    + freezeDividerRowHeight();
    for (int r = _scrollRowOffset; r < dataRow; r++) {
        if (!isRowHidden(r)) screenRow++;
    }
    return screenRow;
}


//-------------------------------------------------------------------------------------------------
// SheetView::isDataRowVisible
//
// Check if a data row is within the currently visible screen area.
// Frozen rows are always visible (unless hidden). Scrollable rows check scroll bounds.
//-------------------------------------------------------------------------------------------------
int
SheetView::isDataRowVisible(int dataRow)
{
    if (isRowHidden(dataRow)) return 0;

    // Frozen rows are always visible
    if (_freezeRow > 0 && dataRow < _freezeRow) return 1;

    if (dataRow < _scrollRowOffset) return 0;

    int visRows = visibleDataRows();
    int visibleCount = 0;
    for (int r = _scrollRowOffset; r <= dataRow; r++) {
        if (!isRowHidden(r)) visibleCount++;
        if (visibleCount > visRows) return 0;
    }
    return 1;
}
