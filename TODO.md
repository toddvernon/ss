# ss Spreadsheet TODO

## Phase 1: Range Selection in EDIT Mode ✓

Foundation for all range-based operations.

**Selection mechanism:**
- `Shift+Arrow` - start/extend selection from current cell
- Selection highlighted (distinct from cell hunt green)
- `ESC` or arrow without shift - cancel selection, back to single cell

**Visual feedback:**
- Anchor cell (where selection started) - highlighted
- Selected range - fill color
- Status line shows range: `cell(A1:C10)` instead of `cell(A1)`

**State:**
```cpp
int _rangeSelectActive;
CxSheetCellCoordinate _rangeAnchor;
CxSheetCellCoordinate _rangeCurrent;
```

---

## Phase 2: Column Width ✓

**Commands:**
- `modify-col-width <+n|-n>` - adjust column width (+n wider, -n narrower)
- `modify-col-fit` - auto-fit column width to content

**With range selection:**
- Commands apply to all columns in the selected range

**JSON storage:**
```json
{
  "columns": {
    "A": {"width": 12},
    "B": {"width": 25}
  }
}
```

---

## Phase 3: Cell Copy/Paste ✓

**Shortcuts:**
- `Ctrl-K` - copy selection to clipboard
- `Ctrl-Y` - paste from clipboard

**Reference adjustment on paste:**
- Relative refs (`A1`) adjust based on paste position offset
- Absolute refs (`$A$1`) stay fixed
- Mixed refs (`$A1`, `A$1`) adjust only the non-$ part

**Commands:**
- `edit-copy` - copy selection to clipboard
- `edit-cut` - copy and clear original
- `edit-paste` - paste at cursor with reference adjustment
- `edit-clear` - clear cell contents

---

## Phase 4: Fill Operations

**Commands:**
| Command | Shortcut | Action |
|---------|----------|--------|
| `edit-copy-down` | TBD | Copy top row down through selection |
| `edit-copy-right` | `Ctrl-R` | Copy left column right through selection |

**Note:** Ctrl-D is now used for date format cycling.

**Behavior:**
- Requires range selection
- Copies formula/value from edge row/column
- References adjust row-by-row or column-by-column

---

## Phase 5: Freeze Panes

Lock rows/columns so they stay visible while rest scrolls.

**Commands:**
- `view-freeze` - freeze rows above and columns left of cursor
- `view-unfreeze` - remove freeze

**Visual:**
- Double-line divider between frozen and scrolling regions
- Frozen panes render at their fixed positions
- Scroll offsets only apply to non-frozen area

**JSON storage:**
```json
{
  "freezeRow": 2,
  "freezeCol": 1
}
```

---

## Phase 6: Number Formatting ✓

**Currency:**
- $ left-aligned, number right-aligned within cell
- Default 2 decimal places
- Thousands separators

Display: `|$     1,234.56|`

**Commands:**
- `modify-cell-number-currency` - toggle currency format
- `modify-cell-number-decimal <n>` - set decimal places (0-10)
- `modify-cell-number-percent` - toggle percent format
- `modify-cell-number-thousands` - toggle thousands separators

**Keyboard shortcuts:**
- `Ctrl-N` or `Ctrl-4` - cycle number formats (plain → $,.2 → $,.0 → ,.2)
- `Ctrl-5` - toggle percent format

**JSON storage:**
```json
{
  "cell": "A1",
  "type": "double",
  "value": 1234.56,
  "format": {
    "currency": true,
    "decimalPlaces": 2,
    "align": "right"
  }
}
```

---

## Phase 7: Alignment ✓

**Commands:**
- `modify-cell-align-left`
- `modify-cell-align-center`
- `modify-cell-align-right`

**Keyboard shortcuts:**
- `Ctrl-A` - cycle alignment (left → center → right)

**With range selection:**
- Applies to all cells in range

---

## Phase 8: Colors ✓

**Commands:**
- `modify-cell-color-fg` - set cell foreground color
- `modify-cell-color-bg` - set cell background color
- `modify-col-color-fg` - set column default foreground
- `modify-col-color-bg` - set column default background

