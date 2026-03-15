//-------------------------------------------------------------------------------------------------
//
//  HelpView.cpp
//  ss (spreadsheet)
//
//  Copyright 2022-2025 Todd Vernon. All rights reserved.
//  Licensed under the Apache License, Version 2.0
//  See LICENSE file for details.
//
//  Modal dialog showing help content organized by collapsible sections.
//
//-------------------------------------------------------------------------------------------------

#include "HelpView.h"
#include <stdlib.h>

#include <cx/base/utfstring.h>

#include "SsVersion.h"

//-------------------------------------------------------------------------------------------------
// Platform-conditional expand/collapse indicators
//-------------------------------------------------------------------------------------------------
static const char *EXPAND_INDICATOR   = "\xe2\x96\xbc";  // ▼ (UTF-8)
static const char *COLLAPSE_INDICATOR = "\xe2\x96\xb6";  // ▶ (UTF-8)


//-------------------------------------------------------------------------------------------------
// HelpView::HelpView (constructor)
//
//-------------------------------------------------------------------------------------------------
HelpView::HelpView( SpreadsheetDefaults *pd, CxScreen *screenPtr )
{
    spreadsheetDefaults = pd;
    screen              = screenPtr;

    // create the box frame for modal display
    frame = new CxBoxFrame(screen);

    // initially not visible
    _visible = 0;
    _helpFileLoaded = 0;
    _cachedContentWidth = 0;
    _isFirstRun = 0;
    // NOTE: No resize callback here - SheetEditor owns all resize handling

    // try to load help file
    loadHelpFile();

    // recalc where everything should display
    recalcScreenPlacements();
}


//-------------------------------------------------------------------------------------------------
// fileIsReadable - helper to check if a file exists and is readable
//-------------------------------------------------------------------------------------------------
static int
fileIsReadable( CxString path )
{
    CxFileAccess::status stat = CxFileAccess::checkStatus(path);
    return (stat == CxFileAccess::FOUND_R || stat == CxFileAccess::FOUND_RW);
}


//-------------------------------------------------------------------------------------------------
// HelpView::findHelpFile
//
// Search for help file in standard locations. Returns TRUE and sets outPath if found.
//
//-------------------------------------------------------------------------------------------------
int
HelpView::findHelpFile( CxString *outPath )
{
    CxString path;
    char *home = getenv("HOME");

    // Modern platforms: try .md first, then .txt

    // 1. CWD - development
    path = "./ss_help.md";
    if (fileIsReadable(path)) { *outPath = path; return TRUE; }
    path = "./ss_help.txt";
    if (fileIsReadable(path)) { *outPath = path; return TRUE; }

    // 2. $HOME/.ss/
    if (home != NULL) {
        path = CxString(home) + "/.ss/ss_help.md";
        if (fileIsReadable(path)) { *outPath = path; return TRUE; }
        path = CxString(home) + "/.ss/ss_help.txt";
        if (fileIsReadable(path)) { *outPath = path; return TRUE; }
    }

    // 3. /usr/local/share/ss/
    path = "/usr/local/share/ss/ss_help.md";
    if (fileIsReadable(path)) { *outPath = path; return TRUE; }
    path = "/usr/local/share/ss/ss_help.txt";
    if (fileIsReadable(path)) { *outPath = path; return TRUE; }

    return FALSE;
}


//-------------------------------------------------------------------------------------------------
// HelpView::loadHelpFile
//
// Find and load the help file, parse into sections.
//
//-------------------------------------------------------------------------------------------------
int
HelpView::loadHelpFile( void )
{
    CxString path;

    if (!findHelpFile(&path)) {
        _helpFileLoaded = 0;
        return FALSE;
    }

    parseMarkdown(path);
    rebuildVisibleItems();

    _helpFileLoaded = 1;

    return TRUE;
}


