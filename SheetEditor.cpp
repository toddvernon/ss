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

#include <cx/base/utfstring.h>
#include <cx/base/utfcharacter.h>
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
// Reset the command line to show cell position and content.
// For formulas, shows the formula text (with = prefix).
// For other types, shows the display value.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::resetPrompt(void)
{
    CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
    CxSheetCell *cell = sheetModel->getCellPtr(pos);

    CxString display = pos.toAddress();

    CxString cellText = getCellDisplayText(cell);
    if (cellText.length() > 0) {
        display = display + " " + cellText;
    }

    commandLineView->setText(display);
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
// SheetEditor::deduceEntryModeFromChar
//
// Determine the appropriate data entry mode based on the first character typed.
//-------------------------------------------------------------------------------------------------
SheetEditor::DataEntryMode
SheetEditor::deduceEntryModeFromChar(char c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
        return ENTRY_TEXT;
    } else if (c >= '0' && c <= '9') {
        return ENTRY_NUMBER;
    } else if (c == '=') {
        return ENTRY_FORMULA;
    } else if (c == '$') {
        return ENTRY_CURRENCY;
    } else if (c == '+' || c == '-') {
        return ENTRY_NUMBER;
    }
    // Default to text for other printable characters
    return ENTRY_TEXT;
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::isValidInputChar
//
// Check if a character is valid input for the given data entry mode.
//-------------------------------------------------------------------------------------------------
int
SheetEditor::isValidInputChar(char c, DataEntryMode mode)
{
    switch (mode) {
        case ENTRY_TEXT:
            // Text accepts anything printable
            return (c >= 32 && c < 127);

        case ENTRY_NUMBER:
            // Numbers accept digits, decimal point, +/-
            return (c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-';

        case ENTRY_CURRENCY:
            // Currency accepts digits, decimal point ($ already in buffer)
            return (c >= '0' && c <= '9') || c == '.';

        case ENTRY_FORMULA:
            // Formulas accept anything printable
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
    if (cell == NULL || cell->getType() == CxSheetCell::EMPTY) {
        return "";
    }

    switch (cell->getType()) {
        case CxSheetCell::TEXT:
            return cell->getText();

        case CxSheetCell::DOUBLE:
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", cell->getDouble().value);
            return CxString(buf);
        }

        case CxSheetCell::FORMULA:
            // Return formula with = prefix
            return CxString("=") + cell->getFormulaText();

        default:
            return "";
    }
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

    // If cell is empty, just enter text mode with empty buffer
    if (cell == NULL || cell->getType() == CxSheetCell::EMPTY) {
        programMode = DATA_ENTRY;
        _dataEntryMode = ENTRY_TEXT;
        _dataEntryBuffer.clear();
        updateDataEntryDisplay();
        return;
    }

    // Enter data entry mode with existing content
    programMode = DATA_ENTRY;

    // Set mode based on cell type
    switch (cell->getType()) {
        case CxSheetCell::TEXT:
            _dataEntryMode = ENTRY_TEXT;
            break;
        case CxSheetCell::DOUBLE:
            _dataEntryMode = ENTRY_NUMBER;
            break;
        case CxSheetCell::FORMULA:
            _dataEntryMode = ENTRY_FORMULA;
            break;
        default:
            _dataEntryMode = ENTRY_TEXT;
            break;
    }

    // Load cell content into buffer (getCellDisplayText handles formatting)
    _dataEntryBuffer.fromCxString(getCellDisplayText(cell), 8);

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

    // BACKSPACE removes last character (stops at empty buffer)
    if (action == CxKeyAction::BACKSPACE) {
        int count = _dataEntryBuffer.charCount();
        if (count > 0) {
            _dataEntryBuffer.remove(count - 1, 1);
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
            updateDataEntryDisplay();
            return;
        }

        // Validate and append if valid for current mode
        if (isValidInputChar(c, _dataEntryMode)) {
            _dataEntryBuffer.append(CxUTFCharacter::fromASCII(c));
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

    // Convert UTF buffer to bytes for cell storage
    CxString bufferText = _dataEntryBuffer.toBytes();

    switch (_dataEntryMode) {
        case ENTRY_TEXT:
            cell.setText(bufferText);
            sheetModel->setCell(pos, cell);
            break;

        case ENTRY_NUMBER:
        {
            // Parse as double
            double value = atof(bufferText.data());
            cell.setDouble(CxDouble(value));
            sheetModel->setCell(pos, cell);
        }
        break;

        case ENTRY_CURRENCY:
        {
            // Skip the $ prefix when parsing
            CxString numStr = bufferText;
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
        {
            // Strip the leading '=' that's used for spreadsheet notation
            CxString formulaText = bufferText;
            if (formulaText.length() > 0 && formulaText.data()[0] == '=') {
                formulaText = formulaText.subString(1, formulaText.length() - 1);
            }
            cell.setFormula(formulaText);
            sheetModel->setCell(pos, cell);
        }
        break;

        default:
            break;
    }

    // Return to edit mode
    programMode = EDIT;
    _dataEntryMode = ENTRY_NONE;
    _dataEntryBuffer.clear();

    // Move cursor down to next row (standard spreadsheet behavior)
    sheetModel->cursorDownRequest();

    // Optimized refresh - only redraw affected cells plus the new cursor position
    CxSList<CxSheetCellCoordinate> affected = sheetModel->getLastAffectedCells();
    affected.append(sheetModel->getCurrentPosition());  // add new cursor location
    sheetView->updateCells(affected);
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

    switch (_dataEntryMode) {
        case ENTRY_TEXT:
            display = CxString("text: ") + bufferBytes;
            break;
        case ENTRY_NUMBER:
            display = CxString("number: ") + bufferBytes;
            break;
        case ENTRY_CURRENCY:
            display = CxString("currency: ") + bufferBytes;
            break;
        case ENTRY_FORMULA:
            display = CxString("formula: ") + bufferBytes;
            break;
        default:
            display = bufferBytes;
            break;
    }

    commandLineView->setText(display);
    commandLineView->updateScreen();
    commandLineView->placeCursor();
    fflush(stdout);
}
