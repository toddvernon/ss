# ss Project Instructions

## Overview
ss is a terminal-based spreadsheet application written in C++. It is a spiritual companion to the cm text editor and follows the same architectural patterns.

## Naming Conventions
When the user refers to:
- **"cx"** - means all cx repositories together: `cx/cx` (library), `cx/cx_tests` (tests), and `cx/cx_apps` (apps)
- **"ss"** - means this application (`cx/cx_apps/ss`)
- **"cm"** - means the companion editor application (`cx/cx_apps/cm`)
- **"cx library"** or **"cx/cx"** - means specifically the shared library at `cx/cx`
- **"cx_tests"** or **"cx tests"** - means the test repository at `cx/cx_tests`

## Related Repositories
- **cx library**: `../../cx/` - shared library with modules: base, commandcompleter, editbuffer, expression, functor, json, keyboard, log, net, screen, sheetModel, thread, tz
- **cm editor**: `../cm/` - reference implementation for terminal UI patterns

## Change Policy
- **Always show proposed changes before applying them** - describe what will be modified and wait for approval before editing files
- After approval, verify changes compile successfully by running `make`

## Do Not Modify
- `darwin_arm64/` - ARM64 build output
- `darwin_x86_64/` - x86_64 build output
- `linux_x86_64/` - Linux build output

## Build Instructions
```bash
make
```

**Always run a complete `make` after code changes** - never compile individual files separately. The build is fast and a full make avoids occasional app crashes that can occur when only some files are recompiled.

## Project Structure
- `Ss.cpp` - main entry point
- `SheetEditor.*` - core editor logic (central coordinator, owns all views)
- `SheetView.*` - spreadsheet grid display and cell navigation
- `CommandLineView.*` - command line interface (pattern from cm)
- `CommandTable.*` - static ESC command table (data only)

## Screen Resize Architecture

**SheetEditor owns the single resize callback.** Sub-elements (SheetView, CommandLineView) do NOT register their own callbacks with the OS.

- SheetEditor registers ONE callback via `screen->addScreenSizeCallback()`
- On resize, SheetEditor coordinates ALL recalcs first, THEN all redraws in correct z-order
- Sub-elements expose public `recalcScreenPlacements()` or `recalcForResize()` methods for SheetEditor to call
- Each visual element owns its own sub-elements - no separate drawing by parent

**NEVER** add resize callbacks to sub-elements. All resize coordination goes through SheetEditor.

## Makefile guidelines
- When modifying makefiles, follow the patterns already present in the cm makefile.
- Makefiles must be portable across old make implementations (SunOS, IRIX, BSD). Only use features and automatic variables already present in the existing makefiles. Do not introduce GNU make extensions such as `$(filter ...)`, `$(wildcard ...)`, `$(patsubst ...)`, `else ifeq`, etc. Use separate `ifeq`/`endif` blocks instead of `else ifeq`.

## Non-negotiable constraints
- DO NOT use the C++ Standard Library: no `std::` anywhere.
- DO NOT introduce templates, including template-based third-party libs.
- DO NOT add new external dependencies.
- Only use libraries that already exist in this repository under: ../../lib with the source in ../../cx directory
- If a feature would normally use STL/RAII/modern C++, implement it using existing in-repo utilities.

## Portability targets
- Code must compile on: macOS (darwin), Linux.
- Note: Unlike cm, ss targets only modern platforms and uses UTF-8 for box drawing and symbols.

## Platform classification

For features with platform-specific behavior (e.g., UI responsiveness, I/O intensity):

- **Modern platforms**: macOS (`_OSX_`), Linux (`_LINUX_`)
  - Can afford more screen I/O, visual effects, real-time updates

Use this pattern for platform-specific code:
```c
#if defined(_OSX_) || defined(_LINUX_)
    // Platform-specific code
#endif
```

## Language/feature restrictions (assume old toolchains)
- Avoid: auto, nullptr, constexpr, lambda, range-for, threads, regex, exceptions (unless already widely used).
- Avoid: <iostream>, <string>, <vector>, <map>, <memory>, <algorithm>, <functional>, <type_traits>, etc.
- DO NOT define classes inside function bodies (local/inner classes). GCC 2.95 crashes generating DWARF2 debug info for local class methods. Always define classes at file scope.
- Prefer C-style interfaces or existing repo abstractions.