//-------------------------------------------------------------------------------------------------
// HelpView::clearSections
//
// Free all section data.
//
//-------------------------------------------------------------------------------------------------
void
HelpView::clearSections( void )
{
    for (int i = 0; i < (int)_sections.entries(); i++) {
        HelpSection *sec = _sections.at(i);
        if (sec != NULL) {
            sec->lines.clearAndDelete();
            delete sec;
        }
    }
    _sections.clear();
}


//-------------------------------------------------------------------------------------------------
// HelpView::parseMarkdown
//
// Parse a markdown file into sections.
// Lines starting with ## become section headers.
// Lines starting with # (single) are skipped (document title).
// All other lines are content within the current section.
// Text before the first ## creates an auto "Overview" section.
//
//-------------------------------------------------------------------------------------------------
void
HelpView::parseMarkdown( CxString filePath )
{
    clearSections();

    CxFile file;
    if (!file.open(filePath, "r")) {
        return;
    }

    HelpSection *currentSection = NULL;
    int hasSeenFirstSection = 0;

    while (!file.eof()) {
        CxString line = file.getUntil('\n');

        // strip trailing \r\n
        line = line.stripTrailing("\r\n");

        // check for ## header (section)
        if (line.length() >= 3 &&
            line.data()[0] == '#' && line.data()[1] == '#' && line.data()[2] == ' ') {

            // create new section
            currentSection = new HelpSection();
            currentSection->title = line.subString(3, line.length() - 3);
            currentSection->isExpanded = 1;  // expanded by default
            _sections.append(currentSection);
            hasSeenFirstSection = 1;
            continue;
        }

        // check for # header (document title) - skip
        if (line.length() >= 2 &&
            line.data()[0] == '#' && line.data()[1] == ' ') {
            continue;
        }

        // content line
        if (!hasSeenFirstSection && line.length() > 0) {
            // auto-create "Overview" section for content before first ##
            currentSection = new HelpSection();
            currentSection->title = "Overview";
            currentSection->isExpanded = 1;  // expanded by default
            _sections.append(currentSection);
            hasSeenFirstSection = 1;
        }

        if (currentSection != NULL) {
            CxString *lineCopy = new CxString(line);
            currentSection->lines.append(lineCopy);
        }
    }
}


