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
- `format-col-width <+n|-n>` - adjust column width (+n wider, -n narrower)
- `format-col-fit` - auto-fit column width to content

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
- `format-cell-number-currency` - toggle currency format
- `format-cell-number-decimal <n>` - set decimal places (0-10)
- `format-cell-number-percent` - toggle percent format
- `format-cell-number-thousands` - toggle thousands separators

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
- `format-cell-align-left`
- `format-cell-align-center`
- `format-cell-align-right`

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
- `format-hide-row` - hide selected row(s)
- `format-show-row` - unhide
- `insert-row` - insert row above cursor
- `delete-row` - delete current row

**Column equivalents:**
- `format-hide-column`
- `format-show-column`
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

## Implementation Order

1. ✓ Range selection in EDIT mode
2. ✓ Column width (format-col-width, format-col-fit)
3. ✓ Copy/paste with reference adjustment (Ctrl-K, Ctrl-Y)
4. Fill operations (Ctrl-D, Ctrl-R)
5. Freeze panes
6. ✓ Number formatting (format-cell-number-*)
7. ✓ Alignment (format-cell-align-*)
8. Colors (requires popup UI)
9. Row/column hide/show/insert/delete