## Includes & headers
- Prefer existing project headers and utilities.
- Use C headers where appropriate: <stdio.h>, <stdlib.h>, <string.h>, <unistd.h> (guarded), etc.
- Minimize OS-specific includes; isolate with `#ifdef` blocks and use only existing defines for platform

## OS-specific code
- Any OS-specific code MUST be isolated behind preprocessor guards.
- Use existing platform abstraction layers if present.
- Never add a new platform directory structure without explicit instruction.

## Code changes workflow
- Make the smallest change that solves the problem.
- Do not reformat unrelated code.
- Do not rename symbols/files unless asked.
- Add comments where portability is non-obvious.

## Output expectation
- When proposing a change, briefly state:
  1) which files you changed,
  2) why it is portable across targets,
  3) what repo-local libraries you used.

## Completer Integration Rules

The `Completer` library (`cx/commandcompleter`) handles command selection via a status-driven loop. When integrating or modifying Completer usage:

- **NEVER hardcode knowledge of the command table into the input loop.** The loop must be blind to what commands exist, how many characters it takes to match, or what the outcome will be. It reacts to `getStatus()` and nothing else.
- **NEVER assume a specific number of processChar calls will reach a result.** Commands may be added, renamed, or restructured at any time.
- **The only correct pattern is a loop that reacts to status:** processChar/processTab/processEnter → check getStatus() → respond accordingly.
- **Only the addCandidate() setup knows the command structure.** The input loop does not.

## FORBIDDEN: Fuzzy / Dehyphenated Matching

**DO NOT implement, suggest, or reference any of the following:**
- Dehyphenated matching (removing hyphens before comparing)
- Fuzzy matching, abbreviated matching, or command shortening
- Matching "gl" to "goto-line", "sa" to "file-save-as", etc.
- A `dehyphenate()` function or any hyphen-stripping logic

**All matching in this project is LITERAL PREFIX matching.** The user's input must be an exact prefix of the candidate name, including hyphens. For example:
- "file" matches "file-save" (literal prefix) - CORRECT
- "fs" matches "file-save" (dehyphenated) - WRONG, DO NOT DO THIS

This applies to the Completer library, CommandTable, and all code in this project.

## ESC Command System

### Architecture
- `CommandTable.h/cpp` - static command table (data only, no matching logic)
- `Completer` library (`cx/commandcompleter`) - handles all prefix matching/completion
- `SheetEditor.h/cpp` - command input state machine uses Completer status-driven API

### How It Works
- ESC → "command> " prompt shows category prefixes
- Type a letter to narrow to a category, TAB completes, ENTER executes
- Freeform arg commands transition to argument input mode
- No-arg commands execute directly on ENTER

### Data Entry (direct typing in EDIT mode)
In EDIT mode, the user can simply start typing to enter data into the current cell.
**No ESC prefix required.** The first character determines the entry mode:

```
<letter>    Text mode - first character is a letter
<digit/+->  Number mode - first character is digit or +/-
$           Currency mode - $ followed by digits (formatted with $, commas, 2 decimals)
=           Formula mode (e.g., =A1+B2)
```

While entering data:
- The command line shows the current input
- ENTER commits the value to the cell and returns to EDIT mode
- ESC cancels the entry and returns to EDIT mode (cell unchanged)

### Command Categories

**file-** (v1 scope)
```
file-load       <filename>    Load spreadsheet from JSON
file-save       [filename]    Save current sheet
file-save-as    <filename>    Save to new file
file-new        [filename]    Create new empty sheet
file-quit                     Exit ss
```

**edit-** (cell operations)
```
edit-cut                      Cut selected cell(s) to buffer
edit-copy                     Copy selected cell(s) to buffer
edit-paste                    Paste from buffer
edit-clear                    Clear cell contents (keep formatting)
edit-delete                   Delete cell contents and formatting
```

**goto-**
```
goto-cell       <cell>        Jump to cell (e.g., goto-cell A:100)
```

