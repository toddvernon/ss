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
        DATA_ENTRY          // entering data into a cell
    };

    enum CommandInputState {
        CMD_INPUT_IDLE,         // not in command input mode
        CMD_INPUT_COMMAND,      // entering command name
        CMD_INPUT_ARGUMENT      // entering command argument
    };

    enum DataEntryMode {
        ENTRY_NONE,             // not in data entry
        ENTRY_TEXT,             // entering text (started with letter)
        ENTRY_NUMBER,           // entering number (started with digit or +/-)
        ENTRY_CURRENCY,         // entering currency (started with $)
        ENTRY_FORMULA           // entering formula (started with =)
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

    // command handlers
    void CMD_Quit(CxString commandLine);
    void CMD_Load(CxString commandLine);
    void CMD_Save(CxString commandLine);
    void CMD_InsertSymbol(CxString commandLine);
    void CMD_FormatWidth(CxString commandLine);
    void CMD_FormatWidthAuto(CxString commandLine);
    void CMD_Copy(CxString commandLine);
    void CMD_Cut(CxString commandLine);
    void CMD_Paste(CxString commandLine);
    void CMD_Clear(CxString commandLine);
    void CMD_FormatAlignLeft(CxString commandLine);
    void CMD_FormatAlignCenter(CxString commandLine);
    void CMD_FormatAlignRight(CxString commandLine);

    ProgramMode programMode;

    CxScreen   *screen;
    CxKeyboard *keyboard;
    SheetView  *sheetView;
    CommandLineView *commandLineView;
    MessageLineView *messageLineView;
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

    // helper methods
    void enterCommandLineMode(void);
    void exitCommandLineMode(void);
    void resetCommandInputState(void);

    // data entry helpers
    DataEntryMode deduceEntryModeFromChar(char c);
    int isValidInputChar(char c, DataEntryMode mode);
    CxString getCellDisplayText(CxSheetCell *cell);

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

    // resize callback - coordinates all redrawing
    void screenResizeCallback(void);
};


#endif
