//-------------------------------------------------------------------------------------------------
//
//  SpreadsheetDefaults.cpp
//  ss (spreadsheet)
//
//  Copyright 2022-2025 Todd Vernon. All rights reserved.
//  Licensed under the Apache License, Version 2.0
//  See LICENSE file for details.
//
//  Configuration handling: colors and spreadsheet settings.
//
//-------------------------------------------------------------------------------------------------

#include "SpreadsheetDefaults.h"
#include <cx/screen/screen.h>


//-------------------------------------------------------------------------------------------------
// SpreadsheetDefaults::SpreadsheetDefaults
//
// Constructor - initializes all colors to NONE (terminal defaults).
//-------------------------------------------------------------------------------------------------
SpreadsheetDefaults::SpreadsheetDefaults(void)
:   _baseNode(NULL)
{
    // Header colors - default to NONE
    _headerTextColor       = new CxAnsiForegroundColor(CxAnsiForegroundColor::NONE);
    _headerBackgroundColor = new CxAnsiBackgroundColor(CxAnsiBackgroundColor::NONE);

    // Divider line color
    _dividerColor = new CxAnsiForegroundColor(CxAnsiForegroundColor::NONE);

    // Command line colors
    _commandLineTextColor       = new CxAnsiForegroundColor(CxAnsiForegroundColor::NONE);
    _commandLineBackgroundColor = new CxAnsiBackgroundColor(CxAnsiBackgroundColor::NONE);

    // Message line color
    _messageLineTextColor = new CxAnsiForegroundColor(CxAnsiForegroundColor::NONE);

    // Selected cell colors
    _selectedCellTextColor       = new CxAnsiForegroundColor(CxAnsiForegroundColor::NONE);
    _selectedCellBackgroundColor = new CxAnsiBackgroundColor(CxAnsiBackgroundColor::NONE);

    // Normal cell colors
    _cellTextColor       = new CxAnsiForegroundColor(CxAnsiForegroundColor::NONE);
    _cellBackgroundColor = new CxAnsiBackgroundColor(CxAnsiBackgroundColor::NONE);

    // Row number color
    _rowNumberTextColor = new CxAnsiForegroundColor(CxAnsiForegroundColor::NONE);

    // Cell hunt colors - default to green for hunt cursor, light green for range
    _cellHuntTextColor       = new CxRGBForegroundColor(255, 255, 255);   // white text
    _cellHuntBackgroundColor = new CxRGBBackgroundColor(60, 150, 60);     // green background
    _cellHuntRangeTextColor       = new CxRGBForegroundColor(40, 40, 40); // dark text
    _cellHuntRangeBackgroundColor = new CxRGBBackgroundColor(180, 220, 180); // light green

    // Range selection colors - light blue fill to distinguish from cursor highlight
    _rangeSelectTextColor       = new CxRGBForegroundColor(40, 40, 60);   // dark text
    _rangeSelectBackgroundColor = new CxRGBBackgroundColor(180, 200, 230); // light blue
}


//-------------------------------------------------------------------------------------------------
// SpreadsheetDefaults::parse
//
// Parse the JSON configuration data.
//-------------------------------------------------------------------------------------------------
int
SpreadsheetDefaults::parse(void)
{
    _baseNode = CxJSONFactory::parse(_data);

    if (_baseNode == NULL) {
        return(false);
    }

    if (_baseNode->type() == CxJSONBase::OBJECT) {
        return(true);
    }

    return(false);
}


//-------------------------------------------------------------------------------------------------
// SpreadsheetDefaults::readFile
//
// Read the configuration file and strip comments.
//-------------------------------------------------------------------------------------------------
int
SpreadsheetDefaults::readFile(CxString fname)
{
    CxFile inFile;

    if (!inFile.open(fname, "r")) {
        inFile.close();

        // Create default file if it doesn't exist
        writeDefaults(fname);

        if (!inFile.open(fname, "r")) {
            return(false);
        }
    }

    while (!inFile.eof()) {
        CxString line = inFile.getUntil('\n');

        line.stripLeading(" \t");
        line.stripTrailing(" \n\r");

        // Skip comment lines (starting with #)
        if (line.firstChar('#') != 0) {
            _data += line;
        }
    }

    inFile.close();

    if (_data.length() == 0) return(false);

    return(true);
}