**insert-** (row/column manipulation)
```
insert-row                    Insert row above cursor
insert-column                 Insert column before cursor
insert-symbol   <symbol>      Insert box drawing symbol (for symbol fill cells)
```

**delete-** (row/column removal)
```
delete-row                    Delete current row
delete-column                 Delete current column
```

**format-** (cell formatting)
```
format-width    <columns>     Set column width
format-align    <left|right|center>
format-decimal  <places>      Set decimal places for numbers
format-bold                   Toggle bold
format-color    <color>       Set text color
```

**view-**
```
view-help                     Show help screen
view-split                    Split to show two regions
view-unsplit                  Return to single view
view-formulas                 Toggle showing formulas vs values
```

**sheet-** (multi-sheet, future)
```
sheet-new                     Add new sheet
sheet-rename    <name>        Rename current sheet
sheet-delete                  Delete current sheet
sheet-next                    Go to next sheet
sheet-prev                    Go to previous sheet
```

### Cell Input Modes (triggered by first character in EDIT mode)
```
=           Formula mode - enter formulas like =A1+B2
$           Currency mode - numbers displayed with $ and commas
<digit/+->  Number mode - plain numeric entry
<letter>    Text mode - literal text entry
```

### Cell Hunt Mode
When in formula mode (after typing =), pressing ESC enters cell hunt mode for selecting cell references:

**Entering cell hunt mode:**
- Cursor jumps back to the cell where formula entry started
- The formula display updates in real-time as you navigate

**Navigation and selection:**
- Arrow keys move the highlight cell-to-cell
- The current cell reference appears in the formula in real-time as you move
- **SPACE** sets the start of a range (cell is anchored)
- After SPACE, arrow keys extend the range (e.g., `$A$1:$C$3`)
- **ENTER** finalizes the selection (single cell or range) and returns to formula editing
- **ESC** cancels cell hunt mode, discards any selection, returns to formula editing

**Reference format:**
- All references inserted via cell hunt are **absolute by default** (e.g., `$A$1`, `$A$1:$C$3`)
- User must manually edit the formula to change to relative references (remove `$` signs)

**Example flow:**
1. User types `=` to enter formula mode, types `SUM(`
2. User presses ESC to enter cell hunt mode
3. User navigates to A1, formula shows `=SUM($A$1`
4. User presses SPACE to anchor range start
5. User navigates to A10, formula shows `=SUM($A$1:$A$10`
6. User presses ENTER, returns to formula mode with `=SUM($A$1:$A$10`
7. User types `)` to complete: `=SUM($A$1:$A$10)`

## Key Libraries

### sheetModel (cx/sheetModel)
The foundational data model for ss:
- **CxSheetModel** - main spreadsheet model (grid, cursor, JSON persistence)
- **CxSheetCell** - cell types: EMPTY, TEXT, DOUBLE, FORMULA
- **CxSheetCellCoordinate** - cell addressing (A1, $C$6, relative/absolute)
- **CxSheetDependencyGraph** - formula recalculation ordering
- **CxSheetVariableDatabase** - resolves cell references in formulas

**IMPORTANT: sheetModel is a pure compute structure.** It contains NO visual attributes (column widths, colors, fonts, etc.). Visual attributes are:
- Stored in the app layer (SheetView, SheetEditor)
- Persisted via `sheetModel->getAppData()`/`setAppData()` which preserves unknown JSON keys
- This separation allows sheetModel to be used by non-visual tools (CLI processors, automation)

### expression (cx/expression)
- **CxExpression** - parses and evaluates formulas
- Uses CxSheetVariableDatabase for cell reference resolution

### Other cx libraries used
- **keyboard** - CxKeyboard, CxKeyAction for terminal input
- **screen** - CxScreen for terminal output, colors, cursor control
- **commandcompleter** - Completer for command completion
- **json** - JSON parsing for file load/save
- **base** - CxString, CxHashmap, CxSList, etc.
- **functor** - CxFunctor, CxDeferCall for callbacks

## UTF-8 Box Drawing
ss uses UTF-8 box drawing characters for cell borders and symbol fills:
```
┌──────────────────────────┐
│                          │
└──────────────────────────┘
```

Symbol fill cell types adapt to cell width automatically.
