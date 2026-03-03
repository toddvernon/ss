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
#include <cx/base/file.h>
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
, spreadsheetDefaults(NULL)
, _filePath(filePath)
, _activeCompleter(NULL)
, _cmdInputState(CMD_INPUT_IDLE)
, _currentCommand(NULL)
, _quitRequested(0)
, _dataEntryMode(ENTRY_NONE)
, _inCellHuntMode(0)
, _cellHuntRangeActive(0)
, _cellHuntInsertPos(0)
, _dataEntryCursorPos(0)
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
    if (spreadsheetDefaults != NULL) {
        delete spreadsheetDefaults;
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

        // Cell hunt mode takes priority when active
        if (_inCellHuntMode) {
            focusCellHunt(keyAction);
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

            // Save old position before moving
            CxSheetCellCoordinate oldPos = sheetModel->getCurrentPosition();

            if (tag == "<arrow-up>") {
                sheetModel->cursorUpRequest();
            } else if (tag == "<arrow-down>") {
                sheetModel->cursorDownRequest();
            } else if (tag == "<arrow-left>") {
                sheetModel->cursorLeftRequest();
            } else if (tag == "<arrow-right>") {
                sheetModel->cursorRightRequest();
            }

            // Get new position and update display (handles scrolling if needed)
            CxSheetCellCoordinate newPos = sheetModel->getCurrentPosition();
            sheetView->updateCursorMove(oldPos, newPos);
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
            CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
            CxSheetCell *cell = sheetModel->getCellPtr(pos);
            if (cell != NULL) {
                cell->clear();
                cell->removeAppAttribute("symbolFill");
                sheetView->updateScreen();
                resetPrompt();
            }
        }
        break;

        //-------------------------------------------------------------------------------------
        // Control key sequences (Ctrl-X prefix commands)
        //-------------------------------------------------------------------------------------
        case CxKeyAction::CONTROL:
        {
            if (keyAction.tag() == "X") {
                return dispatchControlX();
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
        CMD_Save("");
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
// Cursor is positioned after the typed text, not after the hints.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::updateCommandDisplay(void)
{
    CxString prefix = "command> ";
    CxString display = prefix + _cmdBuffer;

    // Show matches - use buffer array for findMatches
    CxString matchNames[10];
    int matchCount = _commandCompleter.findMatches(_cmdBuffer, matchNames, 10);

    // Don't show hints if exact match
    int isExactMatch = (matchCount == 1 && _cmdBuffer == matchNames[0]);

    if (matchCount > 0 && !isExactMatch) {
        display = display + "  ";
        for (int i = 0; i < matchCount && i < 5; i++) {
            display = display + "| ";
            display = display + matchNames[i];
            display = display + " ";
        }
        if (matchCount > 5) {
            display = display + "...";
        }
    }

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
    if (_activeCompleter != NULL) {
        CxString matchNames[10];
        int matchCount = _activeCompleter->findMatches(_argBuffer, matchNames, 10);

        // Don't show hints if exact match
        int isExactMatch = (matchCount == 1 && _argBuffer == matchNames[0]);

        if (matchCount > 0 && !isExactMatch) {
            display = display + "  ";
            for (int i = 0; i < matchCount && i < 5; i++) {
                display = display + "| ";
                display = display + matchNames[i];
                display = display + " ";
            }
            if (matchCount > 5) {
                display = display + "...";
            }
        }
    }

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
// SheetEditor::resetPrompt
//
// Reset the command line to show cell position and content.
// For formulas, shows the formula text (with = prefix).
// For other types, shows the display value.
// Also clears any message on the message line (like "Loaded: ..." messages).
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

    // Try to save the file
    if (sheetModel->saveSheet(filepath)) {
        _filePath = filepath;
        sheetView->setFilePath(_filePath);
        sheetView->updateStatusLine();
        setMessage(CxString("Saved: ") + filepath);
    } else {
        setMessage(CxString("Failed to save: ") + filepath);
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

    // If cell is empty, just enter text mode with empty buffer
    if (cell == NULL || cell->getType() == CxSheetCell::EMPTY) {
        programMode = DATA_ENTRY;
        _dataEntryMode = ENTRY_TEXT;
        _dataEntryBuffer.clear();
        _dataEntryCursorPos = 0;
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

    // Shift+Arrow in formula mode enters cell hunt mode
    if (action == CxKeyAction::SHIFT_CURSOR && _dataEntryMode == ENTRY_FORMULA) {
        CxString tag = keyAction.tag();
        enterCellHuntMode(tag);
        return;
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
        // UP/DOWN could be used to cancel and move cells, but ignore for now
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
    int prefixLen = 0;

    switch (_dataEntryMode) {
        case ENTRY_TEXT:
            display = CxString("text: ") + bufferBytes;
            prefixLen = 6;  // "text: "
            break;
        case ENTRY_NUMBER:
            display = CxString("number: ") + bufferBytes;
            prefixLen = 8;  // "number: "
            break;
        case ENTRY_CURRENCY:
            display = CxString("currency: ") + bufferBytes;
            prefixLen = 10;  // "currency: "
            break;
        case ENTRY_FORMULA:
            display = CxString("formula: ") + bufferBytes;
            prefixLen = 9;  // "formula: "
            break;
        default:
            display = bufferBytes;
            prefixLen = 0;
            break;
    }

    commandLineView->setText(display);
    commandLineView->updateScreen();
    commandLineView->placeCursorAt(prefixLen + _dataEntryCursorPos);
    fflush(stdout);
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::enterCellHuntMode
//
// Enter cell hunt mode for selecting a cell reference in a formula. The direction parameter
// indicates which shift+arrow key was pressed to enter the mode, and we move the hunt cursor
// one cell in that direction from the formula cell.
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

    // Move one cell in the direction of the shift+arrow
    if (direction == "<shift-arrow-up>") {
        if (_cellHuntCurrentPos.getRow() > 0) {
            _cellHuntCurrentPos.setRow(_cellHuntCurrentPos.getRow() - 1);
        }
    } else if (direction == "<shift-arrow-down>") {
        _cellHuntCurrentPos.setRow(_cellHuntCurrentPos.getRow() + 1);
    } else if (direction == "<shift-arrow-left>") {
        if (_cellHuntCurrentPos.getCol() > 0) {
            _cellHuntCurrentPos.setCol(_cellHuntCurrentPos.getCol() - 1);
        }
    } else if (direction == "<shift-arrow-right>") {
        _cellHuntCurrentPos.setCol(_cellHuntCurrentPos.getCol() + 1);
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
        _dataEntryCursorPos = _cellHuntInsertPos + ref.length();
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

    // SPACE - anchor range start (toggle range mode)
    if (action == CxKeyAction::SYMBOL && keyAction.tag() == " ") {
        if (!_cellHuntRangeActive) {
            // Start range selection
            _cellHuntRangeActive = 1;
            _cellHuntAnchorPos = _cellHuntCurrentPos;
            sheetView->setHuntRange(1, _cellHuntAnchorPos, _cellHuntCurrentPos);
        } else {
            // SPACE again could toggle off, but for now we keep it simple
            // and just ignore
        }
        sheetView->updateScreen();
        updateCellHuntDisplay();
        return;
    }

    // Arrow keys - move hunt cursor (shift+arrow also accepted)
    if (action == CxKeyAction::CURSOR || action == CxKeyAction::SHIFT_CURSOR) {
        CxString tag = keyAction.tag();
        CxSheetCellCoordinate oldPos = _cellHuntCurrentPos;

        // Handle both regular arrow and shift+arrow tags
        if (tag == "<arrow-up>" || tag == "<shift-arrow-up>") {
            if (_cellHuntCurrentPos.getRow() > 0) {
                _cellHuntCurrentPos.setRow(_cellHuntCurrentPos.getRow() - 1);
            }
        } else if (tag == "<arrow-down>" || tag == "<shift-arrow-down>") {
            _cellHuntCurrentPos.setRow(_cellHuntCurrentPos.getRow() + 1);
        } else if (tag == "<arrow-left>" || tag == "<shift-arrow-left>") {
            if (_cellHuntCurrentPos.getCol() > 0) {
                _cellHuntCurrentPos.setCol(_cellHuntCurrentPos.getCol() - 1);
            }
        } else if (tag == "<arrow-right>" || tag == "<shift-arrow-right>") {
            _cellHuntCurrentPos.setCol(_cellHuntCurrentPos.getCol() + 1);
        }

        // Update SheetView
        if (_cellHuntRangeActive) {
            sheetView->setHuntRange(1, _cellHuntAnchorPos, _cellHuntCurrentPos);
        }
        sheetView->updateCellHuntMove(oldPos, _cellHuntCurrentPos);

        // Update command line with live reference
        updateCellHuntDisplay();
        return;
    }
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
    CxString bufferBytes = _dataEntryBuffer.toBytes();
    CxString ref = buildCellHuntReference();

    // Create display: formula prefix + buffer up to insert pos + reference + rest of buffer
    CxString beforeInsert = bufferBytes.subString(0, _cellHuntInsertPos);
    CxString afterInsert = "";
    if (_cellHuntInsertPos < (int)bufferBytes.length()) {
        afterInsert = bufferBytes.subString(_cellHuntInsertPos,
                                            bufferBytes.length() - _cellHuntInsertPos);
    }

    CxString display = CxString("formula: ") + beforeInsert + ref + afterInsert;

    commandLineView->setText(display);
    commandLineView->updateScreen();

    // Place cursor after the reference preview
    commandLineView->placeCursorAt(9 + _cellHuntInsertPos + ref.length());
    fflush(stdout);
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
