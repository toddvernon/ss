# Excel to SS Conversion Guide

## Overview

Convert `.xlsx` files to `.sheet` files for the ss terminal spreadsheet application.

## SS JSON Format

```json
{
  "version": 1,
  "currentPosition": "A1",
  "cells": [ ... ],
  "columns": { ... },
  "cursorRow": 0,
  "cursorCol": 0
}
```

## Cell Type Mapping

| Excel Type | SS Type | Fields |
|------------|---------|--------|
| Text/String | `"type": "text"` | `"text": "value"` |
| Number | `"type": "double"` | `"value": 123.45` |
| Date | `"type": "double"` | `"value": <serial>`, `"dateFormat": "mm/dd/yyyy"` |
| Formula | `"type": "formula"` | `"formula": "=SUM(A1:A10)"` |

## Date Handling

Dates in both Excel and ss are stored as **Excel serial numbers** (doubles counting days from Jan 1, 1900). Openpyxl auto-converts these to Python `datetime` objects on load, so convert them back:

```python
def date_to_excel_serial(dt):
    base = datetime.datetime(1899, 12, 30)  # accounts for Excel's 1900 leap year bug
    delta = dt - base
    return delta.days + (delta.seconds / 86400.0)
```

Use `int(serial)` for whole-day dates. Add the `dateFormat` app attribute:

```json
{"cell": "C3", "type": "double", "value": 43845, "dateFormat": "mm/dd/yyyy"}
```

Supported `dateFormat` values: `"mm/dd/yyyy"`, `"yyyy-mm-dd"`, `"mm-dd-yyyy"`, `"yyyy/mm/dd"`

## Number Formatting Attributes

Detect from `cell.number_format` in openpyxl:

| Excel Format | SS Attributes |
|-------------|---------------|
| `$` in format | `"currency": true` |
| `#,##0` | `"thousands": true` |
| `#,##0.00` | `"decimalPlaces": 2` |
| `#,##0` (no decimals) | `"decimalPlaces": 0` |

## Cell-Level Optional Attributes

- `"align"`: `"left"`, `"center"`, or `"right"`
- `"currency"`: `true` — display with `$` and commas
- `"thousands"`: `true` — display with comma separators
- `"decimalPlaces"`: integer — number of decimal places
- `"dateFormat"`: string — date display format
- `"fgColor"`: `"RGB:R,G,B"` or `"ANSI:COLORNAME"`
- `"bgColor"`: same format as fgColor

## Column Definitions

```json
"columns": {
  "A": {"width": 12},
  "B": {"width": 25, "align": "right", "currency": true, "decimalPlaces": 2}
}
```

Excel column widths are roughly in character units already but may need adjustment for terminal display.

## Formula Handling

- Formulas are preserved as-is from Excel (e.g., `=SUM(A1:A10)`)
- SS formulas use the same syntax as Excel for basic operations
- Apply formatting attributes to formula cells too (currency, decimals) based on the Excel number format

## Python Conversion Template

```python
import openpyxl, json, datetime

wb = openpyxl.load_workbook('input.xlsx')
ws = wb['Sheet1']
cells = []

def date_to_excel_serial(dt):
    base = datetime.datetime(1899, 12, 30)
    delta = dt - base
    return delta.days + (delta.seconds / 86400.0)

for row in ws.iter_rows(min_row=ws.min_row, max_row=ws.max_row,
                         min_col=ws.min_column, max_col=ws.max_column):
    for cell in row:
        if cell.value is None:
            continue
        if isinstance(cell.value, str) and cell.value.strip() == '':
            continue

        val = cell.value
        entry = {"cell": cell.coordinate}

        if isinstance(val, str) and val.startswith('='):
            entry["type"] = "formula"
            entry["formula"] = val
        elif isinstance(val, datetime.datetime):
            entry["type"] = "double"
            entry["value"] = int(date_to_excel_serial(val))
            entry["dateFormat"] = "mm/dd/yyyy"
        elif isinstance(val, (int, float)):
            entry["type"] = "double"
            entry["value"] = val
            fmt = cell.number_format or ''
            if '$' in fmt:
                entry["currency"] = True
                entry["decimalPlaces"] = 2 if '#,##0.00' in fmt else 0
            elif '#,##0' in fmt and fmt != 'General':
                entry["thousands"] = True
        else:
            entry["type"] = "text"
            entry["text"] = str(val)

        # Alignment
        if cell.alignment and cell.alignment.horizontal:
            h = cell.alignment.horizontal
            if h in ('left', 'right', 'center'):
                entry["align"] = h

        # Number formatting on formula cells
        if entry.get("type") == "formula":
            fmt = cell.number_format or ''
            if '$' in fmt:
                entry["currency"] = True
                entry["decimalPlaces"] = 2 if '#,##0.00' in fmt else 0

        cells.append(entry)

doc = {
    "version": 1,
    "currentPosition": "A1",
    "cells": cells,
    "columns": {},  # set column widths as needed
    "cursorRow": 0,
    "cursorCol": 0
}

with open('output.sheet', 'w') as f:
    json.dump(doc, f, indent=2)
```

## Notes

- Only Sheet1 (or the sheet with data) is converted — ss is single-sheet
- Empty cells and whitespace-only cells are skipped
- openpyxl requires: `pip install openpyxl`
- Load with `data_only=False` (default) to preserve formulas
