# ss Spreadsheet Help

## Navigation

Arrow keys          Move cursor between cells
Shift+Arrow         Select range of cells
Page Up/Down        Scroll by screen
Ctrl+Arrow          Jump to edge of data region
Home                Move to column A
ESC                 Cancel selection / exit mode

## Data Entry

Just start typing   Enter data in current cell (replaces existing)
Enter               Edit existing cell content
=                   Start formula (e.g., =A1+B2)
Enter               Commit entry (when editing)
ESC                 Cancel entry

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

## Formatting Shortcuts

Ctrl+N or Ctrl+4    Cycle number formats (plain → $,.2 → $,.0 → ,.2)
Ctrl+5              Toggle percent format
Ctrl+A              Cycle alignment (left → center → right)
Ctrl+D              Cycle date formats

## Clipboard

Ctrl+K              Copy selection
Ctrl+Y              Paste
Delete/Backspace    Clear selection

## File Operations

Ctrl+X Ctrl+S       Save file
Ctrl+X Ctrl+C       Quit (prompts to save)

## ESC Commands

Press ESC to enter command mode, then type command prefix.
Commands autocomplete as you type. Organized by category:

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
  insert-symbol <type>        Insert box drawing symbol

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

quit-
  quit-save                   Save and quit
  quit-nosave                 Quit without saving

## Date Formats

Dates are stored as serial numbers and can be entered as:
  2026-03-10        ISO format (yyyy-mm-dd)
  03/10/2026        US format (mm/dd/yyyy)
  03-10-2026        US format with dashes
  2026/03/10        ISO format with slashes

Use Ctrl+D to cycle between display formats.

## Tips

- Format cycling syncs mixed formats first, then cycles
- Column defaults cascade to cells without explicit formatting
- Colors are preserved when editing cell values
- Commands autocomplete as you type
