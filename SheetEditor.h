//-------------------------------------------------------------------------------------------------
//
//  SheetEditor.h
//  ss (spreadsheet)
//
//  Copyright 2022-2025 Todd Vernon. All rights reserved.
//  Licensed under the Apache License, Version 2.0
//  See LICENSE file for details.
//
//  Core sheet editor class - central coordinator for the spreadsheet application.
//
//-------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <sys/types.h>

#include <cx/base/string.h>
#include <cx/base/utfstring.h>
#include <cx/keyboard/keyboard.h>
#include <cx/screen/screen.h>
#include <cx/functor/defercall.h>
#include <cx/sheetModel/sheetModel.h>
#include <cx/commandcompleter/completer.h>

#include "SheetView.h"
#include "CommandLineView.h"
#include "MessageLineView.h"
#include "CommandTable.h"
#include "SpreadsheetDefaults.h"
#include "HelpView.h"

#ifndef _SheetEditor_h_
#define _SheetEditor_h_


//-------------------------------------------------------------------------------------------------
//
// ClipboardCell
//
// Represents a cell in the clipboard, storing its position offset relative to the clipboard
// anchor and a copy of the cell data.
//
//-------------------------------------------------------------------------------------------------

struct ClipboardCell {
    int rowOffset;              // relative to anchor (0 for anchor cell)
    int colOffset;              // relative to anchor
    CxSheetCell cell;           // copy of the cell
};


//-------------------------------------------------------------------------------------------------
//
// SheetEditor
//
// Central coordinator for the spreadsheet application. Owns all views, screen, keyboard,
// and coordinates mode switching. Follows the same architectural pattern as cm's ScreenEditor.
//
//-------------------------------------------------------------------------------------------------

class SheetEditor {

  public:

    enum ProgramMode {
        EDIT,               // normal cell navigation mode
        COMMANDLINE,        // command prompt focused
        DATA_ENTRY,         // entering data into a cell
        HELPVIEW            // help modal displayed
    };

    enum CommandInputState {
        CMD_INPUT_IDLE,         // not in command input mode
        CMD_INPUT_COMMAND,      // entering command name
        CMD_INPUT_ARGUMENT,     // entering command argument
        CMD_INPUT_COLOR_PICKER  // selecting color from palette
    };

    enum DataEntryMode {
        ENTRY_NONE,             // not in data entry
        ENTRY_GENERAL,          // general input - parsed at commit time
        ENTRY_FORMULA,          // entering formula (started with =)
        ENTRY_TEXTMAP           // entering textmap rule (started with @)
    };


    SheetEditor(CxScreen *scr, CxKeyboard *key, CxString filePath = "");
    ~SheetEditor(void);

    void run(void);

    // mode focus handlers
    int  focusEditor(CxKeyAction keyAction);
    int  dispatchControlX(void);
    void focusCommandPrompt(CxKeyAction keyAction);

    // command input methods
    void enterCommandMode(void);
    void handleCommandInput(CxKeyAction keyAction);
    void updateCommandDisplay(void);
    void updateArgumentDisplay(void);
    void executeCurrentCommand(void);

    // command input helpers
    void initCommandCompleters(void);
    void selectCommand(CommandEntry *cmd);
    void cancelCommandInput(void);
    void handleCommandModeInput(CxKeyAction keyAction);
    void handleArgumentModeInput(CxKeyAction keyAction);

    void resetPrompt(void);
    void updateCommandLineDisplay(void);
    void setMessage(CxString message);

    // data entry methods
    void enterDataEntryMode(DataEntryMode mode, char firstChar);
    void editCurrentCell(void);
    void focusDataEntry(CxKeyAction keyAction);
    void commitDataEntry(void);
    void cancelDataEntry(void);
    void updateDataEntryDisplay(void);

    // cell hunt methods (for formula cell reference selection)
    void enterCellHuntMode(CxString direction);
    void exitCellHuntMode(int insertRef);
    void focusCellHunt(CxKeyAction keyAction);
    void updateCellHuntDisplay(void);
    CxString buildCellHuntReference(void);

    // help view methods
    void showHelpView(void);
    void focusHelpView(CxKeyAction keyAction);

    // command handlers
    void CMD_Quit(CxString commandLine);
    void CMD_QuitSave(CxString commandLine);
    void CMD_QuitWithoutSave(CxString commandLine);
    void CMD_Load(CxString commandLine);
    void CMD_Save(CxString commandLine);
    void CMD_InsertSymbol(CxString commandLine);
    void CMD_Copy(CxString commandLine);
    void CMD_Cut(CxString commandLine);
    void CMD_Paste(CxString commandLine);
    void CMD_Clear(CxString commandLine);
    void CMD_FillDown(CxString commandLine);
    void CMD_FillRight(CxString commandLine);
    void CMD_FormatColWidth(CxString commandLine);
    void CMD_FormatColFit(CxString commandLine);
    void CMD_FormatCellAlignLeft(CxString commandLine);
    void CMD_FormatCellAlignCenter(CxString commandLine);
    void CMD_FormatCellAlignRight(CxString commandLine);
    void CMD_FormatCellNumberCurrency(CxString commandLine);
    void CMD_FormatCellNumberDecimal(CxString commandLine);
    void CMD_FormatCellNumberPercent(CxString commandLine);
    void CMD_FormatCellNumberThousands(CxString commandLine);
    void CMD_FormatCellTextWide(CxString commandLine);
    void CMD_InsertRow(CxString commandLine);
    void CMD_InsertColumn(CxString commandLine);
    void CMD_DeleteRow(CxString commandLine);
    void CMD_DeleteColumn(CxString commandLine);

