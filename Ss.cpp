//-------------------------------------------------------------------------------------------------
//
//  Ss.cpp
//  ss (spreadsheet)
//
//  Copyright 2022-2025 Todd Vernon. All rights reserved.
//  Licensed under the Apache License, Version 2.0
//  See LICENSE file for details.
//
//  Main entry point for ss terminal spreadsheet application.
//
//-------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <signal.h>
#include <sys/types.h>

#include <cx/base/string.h>
#include <cx/keyboard/keyboard.h>
#include <cx/screen/screen.h>

#include "SheetEditor.h"


//-------------------------------------------------------------------------------------------------
// main
//
//-------------------------------------------------------------------------------------------------
int main(int argc, char **argv)
{
    CxString filePath;

    if (argc == 2) {
        filePath = argv[1];
    }

    int row = 0;
    int col = 0;

    // create the keyboard object
    CxKeyboard *keyboard = new CxKeyboard();

    // create the screen object
    CxScreen *screen = new CxScreen();

    // get the current cursor position
    CxScreen::getCursorPosition(&row, &col);

    // open alternate screen to preserve the existing screen
    CxScreen::openAlternateScreen();
    CxScreen::clearScreen();

    // create and run the sheet editor
    SheetEditor sheetEditor(screen, keyboard, filePath);
    sheetEditor.run();

    screen->showCursor();
    fflush(stdout);
    CxScreen::clearScreen();

    // switch back to the main screen
    CxScreen::closeAlternateScreen();

    // place the cursor where it was when started
    CxScreen::placeCursor(row, col);

    // restore terminal settings by deleting keyboard
    delete keyboard;
    delete screen;

    // output newline to prevent zsh from showing inverse %
    printf("\n");

    return 0;
}