//-------------------------------------------------------------------------------------------------
// HelpView::rebuildVisibleItems
//
// Rebuild the flat list of visible items from the section structure.
// Called on construction and whenever expand/collapse state changes.
// Pre-computes formatted text for each item to avoid per-redraw computation.
//
//-------------------------------------------------------------------------------------------------
void
HelpView::rebuildVisibleItems( void )
{
    _visibleItems.clearAndDelete();

    int sectionCount = (int)_sections.entries();
    int contentWidth = _cachedContentWidth;

    //---------------------------------------------------------------------------------------------
    // Add welcome message if this is the first run
    //---------------------------------------------------------------------------------------------
    if (_isFirstRun && contentWidth > 0) {
        // Welcome message lines
        const char *welcomeLines[] = {
            "",
            "Welcome to ss!",
            "",
            "It looks like this is the first time you have used ss.",
            "A config file ~/.ssrc has been written to your home directory.",
            "You can configure defaults using this file.",
            "",
            "You can always access help with Ctrl-H.",
            "Explore the help sections below or press ESC to start editing.",
            "",
            NULL
        };

        for (int i = 0; welcomeLines[i] != NULL; i++) {
            HelpViewItem *welcomeItem = new HelpViewItem();
            welcomeItem->type = HELPITEM_WELCOME;
            welcomeItem->sectionIndex = -1;
            welcomeItem->lineIndex = i;

            CxString text = " ";
            text += welcomeLines[i];
            int textLen = (int)text.length();
            if (textLen < contentWidth - 1) {
                int padNeeded = contentWidth - 1 - textLen;
                if (padNeeded > 0 && padNeeded <= (int)_paddingSpaces.length()) {
                    text += _paddingSpaces.subString(0, padNeeded);
                }
            }
            text += " ";
            welcomeItem->formattedText = text;
            _visibleItems.append(welcomeItem);
        }
    }

    for (int s = 0; s < sectionCount; s++) {
        HelpSection *sec = _sections.at(s);

        // add section header (always visible)
        HelpViewItem *secItem = new HelpViewItem();
        secItem->type = HELPITEM_SECTION;
        secItem->sectionIndex = s;
        secItem->lineIndex = -1;

        // pre-compute formatted text for section header
        if (contentWidth > 0) {
            CxString prefix = " ";
            if (sec->isExpanded) {
                prefix += EXPAND_INDICATOR;
            } else {
                prefix += COLLAPSE_INDICATOR;
            }
            prefix += " ";
            int prefixDisplayWidth = 3;
            int textAreaLen = contentWidth - prefixDisplayWidth - 1;

            CxString text = sec->title;
            if ((int)text.length() > textAreaLen) {
                text = text.subString(0, textAreaLen - 3);
                text += "...";
            }
            int padNeeded = textAreaLen - (int)text.length();
            if (padNeeded > 0 && padNeeded <= (int)_paddingSpaces.length()) {
                text += _paddingSpaces.subString(0, padNeeded);
            }

            secItem->formattedText = prefix;
            secItem->formattedText += text;
            secItem->formattedText += " ";
        }

        _visibleItems.append(secItem);

        // if expanded, add content lines
        if (sec->isExpanded) {
            for (int ln = 0; ln < (int)sec->lines.entries(); ln++) {
                CxString *lineText = sec->lines.at(ln);

                HelpViewItem *lineItem = new HelpViewItem();
                if (lineText->length() == 0) {
                    lineItem->type = HELPITEM_BLANK;
                } else {
                    lineItem->type = HELPITEM_LINE;
                }
                lineItem->sectionIndex = s;
                lineItem->lineIndex = ln;

                // pre-compute formatted text
                if (contentWidth > 0) {
                    if (lineItem->type == HELPITEM_BLANK) {
                        // blank line - use pre-built empty line
                        lineItem->formattedText = _emptyLine.subString(0, contentWidth - 1);
                        lineItem->formattedText += " ";
                    } else {
                        // content line
                        CxString prefix = "   ";
                        int prefixDisplayWidth = 3;
                        int textAreaLen = contentWidth - prefixDisplayWidth - 1;

                        CxString text = *lineText;

                        // UTF-8 aware truncation and padding
                        CxUTFString utfText;
                        utfText.fromCxString(text, 1);
                        int displayWidth = utfText.displayWidth();

                        if (displayWidth > textAreaLen) {
                            int targetCols = textAreaLen - 3;
                            int cols = 0;
                            int bytePos = 0;
                            for (int ci = 0; ci < utfText.charCount(); ci++) {
                                const CxUTFCharacter *ch = utfText.at(ci);
                                int w = ch->displayWidth();
                                if (cols + w > targetCols) break;
                                cols += w;
                                bytePos += ch->isTab() ? 1 : ch->byteCount();
                            }
                            text = text.subString(0, bytePos);
                            text += "...";
                            displayWidth = cols + 3;
                        }

                        int padNeeded = textAreaLen - displayWidth;
                        if (padNeeded > 0 && padNeeded <= (int)_paddingSpaces.length()) {
                            text += _paddingSpaces.subString(0, padNeeded);
                        }

                        lineItem->formattedText = prefix;
                        lineItem->formattedText += text;
                        lineItem->formattedText += " ";
                    }
                }

                _visibleItems.append(lineItem);
            }
        } else {
            // add blank line after collapsed section (except for last section)
            if (s < sectionCount - 1) {
                HelpViewItem *blankItem = new HelpViewItem();
                blankItem->type = HELPITEM_BLANK;
                blankItem->sectionIndex = -1;
                blankItem->lineIndex = -1;

                if (contentWidth > 0) {
                    blankItem->formattedText = _emptyLine.subString(0, contentWidth - 1);
                    blankItem->formattedText += " ";
                }

                _visibleItems.append(blankItem);
            }
        }

    }
}