**UI:**
- RGB color palette picker (ESC command triggers picker)
- Arrow keys to navigate, Enter to select, ESC to cancel
- Colors displayed as blocks in command line format indicator

**Attributes:**
- `fgColor` - foreground color (RGB string)
- `bgColor` - cell background (RGB string)

---

## Phase 9: Row/Column Operations (Partial)

**Implemented:**
- `insert-row` - insert row above cursor ✓
- `delete-row` - delete current row ✓
- `insert-column` - insert column before cursor ✓
- `delete-column` - delete current column ✓

**Not implemented:**
- `modify-hide-row` - hide selected row(s)
- `modify-show-row` - unhide
- `modify-hide-column` - hide selected column(s)
- `modify-show-column` - unhide

---

## Phase 10: Post-Commit Parsing (Excel-style Input) ✓

**Implemented:** Flexible input with post-commit type inference.

**Input modes:**
```
First char    Mode              Accepts
─────────────────────────────────────────
=             ENTRY_FORMULA     (special formula mode)
@             ENTRY_TEXTMAP     (textmap rules)
anything else ENTRY_GENERAL     all printable chars
```

At commit time, input is parsed to determine type:
1. Try date patterns → serial double + dateFormat
2. Try number (with optional $, %, commas) → double + format attrs
3. Otherwise → text

**Date patterns recognized:**
```
10/20/2026    mm/dd/yyyy
2026-10-20    yyyy-mm-dd  (ISO)
10-20-2026    mm-dd-yyyy
2026/10/20    yyyy/mm/dd
```

**Keyboard shortcuts:**
- `Ctrl-D` - cycle date formats (yyyy-mm-dd → yyyy/mm/dd → mm/dd/yyyy → mm-dd-yyyy)

**Number patterns (input implies format):**
```
Input           Stored    Format attributes
───────────────────────────────────────────────
1234.56         1234.56   (none)
1,234.56        1234.56   thousands=true
$1234.56        1234.56   currency=true
$1,234.56       1234.56   currency=true, thousands=true
50%             0.5       percent=true
-1,234.56       -1234.56  thousands=true
```

**JSON storage:**
```json
{
  "cell": "A1",
  "type": "double",
  "value": 46318,
  "format": {
    "dateFormat": "mm/dd/yyyy"
  }
}
```

---

## JSON Schema (Complete)

```json
{
  "version": 1,
  "currentPosition": "A1",
  "freezeRow": 0,
  "freezeCol": 0,
  "columns": {
    "A": {"width": 12, "hidden": false},
    "B": {"width": 25, "hidden": false}
  },
  "rows": {
    "1": {"hidden": false}
  },
  "cells": [
    {
      "cell": "A1",
      "type": "double",
      "value": 1234.56,
      "format": {
        "align": "right",
        "currency": true,
        "decimalPlaces": 2
      }
    },
    {
      "cell": "B1",
      "type": "text",
      "text": "Label",
      "format": {
        "align": "left",
        "bold": true
      }
    },
    {
      "cell": "C1",
      "type": "empty",
      "symbolFill": "horizontal"
    }
  ]
}
```

---

## Implementation Status

| Phase | Feature | Status |
|-------|---------|--------|
| 1 | Range Selection | ✓ Complete |
| 2 | Column Width | ✓ Complete |
| 3 | Copy/Paste | ✓ Complete |
| 4 | Fill Operations | Not started |
| 5 | Freeze Panes | Not started |
| 6 | Number Formatting | ✓ Complete |
| 7 | Alignment | ✓ Complete |
| 8 | Colors | ✓ Complete |
| 9 | Row/Column Operations | Partial (hide/show remaining) |
| 10 | Post-Commit Parsing | ✓ Complete |

---

## Remaining Work

1. **Fill Operations (Phase 4)** - copy-down, copy-right
2. **Freeze Panes (Phase 5)** - lock rows/columns while scrolling
3. **Hide/Show Rows/Columns (Phase 9)** - remaining row/column operations
