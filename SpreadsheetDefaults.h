//-------------------------------------------------------------------------------------------------
//
//  SpreadsheetDefaults.h
//  ss (spreadsheet)
//
//  Copyright 2022-2025 Todd Vernon. All rights reserved.
//  Licensed under the Apache License, Version 2.0
//  See LICENSE file for details.
//
//  Configuration definitions for colors and spreadsheet settings.
//
//-------------------------------------------------------------------------------------------------

#include <stdio.h>

#ifndef SpreadsheetDefaults_h
#define SpreadsheetDefaults_h

#include <cx/base/string.h>
#include <cx/base/file.h>
#include <cx/json/json_factory.h>
#include <cx/screen/color.h>

class CxScreen;  // forward declaration for color helpers


//-------------------------------------------------------------------------------------------------
// Class SpreadsheetDefaults
//
// Manages configuration settings loaded from .ssrc file.
// Colors for UI elements: headers, cells, divider line, command/message lines.
//
//-------------------------------------------------------------------------------------------------

class SpreadsheetDefaults
{
public:

    SpreadsheetDefaults(void);
    // constructor

    int loadDefaults(CxString fname);
    // load defaults from file

    // Row/column header colors
    CxColor *headerTextColor(void);
    CxColor *headerBackgroundColor(void);

    // Divider line color
    CxColor *dividerColor(void);

    // Command line colors
    CxColor *commandLineTextColor(void);
    CxColor *commandLineBackgroundColor(void);

    // Message line color
    CxColor *messageLineTextColor(void);

    // Selected cell colors (inverse video replacement)
    CxColor *selectedCellTextColor(void);
    CxColor *selectedCellBackgroundColor(void);

    // Normal cell colors
    CxColor *cellTextColor(void);
    CxColor *cellBackgroundColor(void);

    // Row number color
    CxColor *rowNumberTextColor(void);

    // Cell hunt colors (for formula cell reference selection)
    CxColor *cellHuntTextColor(void);
    CxColor *cellHuntBackgroundColor(void);
    CxColor *cellHuntRangeTextColor(void);
    CxColor *cellHuntRangeBackgroundColor(void);

    // Range selection colors (for EDIT mode multi-cell selection)
    CxColor *rangeSelectTextColor(void);
    CxColor *rangeSelectBackgroundColor(void);

    // Command line dim/edit colors
    CxColor *commandLineDimTextColor(void);
    CxColor *commandLineEditTextColor(void);

    // Color helper methods - apply foreground and background in one call
    void applyHeaderColors(CxScreen *screen);
    void applySelectedCellColors(CxScreen *screen);
    void applyCellColors(CxScreen *screen);
    void applyCommandLineColors(CxScreen *screen);
    void applyCommandLineDimColors(CxScreen *screen);
    void applyCommandLineEditColors(CxScreen *screen);
    void applyCellHuntColors(CxScreen *screen);
    void applyCellHuntRangeColors(CxScreen *screen);
    void applyRangeSelectColors(CxScreen *screen);
    void resetColors(CxScreen *screen);

    void writeDefaults(CxString fname);
    // write out default values to ~/.ssrc

private:

    int readFile(CxString fname);
    int parse(void);

    int parseBooleanField(CxJSONObject *obj, const char *fieldName, int *target);
    int parseColorFromJSON(CxJSONObject *obj, const char *fieldName,
                           CxColor **target, int isBackground);

    static CxColor *parseColor(CxString colorName, int isBackground);

    CxString _data;

    // Row/column header colors
    CxColor *_headerTextColor;
    CxColor *_headerBackgroundColor;

    // Divider line color
    CxColor *_dividerColor;

    // Command line colors
    CxColor *_commandLineTextColor;
    CxColor *_commandLineBackgroundColor;

    // Message line color
    CxColor *_messageLineTextColor;

    // Selected cell colors
    CxColor *_selectedCellTextColor;
    CxColor *_selectedCellBackgroundColor;

    // Normal cell colors
    CxColor *_cellTextColor;
    CxColor *_cellBackgroundColor;

    // Row number color
    CxColor *_rowNumberTextColor;

    // Command line dim/edit colors
    CxColor *_commandLineDimTextColor;
    CxColor *_commandLineEditTextColor;

    // Cell hunt colors
    CxColor *_cellHuntTextColor;
    CxColor *_cellHuntBackgroundColor;
    CxColor *_cellHuntRangeTextColor;
    CxColor *_cellHuntRangeBackgroundColor;

    // Range selection colors
    CxColor *_rangeSelectTextColor;
    CxColor *_rangeSelectBackgroundColor;

    CxJSONBase *_baseNode;
};

#endif /* SpreadsheetDefaults_h */