//-------------------------------------------------------------------------------------------------
// SpreadsheetDefaults::loadDefaults
//
// Load defaults from the specified file.
//-------------------------------------------------------------------------------------------------
int
SpreadsheetDefaults::loadDefaults(CxString fname)
{
    //---------------------------------------------------------------------------------------------
    // Read the defaults file
    //---------------------------------------------------------------------------------------------
    if (readFile(fname) == false) {
        return(false);
    }

    if (parse() == false) {
        return(false);
    }

    //---------------------------------------------------------------------------------------------
    // Query the base node for members
    //---------------------------------------------------------------------------------------------
    CxJSONObject *baseItem = (CxJSONObject *)_baseNode;
    if (baseItem) {
        if (baseItem->type() == CxJSONBase::OBJECT) {
            CxJSONObject *object = (CxJSONObject *)baseItem;

            //-------------------------------------------------------------------------------------
            // Get the colors member
            //-------------------------------------------------------------------------------------
            CxJSONMember *colorsMember = object->find("colors");

            if (colorsMember != NULL) {
                if (colorsMember->object()->type() == CxJSONBase::OBJECT) {
                    CxJSONObject *colorObject = (CxJSONObject *)colorsMember->object();

                    // Header colors
                    parseColorFromJSON(colorObject, "headerTextColor",
                                       &_headerTextColor, FALSE);
                    parseColorFromJSON(colorObject, "headerBackgroundColor",
                                       &_headerBackgroundColor, TRUE);

                    // Divider color
                    parseColorFromJSON(colorObject, "dividerColor",
                                       &_dividerColor, FALSE);

                    // Command line colors
                    parseColorFromJSON(colorObject, "commandLineTextColor",
                                       &_commandLineTextColor, FALSE);
                    parseColorFromJSON(colorObject, "commandLineBackgroundColor",
                                       &_commandLineBackgroundColor, TRUE);

                    // Message line color
                    parseColorFromJSON(colorObject, "messageLineTextColor",
                                       &_messageLineTextColor, FALSE);

                    // Selected cell colors
                    parseColorFromJSON(colorObject, "selectedCellTextColor",
                                       &_selectedCellTextColor, FALSE);
                    parseColorFromJSON(colorObject, "selectedCellBackgroundColor",
                                       &_selectedCellBackgroundColor, TRUE);

                    // Normal cell colors
                    parseColorFromJSON(colorObject, "cellTextColor",
                                       &_cellTextColor, FALSE);
                    parseColorFromJSON(colorObject, "cellBackgroundColor",
                                       &_cellBackgroundColor, TRUE);

                    // Row number color
                    parseColorFromJSON(colorObject, "rowNumberTextColor",
                                       &_rowNumberTextColor, FALSE);
                }
            }
        }
    }

    return(true);
}


//-------------------------------------------------------------------------------------------------
// SpreadsheetDefaults::parseBooleanField
//
// Parse a boolean field from a JSON object.
//-------------------------------------------------------------------------------------------------
int
SpreadsheetDefaults::parseBooleanField(CxJSONObject *obj, const char *fieldName, int *target)
{
    CxJSONMember *member = obj->find(fieldName);
    if (member != NULL && member->object()->type() == CxJSONBase::BOOLEAN) {
        CxJSONBoolean *value = (CxJSONBoolean *)member->object();
        *target = value->get();
        return(TRUE);
    }
    return(FALSE);
}


//-------------------------------------------------------------------------------------------------
// SpreadsheetDefaults::parseColorFromJSON
//
// Parse a color field from a JSON object and assign it to the target pointer.
// isBackground: FALSE = foreground, TRUE = background
//-------------------------------------------------------------------------------------------------
int
SpreadsheetDefaults::parseColorFromJSON(CxJSONObject *obj, const char *fieldName,
                                        CxColor **target, int isBackground)
{
    CxJSONMember *member = obj->find(fieldName);
    if (member != NULL && member->object()->type() == CxJSONBase::STRING) {
        CxJSONString *value = (CxJSONString *)member->object();
        *target = SpreadsheetDefaults::parseColor(value->get(), isBackground);
        return(TRUE);
    }
    return(FALSE);
}


