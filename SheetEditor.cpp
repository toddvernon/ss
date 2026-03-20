//-------------------------------------------------------------------------------------------------
//
//  SheetEditor.cpp
//  ss (spreadsheet)
//
//  Copyright 2022-2025 Todd Vernon. All rights reserved.
//  Licensed under the Apache License, Version 2.0
//  See LICENSE file for details.
//
//  Core sheet editor implementation.
//
//-------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>

#include <cx/base/utfstring.h>
#include <cx/base/utfcharacter.h>
#include <cx/base/file.h>
#include <cx/json/json_utf_object.h>
#include <cx/json/json_utf_member.h>
#include <cx/json/json_utf_number.h>
#include <cx/sheetModel/sheetInputParser.h>
#include <cx/expression/expression.h>
#include "SheetEditor.h"


//-------------------------------------------------------------------------------------------------
// SheetEditor::SheetEditor
//
// Constructor - initializes screen, keyboard, views, and signal handling.
// Following cm's pattern: block SIGWINCH during construction, register callback last.
//-------------------------------------------------------------------------------------------------
SheetEditor::SheetEditor(CxScreen *scr, CxKeyboard *key, CxString filePath)
: programMode(EDIT)
, screen(scr)
, keyboard(key)
, sheetView(NULL)
, commandLineView(NULL)
, messageLineView(NULL)
, helpView(NULL)
, sheetModel(NULL)
, spreadsheetDefaults(NULL)
, _filePath(filePath)
, _activeCompleter(NULL)
, _cmdInputState(CMD_INPUT_IDLE)
, _currentCommand(NULL)
, _quitRequested(0)
, _quitAfterSave(0)
, _dataEntryMode(ENTRY_NONE)
, _inCellHuntMode(0)
, _cellHuntRangeActive(0)
, _cellHuntInsertPos(0)
, _dataEntryCursorPos(0)
, _rangeSelectActive(0)
, _clipboardRows(0)
, _clipboardCols(0)
, _clipboardIsCut(0)
, _hintCount(0)
, _hintStripLen(0)
, _colorPickerIndex(0)
, _colorPickerScrollOffset(0)
, _colorPickerIsForeground(0)
, _colorPickerIsColumn(0)
#ifdef SS_CLAUDE_ENABLED
, _claudeHandler(NULL)
, _claudeView(NULL)
, _claudeViewVisible(0)
, _claudeInputCursorPos(0)
#endif
{
    //---------------------------------------------------------------------------------------------
    // Block SIGWINCH during construction to prevent callbacks on partially-constructed objects.
    // This follows cm's pattern exactly.
    //---------------------------------------------------------------------------------------------
    sigset_t blockSet, oldSet;
    sigemptyset(&blockSet);
    sigaddset(&blockSet, SIGWINCH);
    sigprocmask(SIG_BLOCK, &blockSet, &oldSet);

    //---------------------------------------------------------------------------------------------
    // Load configuration - check current directory first, then home directory
    //---------------------------------------------------------------------------------------------
    spreadsheetDefaults = new SpreadsheetDefaults();

    CxString configPath = ".ssrc";
    CxFile testFile;

    // Try current directory first
    if (!testFile.open(configPath, "r")) {
        // Fall back to home directory
        CxString homeDir = getenv("HOME");
        if (homeDir.length()) {
            configPath = homeDir + "/.ssrc";
        }
    } else {
        testFile.close();
    }

    if (configPath.length()) {
        spreadsheetDefaults->loadDefaults(configPath);
    }

    //---------------------------------------------------------------------------------------------
    // Create the sheet model
    //---------------------------------------------------------------------------------------------
    sheetModel = new CxSheetModel();

    // Load file if provided - track result for startup message
    int fileLoadResult = 0;
    if (_filePath.length() > 0) {
        fileLoadResult = sheetModel->loadSheet(_filePath);
    }

    //---------------------------------------------------------------------------------------------
    // Calculate screen regions
    // Layout:
    //   Row 0 to sheetEndRow-1: sheet area (includes col headers + data cells)
    //   sheetEndRow: divider line (drawn by SheetView as last row of its area)
    //   sheetEndRow + 1: command line
    //   sheetEndRow + 2: message line
    //---------------------------------------------------------------------------------------------
    int totalRows = screen->rows();
    int sheetEndRow = totalRows - 3;    // leave room for command line + message line

    //---------------------------------------------------------------------------------------------
    // Create views
    //---------------------------------------------------------------------------------------------
    sheetView = new SheetView(screen, sheetModel, spreadsheetDefaults, 0, sheetEndRow);
    commandLineView = new CommandLineView(screen, spreadsheetDefaults, sheetEndRow + 1);
    messageLineView = new MessageLineView(screen, spreadsheetDefaults, sheetEndRow + 2);
    helpView = new HelpView(spreadsheetDefaults, screen);

#ifdef SS_CLAUDE_ENABLED
    //---------------------------------------------------------------------------------------------
    // Create Claude handler and view (not visible until user enters claude command)
    //---------------------------------------------------------------------------------------------
    _claudeHandler = new ClaudeHandler(this);

    // Configure API key: .ssrc takes priority, then env var
    CxString configApiKey = spreadsheetDefaults->claudeApiKey();
    if (configApiKey.length() > 0) {
        _claudeHandler->setApiKey(configApiKey);
    } else {
        const char *envKey = getenv("ANTHROPIC_API_KEY");
        if (envKey != NULL) {
            _claudeHandler->setApiKey(CxString(envKey));
        }
    }

    // ClaudeView created but not visible yet - positioned below sheet
    int claudeStartRow = sheetEndRow + 1;
    int claudeEndRow = claudeStartRow + 7;  // 8 rows total
    _claudeView = new ClaudeView(screen, spreadsheetDefaults, claudeStartRow, claudeEndRow);
#endif

    //---------------------------------------------------------------------------------------------
    // Load column widths from app data (if file was loaded)
    //---------------------------------------------------------------------------------------------
    if (fileLoadResult) {
        CxJSONUTFObject *appData = sheetModel->getAppData();
        sheetView->loadColumnWidthsFromAppData(appData);

        // Restore cursor position from app data
        if (appData != NULL) {
            CxJSONUTFMember *rowMember = appData->find("cursorRow");
            CxJSONUTFMember *colMember = appData->find("cursorCol");
            if (rowMember != NULL && colMember != NULL) {
                CxJSONUTFBase *rowBase = rowMember->object();
                CxJSONUTFBase *colBase = colMember->object();
                if (rowBase != NULL && rowBase->type() == CxJSONUTFBase::NUMBER &&
                    colBase != NULL && colBase->type() == CxJSONUTFBase::NUMBER) {
                    int row = (int)((CxJSONUTFNumber *)rowBase)->get();
                    int col = (int)((CxJSONUTFNumber *)colBase)->get();
                    sheetModel->jumpToCell(CxSheetCellCoordinate(row, col));
                }
            }
        }
    }

    //---------------------------------------------------------------------------------------------
    // Initialize command completers
    //---------------------------------------------------------------------------------------------
    initCommandCompleters();
    _activeCompleter = &_commandCompleter;

    //---------------------------------------------------------------------------------------------
    // Register the SINGLE resize callback LAST after all objects are initialized.
    // This is critical - the callback must not fire on partially-constructed objects.
    //---------------------------------------------------------------------------------------------
    screen->addScreenSizeCallback(CxDeferCall(this, &SheetEditor::screenResizeCallback));

#ifdef SS_CLAUDE_ENABLED
    //---------------------------------------------------------------------------------------------
    // Register idle callback for polling Claude API responses (~100ms intervals)
    //---------------------------------------------------------------------------------------------
    keyboard->addIdleCallback(CxDeferCall(this, &SheetEditor::claudeIdleCallback));
#endif

    //---------------------------------------------------------------------------------------------
    // UNBLOCK SIGWINCH now that construction is complete
    //---------------------------------------------------------------------------------------------
    sigprocmask(SIG_SETMASK, &oldSet, NULL);

    //---------------------------------------------------------------------------------------------
    // Enable mouse tracking
    //---------------------------------------------------------------------------------------------
    CxScreen::enableMouseTracking();

    //---------------------------------------------------------------------------------------------
    // Initial screen draw
    //---------------------------------------------------------------------------------------------
    // Set file path for status line display
    sheetView->setFilePath(_filePath);

    sheetView->updateScreen();
    resetPrompt();

    //---------------------------------------------------------------------------------------------
    // Startup message
    //---------------------------------------------------------------------------------------------
    if (_filePath.length() > 0) {
        if (fileLoadResult) {
            setMessage(CxString("Loaded: ") + _filePath);
        } else {
            setMessage(CxString("New sheet: ") + _filePath);
        }
    } else {
        setMessage("New sheet");
    }

    //---------------------------------------------------------------------------------------------
    // Set window title to filename (or "ss" for new sheets)
    //---------------------------------------------------------------------------------------------
    if (_filePath.length() > 0) {
        CxScreen::setWindowTitle(CxString("ss: ") + _filePath);
    } else {
        CxScreen::setWindowTitle("ss");
    }

    //---------------------------------------------------------------------------------------------
    // Final flush to ensure complete initial draw before entering run loop
    //---------------------------------------------------------------------------------------------
    fflush(stdout);
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::~SheetEditor
//
// Destructor
//-------------------------------------------------------------------------------------------------
SheetEditor::~SheetEditor(void)
{
    // Disable mouse tracking before cleanup
    CxScreen::disableMouseTracking();

    if (sheetView != NULL) {
        delete sheetView;
    }
    if (commandLineView != NULL) {
        delete commandLineView;
    }
    if (messageLineView != NULL) {
        delete messageLineView;
    }
    if (helpView != NULL) {
        delete helpView;
    }
    if (sheetModel != NULL) {
        delete sheetModel;
    }
    if (spreadsheetDefaults != NULL) {
        delete spreadsheetDefaults;
    }
#ifdef SS_CLAUDE_ENABLED
    if (_claudeHandler != NULL) {
        delete _claudeHandler;
    }
    if (_claudeView != NULL) {
        delete _claudeView;
    }
#endif
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::run
//
// Main event loop - dispatches keyboard actions based on current program mode.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::run(void)
{
    programMode = EDIT;

    // Show help on first run (when .ssrc was just created)
    if (spreadsheetDefaults->isFirstRun()) {
        helpView->setFirstRun(1);
        showHelpView();
    }

    while (1) {
        // Get next keyboard action (blocking)
        CxKeyAction keyAction = keyboard->getAction();

        // Cell hunt mode takes priority when active
        if (_inCellHuntMode) {
            focusCellHunt(keyAction);
            fflush(stdout);
            if (_quitRequested) {
                return;
            }
            continue;
        }

        // Dispatch based on current program mode
        switch (programMode) {
            case EDIT:
                if (focusEditor(keyAction)) {
                    return;  // quit requested
                }
                break;

            case COMMANDLINE:
                focusCommandPrompt(keyAction);
                break;

            case DATA_ENTRY:
                focusDataEntry(keyAction);
                break;

            case HELPVIEW:
                focusHelpView(keyAction);
                break;

#ifdef SS_CLAUDE_ENABLED
            case CLAUDEVIEW:
                focusClaudeView(keyAction);
                break;
#endif
        }

        // Single flush per action (consolidated from individual view updates)
        fflush(stdout);

        // Check if quit was requested
        if (_quitRequested) {
            return;
        }
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::focusEditor
//
// Handle keyboard input when in EDIT mode (cell navigation).
// Typing starts data entry mode. ESC enters command mode.
// Returns 1 if quit requested, 0 otherwise.
//-------------------------------------------------------------------------------------------------
int
SheetEditor::focusEditor(CxKeyAction keyAction)
{
    int action = keyAction.actionType();

    switch (action) {
        //-------------------------------------------------------------------------------------
        // ESC key - enter command mode (range selection stays active for commands)
        //-------------------------------------------------------------------------------------
        case CxKeyAction::COMMAND:
        {
            enterCommandLineMode();
        }
        break;

        //-------------------------------------------------------------------------------------
        // Arrow key navigation (cancels selection if active)
        //-------------------------------------------------------------------------------------
        case CxKeyAction::CURSOR:
        {
            CxString tag = keyAction.tag();

            // Cancel range selection if active (plain arrow cancels selection)
            if (_rangeSelectActive) {
                // Build list of all cells in the range so we can redraw them unhighlighted
                CxSList<CxSheetCellCoordinate> rangeCells;
                int minRow = _rangeAnchor.getRow();
                int maxRow = _rangeCurrent.getRow();
                int minCol = _rangeAnchor.getCol();
                int maxCol = _rangeCurrent.getCol();
                if (minRow > maxRow) { int t = minRow; minRow = maxRow; maxRow = t; }
                if (minCol > maxCol) { int t = minCol; minCol = maxCol; maxCol = t; }
                for (int r = minRow; r <= maxRow; r++) {
                    for (int c = minCol; c <= maxCol; c++) {
                        rangeCells.append(CxSheetCellCoordinate(r, c));
                    }
                }

                _rangeSelectActive = 0;
                sheetView->setRangeSelection(0, _rangeAnchor, _rangeCurrent);
                sheetView->updateCells(rangeCells);
            }

            // Save old position before moving
            CxSheetCellCoordinate oldPos = sheetModel->getCurrentPosition();

            if (tag == "<arrow-up>") {
                sheetModel->cursorUpRequest();
                // Skip hidden rows
                CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
                if (sheetView->isRowHidden(pos.getRow())) {
                    int visRow = sheetView->nextVisibleRow(pos.getRow(), -1);
                    sheetModel->jumpToCell(CxSheetCellCoordinate(visRow, pos.getCol()));
                }
            } else if (tag == "<arrow-down>") {
                sheetModel->cursorDownRequest();
                // Skip hidden rows
                CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
                if (sheetView->isRowHidden(pos.getRow())) {
                    int visRow = sheetView->nextVisibleRow(pos.getRow(), 1);
                    sheetModel->jumpToCell(CxSheetCellCoordinate(visRow, pos.getCol()));
                }
            } else if (tag == "<arrow-left>") {
                sheetModel->cursorLeftRequest();
                // Skip hidden columns
                CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
                if (sheetView->isColumnHidden(pos.getCol())) {
                    int visCol = sheetView->nextVisibleCol(pos.getCol(), -1);
                    sheetModel->jumpToCell(CxSheetCellCoordinate(pos.getRow(), visCol));
                }
            } else if (tag == "<arrow-right>") {
                sheetModel->cursorRightRequest();
                // Skip hidden columns
                CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
                if (sheetView->isColumnHidden(pos.getCol())) {
                    int visCol = sheetView->nextVisibleCol(pos.getCol(), 1);
                    sheetModel->jumpToCell(CxSheetCellCoordinate(pos.getRow(), visCol));
                }
            }

            // Get new position and update display (handles scrolling if needed)
            CxSheetCellCoordinate newPos = sheetModel->getCurrentPosition();
            sheetView->updateCursorMove(oldPos, newPos);
            sheetView->updateStatusLine();
            resetPrompt();
        }
        break;

        //-------------------------------------------------------------------------------------
        // Ctrl+Arrow key adjusts column width, Ctrl/Option+Up toggles formula highlight
        // Note: Option+Arrow also maps here on macOS (modifier 3)
        //-------------------------------------------------------------------------------------
        case CxKeyAction::CTRL_CURSOR:
        {
            CxString tag = keyAction.tag();
            if (tag == "<ctrl-arrow-right>") {
                adjustColumnWidth(1);
            } else if (tag == "<ctrl-arrow-left>") {
                adjustColumnWidth(-1);
            } else if (tag == "<ctrl-arrow-up>") {
                // Toggle formula reference highlighting
                if (sheetView->isFormulaRefHighlightActive()) {
                    // Turn off highlighting
                    CxSList<CxSheetCellCoordinate> emptyList;
                    sheetView->setFormulaRefHighlight(0, emptyList);
                    sheetView->updateScreen();
                }
                else {
                    // Check if current cell is a formula
                    CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
                    CxSheetCell *cell = sheetModel->getCellPtr(pos);

                    if (cell != NULL && cell->cellType == CxSheetCell::FORMULA) {
                        // Get the parsed formula expression
                        CxExpression *expr = cell->formula;
                        if (expr != NULL) {
                            CxSList<CxSheetCellCoordinate> refCells;

                            // Get list of single cell references (e.g., "A1", "B2")
                            CxSList<CxString> varList = expr->GetVariableList();
                            for (int i = 0; i < (int)varList.entries(); i++) {
                                CxString varName = varList.at(i);
                                refCells.append(CxSheetCellCoordinate(varName));
                            }

                            // Get list of ranges (e.g., "A1:C3") and expand to all cells
                            CxSList<CxString> rangeList = expr->GetRangeList();
                            for (int i = 0; i < (int)rangeList.entries(); i++) {
                                CxString rangeName = rangeList.at(i);

                                // Parse range "A1:C3" into start and end coordinates
                                int colonPos = rangeName.firstChar(':');
                                if (colonPos >= 0) {
                                    CxString startAddr = rangeName.subString(0, colonPos);
                                    CxString endAddr = rangeName.subString(colonPos + 1,
                                                                           rangeName.length() - colonPos - 1);
                                    CxSheetCellCoordinate startCoord(startAddr);
                                    CxSheetCellCoordinate endCoord(endAddr);

                                    // Get bounds (normalize in case they're reversed)
                                    int minRow = (int)startCoord.getRow();
                                    int maxRow = (int)endCoord.getRow();
                                    int minCol = (int)startCoord.getCol();
                                    int maxCol = (int)endCoord.getCol();
                                    if (minRow > maxRow) { int t = minRow; minRow = maxRow; maxRow = t; }
                                    if (minCol > maxCol) { int t = minCol; minCol = maxCol; maxCol = t; }

                                    // Add all cells in range
                                    for (int r = minRow; r <= maxRow; r++) {
                                        for (int c = minCol; c <= maxCol; c++) {
                                            refCells.append(CxSheetCellCoordinate(r, c));
                                        }
                                    }
                                }
                            }

                            // Enable highlighting if we have any references
                            if (refCells.entries() > 0) {
                                sheetView->setFormulaRefHighlight(1, refCells);
                                sheetView->updateScreen();
                            }
                        }
                    }
                }
            }
        }
        break;

        //-------------------------------------------------------------------------------------
        // Shift+Arrow key starts/extends range selection
        //-------------------------------------------------------------------------------------
        case CxKeyAction::SHIFT_CURSOR:
        {
            CxString tag = keyAction.tag();

            // If not already selecting, start a new selection with current cell as anchor
            if (!_rangeSelectActive) {
                _rangeSelectActive = 1;
                _rangeAnchor = sheetModel->getCurrentPosition();
                _rangeCurrent = _rangeAnchor;
            }

            // Save old position before moving
            CxSheetCellCoordinate oldCurrent = _rangeCurrent;

            // Move cursor in the direction indicated
            if (tag == "<shift-arrow-up>") {
                sheetModel->cursorUpRequest();
                CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
                if (sheetView->isRowHidden(pos.getRow())) {
                    int visRow = sheetView->nextVisibleRow(pos.getRow(), -1);
                    sheetModel->jumpToCell(CxSheetCellCoordinate(visRow, pos.getCol()));
                }
            } else if (tag == "<shift-arrow-down>") {
                sheetModel->cursorDownRequest();
                CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
                if (sheetView->isRowHidden(pos.getRow())) {
                    int visRow = sheetView->nextVisibleRow(pos.getRow(), 1);
                    sheetModel->jumpToCell(CxSheetCellCoordinate(visRow, pos.getCol()));
                }
            } else if (tag == "<shift-arrow-left>") {
                sheetModel->cursorLeftRequest();
                CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
                if (sheetView->isColumnHidden(pos.getCol())) {
                    int visCol = sheetView->nextVisibleCol(pos.getCol(), -1);
                    sheetModel->jumpToCell(CxSheetCellCoordinate(pos.getRow(), visCol));
                }
            } else if (tag == "<shift-arrow-right>") {
                sheetModel->cursorRightRequest();
                CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
                if (sheetView->isColumnHidden(pos.getCol())) {
                    int visCol = sheetView->nextVisibleCol(pos.getCol(), 1);
                    sheetModel->jumpToCell(CxSheetCellCoordinate(pos.getRow(), visCol));
                }
            }

            // Update the selection current position
            _rangeCurrent = sheetModel->getCurrentPosition();

            // Check if we moved to a position that requires scrolling
            // If so, need full redraw; otherwise use optimized partial redraw
            if (oldCurrent == _rangeCurrent) {
                // No movement, nothing to update
            } else {
                // Use optimized update that only redraws changed cells
                // (handles scrolling internally via updateCells visibility check)
                sheetView->updateRangeSelectionMove(_rangeAnchor, oldCurrent, _rangeCurrent);
            }
            sheetView->updateStatusLine();
            resetPrompt();
        }
        break;

        //-------------------------------------------------------------------------------------
        // Typing starts data entry mode - mode determined by first character
        //-------------------------------------------------------------------------------------
        case CxKeyAction::LOWERCASE_ALPHA:
        case CxKeyAction::UPPERCASE_ALPHA:
        case CxKeyAction::NUMBER:
        case CxKeyAction::SYMBOL:
        {
            char c = keyAction.tag().data()[0];
            DataEntryMode mode = deduceEntryModeFromChar(c);
            if (mode != ENTRY_NONE) {
                enterDataEntryMode(mode, c);
            }
        }
        break;

        //-------------------------------------------------------------------------------------
        // Enter key edits existing cell content
        //-------------------------------------------------------------------------------------
        case CxKeyAction::NEWLINE:
        {
            editCurrentCell();
        }
        break;

        //-------------------------------------------------------------------------------------
        // Backspace/Delete clears cell contents
        //-------------------------------------------------------------------------------------
        case CxKeyAction::BACKSPACE:
        {
            // Clear selected range or current cell
            CMD_Clear("");
        }
        break;

        //-------------------------------------------------------------------------------------
        // Control key sequences
        //-------------------------------------------------------------------------------------
        case CxKeyAction::CONTROL:
        {
            CxString tag = keyAction.tag();

            if (tag == "X") {
                // Ctrl-X prefix commands
                return dispatchControlX();
            }
            else if (tag == "K") {
                // Ctrl-K: cut selection
                CMD_Cut("");
            }
            else if (tag == "Y") {
                // Ctrl-Y: paste
                CMD_Paste("");
            }
            else if (tag == "N" || tag == "<FS>") {
                // Ctrl-N or Ctrl-4: cycle number formats
                cycleNumberFormat();
            }
            else if (tag == "A") {
                // Ctrl-A: cycle alignment
                cycleAlignment();
            }
            else if (tag == "<GS>") {
                // Ctrl-5: toggle percent format
                togglePercentFormat();
            }
            else if (tag == "D") {
                // Ctrl-D: cycle date formats
                cycleDateFormat();
            }
            else if (tag == "H") {
                // Ctrl-H: show help
                showHelpView();
            }
        }
        break;

        //-------------------------------------------------------------------------------------
        // Option key sequences
        //-------------------------------------------------------------------------------------
        case CxKeyAction::OPTION:
        {
            CxString tag = keyAction.tag();

            if (tag == "<option-arrow-up>") {
                // Toggle formula reference highlighting
                if (sheetView->isFormulaRefHighlightActive()) {
                    // Turn off highlighting
                    CxSList<CxSheetCellCoordinate> emptyList;
                    sheetView->setFormulaRefHighlight(0, emptyList);
                    sheetView->updateScreen();
                }
                else {
                    // Check if current cell is a formula
                    CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
                    CxSheetCell *cell = sheetModel->getCellPtr(pos);

                    if (cell != NULL && cell->cellType == CxSheetCell::FORMULA) {
                        // Get the parsed formula expression
                        CxExpression *expr = cell->formula;
                        if (expr != NULL) {
                            // Get list of variable names (cell references)
                            CxSList<CxString> varList = expr->GetVariableList();
                            CxSList<CxSheetCellCoordinate> refCells;

                            // Convert variable names to coordinates
                            CxSListIterator<CxString> iter = varList.begin();
                            CxSListIterator<CxString> endIter = varList.end();
                            while (iter != endIter) {
                                CxString varName = *iter;
                                ++iter;

                                // Check if it's a range (contains :)
                                int colonPos = varName.firstChar(':');
                                if (colonPos >= 0) {
                                    // Range like A1:C3 - expand to all cells
                                    CxString startAddr = varName.subString(0, colonPos);
                                    CxString endAddr = varName.subString(colonPos + 1,
                                                                         varName.length() - colonPos - 1);
                                    CxSheetCellCoordinate startCoord(startAddr);
                                    CxSheetCellCoordinate endCoord(endAddr);

                                    // Get bounds (normalize in case they're reversed)
                                    int minRow = (int)startCoord.getRow();
                                    int maxRow = (int)endCoord.getRow();
                                    int minCol = (int)startCoord.getCol();
                                    int maxCol = (int)endCoord.getCol();
                                    if (minRow > maxRow) { int t = minRow; minRow = maxRow; maxRow = t; }
                                    if (minCol > maxCol) { int t = minCol; minCol = maxCol; maxCol = t; }

                                    // Add all cells in range
                                    for (int r = minRow; r <= maxRow; r++) {
                                        for (int c = minCol; c <= maxCol; c++) {
                                            refCells.append(CxSheetCellCoordinate(r, c));
                                        }
                                    }
                                }
                                else {
                                    // Single cell reference
                                    refCells.append(CxSheetCellCoordinate(varName));
                                }
                            }

                            // Enable highlighting if we have any references
                            if (refCells.entries() > 0) {
                                sheetView->setFormulaRefHighlight(1, refCells);
                                sheetView->updateScreen();
                            }
                        }
                    }
                }
            }
            else if (tag == "<option-delete>") {
                // Delete key: clear selected range or current cell
                CMD_Clear("");
            }
        }
        break;

        //-------------------------------------------------------------------------------------
        // Mouse click - select cell under cursor, or edit if clicking command line
        //-------------------------------------------------------------------------------------
        case CxKeyAction::MOUSE_PRESS:
        {
            int termRow = keyAction.mouseRow();
            int termCol = keyAction.mouseCol();
            int button = keyAction.mouseButton();

            // Only handle left button (1)
            if (button == 1) {
                // Check if click is on the command line row - equivalent to pressing Enter
                if (termRow == commandLineView->getScreenRow()) {
                    editCurrentCell();
                    break;
                }

                int dataRow, dataCol;
                if (sheetView->terminalToCell(termRow, termCol, &dataRow, &dataCol)) {
                    // Cancel any existing range selection
                    if (_rangeSelectActive) {
                        CxSList<CxSheetCellCoordinate> rangeCells;
                        int minRow = _rangeAnchor.getRow();
                        int maxRow = _rangeCurrent.getRow();
                        int minCol = _rangeAnchor.getCol();
                        int maxCol = _rangeCurrent.getCol();
                        if (minRow > maxRow) { int t = minRow; minRow = maxRow; maxRow = t; }
                        if (minCol > maxCol) { int t = minCol; minCol = maxCol; maxCol = t; }
                        for (int r = minRow; r <= maxRow; r++) {
                            for (int c = minCol; c <= maxCol; c++) {
                                rangeCells.append(CxSheetCellCoordinate(r, c));
                            }
                        }
                        _rangeSelectActive = 0;
                        sheetView->setRangeSelection(0, _rangeAnchor, _rangeCurrent);
                        sheetView->updateCells(rangeCells);
                    }

                    CxSheetCellCoordinate oldPos = sheetModel->getCurrentPosition();
                    CxSheetCellCoordinate newPos(dataRow, dataCol);

                    // Check for shift+click to start/extend range
                    if (keyAction.mouseShift()) {
                        if (!_rangeSelectActive) {
                            _rangeSelectActive = 1;
                            _rangeAnchor = oldPos;
                        }
                        _rangeCurrent = newPos;
                        sheetModel->jumpToCell(newPos);
                        sheetView->setRangeSelection(1, _rangeAnchor, _rangeCurrent);
                        sheetView->updateScreen();
                    } else {
                        // Normal click - just move cursor
                        sheetModel->jumpToCell(newPos);
                        sheetView->updateCursorMove(oldPos, newPos);
                    }
                    sheetView->updateStatusLine();
                    resetPrompt();
                }
            }
        }
        break;

        //-------------------------------------------------------------------------------------
        // Mouse drag - extend range selection
        //-------------------------------------------------------------------------------------
        case CxKeyAction::MOUSE_DRAG:
        {
            int termRow = keyAction.mouseRow();
            int termCol = keyAction.mouseCol();
            int button = keyAction.mouseButton();

            // Only handle left button (1) drag for range selection
            if (button == 1) {
                int dataRow, dataCol;
                if (sheetView->terminalToCell(termRow, termCol, &dataRow, &dataCol)) {
                    // Start or extend range selection
                    if (!_rangeSelectActive) {
                        _rangeSelectActive = 1;
                        _rangeAnchor = sheetModel->getCurrentPosition();
                    }

                    CxSheetCellCoordinate oldCurrent = _rangeCurrent;
                    _rangeCurrent = CxSheetCellCoordinate(dataRow, dataCol);

                    if (oldCurrent.getRow() != _rangeCurrent.getRow() ||
                        oldCurrent.getCol() != _rangeCurrent.getCol()) {
                        sheetModel->jumpToCell(_rangeCurrent);
                        sheetView->setRangeSelection(1, _rangeAnchor, _rangeCurrent);
                        sheetView->updateRangeSelectionMove(_rangeAnchor, oldCurrent, _rangeCurrent);
                        sheetView->updateStatusLine();
                        resetPrompt();
                    }
                }
            }
        }
        break;

        //-------------------------------------------------------------------------------------
        // Mouse release - finalize selection
        //-------------------------------------------------------------------------------------
        case CxKeyAction::MOUSE_RELEASE:
        {
            // Selection is already tracked during drag, nothing special needed on release
        }
        break;

        //-------------------------------------------------------------------------------------
        // Mouse double-click - select cell and enter edit mode
        //-------------------------------------------------------------------------------------
        case CxKeyAction::MOUSE_DOUBLE_CLICK:
        {
            int termRow = keyAction.mouseRow();
            int termCol = keyAction.mouseCol();
            int button = keyAction.mouseButton();

            // Only handle left button (1)
            if (button == 1) {
                int dataRow, dataCol;
                if (sheetView->terminalToCell(termRow, termCol, &dataRow, &dataCol)) {
                    // Move to the clicked cell
                    CxSheetCellCoordinate oldPos = sheetModel->getCurrentPosition();
                    CxSheetCellCoordinate newPos(dataRow, dataCol);
                    sheetModel->jumpToCell(newPos);
                    sheetView->updateCursorMove(oldPos, newPos);
                    sheetView->updateStatusLine();

                    // Enter edit mode for the cell
                    editCurrentCell();
                }
            }
        }
        break;

        //-------------------------------------------------------------------------------------
        // Mouse wheel - scroll viewport
        //-------------------------------------------------------------------------------------
        case CxKeyAction::MOUSE_WHEEL:
        {
            int button = keyAction.mouseButton();
            int scrollAmount = 3;  // Scroll 3 rows at a time

            // button 4 = wheel up, button 5 = wheel down
            int rowDelta = (button == 4) ? -scrollAmount : scrollAmount;

            // Shift+wheel scrolls horizontally
            if (keyAction.mouseShift()) {
                if (sheetView->scrollViewport(0, rowDelta)) {
                    sheetView->updateScreen();
                }
            } else {
                if (sheetView->scrollViewport(rowDelta, 0)) {
                    sheetView->updateScreen();
                }
            }
        }
        break;

        default:
            break;
    }

    return 0;
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::dispatchControlX
//
// Handle Control-X prefix commands (two-key sequences like Ctrl-X Ctrl-S for save).
// Returns 1 if quit requested, 0 otherwise.
//-------------------------------------------------------------------------------------------------
int
SheetEditor::dispatchControlX(void)
{
    // Wait for the second key in the sequence
    CxKeyAction secondAction = keyboard->getAction();

    // Must be a control key for Ctrl-X commands
    if (secondAction.actionType() != CxKeyAction::CONTROL) {
        return 0;
    }

    // Ctrl-X Ctrl-S - Save
    if (secondAction.tag() == "S") {
        if (_filePath.length() > 0) {
            // Have a filename - save directly
            CMD_Save("");
        } else {
            // No filename - enter command mode with file-save, prompt for filename
            programMode = COMMANDLINE;
            _cmdInputState = CMD_INPUT_ARGUMENT;
            _cmdBuffer = "file-save";
            _argBuffer = "";

            // Find the file-save command entry
            for (int i = 0; commandTable[i].name != NULL; i++) {
                if (CxString(commandTable[i].name) == "file-save") {
                    _currentCommand = &commandTable[i];
                    break;
                }
            }

            _activeCompleter = NULL;  // freeform text entry for filename
            updateArgumentDisplay();
        }
        return 0;
    }

    // Ctrl-X Ctrl-C - Quit
    if (secondAction.tag() == "C") {
        CMD_Quit("");
        return 1;
    }

    return 0;
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::focusCommandPrompt
//
// Handle keyboard input when in COMMANDLINE mode.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::focusCommandPrompt(CxKeyAction keyAction)
{
    handleCommandInput(keyAction);
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::enterCommandLineMode
//
// Transition from EDIT to COMMANDLINE mode.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::enterCommandLineMode(void)
{
    programMode = COMMANDLINE;
    _cmdInputState = CMD_INPUT_COMMAND;
    _cmdBuffer = "";
    _argBuffer = "";
    _currentCommand = NULL;

    _activeCompleter = &_commandCompleter;

    updateCommandDisplay();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::exitCommandLineMode
//
// Transition from COMMANDLINE to EDIT mode.
// Uses updateCommandLineDisplay() instead of resetPrompt() to preserve any message
// that was set by the command handler. The message will be cleared on the next user action.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::exitCommandLineMode(void)
{
    programMode = EDIT;
    _cmdInputState = CMD_INPUT_IDLE;
    updateCommandLineDisplay();
    sheetView->updateScreen();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::handleCommandInput
//
// Route command input based on current state.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::handleCommandInput(CxKeyAction keyAction)
{
    int action = keyAction.actionType();

    // ESC cancels command input
    if (action == CxKeyAction::COMMAND) {
        cancelCommandInput();
        return;
    }

    switch (_cmdInputState) {
        case CMD_INPUT_COMMAND:
            handleCommandModeInput(keyAction);
            break;

        case CMD_INPUT_ARGUMENT:
            handleArgumentModeInput(keyAction);
            break;

        case CMD_INPUT_COLOR_PICKER:
            focusColorPicker(keyAction);
            break;

        default:
            break;
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::handleCommandModeInput
//
// Handle input while selecting a command.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::handleCommandModeInput(CxKeyAction keyAction)
{
    int action = keyAction.actionType();

    // ENTER - try to select current match
    if (action == CxKeyAction::NEWLINE) {
        handleCommandEnter();
        return;
    }

    // TAB - cycle through matches
    if (action == CxKeyAction::TAB) {
        handleCommandTab();
        return;
    }

    // BACKSPACE - delete last character
    if (action == CxKeyAction::BACKSPACE) {
        if (_cmdBuffer.length() > 0) {
            _cmdBuffer = _cmdBuffer.subString(0, _cmdBuffer.length() - 1);
        }
        updateCommandDisplay();
        return;
    }

    // Regular character input
    if (action == CxKeyAction::LOWERCASE_ALPHA ||
        action == CxKeyAction::UPPERCASE_ALPHA ||
        action == CxKeyAction::SYMBOL ||
        action == CxKeyAction::NUMBER) {
        handleCommandChar(keyAction);
        return;
    }

    // Mouse click on command line - select a hint
    if (action == CxKeyAction::MOUSE_PRESS) {
        int termRow = keyAction.mouseRow();
        int termCol = keyAction.mouseCol();
        int button = keyAction.mouseButton();

        if (button == 1 && termRow == commandLineView->getScreenRow() && _hintCount > 0) {
            // Find which hint was clicked
            for (int i = 0; i < _hintCount; i++) {
                int start = _hintStartCol[i];
                int end = start + (int)_hintItems[i].length();
                if (termCol >= start && termCol < end) {
                    // Build new buffer: preserved prefix + clicked hint
                    CxString newBuffer = _cmdBuffer.subString(0, _hintStripLen) + _hintItems[i];
                    _cmdBuffer = newBuffer;

                    // Check if this uniquely selects a command
                    CompleterResult result = _commandCompleter.processEnter(_cmdBuffer);
                    if (result.getStatus() == COMPLETER_SELECTED) {
                        CommandEntry *cmd = (CommandEntry *)result.getSelectedData();
                        if (cmd != NULL) {
                            selectCommand(cmd);
                            return;
                        }
                    }

                    // Not unique yet - show next level hints
                    updateCommandDisplay();
                    return;
                }
            }
        }
        return;
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::handleArgumentModeInput
//
// Handle input while entering command argument.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::handleArgumentModeInput(CxKeyAction keyAction)
{
    int action = keyAction.actionType();

    // ENTER - execute command with argument
    if (action == CxKeyAction::NEWLINE) {
        // If using completer, try to select on enter
        if (_activeCompleter != NULL) {
            CompleterResult result = _activeCompleter->processEnter(_argBuffer);
            if (result.getStatus() == COMPLETER_SELECTED) {
                _argBuffer = result.getInput();
            }
        }
        executeCurrentCommand();
        return;
    }

    // TAB - cycle through matches (if using completer)
    if (action == CxKeyAction::TAB && _activeCompleter != NULL) {
        CompleterResult result = _activeCompleter->processTab(_argBuffer);
        _argBuffer = result.getInput();
        updateArgumentDisplay();
        return;
    }

    // BACKSPACE
    if (action == CxKeyAction::BACKSPACE) {
        if (_argBuffer.length() > 0) {
            _argBuffer = _argBuffer.subString(0, _argBuffer.length() - 1);
        }
        updateArgumentDisplay();
        return;
    }

    // Character input - append to argument
    if (action == CxKeyAction::LOWERCASE_ALPHA ||
        action == CxKeyAction::UPPERCASE_ALPHA ||
        action == CxKeyAction::SYMBOL ||
        action == CxKeyAction::NUMBER) {

        char c = keyAction.tag().data()[0];
        if (c >= 32 && c < 127) {
            // If using completer, validate against it
            if (_activeCompleter != NULL) {
                CompleterResult result = _activeCompleter->processChar(_argBuffer, c);
                if (result.getStatus() != COMPLETER_NO_MATCH) {
                    _argBuffer = result.getInput();
                    updateArgumentDisplay();
                }
            } else {
                _argBuffer = _argBuffer + CxString(c);
                updateArgumentDisplay();
            }
        }
    }

    // Mouse click on command line - select an argument hint
    if (action == CxKeyAction::MOUSE_PRESS && _activeCompleter != NULL) {
        int termRow = keyAction.mouseRow();
        int termCol = keyAction.mouseCol();
        int button = keyAction.mouseButton();

        if (button == 1 && termRow == commandLineView->getScreenRow() && _hintCount > 0) {
            for (int i = 0; i < _hintCount; i++) {
                int start = _hintStartCol[i];
                int end = start + (int)_hintItems[i].length();
                if (termCol >= start && termCol < end) {
                    _argBuffer = _hintItems[i];

                    // Check if this is a unique match - auto-execute
                    CompleterResult result = _activeCompleter->processEnter(_argBuffer);
                    if (result.getStatus() == COMPLETER_SELECTED) {
                        _argBuffer = result.getInput();
                        executeCurrentCommand();
                    } else {
                        updateArgumentDisplay();
                    }
                    return;
                }
            }
        }
        return;
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::handleCommandEnter
//
// Handle ENTER key in command mode.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::handleCommandEnter(void)
{
    CompleterResult result = _commandCompleter.processEnter(_cmdBuffer);

    if (result.getStatus() == COMPLETER_SELECTED) {
        CommandEntry *cmd = (CommandEntry *)result.getSelectedData();
        if (cmd != NULL) {
            selectCommand(cmd);
        }
    } else {
        // No unique match
        updateCommandDisplay();
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::handleCommandTab
//
// Handle TAB key in command mode.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::handleCommandTab(void)
{
    CompleterResult result = _commandCompleter.processTab(_cmdBuffer);
    _cmdBuffer = result.getInput();
    updateCommandDisplay();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::handleCommandChar
//
// Handle character input in command mode.
// Follows cm's pattern: processChar handles the character, returns result with updated input.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::handleCommandChar(CxKeyAction keyAction)
{
    char c = keyAction.tag().data()[0];
    CompleterResult result = _commandCompleter.processChar(_cmdBuffer, c);

    switch (result.getStatus()) {
        case COMPLETER_UNIQUE:
        {
            // Single unique match - auto-complete and select
            _cmdBuffer = result.getInput();
            CommandEntry *cmd = (CommandEntry *)result.getSelectedData();
            if (cmd != NULL) {
                selectCommand(cmd);
            }
        }
        break;

        case COMPLETER_PARTIAL:
        case COMPLETER_MULTIPLE:
            // Multiple matches or partial - update buffer and show hints
            _cmdBuffer = result.getInput();
            updateCommandDisplay();
            break;

        case COMPLETER_NO_MATCH:
            // No match - reject the character, don't update _cmdBuffer
            break;

        default:
            _cmdBuffer = result.getInput();
            updateCommandDisplay();
            break;
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::selectCommand
//
// A command has been selected - either execute it or prompt for argument.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::selectCommand(CommandEntry *cmd)
{
    _currentCommand = cmd;

    // Color picker commands - enter color picker mode
    if (cmd->flags & CMD_FLAG_COLOR_PICKER_FG) {
        int isColumn = (cmd->flags & CMD_FLAG_COLUMN_DEFAULT) ? 1 : 0;
        enterColorPickerMode(1, isColumn);  // foreground
        return;
    }
    if (cmd->flags & CMD_FLAG_COLOR_PICKER_BG) {
        int isColumn = (cmd->flags & CMD_FLAG_COLUMN_DEFAULT) ? 1 : 0;
        enterColorPickerMode(0, isColumn);  // background
        return;
    }

    if (cmd->flags & CMD_FLAG_NEEDS_ARG) {
        // Command needs an argument - switch to argument mode
        _cmdInputState = CMD_INPUT_ARGUMENT;
        _argBuffer = "";

        // Use symbol completer for SYMBOL_ARG commands
        if (cmd->flags & CMD_FLAG_SYMBOL_ARG) {
            _activeCompleter = &_symbolCompleter;
        } else {
            _activeCompleter = NULL;  // freeform text entry
        }

        updateArgumentDisplay();
    } else {
        // No argument needed - execute immediately
        executeCurrentCommand();
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::executeCurrentCommand
//
// Execute the currently selected command.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::executeCurrentCommand(void)
{
    if (_currentCommand == NULL || _currentCommand->handler == NULL) {
        exitCommandLineMode();
        return;
    }

    // Save state before handler - handler may change it to request more input
    CommandInputState stateBefore = _cmdInputState;

    // Call the command handler
    (this->*(_currentCommand->handler))(_argBuffer);

    // Return to edit mode unless:
    // - quit was requested
    // - handler switched TO argument mode (e.g., quit-save prompting for filename)
    // - handler changed programMode (e.g., CMD_Claude sets CLAUDEVIEW)
    int handlerRequestedArgInput = (stateBefore != CMD_INPUT_ARGUMENT &&
                                    _cmdInputState == CMD_INPUT_ARGUMENT);
    int handlerChangedMode = (programMode != COMMANDLINE);
    if (!_quitRequested && !handlerRequestedArgInput && !handlerChangedMode) {
        exitCommandLineMode();
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::cancelCommandInput
//
// Cancel command input and return to edit mode.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::cancelCommandInput(void)
{
    exitCommandLineMode();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::updateCommandDisplay
//
// Update the command line to show current input and matches.
// Shows category prefixes when input is empty, then collapses sub-categories
// (like modify-align-) until narrowed down. Cursor positioned after typed text.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::updateCommandDisplay(void)
{
    CxString prefix = "command> ";
    CxString display = prefix + _cmdBuffer;

    // Get all matches
    CxString matchNames[64];
    int matchCount = _activeCompleter->findMatches(_cmdBuffer, matchNames, 64);

    // Don't show hints if exact match
    int isExactMatch = (matchCount == 1 && _cmdBuffer == matchNames[0]);

    // Reset hint tracking for mouse click support
    _hintCount = 0;
    _hintStripLen = 0;

    if (matchCount > 0 && !isExactMatch) {
        display = display + "  ";
        int col = (int)(prefix.length() + _cmdBuffer.length() + 2);  // current display column

        if (_cmdBuffer.length() == 0) {
            // Empty input: show category prefixes (file-, edit-, modify-, etc.)
            _hintStripLen = 0;
            CxString categories[16];
            int categoryCount = 0;

            for (int i = 0; i < matchCount; i++) {
                CxString name = matchNames[i];
                int dashIdx = name.index("-");
                CxString cat;
                if (dashIdx > 0) {
                    cat = name.subString(0, dashIdx + 1);
                } else {
                    cat = name;
                }

                // Add unique categories
                int found = 0;
                for (int j = 0; j < categoryCount; j++) {
                    if (categories[j] == cat) {
                        found = 1;
                        break;
                    }
                }
                if (!found && categoryCount < 16) {
                    categories[categoryCount++] = cat;
                }
            }

            for (int i = 0; i < categoryCount; i++) {
                display = display + "| ";
                col += 2;  // "| "
                if (_hintCount < 16) {
                    _hintItems[_hintCount] = categories[i];
                    _hintStartCol[_hintCount] = col;
                    _hintCount++;
                }
                display = display + categories[i];
                col += (int)categories[i].length();
                display = display + " ";
                col += 1;
            }
        } else {
            // Has input: show suffixes after the last completed segment (last dash)
            // This way "modify-c" strips "modify-" to show "col- | cell-"
            // And "modify-cell-number-" strips all of it to show "currency | decimal | ..."

            // Find last dash in user input - that's our confirmed prefix boundary
            int lastDash = -1;
            for (int j = _cmdBuffer.length() - 1; j >= 0; j--) {
                if (_cmdBuffer.data()[j] == '-') {
                    lastDash = j;
                    break;
                }
            }
            int stripLen = (lastDash >= 0) ? lastDash + 1 : 0;
            _hintStripLen = stripLen;

            CxString displayItems[16];
            int displayCount = 0;

            for (int i = 0; i < matchCount && displayCount < 16; i++) {
                CxString name = matchNames[i];

                // Get the suffix (part after the confirmed prefix)
                CxString suffix = name.subString(stripLen, name.length() - stripLen);

                // Find first dash in suffix to detect sub-category
                int nextDash = -1;
                for (int j = 0; j < (int)suffix.length(); j++) {
                    if (suffix.data()[j] == '-') {
                        nextDash = j;
                        break;
                    }
                }

                CxString displayName;
                if (nextDash > 0) {
                    // Has sub-category in suffix - show up to and including dash
                    displayName = suffix.subString(0, nextDash + 1);
                } else {
                    // No sub-category - show full suffix
                    displayName = suffix;
                }

                // Add unique display items
                int found = 0;
                for (int j = 0; j < displayCount; j++) {
                    if (displayItems[j] == displayName) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    displayItems[displayCount++] = displayName;
                }
            }

            for (int i = 0; i < displayCount && i < 8; i++) {
                display = display + "| ";
                col += 2;
                if (_hintCount < 16) {
                    _hintItems[_hintCount] = displayItems[i];
                    _hintStartCol[_hintCount] = col;
                    _hintCount++;
                }
                display = display + displayItems[i];
                col += (int)displayItems[i].length();
                display = display + " ";
                col += 1;
            }
            if (displayCount > 8) {
                display = display + "...";
            }
        }
    }

    commandLineView->setDimMode(0);
    commandLineView->setText(display);
    commandLineView->updateScreen();

    // Position cursor after typed text (prefix length + buffer length)
    commandLineView->placeCursorAt(prefix.length() + _cmdBuffer.length());
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::updateArgumentDisplay
//
// Update the command line to show argument input.
// Cursor is positioned after the argument text.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::updateArgumentDisplay(void)
{
    CxString prefix = _currentCommand->name;
    prefix = prefix + " ";
    if (_currentCommand->argHint != NULL) {
        prefix = prefix + _currentCommand->argHint;
    }
    prefix = prefix + ": ";

    CxString display = prefix + _argBuffer;

    // Show completion hints if using a completer
    _hintCount = 0;
    _hintStripLen = 0;  // argument hints are full values, not suffixes

    if (_activeCompleter != NULL) {
        CxString matchNames[10];
        int matchCount = _activeCompleter->findMatches(_argBuffer, matchNames, 10);

        // Don't show hints if exact match
        int isExactMatch = (matchCount == 1 && _argBuffer == matchNames[0]);

        if (matchCount > 0 && !isExactMatch) {
            display = display + "  ";
            int col = (int)(prefix.length() + _argBuffer.length() + 2);
            for (int i = 0; i < matchCount && i < 5; i++) {
                display = display + "| ";
                col += 2;
                if (_hintCount < 16) {
                    _hintItems[_hintCount] = matchNames[i];
                    _hintStartCol[_hintCount] = col;
                    _hintCount++;
                }
                display = display + matchNames[i];
                col += (int)matchNames[i].length();
                display = display + " ";
                col += 1;
            }
            if (matchCount > 5) {
                display = display + "...";
            }
        }
    }

    commandLineView->setDimMode(0);
    commandLineView->setText(display);
    commandLineView->updateScreen();

    // Position cursor after the argument text
    commandLineView->placeCursorAt(prefix.length() + _argBuffer.length());
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::initCommandCompleters
//
// Initialize command completers from the command table.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::initCommandCompleters(void)
{
    // Add all commands from the command table
    for (int i = 0; commandTable[i].name != NULL; i++) {
        _commandCompleter.addCandidate(
            commandTable[i].name,
            NULL,  // no child completer
            &commandTable[i]
        );
    }

    // Initialize symbol type completer for insert-symbol command
    _symbolCompleter.addCandidate("horizontal", NULL, NULL);
    _symbolCompleter.addCandidate("vertical", NULL, NULL);
    _symbolCompleter.addCandidate("upper-left", NULL, NULL);
    _symbolCompleter.addCandidate("upper-right", NULL, NULL);
    _symbolCompleter.addCandidate("lower-left", NULL, NULL);
    _symbolCompleter.addCandidate("lower-right", NULL, NULL);
    _symbolCompleter.addCandidate("left-tee", NULL, NULL);
    _symbolCompleter.addCandidate("right-tee", NULL, NULL);
    _symbolCompleter.addCandidate("upper-tee", NULL, NULL);
    _symbolCompleter.addCandidate("lower-tee", NULL, NULL);
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::updateCommandLineDisplay
//
// Update the command line to show cell position and content.
// For formulas, shows the formula text (with = prefix).
// For other types, shows the display value.
// Right-justified: format indicator (alignment, currency, etc.).
// Does NOT clear the message line - messages persist until the next user action.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::updateCommandLineDisplay(void)
{
    CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
    CxSheetCell *cell = sheetModel->getCellPtr(pos);
    int col = pos.getCol();

    // Build left side: cell address + content
    CxString leftText = pos.toAddress();
    CxString cellText = getCellDisplayText(cell);
    if (cellText.length() > 0) {
        leftText = leftText + " " + cellText;
    }

    // Build right side: format indicator
    CxString formatIndicator = getCellFormatIndicator(cell, col);

    // Position cursor and draw
    int screenRow = commandLineView->getScreenRow();
    int screenWidth = CxScreen::cols();
    CxScreen::placeCursor(screenRow, 0);

    // Apply dim colors for command line
    spreadsheetDefaults->applyCommandLineDimColors(screen);
    CxScreen::clearScreenFromCursorToEndOfLine();

    // Output left side text
    printf("%s", leftText.data());

    // If we have format indicator, render it right-justified
    if (formatIndicator.length() > 0) {
        // Calculate left text display width (use UTF for proper width)
        CxUTFString leftUTF;
        leftUTF.fromCxString(leftText, 8);
        int leftWidth = leftUTF.displayWidth();

        // Position for right-justified content (ensure at least 2 spaces gap)
        int rightStartCol = screenWidth - formatIndicator.length();
        if (rightStartCol < leftWidth + 2) {
            rightStartCol = leftWidth + 2;
        }

        // Move to right side position and output format indicator
        CxScreen::placeCursor(screenRow, rightStartCol);
        printf("%s", formatIndicator.data());
    }

    // Reset colors and hide cursor
    spreadsheetDefaults->resetColors(screen);
    screen->hideCursor();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::resetPrompt
//
// Reset the command line to show cell position and content, AND clear any message.
// Called on user actions (cursor movement, typing, etc.) to clear feedback messages.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::resetPrompt(void)
{
    updateCommandLineDisplay();

    // Clear message line if it has content (messages disappear on first action)
    if (messageLineView->getText().length() > 0) {
        messageLineView->setText("");
        messageLineView->updateScreen();
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::setMessage
//
// Display a message on the message line (bottom of screen).
//-------------------------------------------------------------------------------------------------
void
SheetEditor::setMessage(CxString message)
{
    messageLineView->setText(message);
    messageLineView->updateScreen();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::adjustColumnWidth
//
// Adjust the width of the current column (or all columns in selection) by delta.
// Delta is typically +1 or -1. Minimum width is 3 characters.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::adjustColumnWidth(int delta)
{
    int minCol, maxCol;

    if (_rangeSelectActive) {
        // Apply to all columns in selection
        minCol = _rangeAnchor.getCol();
        maxCol = _rangeCurrent.getCol();
        if (minCol > maxCol) {
            int tmp = minCol;
            minCol = maxCol;
            maxCol = tmp;
        }
    } else {
        // Apply to current column only
        CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
        minCol = pos.getCol();
        maxCol = minCol;
    }

    // Adjust width of each column in range
    for (int col = minCol; col <= maxCol; col++) {
        int currentWidth = sheetView->getColumnWidth(col);
        int newWidth = currentWidth + delta;
        if (newWidth < 3) {
            newWidth = 3;  // minimum width
        }
        sheetView->setColumnWidth(col, newWidth);
    }

    // Redraw the sheet
    sheetView->updateScreen();
    sheetView->updateStatusLine();
    resetPrompt();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::autoFitColumnWidth
//
// Auto-fit the width of the current column (or all columns in selection) to content.
// Scans all cells in the column to find the maximum content width.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::autoFitColumnWidth(void)
{
    int minCol, maxCol;

    if (_rangeSelectActive) {
        // Apply to all columns in selection
        minCol = _rangeAnchor.getCol();
        maxCol = _rangeCurrent.getCol();
        if (minCol > maxCol) {
            int tmp = minCol;
            minCol = maxCol;
            maxCol = tmp;
        }
    } else {
        // Apply to current column only
        CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
        minCol = pos.getCol();
        maxCol = minCol;
    }

    // Auto-fit each column in range
    for (int col = minCol; col <= maxCol; col++) {
        int maxWidth = 3;  // minimum width

        // Scan all rows for this column
        unsigned long numRows = sheetModel->numberOfRows();
        for (unsigned long row = 0; row < numRows; row++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);
            CxSheetCell *cell = sheetModel->getCellPtr(coord);
            if (cell != NULL) {
                CxString text = getCellDisplayText(cell);
                CxUTFString utfText;
                utfText.fromCxString(text, 8);
                int len = utfText.displayWidth();
                if (len > maxWidth) {
                    maxWidth = len;
                }
            }
        }

        // Add a little padding
        maxWidth += 1;

        // Set the column width
        sheetView->setColumnWidth(col, maxWidth);
    }

    // Redraw the sheet
    sheetView->updateScreen();
    sheetView->updateStatusLine();
    resetPrompt();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::screenResizeCallback
//
// Called when terminal size changes. Coordinates recalculation and redrawing of all views.
// Following cm's pattern: Phase 1 = all recalc, Phase 2 = all drawing in z-order.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::screenResizeCallback(void)
{
    //---------------------------------------------------------------------------------------------
    // PHASE 1: Recalculate all regions (no drawing)
    //---------------------------------------------------------------------------------------------
    int totalRows = screen->rows();
    int sheetEndRow = totalRows - 3;

#ifdef SS_CLAUDE_ENABLED
    if (_claudeViewVisible) {
        // Claude view takes 8 rows from the bottom, above command/message lines
        int claudeHeight = _claudeView->getHeight();
        sheetEndRow = totalRows - 3 - claudeHeight;

        int claudeStartRow = sheetEndRow + 1;
        int claudeEndRow = claudeStartRow + claudeHeight - 1;
        _claudeView->recalcForResize(claudeStartRow, claudeEndRow);

        commandLineView->recalcForResize(claudeEndRow + 1);
        messageLineView->recalcForResize(claudeEndRow + 2);
    } else {
        commandLineView->recalcForResize(sheetEndRow + 1);
        messageLineView->recalcForResize(sheetEndRow + 2);
    }
#else
    commandLineView->recalcForResize(sheetEndRow + 1);
    messageLineView->recalcForResize(sheetEndRow + 2);
#endif

    sheetView->recalcForResize(0, sheetEndRow);

    //---------------------------------------------------------------------------------------------
    // PHASE 2: Redraw everything in correct z-order
    //---------------------------------------------------------------------------------------------
    CxScreen::clearScreen();

    sheetView->updateScreen();

#ifdef SS_CLAUDE_ENABLED
    if (_claudeViewVisible) {
        _claudeView->updateScreen();
    }
#endif

    // Draw command line: use cell info display when not in command mode
    // (commandLineView->updateScreen() would draw stale command text in CLAUDEVIEW/EDIT modes)
#ifdef SS_CLAUDE_ENABLED
    if (programMode == CLAUDEVIEW || programMode == EDIT) {
        updateCommandLineDisplay();
    } else {
        commandLineView->updateScreen();
    }
#else
    if (programMode == EDIT) {
        updateCommandLineDisplay();
    } else {
        commandLineView->updateScreen();
    }
#endif
    messageLineView->updateScreen();

    // Place cursor based on current mode
#ifdef SS_CLAUDE_ENABLED
    if (programMode == CLAUDEVIEW) {
        _claudeView->placeCursor();
    } else
#endif
    if (programMode == EDIT) {
        sheetView->placeCursor();
    } else {
        commandLineView->placeCursor();
    }

    fflush(stdout);
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_Quit
//
// Quit command handler. If no filename is set (unsaved new sheet), prompt to save first.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_Quit(CxString commandLine)
{
    (void)commandLine;

    // Block quit if there are unsaved changes or no filename
    if (_filePath.length() == 0 || sheetModel->isTouched()) {
        setMessage("Unsaved changes - use quit-save or quit-nosave");
        return;
    }

    _quitRequested = 1;
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_QuitSave
//
// Save and quit. If no filename is set, prompt for one first.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_QuitSave(CxString commandLine)
{
    (void)commandLine;

    if (_filePath.length() > 0) {
        // Have a filename - save and quit immediately
        CMD_Save("");
        CMD_Quit("");
    } else {
        // No filename - enter argument mode to get filename, then quit after save
        _quitAfterSave = 1;
        programMode = COMMANDLINE;
        _cmdInputState = CMD_INPUT_ARGUMENT;
        _cmdBuffer = "quit-save";
        _argBuffer = "";

        // Find the file-save command entry (we'll use its handler)
        for (int i = 0; commandTable[i].name != NULL; i++) {
            if (CxString(commandTable[i].name) == "file-save") {
                _currentCommand = &commandTable[i];
                break;
            }
        }

        _activeCompleter = NULL;  // freeform text entry for filename
        updateArgumentDisplay();
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_QuitNoSave
//
// Quit without saving.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_QuitNoSave(CxString commandLine)
{
    (void)commandLine;
    _quitRequested = 1;
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_Help
//
// Show help screen (ESC command wrapper).
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_Help(CxString commandLine)
{
    (void)commandLine;
    showHelpView();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_Load
//
// Load spreadsheet from file. Uses argument as filename if provided, otherwise uses _filePath.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_Load(CxString commandLine)
{
    CxString filepath = commandLine;
    filepath.stripLeading(" \t");
    filepath.stripTrailing(" \t\n\r");

    // If no argument provided, use current _filePath
    if (filepath.length() == 0) {
        if (_filePath.length() == 0) {
            setMessage("No filename specified");
            return;
        }
        filepath = _filePath;
    }

    // Try to load the file
    if (sheetModel->loadSheet(filepath)) {
        _filePath = filepath;
        sheetView->setFilePath(_filePath);

        // Load column widths from app data
        CxJSONUTFObject *appData = sheetModel->getAppData();
        sheetView->loadColumnWidthsFromAppData(appData);

        // Restore cursor position from app data
        if (appData != NULL) {
            CxJSONUTFMember *rowMember = appData->find("cursorRow");
            CxJSONUTFMember *colMember = appData->find("cursorCol");
            if (rowMember != NULL && colMember != NULL) {
                CxJSONUTFBase *rowBase = rowMember->object();
                CxJSONUTFBase *colBase = colMember->object();
                if (rowBase != NULL && rowBase->type() == CxJSONUTFBase::NUMBER &&
                    colBase != NULL && colBase->type() == CxJSONUTFBase::NUMBER) {
                    int row = (int)((CxJSONUTFNumber *)rowBase)->get();
                    int col = (int)((CxJSONUTFNumber *)colBase)->get();
                    sheetModel->jumpToCell(CxSheetCellCoordinate(row, col));
                }
            }
        }

        CxScreen::setWindowTitle(CxString("ss: ") + filepath);
        setMessage(CxString("Loaded: ") + filepath);
        sheetView->updateScreen();
    } else {
        setMessage(CxString("Failed to load: ") + filepath);
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_Save
//
// Save spreadsheet to file. Uses argument as filename if provided, otherwise uses _filePath.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_Save(CxString commandLine)
{
    CxString filepath = commandLine;
    filepath.stripLeading(" \t");
    filepath.stripTrailing(" \t\n\r");

    // If no argument provided, use current _filePath
    if (filepath.length() == 0) {
        if (_filePath.length() == 0) {
            setMessage("No filename specified");
            return;
        }
        filepath = _filePath;
    }

    // Save column widths to app data before saving
    CxJSONUTFObject *appData = sheetModel->getAppData();
    if (appData == NULL) {
        appData = new CxJSONUTFObject();
        sheetModel->setAppData(appData);
    }
    sheetView->saveColumnWidthsToAppData(appData);

    // Save cursor position to app data
    CxSheetCellCoordinate cursorPos = sheetModel->getCurrentPosition();
    CxJSONUTFNumber *cursorRowNum = new CxJSONUTFNumber((double)cursorPos.getRow());
    CxJSONUTFNumber *cursorColNum = new CxJSONUTFNumber((double)cursorPos.getCol());

    // Remove existing cursor entries if present, then add new ones
    CxJSONUTFMember *existingRow = appData->find("cursorRow");
    CxJSONUTFMember *existingCol = appData->find("cursorCol");
    if (existingRow != NULL) {
        for (int i = 0; i < appData->entries(); i++) {
            if (appData->at(i) == existingRow) {
                appData->removeAt(i);
                delete existingRow;
                break;
            }
        }
    }
    if (existingCol != NULL) {
        for (int i = 0; i < appData->entries(); i++) {
            if (appData->at(i) == existingCol) {
                appData->removeAt(i);
                delete existingCol;
                break;
            }
        }
    }
    appData->append(new CxJSONUTFMember("cursorRow", cursorRowNum));
    appData->append(new CxJSONUTFMember("cursorCol", cursorColNum));

    // Try to save the file
    if (sheetModel->saveSheet(filepath)) {
        _filePath = filepath;
        sheetView->setFilePath(_filePath);
        sheetView->updateStatusLine();
        CxScreen::setWindowTitle(CxString("ss: ") + filepath);
        setMessage(CxString("Saved: ") + filepath);

        // If quit-save was pending, quit now
        if (_quitAfterSave) {
            _quitAfterSave = 0;
            CMD_Quit("");
        }
    } else {
        setMessage(CxString("Failed to save: ") + filepath);
        _quitAfterSave = 0;  // Clear flag on failure
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_InsertSymbol
//
// Insert a box drawing symbol fill into the current cell.
// Valid types: horizontal, vertical, upper-left, upper-right, lower-left, lower-right,
//              left-tee, right-tee, upper-tee, lower-tee
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_InsertSymbol(CxString commandLine)
{
    CxString symbolType = commandLine;
    symbolType.stripLeading(" \t");
    symbolType.stripTrailing(" \t\n\r");

    // Validate symbol type
    if (symbolType != "horizontal" && symbolType != "vertical" &&
        symbolType != "upper-left" && symbolType != "upper-right" &&
        symbolType != "lower-left" && symbolType != "lower-right" &&
        symbolType != "left-tee" && symbolType != "right-tee" &&
        symbolType != "upper-tee" && symbolType != "lower-tee") {

        setMessage(CxString("Unknown symbol type: ") + symbolType);
        return;
    }

    // Get current cell and set the symbol fill attribute
    CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
    CxSheetCell *cell = sheetModel->getCellPtr(pos);

    if (cell == NULL) {
        // Cell doesn't exist yet, create it
        CxSheetCell newCell;
        newCell.setAppAttribute("symbolFill", symbolType.data());
        sheetModel->setCell(pos, newCell);
    } else {
        // Cell exists, set the attribute (clears any existing content type)
        cell->clear();
        cell->setAppAttribute("symbolFill", symbolType.data());
    }

    // Update display
    sheetView->updateScreen();
    setMessage(CxString("Inserted: ") + symbolType);
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatColWidth
//
// Adjust column width by a relative amount (+n or -n).
// Example: "modify-width +5" increases width by 5, "modify-width -3" decreases by 3.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatColWidth(CxString commandLine)
{
    CxString widthStr = commandLine;
    widthStr.stripLeading(" \t");
    widthStr.stripTrailing(" \t\n\r");

    if (widthStr.length() == 0) {
        setMessage("Usage: modify-width +n or -n");
        return;
    }

    // Parse the delta value (must start with + or -)
    char firstChar = widthStr.data()[0];
    if (firstChar != '+' && firstChar != '-') {
        setMessage("Use +n to increase or -n to decrease width");
        return;
    }

    int delta = atoi(widthStr.data());
    if (delta == 0) {
        setMessage("Width change must be non-zero");
        return;
    }

    // Use the existing adjustColumnWidth which handles range selection
    adjustColumnWidth(delta);

    if (delta > 0) {
        setMessage(CxString("Column width increased by ") + CxString(delta));
    } else {
        setMessage(CxString("Column width decreased by ") + CxString(-delta));
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatColFit
//
// Auto-fit the column width of the current column (or all columns in selection) to content.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatColFit(CxString commandLine)
{
    (void)commandLine;  // unused

    autoFitColumnWidth();
    setMessage("Column width auto-fitted");
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::captureCellFormatting
//
// Capture all formatting attributes from a cell before it is replaced.
// This allows preserving formatting when editing a cell's value.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::captureCellFormatting(CxSheetCell *cell, CellFormatAttrs *attrs)
{
    attrs->hasCurrency = 0;
    attrs->hasPercent = 0;
    attrs->hasThousands = 0;
    attrs->hasDecimalPlaces = 0;
    attrs->decimalPlaces = 2;
    attrs->hasAlign = 0;
    attrs->hasFgColor = 0;
    attrs->hasBgColor = 0;

    if (cell == NULL) {
        return;
    }

    attrs->hasCurrency = cell->hasAppAttribute("currency") ? 1 : 0;
    attrs->hasPercent = cell->hasAppAttribute("percent") ? 1 : 0;
    attrs->hasThousands = cell->hasAppAttribute("thousands") ? 1 : 0;
    attrs->hasDecimalPlaces = cell->hasAppAttribute("decimalPlaces") ? 1 : 0;
    attrs->decimalPlaces = cell->getAppAttributeInt("decimalPlaces", 2);
    attrs->hasAlign = cell->hasAppAttribute("align") ? 1 : 0;
    if (attrs->hasAlign) {
        attrs->align = cell->getAppAttributeString("align");
    }
    attrs->hasFgColor = cell->hasAppAttribute("fgColor") ? 1 : 0;
    if (attrs->hasFgColor) {
        attrs->fgColor = cell->getAppAttributeString("fgColor");
    }
    attrs->hasBgColor = cell->hasAppAttribute("bgColor") ? 1 : 0;
    if (attrs->hasBgColor) {
        attrs->bgColor = cell->getAppAttributeString("bgColor");
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::restoreCellFormatting
//
// Restore previously captured formatting attributes to a cell.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::restoreCellFormatting(CxSheetCell *cell, CellFormatAttrs *attrs)
{
    if (cell == NULL) {
        return;
    }

    if (attrs->hasCurrency) cell->setAppAttribute("currency", true);
    if (attrs->hasPercent) cell->setAppAttribute("percent", true);
    if (attrs->hasThousands) cell->setAppAttribute("thousands", true);
    if (attrs->hasDecimalPlaces) cell->setAppAttribute("decimalPlaces", attrs->decimalPlaces);
    if (attrs->hasAlign) cell->setAppAttribute("align", attrs->align.data());
    if (attrs->hasFgColor) cell->setAppAttribute("fgColor", attrs->fgColor.data());
    if (attrs->hasBgColor) cell->setAppAttribute("bgColor", attrs->bgColor.data());
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::deduceEntryModeFromChar
//
// Determine the appropriate data entry mode based on the first character typed.
// Only '=' gets special treatment for formulas; everything else uses ENTRY_GENERAL
// and type is determined at commit time (Excel-style).
//-------------------------------------------------------------------------------------------------
SheetEditor::DataEntryMode
SheetEditor::deduceEntryModeFromChar(char c)
{
    if (c == '=') {
        return ENTRY_FORMULA;
    }
    if (c == '@') {
        return ENTRY_TEXTMAP;
    }
    // All other printable characters use general mode
    if (c >= 32 && c < 127) {
        return ENTRY_GENERAL;
    }
    return ENTRY_NONE;
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::isValidInputChar
//
// Check if a character is valid input for the given data entry mode.
// With post-commit parsing, both ENTRY_GENERAL and ENTRY_FORMULA accept all printable chars.
//-------------------------------------------------------------------------------------------------
int
SheetEditor::isValidInputChar(char c, DataEntryMode mode)
{
    switch (mode) {
        case ENTRY_GENERAL:
        case ENTRY_FORMULA:
        case ENTRY_TEXTMAP:
            // Accept any printable character
            return (c >= 32 && c < 127);

        default:
            return 0;
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::getCellDisplayText
//
// Get the display text for a cell. Returns the appropriate string representation
// based on cell type: raw text for TEXT, formatted number for DOUBLE, formula with
// = prefix for FORMULA.
//-------------------------------------------------------------------------------------------------
CxString
SheetEditor::getCellDisplayText(CxSheetCell *cell)
{
    if (cell == NULL) {
        return "";
    }

    // Textmap cells are EMPTY type with a textmap appAttribute
    if (cell->hasAppAttribute("textmap")) {
        return CxString("@") + cell->getAppAttributeString("textmap");
    }

    if (cell->getType() == CxSheetCell::EMPTY) {
        return "";
    }

    switch (cell->getType()) {
        case CxSheetCell::TEXT:
            return cell->getText();

        case CxSheetCell::DOUBLE:
        {
            int col = sheetModel->getCurrentPosition().getCol();
            CxString numStr = sheetView->formatNumber(cell->getDouble().value, col, cell);
            // For currency, prepend "$ " (shown separately from number)
            if (cell->getAppAttributeBool("currency", false)) {
                return CxString("$ ") + numStr;
            }
            return numStr;
        }

        case CxSheetCell::FORMULA:
            // Return formula with = prefix
            return CxString("=") + cell->getFormulaText();

        default:
            return "";
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::getCellFormatIndicator
//
// Build format indicator string for a cell. Shows only non-default attributes.
// Returns alignment (if differs from type default), currency, percent, thousands, decimals.
//-------------------------------------------------------------------------------------------------
CxString
SheetEditor::getCellFormatIndicator(CxSheetCell *cell, int col)
{
    CxString result;

    // Get effective attributes via SheetView cascaded getters
    CxString align = sheetView->getEffectiveAlign(col, cell);
    int currency = sheetView->getEffectiveCurrency(col, cell);
    int percent = sheetView->getEffectivePercent(col, cell);
    int thousands = sheetView->getEffectiveThousands(col, cell);
    int decimals = sheetView->getEffectiveDecimalPlaces(col, cell);

    // Determine type-default alignment
    CxString typeDefault = "right";  // DOUBLE, FORMULA default
    if (cell != NULL) {
        if (cell->getType() == CxSheetCell::TEXT) typeDefault = "left";
        if (cell->getType() == CxSheetCell::EMPTY) typeDefault = "";
    }

    // Build indicator (non-defaults only)
    if (align.length() > 0 && align != typeDefault) {
        result.append(align);
    }
    if (currency) {
        if (result.length() > 0) result.append(" ");
        result.append("currency");
    }
    if (percent) {
        if (result.length() > 0) result.append(" ");
        result.append("percent");
    }
    if (thousands) {
        if (result.length() > 0) result.append(" ");
        result.append("thousands");
    }
    if (decimals >= 0) {
        if (result.length() > 0) result.append(" ");
        char decBuf[16];
        snprintf(decBuf, sizeof(decBuf), ".%d", decimals);
        result.append(decBuf);
    }

    return result;
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::enterDataEntryMode
//
// Enter data entry mode with the specified type and first character.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::enterDataEntryMode(DataEntryMode mode, char firstChar)
{
    programMode = DATA_ENTRY;
    _dataEntryMode = mode;
    _dataEntryBuffer.clear();
    _dataEntryBuffer.append(CxUTFCharacter::fromASCII(firstChar));
    _dataEntryCursorPos = 1;  // cursor after the first character

    updateDataEntryDisplay();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::editCurrentCell
//
// Edit the current cell's existing content. Enters data entry mode with the cell's
// value pre-filled, allowing the user to modify it.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::editCurrentCell(void)
{
    CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
    CxSheetCell *cell = sheetModel->getCellPtr(pos);

    // Symbol fill cells cannot be edited - ignore Enter key
    // User can type to replace, or use insert-symbol to change type
    if (cell != NULL && cell->hasAppAttribute("symbolFill")) {
        return;
    }

    // Textmap cells - enter textmap mode with @rule pre-filled
    if (cell != NULL && cell->hasAppAttribute("textmap")) {
        programMode = DATA_ENTRY;
        _dataEntryMode = ENTRY_TEXTMAP;
        CxString ruleText = CxString("@") + cell->getAppAttributeString("textmap");
        _dataEntryBuffer.fromCxString(ruleText, 8);
        _dataEntryCursorPos = _dataEntryBuffer.charCount();
        updateDataEntryDisplay();
        return;
    }

    // If cell is empty, just enter general mode with empty buffer
    if (cell == NULL || cell->getType() == CxSheetCell::EMPTY) {
        programMode = DATA_ENTRY;
        _dataEntryMode = ENTRY_GENERAL;
        _dataEntryBuffer.clear();
        _dataEntryCursorPos = 0;
        updateDataEntryDisplay();
        return;
    }

    // Enter data entry mode with existing content
    programMode = DATA_ENTRY;

    // Set mode based on cell type (formulas stay as ENTRY_FORMULA, everything else is ENTRY_GENERAL)
    if (cell->getType() == CxSheetCell::FORMULA) {
        _dataEntryMode = ENTRY_FORMULA;
    } else {
        _dataEntryMode = ENTRY_GENERAL;
    }

    // Load cell content into buffer (getCellDisplayText shows formatted value)
    _dataEntryBuffer.fromCxString(getCellDisplayText(cell), 8);

    // Position cursor at end of existing content
    _dataEntryCursorPos = _dataEntryBuffer.charCount();

    updateDataEntryDisplay();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::focusDataEntry
//
// Handle keyboard input when in DATA_ENTRY mode.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::focusDataEntry(CxKeyAction keyAction)
{
    int action = keyAction.actionType();

    // ENTER commits the data (or cancels if buffer is empty)
    if (action == CxKeyAction::NEWLINE) {
        if (_dataEntryBuffer.isEmpty()) {
            cancelDataEntry();
        } else {
            commitDataEntry();
        }
        return;
    }

    // ESC cancels data entry (unless in formula mode where it enters cell hunt - future)
    if (action == CxKeyAction::COMMAND) {
        cancelDataEntry();
        return;
    }

    // TAB or up/down arrow in formula mode enters cell hunt mode
    if (_dataEntryMode == ENTRY_FORMULA) {
        if (action == CxKeyAction::TAB) {
            enterCellHuntMode("");
            return;
        }
        if (action == CxKeyAction::CURSOR) {
            CxString tag = keyAction.tag();
            if (tag == "<arrow-up>" || tag == "<arrow-down>") {
                enterCellHuntMode(tag);
                return;
            }
        }
    }

    // Arrow keys move cursor within buffer
    if (action == CxKeyAction::CURSOR) {
        CxString tag = keyAction.tag();
        if (tag == "<arrow-left>") {
            if (_dataEntryCursorPos > 0) {
                _dataEntryCursorPos--;
                updateDataEntryDisplay();
            }
        } else if (tag == "<arrow-right>") {
            if (_dataEntryCursorPos < _dataEntryBuffer.charCount()) {
                _dataEntryCursorPos++;
                updateDataEntryDisplay();
            }
        }
        return;
    }

    // BACKSPACE removes character before cursor (stops at start of buffer)
    if (action == CxKeyAction::BACKSPACE) {
        if (_dataEntryCursorPos > 0) {
            _dataEntryBuffer.remove(_dataEntryCursorPos - 1, 1);
            _dataEntryCursorPos--;
        }
        updateDataEntryDisplay();
        return;
    }

    // Character input
    if (action == CxKeyAction::LOWERCASE_ALPHA ||
        action == CxKeyAction::UPPERCASE_ALPHA ||
        action == CxKeyAction::SYMBOL ||
        action == CxKeyAction::NUMBER) {

        char c = keyAction.tag().data()[0];

        // If buffer is empty, re-deduce entry mode based on character typed
        if (_dataEntryBuffer.isEmpty()) {
            _dataEntryMode = deduceEntryModeFromChar(c);
            _dataEntryBuffer.clear();
            _dataEntryBuffer.append(CxUTFCharacter::fromASCII(c));
            _dataEntryCursorPos = 1;
            updateDataEntryDisplay();
            return;
        }

        // Validate and insert at cursor position if valid for current mode
        if (isValidInputChar(c, _dataEntryMode)) {
            _dataEntryBuffer.insert(_dataEntryCursorPos, CxUTFCharacter::fromASCII(c));
            _dataEntryCursorPos++;
            updateDataEntryDisplay();
        }
    }

    // Mouse click in formula mode - enter cell hunt mode and select clicked cell
    if (action == CxKeyAction::MOUSE_PRESS && _dataEntryMode == ENTRY_FORMULA) {
        int termRow = keyAction.mouseRow();
        int termCol = keyAction.mouseCol();
        int button = keyAction.mouseButton();

        if (button == 1) {  // left button only
            int dataRow, dataCol;
            if (sheetView->terminalToCell(termRow, termCol, &dataRow, &dataCol)) {
                // Enter cell hunt mode with the clicked cell
                _inCellHuntMode = 1;
                _cellHuntRangeActive = 0;
                _cellHuntFormulaPos = sheetModel->getCurrentPosition();
                _cellHuntCurrentPos = CxSheetCellCoordinate(dataRow, dataCol);
                _cellHuntAnchorPos = _cellHuntCurrentPos;
                _cellHuntInsertPos = _dataEntryCursorPos;

                // Update SheetView with hunt mode state
                sheetView->setCellHuntMode(1, _cellHuntFormulaPos, _cellHuntCurrentPos);
                sheetView->updateScreen();
                updateCellHuntDisplay();
            }
        }
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::commitDataEntry
//
// Commit the entered data to the current cell.
// Uses post-commit parsing (Excel-style) to determine cell type from input.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::commitDataEntry(void)
{
    CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
    CxSheetCell cell;

    // Convert UTF buffer to bytes for cell storage
    CxString bufferText = _dataEntryBuffer.toBytes();

    // Capture all formatting attributes from the existing cell before replacing it
    CxSheetCell *oldCellPtr = sheetModel->getCellPtr(pos);
    CellFormatAttrs oldAttrs;
    captureCellFormatting(oldCellPtr, &oldAttrs);

    if (_dataEntryMode == ENTRY_TEXTMAP) {
        // Textmap mode - strip leading '@' and store as appAttribute on EMPTY cell
        CxString ruleText = bufferText;
        if (ruleText.length() > 0 && ruleText.data()[0] == '@') {
            ruleText = ruleText.subString(1, ruleText.length() - 1);
        }
        // Cell stays EMPTY; the textmap rule is a display-layer attribute
        cell.setAppAttribute("textmap", ruleText.data());
        sheetModel->setCell(pos, cell);

        // Restore color attributes only (textmap cells don't have number formatting)
        CxSheetCell *cellPtr = sheetModel->getCellPtr(pos);
        if (cellPtr) {
            if (oldAttrs.hasFgColor) cellPtr->setAppAttribute("fgColor", oldAttrs.fgColor.data());
            if (oldAttrs.hasBgColor) cellPtr->setAppAttribute("bgColor", oldAttrs.bgColor.data());
        }
    }
    else if (_dataEntryMode == ENTRY_FORMULA) {
        // Formula mode - strip leading '=' and parse as formula
        CxString formulaText = bufferText;
        if (formulaText.length() > 0 && formulaText.data()[0] == '=') {
            formulaText = formulaText.subString(1, formulaText.length() - 1);
        }

        cell.setFormula(formulaText);
        sheetModel->setCell(pos, cell);

        // Restore all formatting attributes
        CxSheetCell *cellPtr = sheetModel->getCellPtr(pos);
        restoreCellFormatting(cellPtr, &oldAttrs);
    }
    else if (_dataEntryMode == ENTRY_GENERAL) {
        // Post-commit parsing via sheetModel library
        CxSheetInputParseResult result = CxSheetInputParser::parseAndClassify(bufferText);

        if (!result.success) {
            setMessage(CxString("Input error: ") + result.errorMsg);
            return;
        }

        // Set cell value based on parsed type
        if (result.cellType == 2) {  // DOUBLE
            cell.setDouble(CxDouble(result.doubleValue));
        } else if (result.cellType == 1) {  // TEXT
            cell.setText(result.textValue);
        }
        // cellType == 0 (EMPTY) leaves cell as default empty

        sheetModel->setCell(pos, cell);

        // Apply format attributes from parsing (dateFormat, currency, percent, thousands)
        CxSheetCell *cellPtr = sheetModel->getCellPtr(pos);
        if (cellPtr) {
            CxSheetInputParser::applyParsingAttributes(cellPtr, result);

            // Carry forward pre-existing formatting when input didn't specify it
            if (!result.hasCurrency && oldAttrs.hasCurrency) {
                cellPtr->setAppAttribute("currency", true);
            }
            if (!result.hasPercent && oldAttrs.hasPercent) {
                cellPtr->setAppAttribute("percent", true);
            }
            if (!result.hasThousands && oldAttrs.hasThousands) {
                cellPtr->setAppAttribute("thousands", true);
            }
            if (oldAttrs.hasDecimalPlaces && result.dateFormat.length() == 0
                && !result.hasCurrency) {
                cellPtr->setAppAttribute("decimalPlaces", oldAttrs.decimalPlaces);
            }
            if (oldAttrs.hasAlign) {
                cellPtr->setAppAttribute("align", oldAttrs.align.data());
            }

            // Restore color attributes
            if (oldAttrs.hasFgColor) cellPtr->setAppAttribute("fgColor", oldAttrs.fgColor.data());
            if (oldAttrs.hasBgColor) cellPtr->setAppAttribute("bgColor", oldAttrs.bgColor.data());
        }
    }

    // Return to edit mode
    programMode = EDIT;
    _dataEntryMode = ENTRY_NONE;
    _dataEntryBuffer.clear();

    // Move cursor down to next row (standard spreadsheet behavior)
    sheetModel->cursorDownRequest();

    // Skip hidden rows
    {
        CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
        if (sheetView->isRowHidden(pos.getRow())) {
            int visRow = sheetView->nextVisibleRow(pos.getRow(), 1);
            sheetModel->jumpToCell(CxSheetCellCoordinate(visRow, pos.getCol()));
        }
    }

    // Optimized refresh - redraw affected cells plus the new cursor position
    CxSList<CxSheetCellCoordinate> affected = sheetModel->getLastAffectedCells();
    affected.append(sheetModel->getCurrentPosition());
    sheetView->updateCells(affected);

    // Textmap cells depend on other cells but aren't in sheetModel's dependency
    // graph, so redraw any visible textmap cells after data changes
    sheetView->updateVisibleTextmapCells();

    resetPrompt();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::cancelDataEntry
//
// Cancel data entry and return to edit mode without changing the cell.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::cancelDataEntry(void)
{
    programMode = EDIT;
    _dataEntryMode = ENTRY_NONE;
    _dataEntryBuffer.clear();

    resetPrompt();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::updateDataEntryDisplay
//
// Update the command line to show the current data entry.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::updateDataEntryDisplay(void)
{
    CxString display;
    CxString bufferBytes = _dataEntryBuffer.toBytes();
    int prefixLen = 0;

    switch (_dataEntryMode) {
        case ENTRY_GENERAL:
            display = CxString("input: ") + bufferBytes;
            prefixLen = 7;  // "input: "
            break;
        case ENTRY_FORMULA:
            display = CxString("formula: ") + bufferBytes;
            prefixLen = 9;  // "formula: "
            break;
        case ENTRY_TEXTMAP:
            display = CxString("textmap: ") + bufferBytes;
            prefixLen = 9;  // "textmap: "
            break;
        default:
            display = bufferBytes;
            prefixLen = 0;
            break;
    }

    commandLineView->setDimMode(0);
    commandLineView->setText(display);
    commandLineView->updateScreen();

    // Convert character position to display column (for UTF-8 with wide characters)
    int displayCol = _dataEntryBuffer.displayColumnOfChar(_dataEntryCursorPos);
    commandLineView->placeCursorAt(prefixLen + displayCol);
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::enterCellHuntMode
//
// Enter cell hunt mode for selecting a cell reference in a formula. The direction parameter
// indicates which arrow key was pressed to enter the mode (up/down), or empty string for TAB.
// If an arrow was pressed, we move the hunt cursor one cell in that direction.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::enterCellHuntMode(CxString direction)
{
    _inCellHuntMode = 1;
    _cellHuntRangeActive = 0;

    // Save formula cell position (the cell being edited)
    _cellHuntFormulaPos = sheetModel->getCurrentPosition();

    // Start hunt at the formula cell, then move one step in the direction pressed
    _cellHuntCurrentPos = _cellHuntFormulaPos;
    _cellHuntAnchorPos = _cellHuntFormulaPos;

    // Save current cursor position for inserting reference later
    _cellHuntInsertPos = _dataEntryCursorPos;

    // Move one cell in the direction of the arrow (TAB enters with no move)
    if (direction == "<arrow-up>") {
        if (_cellHuntCurrentPos.getRow() > 0) {
            _cellHuntCurrentPos.setRow(_cellHuntCurrentPos.getRow() - 1);
        }
    } else if (direction == "<arrow-down>") {
        _cellHuntCurrentPos.setRow(_cellHuntCurrentPos.getRow() + 1);
    }

    // Update SheetView with hunt mode state
    sheetView->setCellHuntMode(1, _cellHuntFormulaPos, _cellHuntCurrentPos);
    sheetView->updateScreen();

    // Update command line to show formula with live reference
    updateCellHuntDisplay();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::exitCellHuntMode
//
// Exit cell hunt mode. If insertRef is true, insert the selected reference into the formula.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::exitCellHuntMode(int insertRef)
{
    _inCellHuntMode = 0;

    // Turn off hunt mode in SheetView
    sheetView->setCellHuntMode(0, _cellHuntFormulaPos, _cellHuntCurrentPos);

    if (insertRef) {
        // Build the reference string and insert it at the saved cursor position
        CxString ref = buildCellHuntReference();

        // Insert reference at the saved position in the data entry buffer
        for (int i = 0; i < (int)ref.length(); i++) {
            char c = ref.data()[i];
            _dataEntryBuffer.insert(_cellHuntInsertPos + i, CxUTFCharacter::fromASCII(c));
        }
        int endPos = _cellHuntInsertPos + ref.length();

        // Auto-insert closing paren after range selections (e.g., SUM($A$1:$A$10) )
        if (_cellHuntRangeActive) {
            _dataEntryBuffer.insert(endPos, CxUTFCharacter::fromASCII(')'));
            endPos++;
        }
        _dataEntryCursorPos = endPos;
    }

    // Reset hunt state
    _cellHuntRangeActive = 0;

    // Redraw the sheet and update the data entry display
    sheetView->updateScreen();
    updateDataEntryDisplay();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::focusCellHunt
//
// Handle keyboard input while in cell hunt mode.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::focusCellHunt(CxKeyAction keyAction)
{
    int action = keyAction.actionType();

    // ENTER - insert reference and return to formula editing
    if (action == CxKeyAction::NEWLINE) {
        exitCellHuntMode(1);  // insert reference
        return;
    }

    // ESC - cancel without inserting
    if (action == CxKeyAction::COMMAND) {
        exitCellHuntMode(0);  // don't insert
        return;
    }

    // Shift+arrow - start/extend range selection
    if (action == CxKeyAction::SHIFT_CURSOR) {
        CxString tag = keyAction.tag();
        CxSheetCellCoordinate oldPos = _cellHuntCurrentPos;

        // Anchor range on first shift+arrow
        if (!_cellHuntRangeActive) {
            _cellHuntRangeActive = 1;
            _cellHuntAnchorPos = _cellHuntCurrentPos;
        }

        if (tag == "<shift-arrow-up>") {
            if (_cellHuntCurrentPos.getRow() > 0) {
                _cellHuntCurrentPos.setRow(_cellHuntCurrentPos.getRow() - 1);
            }
        } else if (tag == "<shift-arrow-down>") {
            _cellHuntCurrentPos.setRow(_cellHuntCurrentPos.getRow() + 1);
        } else if (tag == "<shift-arrow-left>") {
            if (_cellHuntCurrentPos.getCol() > 0) {
                _cellHuntCurrentPos.setCol(_cellHuntCurrentPos.getCol() - 1);
            }
        } else if (tag == "<shift-arrow-right>") {
            _cellHuntCurrentPos.setCol(_cellHuntCurrentPos.getCol() + 1);
        }

        sheetView->setHuntRange(1, _cellHuntAnchorPos, _cellHuntCurrentPos);
        sheetView->updateCellHuntMove(oldPos, _cellHuntCurrentPos);
        updateCellHuntDisplay();
        return;
    }

    // Arrow keys - move hunt cursor (single cell, resets range)
    if (action == CxKeyAction::CURSOR) {
        CxString tag = keyAction.tag();
        CxSheetCellCoordinate oldPos = _cellHuntCurrentPos;

        if (tag == "<arrow-up>") {
            if (_cellHuntCurrentPos.getRow() > 0) {
                _cellHuntCurrentPos.setRow(_cellHuntCurrentPos.getRow() - 1);
            }
        } else if (tag == "<arrow-down>") {
            _cellHuntCurrentPos.setRow(_cellHuntCurrentPos.getRow() + 1);
        } else if (tag == "<arrow-left>") {
            if (_cellHuntCurrentPos.getCol() > 0) {
                _cellHuntCurrentPos.setCol(_cellHuntCurrentPos.getCol() - 1);
            }
        } else if (tag == "<arrow-right>") {
            _cellHuntCurrentPos.setCol(_cellHuntCurrentPos.getCol() + 1);
        }

        // Plain arrow resets any active range
        if (_cellHuntRangeActive) {
            _cellHuntRangeActive = 0;
            sheetView->setHuntRange(0, _cellHuntAnchorPos, _cellHuntCurrentPos);
        }

        sheetView->updateCellHuntMove(oldPos, _cellHuntCurrentPos);
        updateCellHuntDisplay();
        return;
    }

    // Mouse click - select cell for reference
    if (action == CxKeyAction::MOUSE_PRESS) {
        int termRow = keyAction.mouseRow();
        int termCol = keyAction.mouseCol();
        int button = keyAction.mouseButton();

        if (button == 1) {  // left button only
            int dataRow, dataCol;
            if (sheetView->terminalToCell(termRow, termCol, &dataRow, &dataCol)) {
                CxSheetCellCoordinate oldPos = _cellHuntCurrentPos;
                _cellHuntCurrentPos = CxSheetCellCoordinate(dataRow, dataCol);

                // If shift+click, extend as range from anchor
                if (keyAction.mouseShift() && !_cellHuntRangeActive) {
                    _cellHuntRangeActive = 1;
                    // _cellHuntAnchorPos stays where it was
                }

                // If not yet in range mode and plain click, reset anchor to clicked cell
                if (!_cellHuntRangeActive) {
                    _cellHuntAnchorPos = _cellHuntCurrentPos;
                }

                // Update SheetView
                if (_cellHuntRangeActive) {
                    sheetView->setHuntRange(1, _cellHuntAnchorPos, _cellHuntCurrentPos);
                }
                sheetView->updateCellHuntMove(oldPos, _cellHuntCurrentPos);
                updateCellHuntDisplay();
            }
        }
        return;
    }

    // Mouse drag - extend range selection
    if (action == CxKeyAction::MOUSE_DRAG) {
        int termRow = keyAction.mouseRow();
        int termCol = keyAction.mouseCol();
        int button = keyAction.mouseButton();

        if (button == 1) {  // left button only
            int dataRow, dataCol;
            if (sheetView->terminalToCell(termRow, termCol, &dataRow, &dataCol)) {
                CxSheetCellCoordinate oldPos = _cellHuntCurrentPos;
                CxSheetCellCoordinate newPos(dataRow, dataCol);

                if (oldPos.getRow() != newPos.getRow() || oldPos.getCol() != newPos.getCol()) {
                    // Start range selection if not already active
                    if (!_cellHuntRangeActive) {
                        _cellHuntRangeActive = 1;
                        // Anchor is where we started the drag (previous position)
                    }

                    _cellHuntCurrentPos = newPos;
                    sheetView->setHuntRange(1, _cellHuntAnchorPos, _cellHuntCurrentPos);
                    sheetView->updateCellHuntMove(oldPos, _cellHuntCurrentPos);
                    updateCellHuntDisplay();
                }
            }
        }
        return;
    }

    // Mouse release - insert reference and return to formula editing
    if (action == CxKeyAction::MOUSE_RELEASE) {
        exitCellHuntMode(1);  // insert reference
        return;
    }

    // Mouse wheel - scroll viewport while in cell hunt mode
    if (action == CxKeyAction::MOUSE_WHEEL) {
        int button = keyAction.mouseButton();
        int scrollAmount = 3;

        // button 4 = wheel up, button 5 = wheel down
        int rowDelta = (button == 4) ? -scrollAmount : scrollAmount;

        // Shift+wheel scrolls horizontally
        if (keyAction.mouseShift()) {
            if (sheetView->scrollViewport(0, rowDelta)) {
                sheetView->updateScreen();
                updateCellHuntDisplay();
            }
        } else {
            if (sheetView->scrollViewport(rowDelta, 0)) {
                sheetView->updateScreen();
                updateCellHuntDisplay();
            }
        }
        return;
    }

    // Any other key (printable chars, etc.) - insert reference and process the key
    // as formula input. This lets the user type ")" immediately after selecting a range.
    exitCellHuntMode(1);  // insert reference
    focusDataEntry(keyAction);  // process the key as formula input
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::updateCellHuntDisplay
//
// Update the command line to show the formula with the current cell reference preview.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::updateCellHuntDisplay(void)
{
    // Build the formula string with the reference at the insert position
    CxString ref = buildCellHuntReference();

    // Split buffer at insert position using character-based operations (not byte-based)
    // _cellHuntInsertPos is a character index, so use CxUTFString subString
    int bufferLen = _dataEntryBuffer.charCount();
    CxUTFString beforePart = _dataEntryBuffer.subString(0, _cellHuntInsertPos);
    CxUTFString afterPart;
    if (_cellHuntInsertPos < bufferLen) {
        afterPart = _dataEntryBuffer.subString(_cellHuntInsertPos, bufferLen - _cellHuntInsertPos);
    }

    // Convert to bytes for display string construction
    CxString beforeInsert = beforePart.toBytes();
    CxString afterInsert = afterPart.toBytes();

    CxString display = CxString("formula: ") + beforeInsert + ref + afterInsert;

    commandLineView->setDimMode(0);
    commandLineView->setText(display);
    commandLineView->updateScreen();

    // Place cursor after the reference preview - use display width for proper positioning
    int displayColBeforeInsert = beforePart.displayWidth();
    commandLineView->placeCursorAt(9 + displayColBeforeInsert + ref.length());
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::buildCellHuntReference
//
// Build the cell reference string. Returns relative format like A1 for single cell,
// or A1:C3 for a range. User can manually add $ for absolute references.
//-------------------------------------------------------------------------------------------------
CxString
SheetEditor::buildCellHuntReference(void)
{
    if (_cellHuntRangeActive) {
        // Range reference
        CxString anchorAddr = _cellHuntAnchorPos.toAddress();
        CxString currentAddr = _cellHuntCurrentPos.toAddress();
        return anchorAddr + ":" + currentAddr;
    } else {
        // Single cell reference
        return _cellHuntCurrentPos.toAddress();
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::showHelpView
//
// Display the help modal and switch to HELPVIEW mode.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::showHelpView(void)
{
    screen->flush();
    screen->hideCursor();
    helpView->setVisible(1);
    helpView->rebuildVisibleItems();
    helpView->recalcScreenPlacements();
    helpView->redraw();
    programMode = HELPVIEW;
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::focusHelpView
//
// Handle keyboard input when in HELPVIEW mode.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::focusHelpView(CxKeyAction keyAction)
{
    int action = keyAction.actionType();

    // ESC - dismiss help view
    if (action == CxKeyAction::COMMAND) {
        helpView->setVisible(0);
        programMode = EDIT;
        sheetView->updateScreen();
        resetPrompt();
        screen->showCursor();
        return;
    }

    // ENTER - toggle section expand/collapse or dismiss
    if (action == CxKeyAction::NEWLINE) {
        HelpViewItemType selType = helpView->getSelectedItemType();
        if (selType == HELPITEM_SECTION) {
            helpView->toggleSelectedSection();
            helpView->redraw();
        } else {
            // Dismiss on content lines
            helpView->setVisible(0);
            programMode = EDIT;
            sheetView->updateScreen();
            resetPrompt();
            screen->showCursor();
        }
        return;
    }

    // Mouse handling - click outside dismisses, inside ignored
#if defined(_OSX_) || defined(_LINUX_)
    if (action == CxKeyAction::MOUSE_PRESS || action == CxKeyAction::MOUSE_DOUBLE_CLICK) {
        if (!helpView->isInsideFrame(keyAction.mouseRow(), keyAction.mouseCol())) {
            helpView->setVisible(0);
            programMode = EDIT;
            sheetView->updateScreen();
            resetPrompt();
            screen->showCursor();
        }
        return;
    }
    if (action == CxKeyAction::MOUSE_WHEEL) {
        int button = keyAction.mouseButton();
        if (button == 4) {
            CxKeyAction upAction("CURSOR:<arrow-up>");
            helpView->routeKeyAction(upAction);
            helpView->routeKeyAction(upAction);
            helpView->routeKeyAction(upAction);
        } else if (button == 5) {
            CxKeyAction downAction("CURSOR:<arrow-down>");
            helpView->routeKeyAction(downAction);
            helpView->routeKeyAction(downAction);
            helpView->routeKeyAction(downAction);
        }
        return;
    }
    if (action == CxKeyAction::MOUSE_RELEASE || action == CxKeyAction::MOUSE_DRAG) {
        return;
    }
#endif

    // Route other keys (arrows for navigation)
    helpView->routeKeyAction(keyAction);
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::normalizeRange
//
// Normalize a range defined by start and end coordinates. Returns min/max row and col
// values ensuring minRow <= maxRow and minCol <= maxCol.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::normalizeRange(CxSheetCellCoordinate start, CxSheetCellCoordinate end,
                            int *minRow, int *maxRow, int *minCol, int *maxCol)
{
    *minRow = start.getRow();
    *maxRow = end.getRow();
    if (*minRow > *maxRow) {
        int tmp = *minRow;
        *minRow = *maxRow;
        *maxRow = tmp;
    }

    *minCol = start.getCol();
    *maxCol = end.getCol();
    if (*minCol > *maxCol) {
        int tmp = *minCol;
        *minCol = *maxCol;
        *maxCol = tmp;
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::getColumnRange
//
// Get the column range based on current selection state. If range selection is active,
// returns the normalized column range. Otherwise returns current cursor column as both
// start and end.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::getColumnRange(int *startCol, int *endCol)
{
    if (_rangeSelectActive) {
        *startCol = _rangeAnchor.getCol();
        *endCol = _rangeCurrent.getCol();
        if (*startCol > *endCol) {
            int tmp = *startCol;
            *startCol = *endCol;
            *endCol = tmp;
        }
    } else {
        *startCol = sheetModel->getCurrentPosition().getCol();
        *endCol = *startCol;
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::clearRangeSelection
//
// Clear the active range selection and update the display.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::clearRangeSelection(void)
{
    if (_rangeSelectActive) {
        _rangeSelectActive = 0;
        sheetView->setRangeSelection(0, _rangeAnchor, _rangeCurrent);
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::setCellAttribute
//
// Set a string attribute on a cell, creating the cell if it doesn't exist.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::setCellAttribute(CxSheetCellCoordinate coord, const char *attrName, const char *value)
{
    CxSheetCell *cell = sheetModel->getCellPtr(coord);
    if (cell == NULL) {
        CxSheetCell newCell;
        newCell.setAppAttribute(attrName, value);
        sheetModel->setCell(coord, newCell);
    } else {
        cell->setAppAttribute(attrName, value);
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::setCellAttributeBool
//
// Set a boolean attribute on a cell, creating the cell if it doesn't exist.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::setCellAttributeBool(CxSheetCellCoordinate coord, const char *attrName, int value)
{
    CxSheetCell *cell = sheetModel->getCellPtr(coord);
    if (cell == NULL) {
        CxSheetCell newCell;
        newCell.setAppAttribute(attrName, value ? true : false);
        sheetModel->setCell(coord, newCell);
    } else {
        cell->setAppAttribute(attrName, value ? true : false);
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::setCellAttributeInt
//
// Set an integer attribute on a cell, creating the cell if it doesn't exist.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::setCellAttributeInt(CxSheetCellCoordinate coord, const char *attrName, int value)
{
    CxSheetCell *cell = sheetModel->getCellPtr(coord);
    if (cell == NULL) {
        CxSheetCell newCell;
        newCell.setAppAttribute(attrName, value);
        sheetModel->setCell(coord, newCell);
    } else {
        cell->setAppAttribute(attrName, value);
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::getRangeOrCell
//
// Get the current range selection, or current cell as a single-cell range.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::getRangeOrCell(CxSheetCellCoordinate *start, CxSheetCellCoordinate *end)
{
    if (_rangeSelectActive) {
        *start = _rangeAnchor;
        *end = _rangeCurrent;
    } else {
        *start = sheetModel->getCurrentPosition();
        *end = *start;
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::clearCellAttributeInColumns
//
// Clear a cell-level attribute from all cells in the given column range.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::clearCellAttributeInColumns(int startCol, int endCol, const char *attrName)
{
    int maxRow = (int)sheetModel->numberOfRows();
    for (int col = startCol; col <= endCol; col++) {
        for (int row = 0; row < maxRow; row++) {
            CxSheetCellCoordinate coord(row, col);
            CxSheetCell *cell = sheetModel->getCellPtr(coord);
            if (cell != NULL && cell->hasAppAttribute(attrName)) {
                cell->removeAppAttribute(attrName);
            }
        }
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::applyCellStringAttribute
//
// Apply a string attribute to all cells in the current range/cell.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::applyCellStringAttribute(const char *attrName, const char *value, const char *message)
{
    CxSheetCellCoordinate start, end;
    getRangeOrCell(&start, &end);

    int minRow, maxRow, minCol, maxCol;
    normalizeRange(start, end, &minRow, &maxRow, &minCol, &maxCol);

    int cellCount = 0;
    for (int row = minRow; row <= maxRow; row++) {
        for (int col = minCol; col <= maxCol; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);
            setCellAttribute(coord, attrName, value);
            cellCount++;
        }
    }

    clearRangeSelection();
    sheetView->updateScreen();

    if (cellCount == 1) {
        char buf[64];
        snprintf(buf, sizeof(buf), "(1 cell %s)", message);
        setMessage(buf);
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "(%d cells %s)", cellCount, message);
        setMessage(buf);
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::toggleCellBoolAttribute
//
// Toggle a boolean attribute on all cells in the current range/cell.
// First cell determines the new state; all cells in range get the same state.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::toggleCellBoolAttribute(const char *attrName, const char *label)
{
    CxSheetCellCoordinate start, end;
    getRangeOrCell(&start, &end);

    int minRow, maxRow, minCol, maxCol;
    normalizeRange(start, end, &minRow, &maxRow, &minCol, &maxCol);

    int cellCount = 0;
    int newState = -1;

    for (int row = minRow; row <= maxRow; row++) {
        for (int col = minCol; col <= maxCol; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);

            CxSheetCell *cell = sheetModel->getCellPtr(coord);
            if (cell == NULL) {
                CxSheetCell newCell;
                newCell.setAppAttribute(attrName, true);
                sheetModel->setCell(coord, newCell);
                if (newState < 0) newState = 1;
            } else {
                bool current = cell->getAppAttributeBool(attrName, false);
                if (newState < 0) newState = current ? 0 : 1;
                cell->setAppAttribute(attrName, newState == 1);
            }
            cellCount++;
        }
    }

    clearRangeSelection();
    sheetView->updateScreen();

    char buf[64];
    const char *state = (newState == 1) ? "on" : "off";
    if (cellCount == 1) {
        snprintf(buf, sizeof(buf), "(1 cell %s %s)", label, state);
    } else {
        snprintf(buf, sizeof(buf), "(%d cells %s %s)", cellCount, label, state);
    }
    setMessage(buf);
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::setColumnAlignment
//
// Set alignment as column default for the current column or selected columns.
// Clears cell-level align attributes.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::setColumnAlignment(int alignValue, const char *message)
{
    int startCol, endCol;
    getColumnRange(&startCol, &endCol);

    for (int col = startCol; col <= endCol; col++) {
        sheetView->setColAlign(col, alignValue);
    }

    clearCellAttributeInColumns(startCol, endCol, "align");
    clearRangeSelection();
    sheetView->updateScreen();
    setMessage(message);
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::toggleColumnBoolFormat
//
// Toggle a boolean column format (currency, percent, thousands) for the current column
// or selected columns. Clears cell-level attributes in affected columns.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::toggleColumnBoolFormat(const char *attrName, const char *label)
{
    int startCol, endCol;
    getColumnRange(&startCol, &endCol);

    // Get current state from first column to determine toggle direction
    int currentVal = 0;
    if (strcmp(attrName, "currency") == 0) {
        currentVal = sheetView->getColCurrency(startCol);
    } else if (strcmp(attrName, "percent") == 0) {
        currentVal = sheetView->getColPercent(startCol);
    } else if (strcmp(attrName, "thousands") == 0) {
        currentVal = sheetView->getColThousands(startCol);
    }
    int newVal = (currentVal == 1) ? 2 : 1;  // toggle: 1=on, 2=off

    // Apply to all columns in range
    for (int col = startCol; col <= endCol; col++) {
        if (strcmp(attrName, "currency") == 0) {
            sheetView->setColCurrency(col, newVal);
        } else if (strcmp(attrName, "percent") == 0) {
            sheetView->setColPercent(col, newVal);
        } else if (strcmp(attrName, "thousands") == 0) {
            sheetView->setColThousands(col, newVal);
        }
    }

    clearCellAttributeInColumns(startCol, endCol, attrName);
    clearRangeSelection();
    sheetView->updateScreen();

    char buf[64];
    snprintf(buf, sizeof(buf), "Column %s: %s", label, newVal == 1 ? "on" : "off");
    setMessage(buf);
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::copyRangeToClipboard
//
// Copy cells from start to end coordinates into the clipboard. Stores each cell with its
// offset from the top-left of the range.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::copyRangeToClipboard(CxSheetCellCoordinate start, CxSheetCellCoordinate end)
{
    // Clear existing clipboard
    _clipboard.clear();

    // Normalize range (ensure start is top-left, end is bottom-right)
    int minRow, maxRow, minCol, maxCol;
    normalizeRange(start, end, &minRow, &maxRow, &minCol, &maxCol);

    // Store dimensions and anchor
    _clipboardRows = maxRow - minRow + 1;
    _clipboardCols = maxCol - minCol + 1;
    _clipboardAnchor.setRow(minRow);
    _clipboardAnchor.setCol(minCol);

    // Copy each cell
    for (int row = minRow; row <= maxRow; row++) {
        for (int col = minCol; col <= maxCol; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);

            CxSheetCell *srcCell = sheetModel->getCellPtr(coord);

            ClipboardCell clipCell;
            clipCell.rowOffset = row - minRow;
            clipCell.colOffset = col - minCol;

            if (srcCell != NULL) {
                clipCell.cell = *srcCell;  // copy the cell
            }
            // else clipCell.cell is default empty

            _clipboard.append(clipCell);
        }
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_Copy
//
// Copy the current selection (or current cell if no selection) to the clipboard.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_Copy(CxString commandLine)
{
    (void)commandLine;  // unused

    CxSheetCellCoordinate start, end;

    if (_rangeSelectActive) {
        start = _rangeAnchor;
        end = _rangeCurrent;
    } else {
        // Single cell
        start = sheetModel->getCurrentPosition();
        end = start;
    }

    copyRangeToClipboard(start, end);
    _clipboardIsCut = 0;

    int cellCount = (int)_clipboard.entries();
    if (cellCount == 1) {
        setMessage("(1 cell copied)");
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "(%d cells copied)", cellCount);
        setMessage(buf);
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_Cut
//
// Cut the current selection (or current cell if no selection) to the clipboard.
// Copies the cells and then clears the source using setCell to notify dependency graph.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_Cut(CxString commandLine)
{
    (void)commandLine;  // unused

    CxSheetCellCoordinate start, end;

    if (_rangeSelectActive) {
        start = _rangeAnchor;
        end = _rangeCurrent;
    } else {
        // Single cell
        start = sheetModel->getCurrentPosition();
        end = start;
    }

    // Copy first
    copyRangeToClipboard(start, end);
    _clipboardIsCut = 1;

    int cellCount = (int)_clipboard.entries();

    // Normalize range for clearing
    int minRow, maxRow, minCol, maxCol;
    normalizeRange(start, end, &minRow, &maxRow, &minCol, &maxCol);

    // Clear source cells (use setCell to notify dependency graph)
    for (int row = minRow; row <= maxRow; row++) {
        for (int col = minCol; col <= maxCol; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);

            CxSheetCell emptyCell;
            sheetModel->setCell(coord, emptyCell);
        }
    }

    clearRangeSelection();

    sheetView->updateScreen();

    if (cellCount == 1) {
        setMessage("(1 cell cut)");
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "(%d cells cut)", cellCount);
        setMessage(buf);
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_Paste
//
// Paste clipboard contents at the current cursor position.
// Cut: uses sheetModel->moveCells() to move cells and update all references.
// Copy: adjusts formula references relative to paste position.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_Paste(CxString commandLine)
{
    (void)commandLine;  // unused

    if (_clipboard.entries() == 0) {
        setMessage("Clipboard is empty");
        return;
    }

    CxSheetCellCoordinate pasteAnchor = sheetModel->getCurrentPosition();

    if (_clipboardIsCut) {
        // Build old/new coordinate lists and call model's moveCells
        CxSList<CxSheetCellCoordinate> oldCoords;
        CxSList<CxSheetCellCoordinate> newCoords;

        for (int i = 0; i < (int)_clipboard.entries(); i++) {
            ClipboardCell clipCell = _clipboard.at(i);

            CxSheetCellCoordinate oldCoord;
            oldCoord.setRow(_clipboardAnchor.getRow() + clipCell.rowOffset);
            oldCoord.setCol(_clipboardAnchor.getCol() + clipCell.colOffset);

            CxSheetCellCoordinate newCoord;
            newCoord.setRow(pasteAnchor.getRow() + clipCell.rowOffset);
            newCoord.setCol(pasteAnchor.getCol() + clipCell.colOffset);

            oldCoords.append(oldCoord);
            newCoords.append(newCoord);
        }

        sheetModel->moveCells(oldCoords, newCoords);

    } else {
        // Copy/paste: place cells with formula adjustment via model method
        int rowDelta = pasteAnchor.getRow() - _clipboardAnchor.getRow();
        int colDelta = pasteAnchor.getCol() - _clipboardAnchor.getCol();

        for (int i = 0; i < (int)_clipboard.entries(); i++) {
            ClipboardCell clipCell = _clipboard.at(i);

            CxSheetCellCoordinate targetCoord;
            targetCoord.setRow(pasteAnchor.getRow() + clipCell.rowOffset);
            targetCoord.setCol(pasteAnchor.getCol() + clipCell.colOffset);

            CxSheetCell newCell = clipCell.cell;

            if (newCell.getType() == CxSheetCell::FORMULA) {
                CxString adjusted = sheetModel->adjustFormulaForCopy(
                    newCell.getFormulaText(), rowDelta, colDelta);
                newCell.setFormula(adjusted);
            }

            sheetModel->setCell(targetCoord, newCell);
        }
    }

    clearRangeSelection();

    // Use standard getLastAffectedCells() contract
    CxSList<CxSheetCellCoordinate> affected = sheetModel->getLastAffectedCells();
    sheetView->updateCells(affected);
    sheetView->updateVisibleTextmapCells();

    int cellCount = (int)_clipboard.entries();
    if (cellCount == 1) {
        setMessage("(1 cell pasted)");
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "(%d cells pasted)", cellCount);
        setMessage(buf);
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_Clear
//
// Clear the contents of the current selection (or current cell if no selection).
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_Clear(CxString commandLine)
{
    (void)commandLine;  // unused

    CxSheetCellCoordinate start, end;

    if (_rangeSelectActive) {
        start = _rangeAnchor;
        end = _rangeCurrent;
    } else {
        // Single cell
        start = sheetModel->getCurrentPosition();
        end = start;
    }

    // Normalize range
    int minRow, maxRow, minCol, maxCol;
    normalizeRange(start, end, &minRow, &maxRow, &minCol, &maxCol);

    int cellCount = 0;

    // Clear cells - replace with empty cells to remove all data and attributes
    for (int row = minRow; row <= maxRow; row++) {
        for (int col = minCol; col <= maxCol; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);

            CxSheetCell emptyCell;
            sheetModel->setCell(coord, emptyCell);
            cellCount++;
        }
    }

    clearRangeSelection();

    sheetView->updateScreen();

    if (cellCount == 1) {
        setMessage("(1 cell cleared)");
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "(%d cells cleared)", cellCount);
        setMessage(buf);
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FillDown
//
// Fill the selection down from the first row. The source row (first row of selection) is
// copied to all subsequent rows, adjusting relative row references in formulas.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FillDown(CxString commandLine)
{
    (void)commandLine;

    // Must have range selection
    if (!_rangeSelectActive) {
        setMessage("Select a range first (Shift+Arrow)");
        return;
    }

    // Normalize range
    int minRow, maxRow, minCol, maxCol;
    normalizeRange(_rangeAnchor, _rangeCurrent, &minRow, &maxRow, &minCol, &maxCol);

    // Need at least 2 rows
    if (maxRow - minRow < 1) {
        setMessage("Need at least 2 rows to fill down");
        return;
    }

    sheetModel->fillDown(minRow, maxRow, minCol, maxCol);

    clearRangeSelection();
    CxSList<CxSheetCellCoordinate> affected = sheetModel->getLastAffectedCells();
    sheetView->updateCells(affected);
    sheetView->updateVisibleTextmapCells();

    int rowsFilled = maxRow - minRow;
    char buf[64];
    snprintf(buf, sizeof(buf), "(Filled down %d row%s)",
             rowsFilled, rowsFilled == 1 ? "" : "s");
    setMessage(buf);
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FillRight
//
// Fill the selection right from the first column. The source column (first column of selection)
// is copied to all subsequent columns, adjusting relative column references in formulas.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FillRight(CxString commandLine)
{
    (void)commandLine;

    // Must have range selection
    if (!_rangeSelectActive) {
        setMessage("Select a range first (Shift+Arrow)");
        return;
    }

    // Normalize range
    int minRow, maxRow, minCol, maxCol;
    normalizeRange(_rangeAnchor, _rangeCurrent, &minRow, &maxRow, &minCol, &maxCol);

    // Need at least 2 columns
    if (maxCol - minCol < 1) {
        setMessage("Need at least 2 columns to fill right");
        return;
    }

    sheetModel->fillRight(minRow, maxRow, minCol, maxCol);

    clearRangeSelection();
    CxSList<CxSheetCellCoordinate> affected = sheetModel->getLastAffectedCells();
    sheetView->updateCells(affected);
    sheetView->updateVisibleTextmapCells();

    int colsFilled = maxCol - minCol;
    char buf[64];
    snprintf(buf, sizeof(buf), "(Filled right %d column%s)",
             colsFilled, colsFilled == 1 ? "" : "s");
    setMessage(buf);
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatCellAlignLeft
//
// Set left alignment on the current selection (or current cell if no selection).
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatCellAlignLeft(CxString commandLine)
{
    (void)commandLine;
    applyCellStringAttribute("align", "left", "left-aligned");
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatCellAlignCenter
//
// Set center alignment on the current selection (or current cell if no selection).
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatCellAlignCenter(CxString commandLine)
{
    (void)commandLine;
    applyCellStringAttribute("align", "center", "centered");
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatCellAlignRight
//
// Set right alignment on the current selection (or current cell if no selection).
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatCellAlignRight(CxString commandLine)
{
    (void)commandLine;
    applyCellStringAttribute("align", "right", "right-aligned");
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::cycleNumberFormat
//
// Cycle through number formats on the current selection (or current cell).
// Cycle: plain → $,.2 → $,.0 → ,.2 → ,.0 → plain
// If range has mixed formats, first press syncs to first cell's format.
//-------------------------------------------------------------------------------------------------

// Helper to get number format state from a cell
// Returns: 0=plain, 1=$,.2, 2=$,.0, 3=,.2, 4=,.0
static int getNumberFormatState(CxSheetCell *cell)
{
    if (cell == NULL) return 0;

    int hasCurrency = cell->getAppAttributeBool("currency", false) ? 1 : 0;
    int hasThousands = cell->getAppAttributeBool("thousands", false) ? 1 : 0;
    int decimals = cell->getAppAttributeInt("decimalPlaces", -1);

    if (hasCurrency && hasThousands && decimals == 2) {
        return 1;  // $,.2
    } else if (hasCurrency && hasThousands && decimals == 0) {
        return 2;  // $,.0
    } else if (!hasCurrency && hasThousands && decimals == 2) {
        return 3;  // ,.2
    } else if (!hasCurrency && hasThousands && decimals == 0) {
        return 4;  // ,.0
    } else if (hasCurrency || hasThousands || decimals >= 0) {
        return 4;  // Some other format - treat as state 4
    }
    return 0;  // plain
}

// Helper to apply number format state to a cell
static void applyNumberFormatState(CxSheetCell *cell, int state)
{
    if (cell == NULL) return;

    switch (state) {
        case 0:  // plain
            cell->removeAppAttribute("currency");
            cell->removeAppAttribute("thousands");
            cell->removeAppAttribute("decimalPlaces");
            break;
        case 1:  // $,.2
            cell->setAppAttribute("currency", true);
            cell->setAppAttribute("thousands", true);
            cell->setAppAttribute("decimalPlaces", 2);
            break;
        case 2:  // $,.0
            cell->setAppAttribute("currency", true);
            cell->setAppAttribute("thousands", true);
            cell->setAppAttribute("decimalPlaces", 0);
            break;
        case 3:  // ,.2
            cell->removeAppAttribute("currency");
            cell->setAppAttribute("thousands", true);
            cell->setAppAttribute("decimalPlaces", 2);
            break;
        case 4:  // ,.0
            cell->removeAppAttribute("currency");
            cell->setAppAttribute("thousands", true);
            cell->setAppAttribute("decimalPlaces", 0);
            break;
    }
}

void
SheetEditor::cycleNumberFormat(void)
{
    CxSheetCellCoordinate start, end;

    if (_rangeSelectActive) {
        start = _rangeAnchor;
        end = _rangeCurrent;
    } else {
        start = sheetModel->getCurrentPosition();
        end = start;
    }

    // Normalize range
    int minRow, maxRow, minCol, maxCol;
    normalizeRange(start, end, &minRow, &maxRow, &minCol, &maxCol);

    // Get first cell's state
    CxSheetCellCoordinate firstCoord;
    firstCoord.setRow(minRow);
    firstCoord.setCol(minCol);
    CxSheetCell *firstCell = sheetModel->getCellPtr(firstCoord);
    int firstState = getNumberFormatState(firstCell);

    // Check if all cells have the same state
    int allSame = 1;
    for (int row = minRow; row <= maxRow && allSame; row++) {
        for (int col = minCol; col <= maxCol && allSame; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);
            CxSheetCell *cell = sheetModel->getCellPtr(coord);
            if (getNumberFormatState(cell) != firstState) {
                allSame = 0;
            }
        }
    }

    // If mixed, sync to first cell's format; otherwise advance to next
    int targetState = allSame ? ((firstState + 1) % 5) : firstState;

    // Apply target state to all cells in range
    for (int row = minRow; row <= maxRow; row++) {
        for (int col = minCol; col <= maxCol; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);

            CxSheetCell *cell = sheetModel->getCellPtr(coord);
            if (cell == NULL && targetState != 0) {
                CxSheetCell newCell;
                sheetModel->setCell(coord, newCell);
                cell = sheetModel->getCellPtr(coord);
            }

            applyNumberFormatState(cell, targetState);
        }
    }

    sheetView->updateScreen();
    resetPrompt();

    const char *formatNames[] = { "plain", "$,.2", "$,.0", ",.2" };
    setMessage(formatNames[targetState]);
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::cycleAlignment
//
// Cycle through alignment options on the current selection (or current cell).
// Cycle: left → center → right → left
// If range has mixed alignments, first press syncs to first cell's alignment.
//-------------------------------------------------------------------------------------------------

// Helper to get alignment state from a cell
// Returns: 0=left, 1=center, 2=right
static int getAlignmentState(CxSheetCell *cell)
{
    if (cell == NULL || !cell->hasAppAttribute("align")) return 0;

    CxString align = cell->getAppAttributeString("align");
    if (align == "center") return 1;
    if (align == "right") return 2;
    return 0;  // left or unknown
}

void
SheetEditor::cycleAlignment(void)
{
    CxSheetCellCoordinate start, end;

    if (_rangeSelectActive) {
        start = _rangeAnchor;
        end = _rangeCurrent;
    } else {
        start = sheetModel->getCurrentPosition();
        end = start;
    }

    // Normalize range
    int minRow, maxRow, minCol, maxCol;
    normalizeRange(start, end, &minRow, &maxRow, &minCol, &maxCol);

    // Get first cell's state
    CxSheetCellCoordinate firstCoord;
    firstCoord.setRow(minRow);
    firstCoord.setCol(minCol);
    CxSheetCell *firstCell = sheetModel->getCellPtr(firstCoord);
    int firstState = getAlignmentState(firstCell);

    // Check if all cells have the same state
    int allSame = 1;
    for (int row = minRow; row <= maxRow && allSame; row++) {
        for (int col = minCol; col <= maxCol && allSame; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);
            CxSheetCell *cell = sheetModel->getCellPtr(coord);
            if (getAlignmentState(cell) != firstState) {
                allSame = 0;
            }
        }
    }

    // If mixed, sync to first cell's alignment; otherwise advance to next
    int targetState = allSame ? ((firstState + 1) % 3) : firstState;
    const char *alignments[] = { "left", "center", "right" };

    // Apply target state to all cells in range
    for (int row = minRow; row <= maxRow; row++) {
        for (int col = minCol; col <= maxCol; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);

            CxSheetCell *cell = sheetModel->getCellPtr(coord);
            if (cell == NULL) {
                CxSheetCell newCell;
                sheetModel->setCell(coord, newCell);
                cell = sheetModel->getCellPtr(coord);
            }

            if (cell != NULL) {
                cell->setAppAttribute("align", alignments[targetState]);
            }
        }
    }

    sheetView->updateScreen();
    resetPrompt();

    setMessage(alignments[targetState]);
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::togglePercentFormat
//
// Toggle percent format on the current selection (or current cell).
// If range has mixed percent states, first press syncs to first cell's state.
//-------------------------------------------------------------------------------------------------

// Helper to get percent state from a cell
static int getPercentState(CxSheetCell *cell)
{
    if (cell == NULL) return 0;
    return cell->getAppAttributeBool("percent", false) ? 1 : 0;
}

void
SheetEditor::togglePercentFormat(void)
{
    CxSheetCellCoordinate start, end;

    if (_rangeSelectActive) {
        start = _rangeAnchor;
        end = _rangeCurrent;
    } else {
        start = sheetModel->getCurrentPosition();
        end = start;
    }

    // Normalize range
    int minRow, maxRow, minCol, maxCol;
    normalizeRange(start, end, &minRow, &maxRow, &minCol, &maxCol);

    // Get first cell's state
    CxSheetCellCoordinate firstCoord;
    firstCoord.setRow(minRow);
    firstCoord.setCol(minCol);
    CxSheetCell *firstCell = sheetModel->getCellPtr(firstCoord);
    int firstState = getPercentState(firstCell);

    // Check if all cells have the same state
    int allSame = 1;
    for (int row = minRow; row <= maxRow && allSame; row++) {
        for (int col = minCol; col <= maxCol && allSame; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);
            CxSheetCell *cell = sheetModel->getCellPtr(coord);
            if (getPercentState(cell) != firstState) {
                allSame = 0;
            }
        }
    }

    // If mixed, sync to first cell's state; otherwise toggle
    int targetState = allSame ? (firstState ? 0 : 1) : firstState;

    // Apply to all cells in range
    for (int row = minRow; row <= maxRow; row++) {
        for (int col = minCol; col <= maxCol; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);

            CxSheetCell *cell = sheetModel->getCellPtr(coord);
            if (cell == NULL) {
                CxSheetCell newCell;
                sheetModel->setCell(coord, newCell);
                cell = sheetModel->getCellPtr(coord);
            }

            if (cell != NULL) {
                if (targetState) {
                    cell->setAppAttribute("percent", true);
                } else {
                    cell->removeAppAttribute("percent");
                }
            }
        }
    }

    sheetView->updateScreen();
    resetPrompt();

    setMessage(targetState ? "percent" : "plain");
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::cycleDateFormat
//
// Cycle through date formats on the current selection (or current cell).
// Cycle: yyyy-mm-dd → yyyy/mm/dd → mm/dd/yyyy → mm-dd-yyyy → yyyy-mm-dd
// Only applies to cells that have a dateFormat attribute set.
// If range has mixed formats, first press syncs to first cell's format.
//-------------------------------------------------------------------------------------------------

// Helper to get date format index from a cell
// Returns: 0-3 for known formats, -1 for no dateFormat or unknown
static int getDateFormatIndex(CxSheetCell *cell, const char *formats[], int numFormats)
{
    if (cell == NULL || !cell->hasAppAttribute("dateFormat")) return -1;

    CxString fmt = cell->getAppAttributeString("dateFormat");
    for (int i = 0; i < numFormats; i++) {
        if (fmt == formats[i]) return i;
    }
    return -1;  // unknown format
}

void
SheetEditor::cycleDateFormat(void)
{
    CxSheetCellCoordinate start, end;

    if (_rangeSelectActive) {
        start = _rangeAnchor;
        end = _rangeCurrent;
    } else {
        start = sheetModel->getCurrentPosition();
        end = start;
    }

    // Normalize range
    int minRow, maxRow, minCol, maxCol;
    normalizeRange(start, end, &minRow, &maxRow, &minCol, &maxCol);

    // Date format cycle
    const char *formats[] = { "yyyy-mm-dd", "yyyy/mm/dd", "mm/dd/yyyy", "mm-dd-yyyy" };
    int numFormats = 4;

    // Find first cell with dateFormat to get its index
    int firstIndex = -1;
    for (int row = minRow; row <= maxRow && firstIndex < 0; row++) {
        for (int col = minCol; col <= maxCol && firstIndex < 0; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);
            CxSheetCell *cell = sheetModel->getCellPtr(coord);
            int idx = getDateFormatIndex(cell, formats, numFormats);
            if (idx >= 0) {
                firstIndex = idx;
            } else if (cell != NULL && cell->hasAppAttribute("dateFormat")) {
                // Unknown format - treat as index 3 so it advances to 0
                firstIndex = numFormats - 1;
            }
        }
    }

    if (firstIndex < 0) {
        // No cells with date format in selection
        setMessage("no date format");
        return;
    }

    // Check if all date cells have the same format
    int allSame = 1;
    for (int row = minRow; row <= maxRow && allSame; row++) {
        for (int col = minCol; col <= maxCol && allSame; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);
            CxSheetCell *cell = sheetModel->getCellPtr(coord);
            int idx = getDateFormatIndex(cell, formats, numFormats);
            // Only compare cells that have dateFormat
            if (idx >= 0 && idx != firstIndex) {
                allSame = 0;
            }
        }
    }

    // If mixed, sync to first cell's format; otherwise advance to next
    int targetIndex = allSame ? ((firstIndex + 1) % numFormats) : firstIndex;
    const char *targetFormat = formats[targetIndex];

    // Apply to all cells in range that have dateFormat
    for (int row = minRow; row <= maxRow; row++) {
        for (int col = minCol; col <= maxCol; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);

            CxSheetCell *cell = sheetModel->getCellPtr(coord);
            if (cell != NULL && cell->hasAppAttribute("dateFormat")) {
                cell->setAppAttribute("dateFormat", targetFormat);
            }
        }
    }

    sheetView->updateScreen();
    resetPrompt();

    setMessage(targetFormat);
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatCellNumberCurrency
//
// Toggle currency format on the current selection (or current cell if no selection).
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatCellNumberCurrency(CxString commandLine)
{
    (void)commandLine;

    CxSheetCellCoordinate start, end;
    getRangeOrCell(&start, &end);

    int minRow, maxRow, minCol, maxCol;
    normalizeRange(start, end, &minRow, &maxRow, &minCol, &maxCol);

    int cellCount = 0;
    int newState = -1;

    for (int row = minRow; row <= maxRow; row++) {
        for (int col = minCol; col <= maxCol; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);

            CxSheetCell *cell = sheetModel->getCellPtr(coord);
            if (cell == NULL) {
                CxSheetCell newCell;
                newCell.setAppAttribute("currency", true);
                if (!newCell.hasAppAttribute("decimalPlaces")) {
                    newCell.setAppAttribute("decimalPlaces", 2);
                }
                sheetModel->setCell(coord, newCell);
                if (newState < 0) newState = 1;
            } else {
                bool current = cell->getAppAttributeBool("currency", false);
                if (newState < 0) newState = current ? 0 : 1;
                cell->setAppAttribute("currency", newState == 1);
                if (newState == 1 && !cell->hasAppAttribute("decimalPlaces")) {
                    cell->setAppAttribute("decimalPlaces", 2);
                }
            }
            cellCount++;
        }
    }

    clearRangeSelection();
    sheetView->updateScreen();

    char buf[64];
    const char *state = (newState == 1) ? "on" : "off";
    if (cellCount == 1) {
        snprintf(buf, sizeof(buf), "(1 cell currency %s)", state);
    } else {
        snprintf(buf, sizeof(buf), "(%d cells currency %s)", cellCount, state);
    }
    setMessage(buf);
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatCellNumberDecimal
//
// Set decimal places on the current selection (or current cell if no selection).
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatCellNumberDecimal(CxString commandLine)
{
    // Parse the argument
    CxString arg = commandLine.nextToken(" \t");
    if (arg.length() == 0) {
        setMessage("Usage: modify-decimal <n> (0-10)");
        return;
    }

    int places = atoi(arg.data());
    if (places < 0 || places > 10) {
        setMessage("Decimal places must be 0-10");
        return;
    }

    CxSheetCellCoordinate start, end;
    getRangeOrCell(&start, &end);

    int minRow, maxRow, minCol, maxCol;
    normalizeRange(start, end, &minRow, &maxRow, &minCol, &maxCol);

    int cellCount = 0;
    for (int row = minRow; row <= maxRow; row++) {
        for (int col = minCol; col <= maxCol; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);
            setCellAttributeInt(coord, "decimalPlaces", places);
            cellCount++;
        }
    }

    clearRangeSelection();
    sheetView->updateScreen();

    char buf[64];
    if (cellCount == 1) {
        snprintf(buf, sizeof(buf), "(1 cell set to %d decimals)", places);
    } else {
        snprintf(buf, sizeof(buf), "(%d cells set to %d decimals)", cellCount, places);
    }
    setMessage(buf);
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatCellNumberPercent
//
// Toggle percent format on the current selection (or current cell if no selection).
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatCellNumberPercent(CxString commandLine)
{
    (void)commandLine;
    toggleCellBoolAttribute("percent", "percent");
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatCellNumberThousands
//
// Toggle thousands separators on the current selection (or current cell if no selection).
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatCellNumberThousands(CxString commandLine)
{
    (void)commandLine;
    toggleCellBoolAttribute("thousands", "thousands");
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatCellTextWide
//
// Toggle wide text display on the current selection (or current cell if no selection).
// Wide text inserts spaces between characters: "FUND IV" displays as "F U N D   I V".
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatCellTextWide(CxString commandLine)
{
    (void)commandLine;
    toggleCellBoolAttribute("wideText", "wide text");
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_InsertRow
//
// Insert an empty row above the current cursor position.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_InsertRow(CxString commandLine)
{
    (void)commandLine;

    int row = sheetModel->getCurrentPosition().getRow();
    sheetModel->insertRow(row);
    sheetView->shiftHiddenRows(row, 1);

    // Try optimized terminal insert, fall back to full redraw
    if (!sheetView->terminalInsertRow(row)) {
        sheetView->updateScreen();
    }
    setMessage("Row inserted");
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_InsertColumn
//
// Insert an empty column before the current cursor position.
// Also shifts column widths to keep visual formatting consistent.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_InsertColumn(CxString commandLine)
{
    (void)commandLine;

    int col = sheetModel->getCurrentPosition().getCol();
    sheetModel->insertColumn(col);

    // Shift column widths to match the data shift
    sheetView->shiftColumnWidths(col, 1);

    // Shift column formats to match the data shift
    sheetView->shiftColumnFormats(col, 1);

    // Use optimized redraw that skips row numbers
    sheetView->updateScreenForColumnChange();
    setMessage("Column inserted");
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_DeleteRow
//
// Delete the current row.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_DeleteRow(CxString commandLine)
{
    (void)commandLine;

    int row = sheetModel->getCurrentPosition().getRow();
    sheetModel->deleteRow(row);
    sheetView->shiftHiddenRows(row, -1);

    // Try optimized terminal delete, fall back to full redraw
    if (!sheetView->terminalDeleteRow(row)) {
        sheetView->updateScreen();
    }
    setMessage("Row deleted");
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_DeleteColumn
//
// Delete the current column.
// Also shifts column widths to keep visual formatting consistent.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_DeleteColumn(CxString commandLine)
{
    (void)commandLine;

    int col = sheetModel->getCurrentPosition().getCol();
    sheetModel->deleteColumn(col);

    // Shift column widths to match the data shift
    sheetView->shiftColumnWidths(col, -1);

    // Shift column formats to match the data shift
    sheetView->shiftColumnFormats(col, -1);

    // Use optimized redraw that skips row numbers
    sheetView->updateScreenForColumnChange();
    setMessage("Column deleted");
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatColAlignLeft
//
// Set left alignment as column default for the current column or selected columns.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatColAlignLeft(CxString commandLine)
{
    (void)commandLine;
    setColumnAlignment(1, "Column align: left");
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatColAlignCenter
//
// Set center alignment as column default for the current column or selected columns.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatColAlignCenter(CxString commandLine)
{
    (void)commandLine;
    setColumnAlignment(2, "Column align: center");
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatColAlignRight
//
// Set right alignment as column default for the current column or selected columns.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatColAlignRight(CxString commandLine)
{
    (void)commandLine;
    setColumnAlignment(3, "Column align: right");
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatColNumberCurrency
//
// Toggle currency format as column default for the current column or selected columns.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatColNumberCurrency(CxString commandLine)
{
    (void)commandLine;
    toggleColumnBoolFormat("currency", "currency");
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatColNumberDecimal
//
// Set decimal places as column default for the current column or selected columns.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatColNumberDecimal(CxString commandLine)
{
    int places = atoi(commandLine.data());
    if (places < 0) places = 0;
    if (places > 10) places = 10;

    int startCol, endCol;
    getColumnRange(&startCol, &endCol);

    for (int col = startCol; col <= endCol; col++) {
        sheetView->setColDecimalPlaces(col, places);
    }

    clearCellAttributeInColumns(startCol, endCol, "decimalPlaces");
    clearRangeSelection();
    sheetView->updateScreen();

    char msg[64];
    snprintf(msg, sizeof(msg), "Column decimal places: %d", places);
    setMessage(msg);
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatColNumberPercent
//
// Toggle percent format as column default for the current column or selected columns.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatColNumberPercent(CxString commandLine)
{
    (void)commandLine;
    toggleColumnBoolFormat("percent", "percent");
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatColNumberThousands
//
// Toggle thousands separator as column default for the current column or selected columns.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatColNumberThousands(CxString commandLine)
{
    (void)commandLine;
    toggleColumnBoolFormat("thousands", "thousands");
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatCellColorForeground
//
// Enter color picker mode for cell foreground color.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatCellColorForeground(CxString commandLine)
{
    (void)commandLine;
    // enterColorPickerMode is called from selectCommand when flag is detected
    // This handler is called when color is selected to apply it
    applySelectedColor();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatCellColorBackground
//
// Enter color picker mode for cell background color.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatCellColorBackground(CxString commandLine)
{
    (void)commandLine;
    applySelectedColor();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatColColorForeground
//
// Enter color picker mode for column default foreground color.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatColColorForeground(CxString commandLine)
{
    (void)commandLine;
    applySelectedColor();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatColColorBackground
//
// Enter color picker mode for column default background color.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatColColorBackground(CxString commandLine)
{
    (void)commandLine;
    applySelectedColor();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::enterColorPickerMode
//
// Enter color picker mode. isForeground: 1=foreground, 0=background.
// isColumnDefault: 1=column default, 0=cell-level.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::enterColorPickerMode(int isForeground, int isColumnDefault)
{
    _cmdInputState = CMD_INPUT_COLOR_PICKER;
    _colorPickerIsForeground = isForeground;
    _colorPickerIsColumn = isColumnDefault;
    _colorPickerIndex = 0;
    _colorPickerScrollOffset = 0;

    updateColorPickerDisplay();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::focusColorPicker
//
// Handle keyboard input in color picker mode.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::focusColorPicker(CxKeyAction keyAction)
{
    int action = keyAction.actionType();

    // Get palette size
    int paletteSize = _colorPickerIsForeground ?
        spreadsheetDefaults->getFgPaletteSize() :
        spreadsheetDefaults->getBgPaletteSize();

    // ESC - cancel and exit
    if (action == CxKeyAction::COMMAND) {
        exitColorPickerMode();
        return;
    }

    // ENTER - apply selected color
    if (action == CxKeyAction::NEWLINE) {
        applySelectedColor();
        exitColorPickerMode();
        return;
    }

    // Arrow keys - navigate palette
    if (action == CxKeyAction::CURSOR) {
        CxString tag = keyAction.tag();

        if (tag == "<arrow-left>") {
            if (_colorPickerIndex > 0) {
                _colorPickerIndex--;
                updateColorPickerDisplay();
            }
        } else if (tag == "<arrow-right>") {
            if (_colorPickerIndex < paletteSize - 1) {
                _colorPickerIndex++;
                updateColorPickerDisplay();
            }
        }
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::updateColorPickerDisplay
//
// Update the command line to show the color picker swatches.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::updateColorPickerDisplay(void)
{
    int paletteSize = _colorPickerIsForeground ?
        spreadsheetDefaults->getFgPaletteSize() :
        spreadsheetDefaults->getBgPaletteSize();

    // Calculate how many swatches fit on screen
    // For bg: each swatch is 6 chars wide ([xxxx] or [none])
    // For fg: each swatch is 6 chars wide (1234 + space, or none + space)
    int swatchWidth = _colorPickerIsForeground ? 5 : 6;
    int labelWidth = _colorPickerIsForeground ? 11 : 11;  // "fg color: " or "bg color: "
    int screenWidth = screen->cols();
    int availableWidth = screenWidth - labelWidth - 2;  // leave margin
    int maxSwatches = availableWidth / swatchWidth;
    if (maxSwatches < 1) maxSwatches = 1;

    // Adjust scroll offset to keep selection visible
    if (_colorPickerIndex < _colorPickerScrollOffset) {
        _colorPickerScrollOffset = _colorPickerIndex;
    } else if (_colorPickerIndex >= _colorPickerScrollOffset + maxSwatches) {
        _colorPickerScrollOffset = _colorPickerIndex - maxSwatches + 1;
    }

    // Build display string with color codes
    CxString label = _colorPickerIsForeground ? "fg color: " : "bg color: ";

    // Position cursor and start output
    commandLineView->setDimMode(0);
    CxScreen::placeCursor(screen->rows() - 2, 0);
    CxScreen::clearScreenFromCursorToEndOfLine();

    // Output label
    spreadsheetDefaults->applyCommandLineEditColors(screen);
    printf("%s", label.data());

    // Output swatches
    int endIndex = _colorPickerScrollOffset + maxSwatches;
    if (endIndex > paletteSize) endIndex = paletteSize;

    for (int i = _colorPickerScrollOffset; i < endIndex; i++) {
        CxColor *color = _colorPickerIsForeground ?
            spreadsheetDefaults->getFgPaletteColor(i) :
            spreadsheetDefaults->getBgPaletteColor(i);
        CxString colorStr = _colorPickerIsForeground ?
            spreadsheetDefaults->getFgPaletteString(i) :
            spreadsheetDefaults->getBgPaletteString(i);

        // Check if this is "none" (ANSI:NONE or contains NONE)
        int isNone = (colorStr.index("NONE") >= 0);
        int isSelected = (i == _colorPickerIndex);

        if (_colorPickerIsForeground) {
            // Foreground colors: show "1234" or "none" in that color
            if (isSelected) {
                // Inverse video for selection - use color as background
                if (isNone) {
                    screen->resetForegroundColor();
                    screen->resetBackgroundColor();
                    printf("[none]");
                } else {
                    // Create matching background color from foreground
                    screen->setBackgroundColor(color);
                    screen->resetForegroundColor();
                    printf(" 1234 ");
                }
                spreadsheetDefaults->resetColors(screen);
            } else {
                if (isNone) {
                    spreadsheetDefaults->applyCommandLineEditColors(screen);
                    printf("none ");
                } else {
                    screen->setForegroundColor(color);
                    printf("1234 ");
                    spreadsheetDefaults->resetColors(screen);
                }
            }
        } else {
            // Background colors: show color blocks
            if (isSelected) {
                // Selection indicator: brackets
                spreadsheetDefaults->applyCommandLineEditColors(screen);
                printf("[");
                if (isNone) {
                    printf("none");
                } else {
                    screen->setBackgroundColor(color);
                    printf("    ");
                    spreadsheetDefaults->resetColors(screen);
                }
                spreadsheetDefaults->applyCommandLineEditColors(screen);
                printf("]");
            } else {
                if (isNone) {
                    spreadsheetDefaults->applyCommandLineEditColors(screen);
                    printf(" none ");
                } else {
                    printf(" ");
                    screen->setBackgroundColor(color);
                    printf("    ");
                    spreadsheetDefaults->resetColors(screen);
                    printf(" ");
                }
            }
        }
    }

    // Reset colors
    spreadsheetDefaults->resetColors(screen);

    // Hide cursor during picker mode
    screen->hideCursor();

    fflush(stdout);
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::applySelectedColor
//
// Apply the selected color to the current cell(s) or column(s).
//-------------------------------------------------------------------------------------------------
void
SheetEditor::applySelectedColor(void)
{
    CxString colorStr = _colorPickerIsForeground ?
        spreadsheetDefaults->getFgPaletteString(_colorPickerIndex) :
        spreadsheetDefaults->getBgPaletteString(_colorPickerIndex);

    // Check if this is a "none" color (ANSI:NONE or similar)
    int isNone = (colorStr.index("NONE") >= 0);
    const char *attrName = _colorPickerIsForeground ? "fgColor" : "bgColor";

    if (_colorPickerIsColumn) {
        // Apply to column default(s)
        int startCol, endCol;
        getColumnRange(&startCol, &endCol);

        for (int col = startCol; col <= endCol; col++) {
            if (_colorPickerIsForeground) {
                sheetView->setColFgColor(col, isNone ? CxString("") : colorStr);
            } else {
                sheetView->setColBgColor(col, isNone ? CxString("") : colorStr);
            }
        }

        clearRangeSelection();

        setMessage(isNone ? "Column color cleared" : "Column color set");
    } else {
        // Apply to cell(s)
        CxSheetCellCoordinate start, end;
        if (_rangeSelectActive) {
            start = _rangeAnchor;
            end = _rangeCurrent;
        } else {
            start = sheetModel->getCurrentPosition();
            end = start;
        }

        int minRow, maxRow, minCol, maxCol;
        normalizeRange(start, end, &minRow, &maxRow, &minCol, &maxCol);

        for (int row = minRow; row <= maxRow; row++) {
            for (int col = minCol; col <= maxCol; col++) {
                CxSheetCellCoordinate coord(row, col);
                CxSheetCell *cell = sheetModel->getCellPtr(coord);

                // Create cell if it doesn't exist
                if (cell == NULL) {
                    sheetModel->setCell(coord, CxSheetCell());
                    cell = sheetModel->getCellPtr(coord);
                }

                if (cell != NULL) {
                    if (isNone) {
                        // Remove color attribute (use default)
                        cell->removeAppAttribute(attrName);
                    } else {
                        cell->setAppAttribute(attrName, colorStr.data());
                    }
                }
            }
        }

        clearRangeSelection();

        setMessage(isNone ? "Cell color cleared" : "Cell color set");
    }

    sheetView->updateScreen();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::exitColorPickerMode
//
// Exit color picker mode and return to normal mode.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::exitColorPickerMode(void)
{
    _cmdInputState = CMD_INPUT_IDLE;
    exitCommandLineMode();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_HideRow
//
// Hide current row, or all rows in the selection range.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_HideRow(CxString commandLine)
{
    (void)commandLine;

    if (_rangeSelectActive) {
        int minRow, maxRow, minCol, maxCol;
        normalizeRange(_rangeAnchor, _rangeCurrent, &minRow, &maxRow, &minCol, &maxCol);

        for (int row = minRow; row <= maxRow; row++) {
            sheetView->setRowHidden(row, 1);
        }

        // Clear range selection
        clearRangeSelection();

        // Move cursor to next visible row if it's now hidden
        CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
        if (sheetView->isRowHidden(pos.getRow())) {
            int newRow = sheetView->nextVisibleRow(pos.getRow(), 1);
            sheetModel->jumpToCell(CxSheetCellCoordinate(newRow, pos.getCol()));
        }
    } else {
        int row = sheetModel->getCurrentPosition().getRow();
        int col = sheetModel->getCurrentPosition().getCol();
        sheetView->setRowHidden(row, 1);

        // Move cursor to next visible row
        int newRow = sheetView->nextVisibleRow(row, 1);
        sheetModel->jumpToCell(CxSheetCellCoordinate(newRow, col));
    }

    sheetView->updateScreen();
    setMessage("Row hidden");
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_HideColumn
//
// Hide current column, or all columns in the selection range.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_HideColumn(CxString commandLine)
{
    (void)commandLine;

    if (_rangeSelectActive) {
        int minRow, maxRow, minCol, maxCol;
        normalizeRange(_rangeAnchor, _rangeCurrent, &minRow, &maxRow, &minCol, &maxCol);

        for (int col = minCol; col <= maxCol; col++) {
            sheetView->setColumnHidden(col, 1);
        }

        // Clear range selection
        clearRangeSelection();

        // Move cursor to next visible column if it's now hidden
        CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
        if (sheetView->isColumnHidden(pos.getCol())) {
            int newCol = sheetView->nextVisibleCol(pos.getCol(), 1);
            sheetModel->jumpToCell(CxSheetCellCoordinate(pos.getRow(), newCol));
        }
    } else {
        int row = sheetModel->getCurrentPosition().getRow();
        int col = sheetModel->getCurrentPosition().getCol();
        sheetView->setColumnHidden(col, 1);

        // Move cursor to next visible column
        int newCol = sheetView->nextVisibleCol(col, 1);
        sheetModel->jumpToCell(CxSheetCellCoordinate(row, newCol));
    }

    sheetView->updateScreen();
    setMessage("Column hidden");
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_ShowRow
//
// Unhide hidden rows within the selection range. Requires range selection.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_ShowRow(CxString commandLine)
{
    (void)commandLine;

    if (!_rangeSelectActive) {
        setMessage("Select a range spanning the hidden rows first");
        return;
    }

    int minRow, maxRow, minCol, maxCol;
    normalizeRange(_rangeAnchor, _rangeCurrent, &minRow, &maxRow, &minCol, &maxCol);

    int unhidden = 0;
    for (int row = minRow; row <= maxRow; row++) {
        if (sheetView->isRowHidden(row)) {
            sheetView->setRowHidden(row, 0);
            unhidden++;
        }
    }

    clearRangeSelection();
    sheetView->updateScreen();

    if (unhidden > 0) {
        CxString msg;
        msg.printf("%d row%s shown", unhidden, unhidden == 1 ? "" : "s");
        setMessage(msg);
    } else {
        setMessage("No hidden rows in selection");
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_ShowColumn
//
// Unhide hidden columns within the selection range. Requires range selection.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_ShowColumn(CxString commandLine)
{
    (void)commandLine;

    if (!_rangeSelectActive) {
        setMessage("Select a range spanning the hidden columns first");
        return;
    }

    int minRow, maxRow, minCol, maxCol;
    normalizeRange(_rangeAnchor, _rangeCurrent, &minRow, &maxRow, &minCol, &maxCol);

    int unhidden = 0;
    for (int col = minCol; col <= maxCol; col++) {
        if (sheetView->isColumnHidden(col)) {
            sheetView->setColumnHidden(col, 0);
            unhidden++;
        }
    }

    clearRangeSelection();
    sheetView->updateScreen();

    if (unhidden > 0) {
        CxString msg;
        msg.printf("%d column%s shown", unhidden, unhidden == 1 ? "" : "s");
        setMessage(msg);
    } else {
        setMessage("No hidden columns in selection");
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_ShowAll
//
// Unhide all hidden rows and columns.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_ShowAll(CxString commandLine)
{
    (void)commandLine;

    sheetView->showAllRows();
    sheetView->showAllColumns();

    if (_rangeSelectActive) {
        clearRangeSelection();
    }

    sheetView->updateScreen();
    setMessage("All rows and columns shown");
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_ViewFreeze
//
// Freeze rows and/or columns based on the current range selection.
// The selection must start at A1. The extent of the selection determines what gets frozen:
//   A1:C1 (one row)    → freeze columns A-C only
//   A1:A6 (one column) → freeze rows 1-6 only
//   A1:C6 (multi)      → freeze both rows and columns
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_ViewFreeze(CxString commandLine)
{
    (void)commandLine;

    int minRow, maxRow, minCol, maxCol;

    if (!_rangeSelectActive) {
        // No range selected - use current cell as single-cell selection
        CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
        minRow = maxRow = pos.getRow();
        minCol = maxCol = pos.getCol();
    } else {
        normalizeRange(_rangeAnchor, _rangeCurrent, &minRow, &maxRow, &minCol, &maxCol);
    }

    // Range must include A1
    if (minRow != 0 || minCol != 0) {
        setMessage("Freeze selection must start at A1");
        return;
    }

    // Determine what to freeze based on selection shape
    int freezeRow = 0;
    int freezeCol = 0;

    int multiRow = (maxRow > 0);
    int multiCol = (maxCol > 0);

    if (multiRow && multiCol) {
        // Multi-row and multi-col: freeze both
        freezeRow = maxRow + 1;
        freezeCol = maxCol + 1;
    } else if (multiRow) {
        // Single column wide (A1:A6): freeze rows only
        freezeRow = maxRow + 1;
    } else if (multiCol) {
        // Single row tall (A1:C1): freeze columns only
        freezeCol = maxCol + 1;
    } else {
        // Just A1 selected - freeze column A only
        freezeCol = 1;
    }

    clearRangeSelection();
    sheetView->setFreeze(freezeRow, freezeCol);
    sheetView->updateScreen();

    CxString msg;
    if (freezeRow > 0 && freezeCol > 0) {
        msg.printf("Frozen %d row%s and %d column%s",
                   freezeRow, freezeRow == 1 ? "" : "s",
                   freezeCol, freezeCol == 1 ? "" : "s");
    } else if (freezeRow > 0) {
        msg.printf("Frozen %d row%s", freezeRow, freezeRow == 1 ? "" : "s");
    } else {
        msg.printf("Frozen %d column%s", freezeCol, freezeCol == 1 ? "" : "s");
    }
    setMessage(msg);
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_ViewUnfreeze
//
// Remove freeze panes.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_ViewUnfreeze(CxString commandLine)
{
    (void)commandLine;

    int fr, fc;
    sheetView->getFreeze(&fr, &fc);

    if (fr == 0 && fc == 0) {
        setMessage("No freeze to remove");
        return;
    }

    sheetView->setFreeze(0, 0);
    sheetView->updateScreen();
    setMessage("Freeze removed");
}


#ifdef SS_CLAUDE_ENABLED
//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_Claude
//
// Command handler for the "claude" ESC command. Opens Claude AI chat mode.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_Claude(CxString commandLine)
{
    (void)commandLine;
    enterClaudeMode();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::enterClaudeMode
//
// Show the Claude chat area, shrink sheet view, and switch to CLAUDEVIEW mode.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::enterClaudeMode(void)
{
    _claudeViewVisible = 1;

    // Reset command line state so it shows cell info instead of stale "command> claude"
    _cmdInputState = CMD_INPUT_IDLE;

    // Recalculate all view positions
    int totalRows = screen->rows();
    int claudeHeight = _claudeView->getHeight();
    int sheetEndRow = totalRows - 3 - claudeHeight;

    int claudeStartRow = sheetEndRow + 1;
    int claudeEndRow = claudeStartRow + claudeHeight - 1;

    // Phase 1: recalc all
    sheetView->recalcForResize(0, sheetEndRow);
    _claudeView->recalcForResize(claudeStartRow, claudeEndRow);
    commandLineView->recalcForResize(claudeEndRow + 1);
    messageLineView->recalcForResize(claudeEndRow + 2);

    // Phase 2: redraw all
    CxScreen::clearScreen();
    sheetView->updateScreen();

    _claudeView->setDisplayLines(_claudeHandler->getDisplayLines(),
                                 _claudeHandler->getDisplayLineCount());
    _claudeView->setInputText(_claudeInputBuffer.toBytes());
    _claudeView->setInputCursorPos(_claudeInputCursorPos);
    _claudeView->scrollToBottom();
    _claudeView->updateScreen();

    // Draw cell info (not commandLineView->updateScreen() which has stale command text)
    updateCommandLineDisplay();
    messageLineView->updateScreen();

    programMode = CLAUDEVIEW;
    _claudeView->placeCursor();
    fflush(stdout);
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::exitClaudeMode
//
// Hide Claude chat, expand sheet view back, return to EDIT mode.
// Conversation history is preserved in ClaudeHandler.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::exitClaudeMode(void)
{
    _claudeViewVisible = 0;

    // Recalculate without Claude view
    int totalRows = screen->rows();
    int sheetEndRow = totalRows - 3;

    sheetView->recalcForResize(0, sheetEndRow);
    commandLineView->recalcForResize(sheetEndRow + 1);
    messageLineView->recalcForResize(sheetEndRow + 2);

    // Redraw all
    CxScreen::clearScreen();
    sheetView->updateScreen();
    commandLineView->updateScreen();
    messageLineView->updateScreen();

    programMode = EDIT;
    resetPrompt();
    sheetView->placeCursor();
    fflush(stdout);
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::focusClaudeView
//
// Handle keyboard input when in CLAUDEVIEW mode.
// ENTER sends message, ESC exits, arrows scroll, printable chars edit input.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::focusClaudeView(CxKeyAction keyAction)
{
    int action = keyAction.actionType();

    switch (action) {
        //-------------------------------------------------------------------------------------
        // ESC - exit Claude view (history preserved)
        //-------------------------------------------------------------------------------------
        case CxKeyAction::COMMAND:
        {
            exitClaudeMode();
        }
        break;

        //-------------------------------------------------------------------------------------
        // ENTER - send message
        //-------------------------------------------------------------------------------------
        case CxKeyAction::NEWLINE:
        {
            CxString inputText = _claudeInputBuffer.toBytes();
            if (inputText.length() > 0) {
                // Handle /clear command
                if (inputText == "/clear") {
                    _claudeHandler->clearHistory();
                    _claudeHandler->clearDisplayLines();
                    _claudeInputBuffer = CxUTFString();
                    _claudeInputCursorPos = 0;

                    _claudeView->setDisplayLines(_claudeHandler->getDisplayLines(),
                                                 _claudeHandler->getDisplayLineCount());
                    _claudeView->setInputText("");
                    _claudeView->setInputCursorPos(0);
                    _claudeView->scrollToBottom();
                    _claudeView->updateScreen();
                    _claudeView->placeCursor();
                } else {
                    _claudeHandler->sendMessage(inputText);
                    _claudeInputBuffer = CxUTFString();
                    _claudeInputCursorPos = 0;

                    // Update view
                    _claudeView->setDisplayLines(_claudeHandler->getDisplayLines(),
                                                 _claudeHandler->getDisplayLineCount());
                    _claudeView->setInputText("");
                    _claudeView->setInputCursorPos(0);
                    _claudeView->scrollToBottom();
                    _claudeView->updateScreen();
                    _claudeView->placeCursor();
                }
            }
        }
        break;

        //-------------------------------------------------------------------------------------
        // Arrow keys - scroll chat or move cursor in input
        //-------------------------------------------------------------------------------------
        case CxKeyAction::CURSOR:
        {
            CxString tag = keyAction.tag();
            if (tag == "<arrow-up>") {
                _claudeView->scrollUp();
                _claudeView->updateScreen();
                _claudeView->placeCursor();
            } else if (tag == "<arrow-down>") {
                _claudeView->scrollDown();
                _claudeView->updateScreen();
                _claudeView->placeCursor();
            } else if (tag == "<arrow-left>") {
                if (_claudeInputCursorPos > 0) {
                    _claudeInputCursorPos--;
                    _claudeView->setInputCursorPos(_claudeInputCursorPos);
                    _claudeView->placeCursor();
                }
            } else if (tag == "<arrow-right>") {
                if (_claudeInputCursorPos < _claudeInputBuffer.charCount()) {
                    _claudeInputCursorPos++;
                    _claudeView->setInputCursorPos(_claudeInputCursorPos);
                    _claudeView->placeCursor();
                }
            }
        }
        break;

        //-------------------------------------------------------------------------------------
        // Backspace
        //-------------------------------------------------------------------------------------
        case CxKeyAction::BACKSPACE:
        {
            if (_claudeInputCursorPos > 0 && _claudeInputBuffer.charCount() > 0) {
                _claudeInputBuffer.remove(_claudeInputCursorPos - 1, 1);
                _claudeInputCursorPos--;
                _claudeView->setInputText(_claudeInputBuffer.toBytes());
                _claudeView->setInputCursorPos(_claudeInputCursorPos);
                _claudeView->updateScreen();
                _claudeView->placeCursor();
            }
        }
        break;

        //-------------------------------------------------------------------------------------
        // Printable characters - insert into input buffer
        //-------------------------------------------------------------------------------------
        case CxKeyAction::LOWERCASE_ALPHA:
        case CxKeyAction::UPPERCASE_ALPHA:
        case CxKeyAction::NUMBER:
        case CxKeyAction::SYMBOL:
        {
            CxString tag = keyAction.tag();
            if (tag.length() > 0) {
                CxUTFCharacter ch = CxUTFCharacter::fromASCII(tag.data()[0]);
                _claudeInputBuffer.insert(_claudeInputCursorPos, ch);
                _claudeInputCursorPos++;
                _claudeView->setInputText(_claudeInputBuffer.toBytes());
                _claudeView->setInputCursorPos(_claudeInputCursorPos);
                _claudeView->updateScreen();
                _claudeView->placeCursor();
            }
        }
        break;

        //-------------------------------------------------------------------------------------
        // Space (TAB action type in some cases, but space is usually SYMBOL)
        //-------------------------------------------------------------------------------------

        default:
            break;
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::claudeIdleCallback
//
// Called ~100ms during keyboard idle. Polls ClaudeHandler for streaming API responses
// and updates the Claude view if new content is available.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::claudeIdleCallback(void)
{
    if (_claudeHandler == NULL) return;

    if (_claudeHandler->isRunning()) {
        if (_claudeHandler->poll()) {
            _claudeView->setDisplayLines(_claudeHandler->getDisplayLines(),
                                         _claudeHandler->getDisplayLineCount());
            _claudeView->scrollToBottom();

            if (_claudeViewVisible) {
                _claudeView->updateScreen();
                if (programMode == CLAUDEVIEW) {
                    _claudeView->placeCursor();
                }
                fflush(stdout);
            }
        }
    } else if (_claudeHandler->needsRedraw()) {
        _claudeHandler->clearNeedsRedraw();
        _claudeView->setDisplayLines(_claudeHandler->getDisplayLines(),
                                     _claudeHandler->getDisplayLineCount());
        _claudeView->scrollToBottom();

        if (_claudeViewVisible) {
            _claudeView->updateScreen();
            if (programMode == CLAUDEVIEW) {
                _claudeView->placeCursor();
            }
            fflush(stdout);
        }
    }
}
#endif
