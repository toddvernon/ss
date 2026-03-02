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
#include <cx/keyboard/keyboard.h>
#include <cx/screen/screen.h>
#include <cx/functor/defercall.h>
#include <cx/sheetModel/sheetModel.h>
#include <cx/commandcompleter/completer.h>

#include "SheetView.h"
#include "CommandLineView.h"
#include "MessageLineView.h"
#include "CommandTable.h"

#ifndef _SheetEditor_h_
#define _SheetEditor_h_


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
    void setMessage(CxString message);

    // data entry methods
    void enterDataEntryMode(DataEntryMode mode, char firstChar);
    void editCurrentCell(void);
    void focusDataEntry(CxKeyAction keyAction);
    void commitDataEntry(void);
    void cancelDataEntry(void);
    void updateDataEntryDisplay(void);

    // command handlers
    void CMD_Quit(CxString commandLine);

    ProgramMode programMode;

    CxScreen   *screen;
    CxKeyboard *keyboard;
    SheetView  *sheetView;
    CommandLineView *commandLineView;
    MessageLineView *messageLineView;
    CxSheetModel *sheetModel;

    CxString _filePath;

  private:

    // command completion
    Completer       _commandCompleter;
    Completer      *_activeCompleter;

    // command input state
    CommandInputState _cmdInputState;
    CxString _cmdBuffer;            // input for active completer
    CxString _argBuffer;            // freeform argument text
    CommandEntry *_currentCommand;  // selected command (after completion)
    int _quitRequested;             // set by CMD_Quit to signal exit

    // data entry state
    DataEntryMode _dataEntryMode;
    CxString _dataEntryBuffer;      // current input being entered

    // helper methods
    void enterCommandLineMode(void);
    void exitCommandLineMode(void);
    void resetCommandInputState(void);

    // decomposed command input handlers
    void handleCommandEnter(void);
    void handleCommandTab(void);
    void handleCommandChar(CxKeyAction keyAction);

    // resize callback - coordinates all redrawing
    void screenResizeCallback(void);
};


#endif
