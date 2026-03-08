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
| `edit-copy-down` | `Ctrl-D` | Copy top row down through selection |
| `edit-copy-right` | `Ctrl-R` | Copy left column right through selection |

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

**With range selection:**
- Applies to all cells in range

---

## Phase 8: Colors (Deferred)

Requires popup UI similar to cm's project window.

**Attributes:**
- `textColor` - foreground color
- `backgroundColor` - cell background

**UI:**
- Visual color picker popup
- Named colors and/or RGB selection
- Preview before applying

---

## Phase 9: Row Operations

**Commands:**
- `modify-hide-row` - hide selected row(s)
- `modify-show-row` - unhide
- `insert-row` - insert row above cursor
- `delete-row` - delete current row

**Column equivalents:**
- `modify-hide-column`
- `modify-show-column`
- `insert-column`
- `delete-column`

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

## Phase 10: Post-Commit Parsing (Excel-style Input)

**Architectural change:** Switch from strict per-character validation to flexible input with post-commit type inference.

**Current approach (problematic):**
```
First char    Mode              Rejects
─────────────────────────────────────────
digit/+/-     ENTRY_NUMBER      / and letters
letter        ENTRY_TEXT        (none)
$             ENTRY_CURRENCY    letters
=             ENTRY_FORMULA     (special)
```
Result: `10/20/2026` fails because `/` is rejected in number mode.

**New approach (Excel-style):**
```
First char    Mode              Accepts
─────────────────────────────────────────
=             ENTRY_FORMULA     (unchanged - special)
anything else ENTRY_GENERAL     all printable chars
```

At commit time, parse the input to determine type:
1. Try date patterns → serial double + dateFormat
2. Try number (with optional $, %, commas) → double + format attrs
3. Otherwise → text

**Date patterns to recognize:**
```
10/20/2026    mm/dd/yyyy
2026-10-20    yyyy-mm-dd  (ISO)
10-20-2026    mm-dd-yyyy
20-Oct-2026   dd-mon-yyyy
Oct 20, 2026  mon dd, yyyy
```

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

**Output formatting commands:**
- `modify-cell-date-mdy` - mm/dd/yyyy
- `modify-cell-date-ymd` - yyyy-mm-dd
- `modify-cell-date-dmy` - dd/mm/yyyy
- `modify-cell-date-long` - "October 20, 2026"

**Time support (future):**
- `TIME(h,m,s)` function (fractional day)
- `HOUR()`, `MINUTE()`, `SECOND()` extraction
- Input patterns: `10:30`, `10:30:45`, `10:30 PM`

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

**Implementation steps:**
1. Collapse ENTRY_TEXT/NUMBER/CURRENCY into ENTRY_GENERAL
2. Accept all printable chars (except = which stays ENTRY_FORMULA)
3. At commit: parse input with tryParseDate(), tryParseNumber()
4. Set cell type + format attributes based on parse result
5. Add date display formatting in SheetView
6. Add modify-cell-date-* commands

---

## Implementation Order

1. ✓ Range selection in EDIT mode
2. ✓ Column width (modify-col-width, modify-col-fit)
3. ✓ Copy/paste with reference adjustment (Ctrl-K, Ctrl-Y)
4. ✓ Number formatting (modify-cell-number-*)
5. ✓ Alignment (modify-cell-align-*)
6. **Post-commit parsing (Phase 10)** ← NEXT PRIORITY
7. Fill operations (Ctrl-D, Ctrl-R)
8. Freeze panes
9. Row/column hide/show/insert/delete
10. Colors (requires popup UI)