    // Column format commands
    void CMD_FormatColAlignLeft(CxString commandLine);
    void CMD_FormatColAlignCenter(CxString commandLine);
    void CMD_FormatColAlignRight(CxString commandLine);
    void CMD_FormatColNumberCurrency(CxString commandLine);
    void CMD_FormatColNumberDecimal(CxString commandLine);
    void CMD_FormatColNumberPercent(CxString commandLine);
    void CMD_FormatColNumberThousands(CxString commandLine);

    // Cell/column color commands
    void CMD_FormatCellColorForeground(CxString commandLine);
    void CMD_FormatCellColorBackground(CxString commandLine);
    void CMD_FormatColColorForeground(CxString commandLine);
    void CMD_FormatColColorBackground(CxString commandLine);

    // Color picker methods
    void enterColorPickerMode(int isForeground, int isColumnDefault);
    void focusColorPicker(CxKeyAction keyAction);
    void updateColorPickerDisplay(void);
    void applySelectedColor(void);
    void exitColorPickerMode(void);

    // Format cycling
    void cycleNumberFormat(void);
    void cycleAlignment(void);
    void togglePercentFormat(void);
    void cycleDateFormat(void);

    ProgramMode programMode;

    CxScreen   *screen;
    CxKeyboard *keyboard;
    SheetView  *sheetView;
    CommandLineView *commandLineView;
    MessageLineView *messageLineView;
    HelpView   *helpView;
    CxSheetModel *sheetModel;
    SpreadsheetDefaults *spreadsheetDefaults;

    CxString _filePath;

  private:

    // command completion
    Completer       _commandCompleter;
    Completer       _symbolCompleter;   // for insert-symbol argument completion
    Completer      *_activeCompleter;

    // command input state
    CommandInputState _cmdInputState;
    CxString _cmdBuffer;            // input for active completer
    CxString _argBuffer;            // freeform argument text
    CommandEntry *_currentCommand;  // selected command (after completion)
    int _quitRequested;             // set by CMD_Quit to signal exit
    int _quitAfterSave;             // set by quit-save to quit after save completes

    // data entry state
    DataEntryMode _dataEntryMode;
    CxUTFString _dataEntryBuffer;   // current input being entered (UTF-8 aware)
    int _dataEntryCursorPos;        // cursor position within buffer (0 = start)

    // cell hunt state (for formula cell reference selection)
    int _inCellHuntMode;                      // 1 if in cell hunt mode
    int _cellHuntRangeActive;                 // 1 after SPACE pressed (selecting range)
    CxSheetCellCoordinate _cellHuntFormulaPos; // cell where formula entry started
    CxSheetCellCoordinate _cellHuntAnchorPos;  // range start (after SPACE)
    CxSheetCellCoordinate _cellHuntCurrentPos; // current hunt cursor position
    int _cellHuntInsertPos;                   // cursor position in formula to insert ref

    // range selection state (for EDIT mode multi-cell selection)
    int _rangeSelectActive;                   // 1 if range selection is active
    CxSheetCellCoordinate _rangeAnchor;       // where selection started
    CxSheetCellCoordinate _rangeCurrent;      // current end of selection

    // clipboard state
    CxSList<ClipboardCell> _clipboard;
    CxSheetCellCoordinate _clipboardAnchor;   // top-left of original copied range
    int _clipboardRows;                       // dimensions of clipboard content
    int _clipboardCols;

    // color picker state
    int _colorPickerIndex;           // current selection in palette
    int _colorPickerScrollOffset;    // scroll position for display
    int _colorPickerIsForeground;    // 1 = foreground, 0 = background
    int _colorPickerIsColumn;        // 1 = column default, 0 = cell color

    // helper methods
    void enterCommandLineMode(void);
    void exitCommandLineMode(void);
    void resetCommandInputState(void);

    // data entry helpers
    DataEntryMode deduceEntryModeFromChar(char c);
    int isValidInputChar(char c, DataEntryMode mode);
    CxString getCellDisplayText(CxSheetCell *cell);
    CxString getCellFormatIndicator(CxSheetCell *cell, int col);

    // post-commit parsing helpers (Excel-style type inference)
    int tryParseNumber(CxString input, double *value, int *hasCurrency,
                       int *hasPercent, int *hasThousands);
    int tryParseDate(CxString input, double *serialDate, CxString *dateFormat);

    // decomposed command input handlers
    void handleCommandEnter(void);
    void handleCommandTab(void);
    void handleCommandChar(CxKeyAction keyAction);

    // column width helpers
    void adjustColumnWidth(int delta);
    void autoFitColumnWidth(void);

    // clipboard helpers
    void copyRangeToClipboard(CxSheetCellCoordinate start, CxSheetCellCoordinate end);
    CxString adjustFormulaReferences(CxString formula, int rowDelta, int colDelta);

    // range helpers
    void normalizeRange(CxSheetCellCoordinate start, CxSheetCellCoordinate end,
                        int *minRow, int *maxRow, int *minCol, int *maxCol);
    void getColumnRange(int *startCol, int *endCol);
    void clearRangeSelection(void);

    // cell attribute helpers
    void setCellAttribute(CxSheetCellCoordinate coord, const char *attrName, const char *value);
    void setCellAttributeBool(CxSheetCellCoordinate coord, const char *attrName, int value);
    void setCellAttributeInt(CxSheetCellCoordinate coord, const char *attrName, int value);

    // resize callback - coordinates all redrawing
    void screenResizeCallback(void);
};


#endif
