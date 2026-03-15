# ss Spreadsheet Help

## Navigation

Arrow keys          Move cursor between cells
Shift+Arrow         Select range of cells
Page Up/Down        Scroll by screen
Ctrl+Arrow          Jump to edge of data region
Home                Move to column A
ESC                 Cancel selection / exit mode
Ctrl+H              Show this help screen

## Data Entry

Just start typing to enter data in the current cell:

  <letter>          Text mode (first char is a letter)
  <digit> or +/-    Number mode
  $                 Currency mode ($1,234.56)
  =                 Formula mode (e.g., =A1+B2)
  @                 Textmap mode (conditional text labels)

Enter (on a cell)   Edit existing cell content
Enter (while editing)  Commit entry to cell
ESC (while editing) Cancel entry, cell unchanged

## Formulas

=A1+B2              Cell references
=SUM(A1:A10)        Range functions
=$A$1               Absolute reference
=A$1 or =$A1        Mixed reference

In formula mode, press ESC to enter cell hunt mode:
  Arrow keys        Navigate to select cell reference
  Space             Start range selection
  Enter             Insert reference and return to formula
  ESC               Cancel cell hunt

## Textmap Rules

Type @ to enter a textmap rule. Rules map other cell values to text labels:

  @C1>100: Over Budget; C1<50: Under Budget; On Track

  Semicolons separate rules. Each rule: <cellRef><op><value>: <label>
  Operators: = < > <= >= !=
  Last entry without ':' is the default label.

## Formatting Shortcuts

Ctrl+N or Ctrl+4    Cycle number formats (plain > $,.2 > $,.0 > ,.2)
Ctrl+5              Toggle percent format
Ctrl+A              Cycle alignment (left > center > right)
Ctrl+D              Cycle date formats

## Clipboard

Ctrl+K              Copy selection
Ctrl+Y              Paste
Delete/Backspace    Clear selection

Paste adjusts relative references. Absolute ($) references stay fixed.

## File Operations

Ctrl+X Ctrl+S       Save file
Ctrl+X Ctrl+C       Quit (prompts to save)

## ESC Commands

Press ESC to open the command prompt. Then just start typing.

How it works:
  1. Each keystroke narrows the matches. The shared prefix fills in automatically.
  2. When your input uniquely identifies a command, it executes immediately.
  3. Invalid keystrokes are rejected — you cannot type a wrong path.

Example: to delete a row, type ESC then d then r. After 'd', the prompt
fills "delete-". After 'r', "delete-row" is the only match, so it runs.
Three keystrokes total.

Commands are organized by category:

file-
  file-load                   Load spreadsheet from file
  file-save                   Save spreadsheet to file
  file-quit                   Quit spreadsheet

edit-
  edit-copy                   Copy selection to clipboard
  edit-cut                    Cut selection to clipboard
  edit-paste                  Paste from clipboard
  edit-clear                  Clear cell contents
  edit-fill-down              Fill selection down from first row
  edit-fill-right             Fill selection right from first column

insert-
  insert-row-before           Insert row before cursor
  insert-column-before        Insert column before cursor
  insert-symbol <type>        Insert box drawing symbol (see Symbol Types)

delete-
  delete-row                  Delete current row
  delete-column               Delete current column

modify-col-
  modify-col-width <+n|-n>    Adjust column width
  modify-col-fit              Auto-fit column width to content
  modify-col-align-left       Set column default align left
  modify-col-align-center     Set column default align center
  modify-col-align-right      Set column default align right
  modify-col-number-currency  Toggle column default currency ($)
  modify-col-number-decimal <n>  Set column default decimals
  modify-col-number-percent   Toggle column default percent (%)
  modify-col-number-thousands Toggle column default thousands (,)
  modify-col-color-foreground Set column default text color
  modify-col-color-background Set column default background color
  modify-col-hide             Hide current column (or columns in selection)
  modify-col-show             Show hidden columns (requires range selection)

modify-row-
  modify-row-hide             Hide current row (or rows in selection)
  modify-row-show             Show hidden rows (requires range selection)

modify-cell-
  modify-cell-align-left      Left-align cell contents
  modify-cell-align-center    Center cell contents
  modify-cell-align-right     Right-align cell contents
  modify-cell-number-currency Toggle currency format ($)
  modify-cell-number-decimal <n>  Set decimal places (0-10)
  modify-cell-number-percent  Toggle percent format (%)
  modify-cell-number-thousands Toggle thousands separators (,)
  modify-cell-text-wide       Toggle wide text spacing
  modify-cell-color-foreground Set cell text color
  modify-cell-color-background Set cell background color

modify-show-all               Show all hidden rows and columns

view-
  view-freeze                 Freeze selected region (selection must start at A1)
  view-unfreeze               Remove freeze panes

quit-
  quit-save                   Save and quit
  quit-nosave                 Quit without saving

## Freeze Panes

Select a range starting at A1, then ESC > view-freeze:

  A1:C1             Freeze columns A-C (horizontal headers)
  A1:A6             Freeze rows 1-6 (vertical headers)
  A1:C6             Freeze both rows and columns

Frozen rows/columns stay visible while the rest of the sheet scrolls.
The status line shows [Frozen] when active. Use view-unfreeze to remove.

## Symbol Types

Used with insert-symbol to fill a cell with box drawing characters:

  horizontal        ─────── (horizontal line)
  vertical          │ (vertical line)
  upper-left        ┌ (corner)
  upper-right       ┐ (corner)
  lower-left        └ (corner)
  lower-right       ┘ (corner)
  left-tee          ├ (tee junction)
  right-tee         ┤ (tee junction)
  upper-tee         ┬ (tee junction)
  lower-tee         ┴ (tee junction)

Symbol fills adapt to cell width automatically.

## Date Formats

Dates are stored as serial numbers and can be entered as:
  2026-03-10        ISO format (yyyy-mm-dd)
  03/10/2026        US format (mm/dd/yyyy)
  03-10-2026        US format with dashes
  2026/03/10        ISO format with slashes

Use Ctrl+D to cycle between display formats.

## Text Overflow

Long text spills into neighboring empty cells:
- Left-aligned text overflows rightward
- Right-aligned text overflows leftward
- Overflow stops at occupied cells, highlighted cells, and freeze dividers

## Tips

- Format cycling syncs mixed formats first, then cycles
- Column defaults cascade to cells without explicit formatting
- Colors are preserved when editing cell values
- To unhide rows/columns, select a range spanning the gap, then use show
- ESC commands auto-complete and auto-execute — most take 2-3 keystrokes