//-------------------------------------------------------------------------------------------------
// SpreadsheetDefaults::parseColor
//
// Unified color parser for both foreground and background colors.
// Format: "TYPE:VALUE" where TYPE is ANSI, XTERM256, or RGB
// isBackground: FALSE = foreground, TRUE = background
//-------------------------------------------------------------------------------------------------
/* static */
CxColor *
SpreadsheetDefaults::parseColor(CxString colorString, int isBackground)
{
    // If there is no colon the color selector is not present
    if (colorString.firstChar(":") == -1) {
        if (isBackground)
            return new CxAnsiBackgroundColor(CxAnsiBackgroundColor::NONE);
        return new CxAnsiForegroundColor(CxAnsiForegroundColor::NONE);
    }

    // Extract the color type selector before the colon
    CxString colorTypeSelector = colorString.nextToken(" :\t\n\r");

    // Proactively get the next three arguments (for RGB)
    CxString item1 = colorString.nextToken(" ,\n\r\t:");
    CxString item2 = colorString.nextToken(" ,\n\r\t:");
    CxString item3 = colorString.nextToken(" ,\n\r\t:");

    // If the color type selector is empty
    if (colorTypeSelector.length() == 0) {
        if (isBackground)
            return new CxAnsiBackgroundColor(CxAnsiBackgroundColor::NONE);
        return new CxAnsiForegroundColor(CxAnsiForegroundColor::NONE);
    }

    // ANSI color type
    if (CxString::toUpper(colorTypeSelector) == "ANSI") {
        if (isBackground)
            return new CxAnsiBackgroundColor(item1);
        return new CxAnsiForegroundColor(item1);
    }

    // XTERM256 color type
    if (CxString::toUpper(colorTypeSelector) == "XTERM256") {
        if (isBackground)
            return new CxXterm256BackgroundColor(item1);
        return new CxXterm256ForegroundColor(item1);
    }

    // RGB color type
    if (CxString::toUpper(colorTypeSelector) == "RGB") {
        int red   = item1.toInt();
        int green = item2.toInt();
        int blue  = item3.toInt();
        if (isBackground)
            return new CxRGBBackgroundColor(red, green, blue);
        return new CxRGBForegroundColor(red, green, blue);
    }

    // Fallback: return NONE color of the correct type
    if (isBackground)
        return new CxAnsiBackgroundColor(CxAnsiBackgroundColor::NONE);
    return new CxAnsiForegroundColor(CxAnsiForegroundColor::NONE);
}


//-------------------------------------------------------------------------------------------------
// Color accessors
//-------------------------------------------------------------------------------------------------

CxColor *SpreadsheetDefaults::headerTextColor(void)
{
    return _headerTextColor;
}

CxColor *SpreadsheetDefaults::headerBackgroundColor(void)
{
    return _headerBackgroundColor;
}

CxColor *SpreadsheetDefaults::dividerColor(void)
{
    return _dividerColor;
}

CxColor *SpreadsheetDefaults::commandLineTextColor(void)
{
    return _commandLineTextColor;
}

CxColor *SpreadsheetDefaults::commandLineBackgroundColor(void)
{
    return _commandLineBackgroundColor;
}

CxColor *SpreadsheetDefaults::messageLineTextColor(void)
{
    return _messageLineTextColor;
}

CxColor *SpreadsheetDefaults::selectedCellTextColor(void)
{
    return _selectedCellTextColor;
}

CxColor *SpreadsheetDefaults::selectedCellBackgroundColor(void)
{
    return _selectedCellBackgroundColor;
}

CxColor *SpreadsheetDefaults::cellTextColor(void)
{
    return _cellTextColor;
}

CxColor *SpreadsheetDefaults::cellBackgroundColor(void)
{
    return _cellBackgroundColor;
}

CxColor *SpreadsheetDefaults::rowNumberTextColor(void)
{
    return _rowNumberTextColor;
}

CxColor *SpreadsheetDefaults::cellHuntTextColor(void)
{
    return _cellHuntTextColor;
}

CxColor *SpreadsheetDefaults::cellHuntBackgroundColor(void)
{
    return _cellHuntBackgroundColor;
}

CxColor *SpreadsheetDefaults::cellHuntRangeTextColor(void)
{
    return _cellHuntRangeTextColor;
}

CxColor *SpreadsheetDefaults::cellHuntRangeBackgroundColor(void)
{
    return _cellHuntRangeBackgroundColor;
}

CxColor *SpreadsheetDefaults::rangeSelectTextColor(void)
{
    return _rangeSelectTextColor;
}

CxColor *SpreadsheetDefaults::rangeSelectBackgroundColor(void)
{
    return _rangeSelectBackgroundColor;
}


//-------------------------------------------------------------------------------------------------
// Color helper methods
//-------------------------------------------------------------------------------------------------

void SpreadsheetDefaults::applyHeaderColors(CxScreen *screen)
{
    screen->setForegroundColor(_headerTextColor);
    screen->setBackgroundColor(_headerBackgroundColor);
}

void SpreadsheetDefaults::applySelectedCellColors(CxScreen *screen)
{
    screen->setForegroundColor(_selectedCellTextColor);
    screen->setBackgroundColor(_selectedCellBackgroundColor);
}

void SpreadsheetDefaults::applyCellColors(CxScreen *screen)
{
    screen->setForegroundColor(_cellTextColor);
    screen->setBackgroundColor(_cellBackgroundColor);
}

void SpreadsheetDefaults::applyCommandLineColors(CxScreen *screen)
{
    screen->setForegroundColor(_commandLineTextColor);
    screen->setBackgroundColor(_commandLineBackgroundColor);
}

void SpreadsheetDefaults::applyCellHuntColors(CxScreen *screen)
{
    screen->setForegroundColor(_cellHuntTextColor);
    screen->setBackgroundColor(_cellHuntBackgroundColor);
}

void SpreadsheetDefaults::applyCellHuntRangeColors(CxScreen *screen)
{
    screen->setForegroundColor(_cellHuntRangeTextColor);
    screen->setBackgroundColor(_cellHuntRangeBackgroundColor);
}

