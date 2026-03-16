//-------------------------------------------------------------------------------------------------
//
//  HelpView.h
//  ss (spreadsheet)
//
//  Copyright 2022-2025 Todd Vernon. All rights reserved.
//  Licensed under the Apache License, Version 2.0
//  See LICENSE file for details.
//
//  Modal help view definitions.
//
//-------------------------------------------------------------------------------------------------

#include <stdio.h>

#include <sys/types.h>
#include <cx/base/string.h>
#include <cx/base/slist.h>
#include <cx/base/fileaccess.h>
#include <cx/base/file.h>

#include <cx/keyboard/keyboard.h>
#include <cx/screen/screen.h>
#include <cx/screen/boxframe.h>

#include "SpreadsheetDefaults.h"


#ifndef _HelpView_h_
#define _HelpView_h_


//-------------------------------------------------------------------------------------------------
// HelpViewItemType
//
// Type of item in the visible items list.
//
//-------------------------------------------------------------------------------------------------
enum HelpViewItemType {
    HELPITEM_SECTION,    // ## header - selectable, expandable/collapsible
    HELPITEM_LINE,       // content line under a section - selectable
    HELPITEM_BLANK,      // empty line (visual spacing, non-selectable)
    HELPITEM_SEPARATOR,  // visual separator between sections (non-selectable)
    HELPITEM_WELCOME     // welcome message for first run (non-selectable)
};


//-------------------------------------------------------------------------------------------------
// HelpSection
//
// A parsed section from the markdown help file.
//
//-------------------------------------------------------------------------------------------------
struct HelpSection {
    CxString title;              // header text (from ## line, without prefix)
    int isExpanded;              // 1=expanded, 0=collapsed
    CxSList<CxString*> lines;    // content lines in this section
};


//-------------------------------------------------------------------------------------------------
// HelpViewItem
//
// A single visible row in the help view.
//
//-------------------------------------------------------------------------------------------------
struct HelpViewItem {
    HelpViewItemType type;
    int sectionIndex;    // which HelpSection (0+), -1 for standalone
    int lineIndex;       // line within section, -1 for headers
    CxString formattedText;  // pre-computed display text (with padding)
};


//-------------------------------------------------------------------------------------------------
//
//  HelpView
//
//  Modal dialog showing help content organized by collapsible sections.
//
//-------------------------------------------------------------------------------------------------
class HelpView
{
  public:

    HelpView( SpreadsheetDefaults *pd, CxScreen *screen );
    // Constructor

    int loadHelpFile( void );
    // Search for and load help file

    void routeKeyAction( CxKeyAction keyAction );
    // route a keyboard action

    void redraw( void );
    // redraw the modal

    void recalcScreenPlacements( void );
    // recalcs the centered modal bounds

    void rebuildVisibleItems( void );
    // rebuild the flat list of visible items from sections

    HelpViewItemType getSelectedItemType( void );
    // get the type of the currently selected item

    void toggleSelectedSection( void );
    // toggle expand/collapse on section header

    void setVisible( int visible );
    // set visibility state for resize handling

    int isInsideFrame( int row, int col );
    // returns 1 if screen position is inside the dialog frame

    void setFirstRun( int firstRun );
    // set first run flag for welcome message display

    CxString getContextFooter( void );
    // build footer string based on currently selected item type

  private:

    int findHelpFile( CxString *outPath );
    // search for help file in standard locations

    void parseMarkdown( CxString filePath );
    // parse markdown file into sections

    void clearSections( void );
    // free all section data

    int handleArrows( CxKeyAction keyAction );
    // handle the arrow keys

    int reframe( void );
    // make sure selection is visible in list

    void redrawLine( int logicalIndex, int isSelected );
    // redraw a single content line

    void redrawFooter( void );
    // redraw just the footer

    SpreadsheetDefaults *spreadsheetDefaults;
    // pointer to the spreadsheet defaults

    CxScreen *screen;
    // pointer to the screen object

    CxBoxFrame *frame;
    // box frame for modal display

    // parsed help sections
    CxSList<HelpSection*> _sections;

    // flat list of visible items (rebuilt on expand/collapse)
    CxSList<HelpViewItem*> _visibleItems;

    // screen placement values (zero based)
    int  screenNumberOfLines;
    int  screenNumberOfCols;

    int  screenHelpTitleBarLine;
    int  screenHelpFrameLine;
    int  screenHelpNumberOfLines;
    int  screenHelpNumberOfCols;
    int  screenHelpFirstListLine;
    int  screenHelpLastListLine;

    // scrolling and selection
    int firstVisibleListIndex;
    int selectedListItemIndex;

    int _visible;           // whether modal is currently displayed
    int _helpFileLoaded;    // whether help file was successfully loaded
    int _isFirstRun;        // whether this is the first run (show welcome message)

    // pre-built strings for efficient redraw (built in recalcScreenPlacements)
    CxString _paddingSpaces;   // spaces for padding lines
    CxString _separatorLine;   // pre-built separator line
    CxString _emptyLine;       // pre-built empty line
    int _cachedContentWidth;   // content width when strings were built

    CxString _lastFooter;      // cached footer for change detection

    // frame bounds for mouse hit testing
    int _frameTop, _frameLeft, _frameBottom, _frameRight;
};

#endif