//-------------------------------------------------------------------------------------------------
// HelpView::recalcScreenPlacements
//
// Calculate centered modal bounds with 10% margins on each side.
// Content height is based on visible item count, clamped to min/max limits.
//
//-------------------------------------------------------------------------------------------------
void
HelpView::recalcScreenPlacements( void )
{
    // get screen dimensions
    screenNumberOfLines = screen->rows();
    screenNumberOfCols  = screen->cols();

    // calculate horizontal margins (10% on each side for wider help text)
    int marginCols = (int)(screenNumberOfCols * 0.10);
    int frameLeft  = marginCols;
    int frameRight = screenNumberOfCols - marginCols - 1;

    // ensure minimum width
    int frameWidth = frameRight - frameLeft + 1;
    if (frameWidth < 40) {
        frameLeft  = (screenNumberOfCols - 40) / 2;
        frameRight = frameLeft + 39;
    }

    // calculate content height - always 80% of terminal height
    // Frame layout: top border + title + separator + content + separator + footer + bottom border = 6 + content
    screenHelpNumberOfLines = (int)(screenNumberOfLines * 0.80) - 6;  // reserve 6 for frame rows
    if (screenHelpNumberOfLines < 5) {
        screenHelpNumberOfLines = 5;
    }

    // total height = content lines + 6 (top, title, sep, content..., sep, footer, bottom)
    int totalHeight = screenHelpNumberOfLines + 6;

    // vertical centering
    int frameTop    = (screenNumberOfLines - totalHeight) / 2;
    int frameBottom = frameTop + totalHeight - 1;

    // store column info for redraw
    screenHelpNumberOfCols = frameRight - frameLeft - 1;  // content width

    // update the frame bounds
    frame->resize(frameTop, frameLeft, frameBottom, frameRight);

    // content starts after top border, title, and separator (row + 3)
    screenHelpTitleBarLine  = frameTop + 1;  // title is on row 1
    screenHelpFrameLine     = frameTop + 2;  // separator is on row 2
    screenHelpFirstListLine = frameTop + 3;  // content starts on row 3
    screenHelpLastListLine  = frameBottom - 3;  // before footer separator

    // set the first list index visible in the scrolling list
    firstVisibleListIndex = 0;

    // set the selected item (start at first selectable item)
    selectedListItemIndex = 0;
    int totalItems = (int)_visibleItems.entries();
    while (selectedListItemIndex < totalItems) {
        HelpViewItemType t = _visibleItems.at(selectedListItemIndex)->type;
        if (t != HELPITEM_SEPARATOR) break;
        selectedListItemIndex++;
    }
    if (selectedListItemIndex >= totalItems && totalItems > 0) {
        selectedListItemIndex = 0;
    }

    // build pre-computed strings for efficient redraw
    int contentWidth = frame->contentWidth();
    if (contentWidth != _cachedContentWidth) {
        _cachedContentWidth = contentWidth;

        // build padding string (enough spaces to pad any line)
        _paddingSpaces = "";
        for (int i = 0; i < contentWidth + 10; i++) {
            _paddingSpaces += " ";
        }

        // build separator line
        _separatorLine = "";
        for (int i = 0; i < contentWidth; i++) {
            _separatorLine += "\xe2\x94\x80";  // ─ (U+2500)
        }

        // build empty line
        _emptyLine = "";
        for (int i = 0; i < contentWidth; i++) {
            _emptyLine += " ";
        }

        // rebuild visible items to recompute formatted text with new width
        rebuildVisibleItems();
    }
}


