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
#include <cx/json/json_utf_object.h>
#include <cx/sheetModel/sheetInputParser.h>
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
, _rangeSelectActive(0)
, _clipboardRows(0)
, _clipboardCols(0)
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
    // Load column widths from app data (if file was loaded)
    //---------------------------------------------------------------------------------------------
    if (fileLoadResult) {
        CxJSONUTFObject *appData = sheetModel->getAppData();
        sheetView->loadColumnWidthsFromAppData(appData);
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
                _rangeSelectActive = 0;
                sheetView->setRangeSelection(0, _rangeAnchor, _rangeCurrent);
            }

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
        // Ctrl+Arrow key adjusts column width
        //-------------------------------------------------------------------------------------
        case CxKeyAction::CTRL_CURSOR:
        {
            CxString tag = keyAction.tag();
            if (tag == "<ctrl-arrow-right>") {
                adjustColumnWidth(1);
            } else if (tag == "<ctrl-arrow-left>") {
                adjustColumnWidth(-1);
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
            } else if (tag == "<shift-arrow-down>") {
                sheetModel->cursorDownRequest();
            } else if (tag == "<shift-arrow-left>") {
                sheetModel->cursorLeftRequest();
            } else if (tag == "<shift-arrow-right>") {
                sheetModel->cursorRightRequest();
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
                // Ctrl-K: copy selection
                CMD_Copy("");
            }
            else if (tag == "Y") {
                // Ctrl-Y: paste
                CMD_Paste("");
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
// Shows category prefixes when input is empty, then collapses sub-categories
// (like modify-align-) until narrowed down. Cursor positioned after typed text.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::updateCommandDisplay(void)
{
    CxString prefix = "command> ";
    CxString display = prefix + _cmdBuffer;

    // Get all matches
    CxString matchNames[32];
    int matchCount = _activeCompleter->findMatches(_cmdBuffer, matchNames, 32);

    // Don't show hints if exact match
    int isExactMatch = (matchCount == 1 && _cmdBuffer == matchNames[0]);

    if (matchCount > 0 && !isExactMatch) {
        display = display + "  ";

        if (_cmdBuffer.length() == 0) {
            // Empty input: show category prefixes (file-, edit-, modify-, etc.)
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
                display = display + categories[i];
                display = display + " ";
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
                display = display + displayItems[i];
                display = display + " ";
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
// Does NOT clear the message line - messages persist until the next user action.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::updateCommandLineDisplay(void)
{
    CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
    CxSheetCell *cell = sheetModel->getCellPtr(pos);

    CxString display = pos.toAddress();

    CxString cellText = getCellDisplayText(cell);
    if (cellText.length() > 0) {
        display = display + " " + cellText;
    }

    commandLineView->setDimMode(1);
    commandLineView->setText(display);
    commandLineView->updateScreen();
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
                int len = text.length();
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
    (void)commandLine;
    _quitRequested = 1;
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_QuitSave
//
// Save and quit.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_QuitSave(CxString commandLine)
{
    (void)commandLine;
    CMD_Save("");
    CMD_Quit("");
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_QuitWithoutSave
//
// Quit without saving.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_QuitWithoutSave(CxString commandLine)
{
    (void)commandLine;
    CMD_Quit("");
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
            CxString numStr = sheetView->formatNumber(cell->getDouble().value, cell);
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
// Uses post-commit parsing (Excel-style) to determine cell type from input.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::commitDataEntry(void)
{
    CxSheetCellCoordinate pos = sheetModel->getCurrentPosition();
    CxSheetCell cell;

    // Convert UTF buffer to bytes for cell storage
    CxString bufferText = _dataEntryBuffer.toBytes();

    if (_dataEntryMode == ENTRY_TEXTMAP) {
        // Textmap mode - strip leading '@' and store as appAttribute on EMPTY cell
        CxString ruleText = bufferText;
        if (ruleText.length() > 0 && ruleText.data()[0] == '@') {
            ruleText = ruleText.subString(1, ruleText.length() - 1);
        }
        // Cell stays EMPTY; the textmap rule is a display-layer attribute
        cell.setAppAttribute("textmap", ruleText.data());
        sheetModel->setCell(pos, cell);
    }
    else if (_dataEntryMode == ENTRY_FORMULA) {
        // Formula mode - strip leading '=' and parse as formula
        CxString formulaText = bufferText;
        if (formulaText.length() > 0 && formulaText.data()[0] == '=') {
            formulaText = formulaText.subString(1, formulaText.length() - 1);
        }
        cell.setFormula(formulaText);
        sheetModel->setCell(pos, cell);
    }
    else if (_dataEntryMode == ENTRY_GENERAL) {
        // Post-commit parsing via sheetModel library
        CxSheetInputParseResult result = CxSheetInputParser::parseAndClassify(bufferText);

        if (!result.success) {
            setMessage(CxString("Input error: ") + result.errorMsg);
            return;
        }

        // Preserve formatting attributes from the existing cell when the new
        // input doesn't explicitly specify them (e.g. typing "25" into a
        // currency cell should keep currency formatting).
        CxSheetCell *oldCellPtr = sheetModel->getCellPtr(pos);
        int oldHasCurrency = 0;
        int oldHasPercent = 0;
        int oldHasThousands = 0;
        int oldHasDecimalPlaces = 0;
        int oldDecimalPlaces = 2;
        int oldHasAlign = 0;
        CxString oldAlign;
        if (oldCellPtr) {
            oldHasCurrency = oldCellPtr->hasAppAttribute("currency") ? 1 : 0;
            oldHasPercent = oldCellPtr->hasAppAttribute("percent") ? 1 : 0;
            oldHasThousands = oldCellPtr->hasAppAttribute("thousands") ? 1 : 0;
            oldHasDecimalPlaces = oldCellPtr->hasAppAttribute("decimalPlaces") ? 1 : 0;
            oldDecimalPlaces = oldCellPtr->getAppAttributeInt("decimalPlaces", 2);
            oldHasAlign = oldCellPtr->hasAppAttribute("align") ? 1 : 0;
            if (oldHasAlign) {
                oldAlign = oldCellPtr->getAppAttributeString("align");
            }
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
            if (!result.hasCurrency && oldHasCurrency) {
                cellPtr->setAppAttribute("currency", true);
            }
            if (!result.hasPercent && oldHasPercent) {
                cellPtr->setAppAttribute("percent", true);
            }
            if (!result.hasThousands && oldHasThousands) {
                cellPtr->setAppAttribute("thousands", true);
            }
            if (oldHasDecimalPlaces && result.dateFormat.length() == 0) {
                cellPtr->setAppAttribute("decimalPlaces", oldDecimalPlaces);
            }
            if (oldHasAlign) {
                cellPtr->setAppAttribute("align", oldAlign.data());
            }
        }
    }

    // Return to edit mode
    programMode = EDIT;
    _dataEntryMode = ENTRY_NONE;
    _dataEntryBuffer.clear();

    // Move cursor down to next row (standard spreadsheet behavior)
    sheetModel->cursorDownRequest();

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
    commandLineView->placeCursorAt(prefixLen + _dataEntryCursorPos);
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

    commandLineView->setDimMode(0);
    commandLineView->setText(display);
    commandLineView->updateScreen();

    // Place cursor after the reference preview
    commandLineView->placeCursorAt(9 + _cellHuntInsertPos + ref.length());
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
    int minRow = start.getRow();
    int maxRow = end.getRow();
    if (minRow > maxRow) {
        int tmp = minRow;
        minRow = maxRow;
        maxRow = tmp;
    }

    int minCol = start.getCol();
    int maxCol = end.getCol();
    if (minCol > maxCol) {
        int tmp = minCol;
        minCol = maxCol;
        maxCol = tmp;
    }

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
// SheetEditor::adjustFormulaReferences
//
// Adjust cell references in a formula by the given row and column deltas.
// Relative references are adjusted, absolute references ($) are preserved.
//-------------------------------------------------------------------------------------------------
CxString
SheetEditor::adjustFormulaReferences(CxString formula, int rowDelta, int colDelta)
{
    // Parse the formula to get variable and range lists
    CxExpression expr(formula);
    expr.Parse();

    CxSList<CxString> ranges = expr.GetRangeList();
    CxSList<CxString> vars = expr.GetVariableList();

    CxString result = formula;

    // Process ranges first (they contain cell references we shouldn't double-process)
    for (int i = 0; i < (int)ranges.entries(); i++) {
        CxString rangeStr = ranges.at(i);

        // Parse the range (format: "A1:B2" or "$A$1:$B$2")
        int colonPos = -1;
        for (int j = 0; j < (int)rangeStr.length(); j++) {
            if (rangeStr.data()[j] == ':') {
                colonPos = j;
                break;
            }
        }

        if (colonPos > 0) {
            CxString startStr = rangeStr.subString(0, colonPos);
            CxString endStr = rangeStr.subString(colonPos + 1, rangeStr.length() - colonPos - 1);

            CxSheetCellCoordinate startCoord(startStr);
            CxSheetCellCoordinate endCoord(endStr);

            // Adjust non-absolute coordinates
            if (!startCoord.isRowAbsolute()) {
                int newRow = (int)startCoord.getRow() + rowDelta;
                if (newRow < 0) newRow = 0;
                startCoord.setRow(newRow);
            }
            if (!startCoord.isColAbsolute()) {
                int newCol = (int)startCoord.getCol() + colDelta;
                if (newCol < 0) newCol = 0;
                startCoord.setCol(newCol);
            }
            if (!endCoord.isRowAbsolute()) {
                int newRow = (int)endCoord.getRow() + rowDelta;
                if (newRow < 0) newRow = 0;
                endCoord.setRow(newRow);
            }
            if (!endCoord.isColAbsolute()) {
                int newCol = (int)endCoord.getCol() + colDelta;
                if (newCol < 0) newCol = 0;
                endCoord.setCol(newCol);
            }

            // Build new range string preserving absolute markers
            CxString newRange = startCoord.toAbsoluteAddress() + ":" + endCoord.toAbsoluteAddress();

            // Replace in result (first occurrence only since ranges are unique)
            int pos = result.index(rangeStr);
            if (pos >= 0) {
                CxString before = result.subString(0, pos);
                CxString after = result.subString(pos + rangeStr.length(),
                                                   result.length() - pos - rangeStr.length());
                result = before + newRange + after;
            }
        }
    }

    // Process single cell references (skip those that are part of ranges)
    for (int i = 0; i < (int)vars.entries(); i++) {
        CxString varStr = vars.at(i);

        // Check if this variable is part of a range (skip if so)
        int isPartOfRange = 0;
        for (int j = 0; j < (int)ranges.entries(); j++) {
            CxString rangeStr = ranges.at(j);
            if (rangeStr.index(varStr) >= 0) {
                isPartOfRange = 1;
                break;
            }
        }
        if (isPartOfRange) {
            continue;
        }

        CxSheetCellCoordinate coord(varStr);

        // Adjust non-absolute coordinates
        if (!coord.isRowAbsolute()) {
            int newRow = (int)coord.getRow() + rowDelta;
            if (newRow < 0) newRow = 0;
            coord.setRow(newRow);
        }
        if (!coord.isColAbsolute()) {
            int newCol = (int)coord.getCol() + colDelta;
            if (newCol < 0) newCol = 0;
            coord.setCol(newCol);
        }

        // Build new reference preserving absolute markers
        CxString newRef = coord.toAbsoluteAddress();

        // Replace in result (careful to match whole reference, not substrings)
        // We need to find exact matches, not partial matches like "A1" in "A10"
        int searchPos = 0;
        while (searchPos < (int)result.length()) {
            int pos = result.index(varStr, searchPos);
            if (pos < 0) break;

            // Check if this is a complete reference (not part of a larger identifier)
            int isBoundedStart = (pos == 0) ||
                                  (!((result.data()[pos-1] >= 'A' && result.data()[pos-1] <= 'Z') ||
                                     (result.data()[pos-1] >= 'a' && result.data()[pos-1] <= 'z') ||
                                     (result.data()[pos-1] >= '0' && result.data()[pos-1] <= '9') ||
                                     result.data()[pos-1] == '$'));

            int endPos = pos + varStr.length();
            int isBoundedEnd = (endPos >= (int)result.length()) ||
                               (!((result.data()[endPos] >= 'A' && result.data()[endPos] <= 'Z') ||
                                  (result.data()[endPos] >= 'a' && result.data()[endPos] <= 'z') ||
                                  (result.data()[endPos] >= '0' && result.data()[endPos] <= '9')));

            if (isBoundedStart && isBoundedEnd) {
                CxString before = result.subString(0, pos);
                CxString after = result.subString(endPos, result.length() - endPos);
                result = before + newRef + after;
                searchPos = pos + newRef.length();
            } else {
                searchPos = endPos;
            }
        }
    }

    return result;
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
// Copies the cells and then clears the source.
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

    int cellCount = (int)_clipboard.entries();

    // Normalize range for clearing
    int minRow = start.getRow();
    int maxRow = end.getRow();
    if (minRow > maxRow) {
        int tmp = minRow;
        minRow = maxRow;
        maxRow = tmp;
    }

    int minCol = start.getCol();
    int maxCol = end.getCol();
    if (minCol > maxCol) {
        int tmp = minCol;
        minCol = maxCol;
        maxCol = tmp;
    }

    // Clear source cells
    for (int row = minRow; row <= maxRow; row++) {
        for (int col = minCol; col <= maxCol; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);

            CxSheetCell *cell = sheetModel->getCellPtr(coord);
            if (cell != NULL) {
                cell->clear();
                cell->removeAppAttribute("symbolFill");
            }
        }
    }

    // Clear range selection
    if (_rangeSelectActive) {
        _rangeSelectActive = 0;
        sheetView->setRangeSelection(0, _rangeAnchor, _rangeCurrent);
    }

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
// Paste clipboard contents at the current cursor position. Formulas have their references
// adjusted based on the paste offset.
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_Paste(CxString commandLine)
{
    (void)commandLine;  // unused

    if (_clipboard.entries() == 0) {
        setMessage("Clipboard is empty");
        return;
    }

    // Get paste anchor (current position is top-left of paste)
    CxSheetCellCoordinate pasteAnchor = sheetModel->getCurrentPosition();

    // Calculate the delta from original position to paste position
    // All cells move by the same delta: pasteAnchor - clipboardAnchor
    int rowDelta = pasteAnchor.getRow() - _clipboardAnchor.getRow();
    int colDelta = pasteAnchor.getCol() - _clipboardAnchor.getCol();

    // Paste each cell
    for (int i = 0; i < (int)_clipboard.entries(); i++) {
        ClipboardCell clipCell = _clipboard.at(i);

        int targetRow = pasteAnchor.getRow() + clipCell.rowOffset;
        int targetCol = pasteAnchor.getCol() + clipCell.colOffset;

        CxSheetCellCoordinate targetCoord;
        targetCoord.setRow(targetRow);
        targetCoord.setCol(targetCol);

        CxSheetCell newCell = clipCell.cell;

        // If it's a formula, adjust references
        if (newCell.getType() == CxSheetCell::FORMULA) {
            CxString adjustedFormula = adjustFormulaReferences(newCell.getFormulaText(),
                                                                rowDelta, colDelta);
            newCell.setFormula(adjustedFormula);
        }

        sheetModel->setCell(targetCoord, newCell);
    }

    // Clear range selection if active
    if (_rangeSelectActive) {
        _rangeSelectActive = 0;
        sheetView->setRangeSelection(0, _rangeAnchor, _rangeCurrent);
    }

    sheetView->updateScreen();

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
    int minRow = start.getRow();
    int maxRow = end.getRow();
    if (minRow > maxRow) {
        int tmp = minRow;
        minRow = maxRow;
        maxRow = tmp;
    }

    int minCol = start.getCol();
    int maxCol = end.getCol();
    if (minCol > maxCol) {
        int tmp = minCol;
        minCol = maxCol;
        maxCol = tmp;
    }

    int cellCount = 0;

    // Clear cells
    for (int row = minRow; row <= maxRow; row++) {
        for (int col = minCol; col <= maxCol; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);

            CxSheetCell *cell = sheetModel->getCellPtr(coord);
            if (cell != NULL) {
                cell->clear();
                cell->removeAppAttribute("symbolFill");
            }
            cellCount++;
        }
    }

    // Clear range selection
    if (_rangeSelectActive) {
        _rangeSelectActive = 0;
        sheetView->setRangeSelection(0, _rangeAnchor, _rangeCurrent);
    }

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
// SheetEditor::CMD_FormatCellAlignLeft
//
// Set left alignment on the current selection (or current cell if no selection).
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatCellAlignLeft(CxString commandLine)
{
    (void)commandLine;  // unused

    CxSheetCellCoordinate start, end;

    if (_rangeSelectActive) {
        start = _rangeAnchor;
        end = _rangeCurrent;
    } else {
        start = sheetModel->getCurrentPosition();
        end = start;
    }

    // Normalize range
    int minRow = start.getRow();
    int maxRow = end.getRow();
    if (minRow > maxRow) {
        int tmp = minRow;
        minRow = maxRow;
        maxRow = tmp;
    }

    int minCol = start.getCol();
    int maxCol = end.getCol();
    if (minCol > maxCol) {
        int tmp = minCol;
        minCol = maxCol;
        maxCol = tmp;
    }

    int cellCount = 0;

    // Apply alignment
    for (int row = minRow; row <= maxRow; row++) {
        for (int col = minCol; col <= maxCol; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);

            // Get or create cell
            CxSheetCell *cell = sheetModel->getCellPtr(coord);
            if (cell == NULL) {
                CxSheetCell newCell;
                newCell.setAppAttribute("align", "left");
                sheetModel->setCell(coord, newCell);
            } else {
                cell->setAppAttribute("align", "left");
            }
            cellCount++;
        }
    }

    // Clear range selection
    if (_rangeSelectActive) {
        _rangeSelectActive = 0;
        sheetView->setRangeSelection(0, _rangeAnchor, _rangeCurrent);
    }

    sheetView->updateScreen();

    if (cellCount == 1) {
        setMessage("(1 cell left-aligned)");
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "(%d cells left-aligned)", cellCount);
        setMessage(buf);
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatCellAlignCenter
//
// Set center alignment on the current selection (or current cell if no selection).
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatCellAlignCenter(CxString commandLine)
{
    (void)commandLine;  // unused

    CxSheetCellCoordinate start, end;

    if (_rangeSelectActive) {
        start = _rangeAnchor;
        end = _rangeCurrent;
    } else {
        start = sheetModel->getCurrentPosition();
        end = start;
    }

    // Normalize range
    int minRow = start.getRow();
    int maxRow = end.getRow();
    if (minRow > maxRow) {
        int tmp = minRow;
        minRow = maxRow;
        maxRow = tmp;
    }

    int minCol = start.getCol();
    int maxCol = end.getCol();
    if (minCol > maxCol) {
        int tmp = minCol;
        minCol = maxCol;
        maxCol = tmp;
    }

    int cellCount = 0;

    // Apply alignment
    for (int row = minRow; row <= maxRow; row++) {
        for (int col = minCol; col <= maxCol; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);

            // Get or create cell
            CxSheetCell *cell = sheetModel->getCellPtr(coord);
            if (cell == NULL) {
                CxSheetCell newCell;
                newCell.setAppAttribute("align", "center");
                sheetModel->setCell(coord, newCell);
            } else {
                cell->setAppAttribute("align", "center");
            }
            cellCount++;
        }
    }

    // Clear range selection
    if (_rangeSelectActive) {
        _rangeSelectActive = 0;
        sheetView->setRangeSelection(0, _rangeAnchor, _rangeCurrent);
    }

    sheetView->updateScreen();

    if (cellCount == 1) {
        setMessage("(1 cell centered)");
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "(%d cells centered)", cellCount);
        setMessage(buf);
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatCellAlignRight
//
// Set right alignment on the current selection (or current cell if no selection).
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatCellAlignRight(CxString commandLine)
{
    (void)commandLine;  // unused

    CxSheetCellCoordinate start, end;

    if (_rangeSelectActive) {
        start = _rangeAnchor;
        end = _rangeCurrent;
    } else {
        start = sheetModel->getCurrentPosition();
        end = start;
    }

    // Normalize range
    int minRow = start.getRow();
    int maxRow = end.getRow();
    if (minRow > maxRow) {
        int tmp = minRow;
        minRow = maxRow;
        maxRow = tmp;
    }

    int minCol = start.getCol();
    int maxCol = end.getCol();
    if (minCol > maxCol) {
        int tmp = minCol;
        minCol = maxCol;
        maxCol = tmp;
    }

    int cellCount = 0;

    // Apply alignment
    for (int row = minRow; row <= maxRow; row++) {
        for (int col = minCol; col <= maxCol; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);

            // Get or create cell
            CxSheetCell *cell = sheetModel->getCellPtr(coord);
            if (cell == NULL) {
                CxSheetCell newCell;
                newCell.setAppAttribute("align", "right");
                sheetModel->setCell(coord, newCell);
            } else {
                cell->setAppAttribute("align", "right");
            }
            cellCount++;
        }
    }

    // Clear range selection
    if (_rangeSelectActive) {
        _rangeSelectActive = 0;
        sheetView->setRangeSelection(0, _rangeAnchor, _rangeCurrent);
    }

    sheetView->updateScreen();

    if (cellCount == 1) {
        setMessage("(1 cell right-aligned)");
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "(%d cells right-aligned)", cellCount);
        setMessage(buf);
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatCellNumberCurrency
//
// Toggle currency format on the current selection (or current cell if no selection).
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatCellNumberCurrency(CxString commandLine)
{
    (void)commandLine;  // unused

    CxSheetCellCoordinate start, end;

    if (_rangeSelectActive) {
        start = _rangeAnchor;
        end = _rangeCurrent;
    } else {
        start = sheetModel->getCurrentPosition();
        end = start;
    }

    // Normalize range
    int minRow = start.getRow();
    int maxRow = end.getRow();
    if (minRow > maxRow) { int tmp = minRow; minRow = maxRow; maxRow = tmp; }

    int minCol = start.getCol();
    int maxCol = end.getCol();
    if (minCol > maxCol) { int tmp = minCol; minCol = maxCol; maxCol = tmp; }

    int cellCount = 0;
    int newState = -1;  // -1 means not determined yet

    // Toggle currency attribute
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

    // Clear range selection
    if (_rangeSelectActive) {
        _rangeSelectActive = 0;
        sheetView->setRangeSelection(0, _rangeAnchor, _rangeCurrent);
    }

    sheetView->updateScreen();

    const char *action = (newState == 1) ? "currency on" : "currency off";
    if (cellCount == 1) {
        char buf[64];
        snprintf(buf, sizeof(buf), "(1 cell %s)", action);
        setMessage(buf);
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "(%d cells %s)", cellCount, action);
        setMessage(buf);
    }
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

    if (_rangeSelectActive) {
        start = _rangeAnchor;
        end = _rangeCurrent;
    } else {
        start = sheetModel->getCurrentPosition();
        end = start;
    }

    // Normalize range
    int minRow = start.getRow();
    int maxRow = end.getRow();
    if (minRow > maxRow) { int tmp = minRow; minRow = maxRow; maxRow = tmp; }

    int minCol = start.getCol();
    int maxCol = end.getCol();
    if (minCol > maxCol) { int tmp = minCol; minCol = maxCol; maxCol = tmp; }

    int cellCount = 0;

    // Set decimal places
    for (int row = minRow; row <= maxRow; row++) {
        for (int col = minCol; col <= maxCol; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);

            CxSheetCell *cell = sheetModel->getCellPtr(coord);
            if (cell == NULL) {
                CxSheetCell newCell;
                newCell.setAppAttribute("decimalPlaces", places);
                sheetModel->setCell(coord, newCell);
            } else {
                cell->setAppAttribute("decimalPlaces", places);
            }
            cellCount++;
        }
    }

    // Clear range selection
    if (_rangeSelectActive) {
        _rangeSelectActive = 0;
        sheetView->setRangeSelection(0, _rangeAnchor, _rangeCurrent);
    }

    sheetView->updateScreen();

    if (cellCount == 1) {
        char buf[64];
        snprintf(buf, sizeof(buf), "(1 cell set to %d decimals)", places);
        setMessage(buf);
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "(%d cells set to %d decimals)", cellCount, places);
        setMessage(buf);
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatCellNumberPercent
//
// Toggle percent format on the current selection (or current cell if no selection).
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatCellNumberPercent(CxString commandLine)
{
    (void)commandLine;  // unused

    CxSheetCellCoordinate start, end;

    if (_rangeSelectActive) {
        start = _rangeAnchor;
        end = _rangeCurrent;
    } else {
        start = sheetModel->getCurrentPosition();
        end = start;
    }

    // Normalize range
    int minRow = start.getRow();
    int maxRow = end.getRow();
    if (minRow > maxRow) { int tmp = minRow; minRow = maxRow; maxRow = tmp; }

    int minCol = start.getCol();
    int maxCol = end.getCol();
    if (minCol > maxCol) { int tmp = minCol; minCol = maxCol; maxCol = tmp; }

    int cellCount = 0;
    int newState = -1;

    // Toggle percent attribute
    for (int row = minRow; row <= maxRow; row++) {
        for (int col = minCol; col <= maxCol; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);

            CxSheetCell *cell = sheetModel->getCellPtr(coord);
            if (cell == NULL) {
                CxSheetCell newCell;
                newCell.setAppAttribute("percent", true);
                sheetModel->setCell(coord, newCell);
                if (newState < 0) newState = 1;
            } else {
                bool current = cell->getAppAttributeBool("percent", false);
                if (newState < 0) newState = current ? 0 : 1;
                cell->setAppAttribute("percent", newState == 1);
            }
            cellCount++;
        }
    }

    // Clear range selection
    if (_rangeSelectActive) {
        _rangeSelectActive = 0;
        sheetView->setRangeSelection(0, _rangeAnchor, _rangeCurrent);
    }

    sheetView->updateScreen();

    const char *action = (newState == 1) ? "percent on" : "percent off";
    if (cellCount == 1) {
        char buf[64];
        snprintf(buf, sizeof(buf), "(1 cell %s)", action);
        setMessage(buf);
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "(%d cells %s)", cellCount, action);
        setMessage(buf);
    }
}


//-------------------------------------------------------------------------------------------------
// SheetEditor::CMD_FormatCellNumberThousands
//
// Toggle thousands separators on the current selection (or current cell if no selection).
//-------------------------------------------------------------------------------------------------
void
SheetEditor::CMD_FormatCellNumberThousands(CxString commandLine)
{
    (void)commandLine;  // unused

    CxSheetCellCoordinate start, end;

    if (_rangeSelectActive) {
        start = _rangeAnchor;
        end = _rangeCurrent;
    } else {
        start = sheetModel->getCurrentPosition();
        end = start;
    }

    // Normalize range
    int minRow = start.getRow();
    int maxRow = end.getRow();
    if (minRow > maxRow) { int tmp = minRow; minRow = maxRow; maxRow = tmp; }

    int minCol = start.getCol();
    int maxCol = end.getCol();
    if (minCol > maxCol) { int tmp = minCol; minCol = maxCol; maxCol = tmp; }

    int cellCount = 0;
    int newState = -1;

    // Toggle thousands attribute
    for (int row = minRow; row <= maxRow; row++) {
        for (int col = minCol; col <= maxCol; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);

            CxSheetCell *cell = sheetModel->getCellPtr(coord);
            if (cell == NULL) {
                CxSheetCell newCell;
                newCell.setAppAttribute("thousands", true);
                sheetModel->setCell(coord, newCell);
                if (newState < 0) newState = 1;
            } else {
                bool current = cell->getAppAttributeBool("thousands", false);
                if (newState < 0) newState = current ? 0 : 1;
                cell->setAppAttribute("thousands", newState == 1);
            }
            cellCount++;
        }
    }

    // Clear range selection
    if (_rangeSelectActive) {
        _rangeSelectActive = 0;
        sheetView->setRangeSelection(0, _rangeAnchor, _rangeCurrent);
    }

    sheetView->updateScreen();

    const char *action = (newState == 1) ? "thousands on" : "thousands off";
    if (cellCount == 1) {
        char buf[64];
        snprintf(buf, sizeof(buf), "(1 cell %s)", action);
        setMessage(buf);
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "(%d cells %s)", cellCount, action);
        setMessage(buf);
    }
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
    (void)commandLine;  // unused

    CxSheetCellCoordinate start, end;

    if (_rangeSelectActive) {
        start = _rangeAnchor;
        end = _rangeCurrent;
    } else {
        start = sheetModel->getCurrentPosition();
        end = start;
    }

    // Normalize range
    int minRow = start.getRow();
    int maxRow = end.getRow();
    if (minRow > maxRow) { int tmp = minRow; minRow = maxRow; maxRow = tmp; }

    int minCol = start.getCol();
    int maxCol = end.getCol();
    if (minCol > maxCol) { int tmp = minCol; minCol = maxCol; maxCol = tmp; }

    int cellCount = 0;
    int newState = -1;

    // Toggle wideText attribute
    for (int row = minRow; row <= maxRow; row++) {
        for (int col = minCol; col <= maxCol; col++) {
            CxSheetCellCoordinate coord;
            coord.setRow(row);
            coord.setCol(col);

            CxSheetCell *cell = sheetModel->getCellPtr(coord);
            if (cell == NULL) {
                CxSheetCell newCell;
                newCell.setAppAttribute("wideText", true);
                sheetModel->setCell(coord, newCell);
                if (newState < 0) newState = 1;
            } else {
                bool current = cell->getAppAttributeBool("wideText", false);
                if (newState < 0) newState = current ? 0 : 1;
                cell->setAppAttribute("wideText", newState == 1);
            }
            cellCount++;
        }
    }

    // Clear range selection
    if (_rangeSelectActive) {
        _rangeSelectActive = 0;
        sheetView->setRangeSelection(0, _rangeAnchor, _rangeCurrent);
    }

    sheetView->updateScreen();

    const char *action = (newState == 1) ? "wide text on" : "wide text off";
    if (cellCount == 1) {
        char buf[64];
        snprintf(buf, sizeof(buf), "(1 cell %s)", action);
        setMessage(buf);
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "(%d cells %s)", cellCount, action);
        setMessage(buf);
    }
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

    // Use optimized redraw that skips row numbers
    sheetView->updateScreenForColumnChange();
    setMessage("Column deleted");
}
