//-------------------------------------------------------------------------------------------------
//
//  CommandTable.cpp
//  ss (spreadsheet)
//
//  Copyright 2022-2025 Todd Vernon. All rights reserved.
//  Licensed under the Apache License, Version 2.0
//  See LICENSE file for details.
//
//  Static command table defining all ESC commands. Commands follow a category-action
//  naming convention (file-, edit-, modify-, view-, etc.).
//
//-------------------------------------------------------------------------------------------------

#include "CommandTable.h"
#include "SheetEditor.h"

//-------------------------------------------------------------------------------------------------
// Command table
//
// Commands are registered into Completer objects at startup for literal prefix matching.
// Phase 1: Minimal command set - just file-quit for now.
//-------------------------------------------------------------------------------------------------
CommandEntry commandTable[] = {

    //--- file- ---------------------------------------------------------------
    { "file-load",
      NULL,
      "Load spreadsheet from file",
      1,
      &SheetEditor::CMD_Load,
      NULL },

    { "file-save",
      NULL,
      "Save spreadsheet to file",
      1,
      &SheetEditor::CMD_Save,
      NULL },

    { "file-quit",
      NULL,
      "Quit spreadsheet",
      0,
      &SheetEditor::CMD_Quit,
      NULL },

    //--- edit- ---------------------------------------------------------------
    { "edit-copy",
      NULL,
      "Copy selection to clipboard",
      0,
      &SheetEditor::CMD_Copy,
      NULL },

    { "edit-cut",
      NULL,
      "Cut selection to clipboard",
      0,
      &SheetEditor::CMD_Cut,
      NULL },

    { "edit-paste",
      NULL,
      "Paste from clipboard",
      0,
      &SheetEditor::CMD_Paste,
      NULL },

    { "edit-clear",
      NULL,
      "Clear cell contents",
      0,
      &SheetEditor::CMD_Clear,
      NULL },

    //--- modify-col- (column operations) ----------------------------------------
    { "modify-col-width",
      "<+n|-n>",
      "Adjust column width (+n wider, -n narrower)",
      CMD_FLAG_NEEDS_ARG,
      &SheetEditor::CMD_FormatColWidth,
      NULL },

    { "modify-col-fit",
      NULL,
      "Auto-fit column width to content",
      0,
      &SheetEditor::CMD_FormatColFit,
      NULL },

    //--- modify-cell-align- (cell alignment) -------------------------------------
    { "modify-cell-align-left",
      NULL,
      "Left-align cell contents",
      0,
      &SheetEditor::CMD_FormatCellAlignLeft,
      NULL },

    { "modify-cell-align-center",
      NULL,
      "Center cell contents",
      0,
      &SheetEditor::CMD_FormatCellAlignCenter,
      NULL },

    { "modify-cell-align-right",
      NULL,
      "Right-align cell contents",
      0,
      &SheetEditor::CMD_FormatCellAlignRight,
      NULL },

    //--- modify-cell-number- (number formatting) ---------------------------------
    { "modify-cell-number-currency",
      NULL,
      "Toggle currency format ($)",
      0,
      &SheetEditor::CMD_FormatCellNumberCurrency,
      NULL },

    { "modify-cell-number-decimal",
      "<n>",
      "Set decimal places (0-10)",
      CMD_FLAG_NEEDS_ARG,
      &SheetEditor::CMD_FormatCellNumberDecimal,
      NULL },

    { "modify-cell-number-percent",
      NULL,
      "Toggle percent format (%)",
      0,
      &SheetEditor::CMD_FormatCellNumberPercent,
      NULL },

    { "modify-cell-number-thousands",
      NULL,
      "Toggle thousands separators (,)",
      0,
      &SheetEditor::CMD_FormatCellNumberThousands,
      NULL },

    //--- modify-cell-text- (text formatting) ----------------------------------
    { "modify-cell-text-wide",
      NULL,
      "Toggle wide text spacing (F U N D)",
      0,
      &SheetEditor::CMD_FormatCellTextWide,
      NULL },

    //--- insert- -------------------------------------------------------------
    { "insert-symbol",
      "<type>",
      "Insert box drawing symbol",
      CMD_FLAG_NEEDS_ARG | CMD_FLAG_SYMBOL_ARG,
      &SheetEditor::CMD_InsertSymbol,
      NULL },

    { "insert-row-before",
      NULL,
      "Insert row before cursor",
      0,
      &SheetEditor::CMD_InsertRow,
      NULL },

    { "insert-column-before",
      NULL,
      "Insert column before cursor",
      0,
      &SheetEditor::CMD_InsertColumn,
      NULL },

    //--- delete- -------------------------------------------------------------
    { "delete-row",
      NULL,
      "Delete current row",
      0,
      &SheetEditor::CMD_DeleteRow,
      NULL },

    { "delete-column",
      NULL,
      "Delete current column",
      0,
      &SheetEditor::CMD_DeleteColumn,
      NULL },

    //--- quit- ---------------------------------------------------------------
    { "quit-save",
      NULL,
      "Save and quit",
      0,
      &SheetEditor::CMD_QuitSave,
      NULL },

    { "quit-nosave",
      NULL,
      "Quit without saving",
      0,
      &SheetEditor::CMD_QuitWithoutSave,
      NULL },

    //--- end -----------------------------------------------------------------
    { NULL, NULL, NULL, 0, NULL, NULL }
};