//-------------------------------------------------------------------------------------------------
// HelpView::redraw
//
// Draw centered modal dialog with box frame, title, section content, and footer.
//
//-------------------------------------------------------------------------------------------------
void
HelpView::redraw( void )
{
    int cursorRow = 0;

    reframe();

    // get frame content bounds
    int contentLeft  = frame->contentLeft();
    int contentWidth = frame->contentWidth();

    //---------------------------------------------------------------------------------------------
    // set frame colors and draw the frame with title and footer
    //---------------------------------------------------------------------------------------------
    frame->setFrameColor(spreadsheetDefaults->headerTextColor(),
                         spreadsheetDefaults->headerBackgroundColor());

    // build context-sensitive footer based on current selection
    CxString footer = getContextFooter();

    CxString title = "ss spreadsheet ";
    title += SS_VERSION;
    frame->drawWithTitleAndFooter(title, footer);

    //---------------------------------------------------------------------------------------------
    // draw the visible items (using pre-computed formattedText)
    //---------------------------------------------------------------------------------------------
    for (int c = 0; c < screenHelpNumberOfLines; c++) {

        // get the logical list index
        int logicalItem = firstVisibleListIndex + c;
        int row = screenHelpFirstListLine + c;

        // position cursor at start of content area (matching cm's pattern exactly)
        screen->placeCursor(row, contentLeft);

        // if this item exists in the visible list
        if (logicalItem < (int)_visibleItems.entries()) {

            HelpViewItem *item = _visibleItems.at(logicalItem);

            //-------------------------------------------------------------------------------------
            // draw with selection highlight or normal colors
            //-------------------------------------------------------------------------------------
            int isSelected = (selectedListItemIndex == logicalItem);
            int isSeparator = (item->type == HELPITEM_SEPARATOR);
            int isSection = (item->type == HELPITEM_SECTION);
            int isWelcome = (item->type == HELPITEM_WELCOME);

            // Use static CxScreen:: calls - instance calls don't work in this context
            if (isSelected && !isSeparator) {
                CxScreen::setForegroundColor(spreadsheetDefaults->selectedCellTextColor());
                CxScreen::setBackgroundColor(spreadsheetDefaults->selectedCellBackgroundColor());
            } else if (isWelcome) {
                CxScreen::setForegroundColor(spreadsheetDefaults->messageLineTextColor());
                // No background color - use terminal default
            } else if (isSection) {
                CxScreen::setForegroundColor(spreadsheetDefaults->headerHighlightTextColor());
                // No background color - use terminal default
            } else {
                CxScreen::setForegroundColor(spreadsheetDefaults->cellTextColor());
                // No background color - use terminal default
            }

            // draw the pre-computed line (or separator)
            if (isSeparator) {
                screen->writeText(_separatorLine);
            } else {
                screen->writeText(item->formattedText);
            }

            CxScreen::resetColors();

            if (isSelected && !isSeparator) {
                cursorRow = row;
            }

        //-----------------------------------------------------------------------------------------
        // draw empty line if beyond visible items
        //-----------------------------------------------------------------------------------------
        } else {
            screen->writeText(_emptyLine);
        }
    }

    screen->placeCursor(cursorRow, contentLeft);
    CxScreen::resetColors();

    // cache the footer for change detection
    _lastFooter = footer;

    screen->flush();
}


//-------------------------------------------------------------------------------------------------
// HelpView::redrawLine
//
// Redraw a single content line at the given logical index.
// Used for incremental selection updates.
//
//-------------------------------------------------------------------------------------------------
void
HelpView::redrawLine( int logicalIndex, int isSelected )
{
    // check bounds
    if (logicalIndex < firstVisibleListIndex ||
        logicalIndex >= firstVisibleListIndex + screenHelpNumberOfLines) {
        return;  // not visible
    }

    int contentLeft = frame->contentLeft();
    int row = screenHelpFirstListLine + (logicalIndex - firstVisibleListIndex);

    screen->placeCursor(row, contentLeft);

    if (logicalIndex < (int)_visibleItems.entries()) {
        HelpViewItem *item = _visibleItems.at(logicalIndex);
        int isSeparator = (item->type == HELPITEM_SEPARATOR);
        int isSection = (item->type == HELPITEM_SECTION);
        int isWelcome = (item->type == HELPITEM_WELCOME);

        // Use static CxScreen:: calls - instance calls don't work in this context
        if (isSelected && !isSeparator) {
            CxScreen::setForegroundColor(spreadsheetDefaults->selectedCellTextColor());
            CxScreen::setBackgroundColor(spreadsheetDefaults->selectedCellBackgroundColor());
        } else if (isWelcome) {
            CxScreen::setForegroundColor(spreadsheetDefaults->messageLineTextColor());
            // No background color - use terminal default
        } else if (isSection) {
            CxScreen::setForegroundColor(spreadsheetDefaults->headerHighlightTextColor());
            // No background color - use terminal default
        } else {
            CxScreen::setForegroundColor(spreadsheetDefaults->cellTextColor());
            // No background color - use terminal default
        }

        if (isSeparator) {
            screen->writeText(_separatorLine);
        } else {
            screen->writeText(item->formattedText);
        }

        CxScreen::resetColors();
    }
}


