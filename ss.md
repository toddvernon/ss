# what are we doing

We are going to build a program called ss.  Its going to be a classic spreadsheet program with a grid of cells.  

Each cell can hold a value that is eithery 
	- text 
	- double
	- formulula
	- symbol fill

This program is meant to be a spirtual companion to the emacs like program cm that is located at
at the path ../cm.  When in doubt how it should work follow how cm does it.  This includes, look, feel, class construction, models
and views, libraries, everything possible.

When something seems like it might be of general use ask if that functionality should be in a library with unit tests located at
cx/cx_tests.  The class sheetModel in the library directory and associated cx_test directory was constructed for this program.

# features

1) This is a terminal app, the terminal is resizable and should use the same single path screen layout logic and mechanzim cm uses based
on sigwinch.  The app should switch terminal screens like cm does to perserve the terminal launch screen and restore after execution.

2) It uses the same claude rules as cm that can be found in the cm directory

3) It gnerally only uses the cx libraries and no stl or more modern C++ things that aren't already found in the program cm or its libs

4) The terminal screen is broken into 4 zones initially.  The spreadsheet area, the dividing line, the command line, and status 
line (like cm).  The dividing line should be owned by the sheet section however as we will likley add split horizontal and verticle 
at some point to see multiple sheets or different parts of the same sheet.

5) the program is generally in edit mode (like cm).  The arrow keys move a highlight box around cell to cell horizantally and verticlly

6) The command structure should patterm cm with regards to heavy use of command completion and a command layout that basically mimics 
a menu sytem but in the commandline. See cm and cx/commandompleter.

7) When the cursor moves off the right side of the screen the entire ss area scroll with it like a typical ss. same for scrolling
left and up and down

8) The ESC key enters command mode and jumps the cursor to the the commandline.  

9) Once in command mode, 
	the command line offers up the top level possible command prefixes (see the cm app).  This will likely be
	"command>     = | " | <$><number> | file- | view-"  similar to the command structure of cm.

	Onec in command mode, another <ESC> exits command mode aborting the command like cm, with the exception of formula mode described next

##	<ESC> =

	In formula mode the user can enter a formula as is customary.  However if the ESC key is pressed in formula mode the program
	enters cell hunt mode for selecting cell references.

	**Entering cell hunt mode:**
	- Cursor jumps back to the cell where command mode was entered
	- The formula display updates in real-time as you navigate

	**Navigation and selection:**
	- Arrow keys move the highlight cell-to-cell
	- The current cell reference appears in the formula in real-time as you move
	- SPACE sets the start of a range (cell is anchored)
	- After SPACE, arrow keys extend the range (e.g., $A$1:$C$3)
	- ENTER finalizes the selection (single cell or range) and returns to formula editing
	- ESC cancels cell hunt mode, discards any selection, returns to formula editing

	**Reference format:**
	- All references inserted via cell hunt are absolute by default (e.g., $A$1, $A$1:$C$3)
	- User must manually edit the formula to change to relative references (remove $ signs)

	**Example flow:**
	1. User types = to enter formula mode, types SUM(
	2. User presses ESC to enter cell hunt mode
	3. User navigates to A1, formula shows =SUM($A$1
	4. User presses SPACE to anchor range start
	5. User navigates to A10, formula shows =SUM($A$1:$A$10
	6. User presses ENTER, returns to formula mode with =SUM($A$1:$A$10
	7. User types ) to complete: =SUM($A$1:$A$10)

## <ESC> <$><digits | +->
	if the user enters <number> meaning a +- or digit, then the user is in number mode.  the program should accept only numbers.  However
	if the user enters a $ first then the cell will be set dollar formatting.  Still a number but prepended with $ and the format will
	assumed to be two digits after the decimal point and commas in traditinal places when displayed in the cell.  Not required for 
	entry however. when the user types <return> the number is set in the cell and cell is type number.

## <ESC> <letter> will follow the conventions of command completion used in cm.
	for instance "command> file-    | load | save" , etc

10) unlike cm this program is targeted only at linux and macOS and does not have to support older platforms.  For this reason it will use
the utf versions of CxString as it will use symbols and line drawing symbols in that character set.

11) each cell needs a formatting identifier.  This is initally a placeholder but at some point we will add traditional formatting
for text elements like left/right/center, number formats like money, number of decimal points, commas, color, bold, etc 

12) symbol fill is a bit unique.  symbol fill will primarily be used for box drawing.  so a symbol fill of upper-left will be the upper
left utf box symbol followed by the utf horizontal dashs that will fill the cell regardless of the size of the cell.  In other words the
content adapt to keep the box part look correct if the cell size changes.  Same for upper-right, lower-left and lower-right, horizontal.  

this is cell
┌──────────────────────────┐
│                          │
└──────────────────────────┘
this is cell                this is a cell


13) cells height will always be one terminal row.  Cell width is variable but consistent for all rows in the spreadsheet.  An ESC command
will set the column width based on the column the cursor is in when invocated <ESC> format- | cellwidth ?


# Project layout

cx
└───cx (libs)
       └───various libs
└───cx_tests
       └───unit tests
└───cx_apps
       └── cm
       └── ss

Like cm the ss program lives under the cx_apps directory in the directory ss.  It uses the librarys in cx/cx/.. for everthing.  
for instance classes that will be used are cx/expression, cx/keyboard, cx/json, cx/sheetModel, cx/base, cx/functor, cx/commandcompleter, cx/screen, and
perhaps others.  Of particular note is cx/sheetModel.  its the foundational data model for this app, understanding row and cols, 
cell expressions.

# building

Use an identical build structure to cm and don't use make varients not found in cm makefile.  Not the platform specific build nature
and auto plaform discovery of the makefile.


# scope of version 1

invoke ss
user can navigate cells with arrows
press esc and enter a basic data type or a formula
sheet recalcs
<esc> file-  | file | load

   