void SpreadsheetDefaults::applyRangeSelectColors(CxScreen *screen)
{
    screen->setForegroundColor(_rangeSelectTextColor);
    screen->setBackgroundColor(_rangeSelectBackgroundColor);
}

void SpreadsheetDefaults::resetColors(CxScreen *screen)
{
    screen->resetForegroundColor();
    screen->resetBackgroundColor();
}


//-------------------------------------------------------------------------------------------------
// SpreadsheetDefaults::writeDefaults
//
// Write out default configuration to the specified file.
//-------------------------------------------------------------------------------------------------
void
SpreadsheetDefaults::writeDefaults(CxString path)
{
    CxFile file;

    if (!file.open(path, "w")) {
        return;
    }

    // Platform-specific default color values
    // Cell backgrounds use ANSI:NONE to inherit terminal background
#if defined(_OSX_) || defined(_LINUX_)
    const char *hdr = "# Uses RGB true color - requires 24-bit color terminal support";
    const char *headerFg     = "RGB:220,220,240";
    const char *headerBg     = "RGB:60,70,100";
    const char *divider      = "RGB:80,100,140";
    const char *cmdLineFg    = "RGB:200,210,230";
    const char *cmdLineBg    = "RGB:50,55,75";
    const char *msgLineFg    = "RGB:255,180,100";
    const char *selectedFg   = "RGB:255,255,255";
    const char *selectedBg   = "RGB:70,130,180";
    const char *cellFg       = "ANSI:NONE";
    const char *cellBg       = "ANSI:NONE";
    const char *rowNumFg     = "RGB:100,105,120";
#elif defined(_SOLARIS6_) || defined(_SOLARIS10_) || defined(_IRIX6_)
    const char *hdr = "# Uses XTERM 256 color palette for broad terminal compatibility";
    const char *headerFg     = "XTERM256:Grey84";
    const char *headerBg     = "XTERM256:DarkSlateGray";
    const char *divider      = "XTERM256:SteelBlue";
    const char *cmdLineFg    = "XTERM256:Grey89";
    const char *cmdLineBg    = "XTERM256:Grey23";
    const char *msgLineFg    = "XTERM256:Orange1";
    const char *selectedFg   = "XTERM256:White";
    const char *selectedBg   = "XTERM256:SteelBlue";
    const char *cellFg       = "ANSI:NONE";
    const char *cellBg       = "ANSI:NONE";
    const char *rowNumFg     = "XTERM256:Grey42";
#else
    const char *hdr = "# Uses ANSI 16-color palette for maximum terminal compatibility";
    const char *headerFg     = "ANSI:BRIGHT_WHITE";
    const char *headerBg     = "ANSI:BLUE";
    const char *divider      = "ANSI:BRIGHT_BLUE";
    const char *cmdLineFg    = "ANSI:WHITE";
    const char *cmdLineBg    = "ANSI:BLUE";
    const char *msgLineFg    = "ANSI:BRIGHT_YELLOW";
    const char *selectedFg   = "ANSI:BRIGHT_WHITE";
    const char *selectedBg   = "ANSI:CYAN";
    const char *cellFg       = "ANSI:NONE";
    const char *cellBg       = "ANSI:NONE";
    const char *rowNumFg     = "ANSI:BRIGHT_BLACK";
#endif

    file.printf("# .ssrc defaults file\n");
    file.printf("%s\n", hdr);
    file.printf("# color syntax is ANSI:<name>, XTERM256:<name>, RGB:<R>,<G>,<B>\n");
    file.printf("# --------------------------------------------------------------------------------\n");
    file.printf("\n");
    file.printf("{\n");
    file.printf("    \"colors\": {\n");
    file.printf("        \"headerTextColor\": \"%s\",\n", headerFg);
    file.printf("        \"headerBackgroundColor\": \"%s\",\n", headerBg);
    file.printf("        \"dividerColor\": \"%s\",\n", divider);
    file.printf("        \"commandLineTextColor\": \"%s\",\n", cmdLineFg);
    file.printf("        \"commandLineBackgroundColor\": \"%s\",\n", cmdLineBg);
    file.printf("        \"messageLineTextColor\": \"%s\",\n", msgLineFg);
    file.printf("        \"selectedCellTextColor\": \"%s\",\n", selectedFg);
    file.printf("        \"selectedCellBackgroundColor\": \"%s\",\n", selectedBg);
    file.printf("        \"cellTextColor\": \"%s\",\n", cellFg);
    file.printf("        \"cellBackgroundColor\": \"%s\",\n", cellBg);
    file.printf("        \"rowNumberTextColor\": \"%s\"\n", rowNumFg);
    file.printf("    }\n");
    file.printf("}\n");

    file.close();
}