//-------------------------------------------------------------------------------------------------
// HelpView::redrawFooter
//
// Redraw just the footer line if it changed.
//
//-------------------------------------------------------------------------------------------------
void
HelpView::redrawFooter( void )
{
    CxString footer = getContextFooter();
    if (footer == _lastFooter) {
        return;  // no change
    }

    _lastFooter = footer;

    // redraw just the footer using frame method
    frame->setFrameColor(spreadsheetDefaults->headerTextColor(),
                         spreadsheetDefaults->headerBackgroundColor());
    frame->drawFooter(footer);
}


//-------------------------------------------------------------------------------------------------
// HelpView::getSelectedItemType
//
// Return the type of the currently selected item.
//
//-------------------------------------------------------------------------------------------------
HelpViewItemType
HelpView::getSelectedItemType( void )
{
    if (selectedListItemIndex < 0 || selectedListItemIndex >= (int)_visibleItems.entries()) {
        return HELPITEM_SECTION;
    }
    return _visibleItems.at(selectedListItemIndex)->type;
}


//-------------------------------------------------------------------------------------------------
// HelpView::getContextFooter
//
// Build a context-sensitive footer string based on the currently selected item.
//
//-------------------------------------------------------------------------------------------------
CxString
HelpView::getContextFooter( void )
{
    HelpViewItemType selType = getSelectedItemType();

    // section header
    if (selType == HELPITEM_SECTION) {
        return "[Enter] Expand/Collapse  [Esc] Close";
    }

    // content line or other
    return "[Esc] Close";
}


//-------------------------------------------------------------------------------------------------
// HelpView::toggleSelectedSection
//
// Toggle expand/collapse of the selected section header and rebuild visible items.
//
//-------------------------------------------------------------------------------------------------
void
HelpView::toggleSelectedSection( void )
{
    if (selectedListItemIndex < 0 || selectedListItemIndex >= (int)_visibleItems.entries()) {
        return;
    }

    HelpViewItem *item = _visibleItems.at(selectedListItemIndex);

    if (item->type != HELPITEM_SECTION) {
        return;
    }

    HelpSection *sec = _sections.at(item->sectionIndex);
    if (sec == NULL) {
        return;
    }

    sec->isExpanded = !sec->isExpanded;
    rebuildVisibleItems();

    // ensure selected index is still valid
    if (selectedListItemIndex >= (int)_visibleItems.entries()) {
        selectedListItemIndex = (int)_visibleItems.entries() - 1;
        if (selectedListItemIndex < 0) {
            selectedListItemIndex = 0;
        }
    }
}


//-------------------------------------------------------------------------------------------------
// HelpView::setVisible
//
// Set the visibility state for resize handling.
//
//-------------------------------------------------------------------------------------------------
void
HelpView::setVisible( int visible )
{
    _visible = visible;
}


//-------------------------------------------------------------------------------------------------
// HelpView::setFirstRun
//
// Set the first run flag for welcome message display.
//
//-------------------------------------------------------------------------------------------------
void
HelpView::setFirstRun( int firstRun )
{
    _isFirstRun = firstRun;
    rebuildVisibleItems();
}


//-------------------------------------------------------------------------------------------------
// HelpView::routeKeyAction
//
// Handle keyboard actions for navigation.
//
//-------------------------------------------------------------------------------------------------
void
HelpView::routeKeyAction( CxKeyAction keyAction )
{
    switch (keyAction.actionType())
    {
        case CxKeyAction::CURSOR:
        {
            // handleArrows() does its own incremental redraw
            handleArrows(keyAction);
        }
        break;

        default:
            break;
    }

    return;
}


