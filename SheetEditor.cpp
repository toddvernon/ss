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
#include <signal.h>

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
, sheetModel(NULL)
, _filePath(filePath)
, _activeCompleter(NULL)
, _cmdInputState(CMD_INPUT_IDLE)
, _currentCommand(NULL)
, _quitRequested(0)
, _dataEntryMode(ENTRY_NONE)
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
    // Create the sheet model
    //---------------------------------------------------------------------------------------------
    sheetModel = new CxSheetModel();

    // Load file if provided
    if (_filePath.length() > 0) {
        sheetModel->loadSheet(_filePath);
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
    sheetView = new SheetView(screen, sheetModel, 0, sheetEndRow);
    commandLineView = new CommandLineView(screen, sheetEndRow + 1);
    messageLineView = new MessageLineView(screen, sheetEndRow + 2);

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

    //---------------------------------------------------------------------------------------------
    // UNBLOCK SIGWINCH now that construction is complete
    //---------------------------------------------------------------------------------------------
    sigprocmask(SIG_SETMASK, &oldSet, NULL);

    //---------------------------------------------------------------------------------------------
    // Initial screen draw
    //---------------------------------------------------------------------------------------------
    sheetView->updateScreen();
    resetPrompt();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::~SheetEditor
//
// Destructor
//-------------------------------------------------------------------------------------------------
SheetEditor::~SheetEditor(void)
{
    if (sheetView != NULL) {
        delete sheetView;
    }
    if (commandLineView != NULL) {
        delete commandLineView;
    }
    if (messageLineView != NULL) {
        delete messageLineView;
    }
    if (sheetModel != NULL) {
        delete sheetModel;
    }
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

    while (1) {
        // Get next keyboard action (blocking)
        CxKeyAction keyAction = keyboard->getAction();

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
        }

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
        // ESC key enters command mode
        //-------------------------------------------------------------------------------------
        case CxKeyAction::COMMAND:
            enterCommandLineMode();
            break;

        //-------------------------------------------------------------------------------------
        // Arrow key navigation
        //-------------------------------------------------------------------------------------
        case CxKeyAction::CURSOR:
        {
            CxString tag = keyAction.tag();

            if (tag == "<arrow-up>") {
                sheetModel->cursorUpRequest();
            } else if (tag == "<arrow-down>") {
                sheetModel->cursorDownRequest();
            } else if (tag == "<arrow-left>") {
                sheetModel->cursorLeftRequest();
            } else if (tag == "<arrow-right>") {
                sheetModel->cursorRightRequest();
            }

            sheetView->updateScreen();
            resetPrompt();
        }
        break;

        //-------------------------------------------------------------------------------------
        // Letter keys start text entry mode
        //-------------------------------------------------------------------------------------
        case CxKeyAction::LOWERCASE_ALPHA:
        case CxKeyAction::UPPERCASE_ALPHA:
        {
            char c = keyAction.tag().data()[0];
            enterDataEntryMode(ENTRY_TEXT, c);
        }
        break;

        //-------------------------------------------------------------------------------------
        // Number keys start number entry mode
        //-------------------------------------------------------------------------------------
        case CxKeyAction::NUMBER:
        {
            char c = keyAction.tag().data()[0];
            enterDataEntryMode(ENTRY_NUMBER, c);
        }
        break;

        //-------------------------------------------------------------------------------------
        // Symbol keys: = starts formula, $ starts currency, +/- starts number
        //-------------------------------------------------------------------------------------
        case CxKeyAction::SYMBOL:
        {
            char c = keyAction.tag().data()[0];
            if (c == '=') {
                enterDataEntryMode(ENTRY_FORMULA, c);
            } else if (c == '$') {
                enterDataEntryMode(ENTRY_CURRENCY, c);
            } else if (c == '+' || c == '-') {
                enterDataEntryMode(ENTRY_NUMBER, c);
            }
        }
        break;

        default:
            break;
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
//-------------------------------------------------------------------------------------------------
void
SheetEditor::exitCommandLineMode(void)
{
    programMode = EDIT;
    _cmdInputState = CMD_INPUT_IDLE;
    resetPrompt();
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
        executeCurrentCommand();
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
            _argBuffer = _argBuffer + CxString(c);
            updateArgumentDisplay();
        }
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
//-------------------------------------------------------------------------------------------------
void
SheetEditor::handleCommandChar(CxKeyAction keyAction)
{
    char c = keyAction.tag().data()[0];
    _cmdBuffer = _cmdBuffer + CxString(c);

    CompleterResult result = _commandCompleter.processChar(_cmdBuffer, c);

    // If we got a unique selection, auto-select it
    if (result.getStatus() == COMPLETER_SELECTED) {
        CommandEntry *cmd = (CommandEntry *)result.getSelectedData();
        if (cmd != NULL) {
            selectCommand(cmd);
            return;
        }
    }

    updateCommandDisplay();
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

    if (cmd->flags & CMD_FLAG_NEEDS_ARG) {
        // Command needs an argument - switch to argument mode
        _cmdInputState = CMD_INPUT_ARGUMENT;
        _argBuffer = "";
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

    // Call the command handler
    (this->*(_currentCommand->handler))(_argBuffer);

    // Return to edit mode (unless quit was requested)
    if (!_quitRequested) {
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
//-------------------------------------------------------------------------------------------------
void
SheetEditor::updateCommandDisplay(void)
{
    CxString display = CxString("command> ") + _cmdBuffer;

    // Show matches - use buffer array for findMatches
    CxString matchNames[10];
    int matchCount = _commandCompleter.findMatches(_cmdBuffer, matchNames, 10);

    if (matchCount > 0) {
        display = display + "   [";
        for (int i = 0; i < matchCount && i < 5; i++) {
            if (i > 0) {
                display = display + " | ";
            }
            display = display + matchNames[i];
        }
        if (matchCount > 5) {
            display = display + " ...";
        }
        display = display + "]";
    }

    commandLineView->setText(display);
    commandLineView->updateScreen();
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::updateArgumentDisplay
//
// Update the command line to show argument input.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::updateArgumentDisplay(void)
{
    CxString display = _currentCommand->name;
    display = display + " ";
    if (_currentCommand->argHint != NULL) {
        display = display + _currentCommand->argHint;
    }
    display = display + ": " + _argBuffer;

    commandLineView->setText(display);
    commandLineView->updateScreen();
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
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::resetPrompt
//
// Reset the command line to show cell position.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::resetPrompt(void)
{
    CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
    commandLineView->setText(pos.toAddress());
    commandLineView->updateScreen();
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

    sheetView->recalcForResize(0, sheetEndRow);
    commandLineView->recalcForResize(sheetEndRow + 1);
    messageLineView->recalcForResize(sheetEndRow + 2);

    //---------------------------------------------------------------------------------------------
    // PHASE 2: Redraw everything in correct z-order
    //---------------------------------------------------------------------------------------------
    CxScreen::clearScreen();

    sheetView->updateScreen();
    commandLineView->updateScreen();
    messageLineView->updateScreen();

    // Place cursor based on current mode
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
// Quit command handler.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_Quit(CxString commandLine)
{
    _quitRequested = 1;
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
    _dataEntryBuffer = CxString(firstChar);

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

    // ENTER commits the data
    if (action == CxKeyAction::NEWLINE) {
        commitDataEntry();
        return;
    }

    // ESC cancels data entry (unless in formula mode where it enters cell hunt - future)
    if (action == CxKeyAction::COMMAND) {
        cancelDataEntry();
        return;
    }

    // BACKSPACE removes last character
    if (action == CxKeyAction::BACKSPACE) {
        if (_dataEntryBuffer.length() > 0) {
            _dataEntryBuffer = _dataEntryBuffer.subString(0, _dataEntryBuffer.length() - 1);
            // If buffer is empty, cancel entry
            if (_dataEntryBuffer.length() == 0) {
                cancelDataEntry();
                return;
            }
        }
        updateDataEntryDisplay();
        return;
    }

    // Character input - append to buffer
    if (action == CxKeyAction::LOWERCASE_ALPHA ||
        action == CxKeyAction::UPPERCASE_ALPHA ||
        action == CxKeyAction::SYMBOL ||
        action == CxKeyAction::NUMBER) {

        char c = keyAction.tag().data()[0];

        // Validate input based on entry mode
        int valid = 0;
        switch (_dataEntryMode) {
            case ENTRY_TEXT:
                // Text accepts anything printable
                valid = (c >= 32 && c < 127);
                break;

            case ENTRY_NUMBER:
                // Numbers accept digits, decimal point, +/-
                valid = (c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-';
                break;

            case ENTRY_CURRENCY:
                // Currency accepts digits, decimal point ($ already in buffer)
                valid = (c >= '0' && c <= '9') || c == '.';
                break;

            case ENTRY_FORMULA:
                // Formulas accept anything printable
                valid = (c >= 32 && c < 127);
                break;

            default:
                break;
        }

        if (valid) {
            _dataEntryBuffer = _dataEntryBuffer + CxString(c);
            updateDataEntryDisplay();
        }
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::commitDataEntry
//
// Commit the entered data to the current cell.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::commitDataEntry(void)
{
    CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
    CxSheetCell cell;

    switch (_dataEntryMode) {
        case ENTRY_TEXT:
            cell.setText(_dataEntryBuffer);
            sheetModel->setCell(pos, cell);
            break;

        case ENTRY_NUMBER:
        {
            // Parse as double
            double value = atof(_dataEntryBuffer.data());
            cell.setDouble(CxDouble(value));
            sheetModel->setCell(pos, cell);
        }
        break;

        case ENTRY_CURRENCY:
        {
            // Skip the $ prefix when parsing
            CxString numStr = _dataEntryBuffer;
            if (numStr.length() > 0 && numStr.data()[0] == '$') {
                numStr = numStr.subString(1, numStr.length() - 1);
            }
            double value = atof(numStr.data());
            cell.setDouble(CxDouble(value));
            sheetModel->setCell(pos, cell);
            // TODO: Set currency formatting flag when formatting is implemented
        }
        break;

        case ENTRY_FORMULA:
            cell.setFormula(_dataEntryBuffer);
            sheetModel->setCell(pos, cell);
            break;

        default:
            break;
    }

    // Return to edit mode
    programMode = EDIT;
    _dataEntryMode = ENTRY_NONE;
    _dataEntryBuffer = "";

    // Refresh display
    sheetView->updateScreen();
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
    _dataEntryBuffer = "";

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

    switch (_dataEntryMode) {
        case ENTRY_TEXT:
            display = CxString("text: ") + _dataEntryBuffer;
            break;
        case ENTRY_NUMBER:
            display = CxString("number: ") + _dataEntryBuffer;
            break;
        case ENTRY_CURRENCY:
            display = CxString("currency: ") + _dataEntryBuffer;
            break;
        case ENTRY_FORMULA:
            display = CxString("formula: ") + _dataEntryBuffer;
            break;
        default:
            display = _dataEntryBuffer;
            break;
    }

    commandLineView->setText(display);
    commandLineView->updateScreen();
}