//-------------------------------------------------------------------------------------------------
// HelpView::reframe
//
// If selected item isn't visible, adjust the scroll window.
//
//-------------------------------------------------------------------------------------------------
int
HelpView::reframe( void )
{
    int changeMade = false;

    // safety: ensure firstVisibleListIndex is never negative
    if (firstVisibleListIndex < 0) {
        firstVisibleListIndex = 0;
    }

    // safety: ensure selectedListItemIndex is valid
    if (selectedListItemIndex < 0) {
        selectedListItemIndex = 0;
    }

    while (selectedListItemIndex < firstVisibleListIndex) {
        changeMade = true;
        firstVisibleListIndex--;
        if (firstVisibleListIndex < 0) {
            firstVisibleListIndex = 0;
            break;
        }
    }

    while (selectedListItemIndex >= firstVisibleListIndex + screenHelpNumberOfLines) {
        changeMade = true;
        firstVisibleListIndex++;
    }

    if (changeMade) return(true);

    return(false);
}


//-------------------------------------------------------------------------------------------------
// HelpView::handleArrows
//
// Move selection up/down through the visible items list.
// Skips non-selectable items (SEPARATOR, BLANK).
// Does incremental redraw - only redraws changed lines.
//
//-------------------------------------------------------------------------------------------------
int
HelpView::handleArrows( CxKeyAction keyAction )
{
    int totalItems = (int)_visibleItems.entries();
    int prevIndex = selectedListItemIndex;

    if (keyAction.tag() == "<arrow-down>") {

        selectedListItemIndex++;

        // skip non-selectable items (only SEPARATOR is non-selectable)
        while (selectedListItemIndex < totalItems) {
            HelpViewItemType t = _visibleItems.at(selectedListItemIndex)->type;
            if (t != HELPITEM_SEPARATOR) break;
            selectedListItemIndex++;
        }

        // if we ran off the end, stay put
        if (selectedListItemIndex >= totalItems) {
            selectedListItemIndex = prevIndex;
            return(false);  // no change
        }

        // check if scroll needed
        if (reframe()) {
            // scroll happened - need full content redraw
            redraw();
        } else {
            // no scroll - incremental redraw of just 2 lines + footer
            redrawLine(prevIndex, 0);      // old line, not selected
            redrawLine(selectedListItemIndex, 1);  // new line, selected

            redrawFooter();

            // position cursor on selected line
            int contentLeft = frame->contentLeft();
            int row = screenHelpFirstListLine + (selectedListItemIndex - firstVisibleListIndex);
            screen->placeCursor(row, contentLeft);
            screen->flush();
        }

        return(true);
    }

    if (keyAction.tag() == "<arrow-up>") {

        selectedListItemIndex--;

        // skip non-selectable items (only SEPARATOR is non-selectable)
        while (selectedListItemIndex >= 0) {
            HelpViewItemType t = _visibleItems.at(selectedListItemIndex)->type;
            if (t != HELPITEM_SEPARATOR) break;
            selectedListItemIndex--;
        }

        // if we ran past the top, stay put
        if (selectedListItemIndex < 0) {
            selectedListItemIndex = prevIndex;
            return(false);  // no change
        }

        // check if scroll needed
        if (reframe()) {
            // scroll happened - need full content redraw
            redraw();
        } else {
            // no scroll - incremental redraw of just 2 lines + footer
            redrawLine(prevIndex, 0);      // old line, not selected
            redrawLine(selectedListItemIndex, 1);  // new line, selected

            redrawFooter();

            // position cursor on selected line
            int contentLeft = frame->contentLeft();
            int row = screenHelpFirstListLine + (selectedListItemIndex - firstVisibleListIndex);
            screen->placeCursor(row, contentLeft);
            screen->flush();
        }

        return(true);
    }

    return(false);
}
