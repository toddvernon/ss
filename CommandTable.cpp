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
//  naming convention (file-, edit-, format-, view-, etc.).
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

    //--- format-col- (column operations) ----------------------------------------
    { "format-col-width",
      "<+n|-n>",
      "Adjust column width (+n wider, -n narrower)",
      CMD_FLAG_NEEDS_ARG,
      &SheetEditor::CMD_FormatColWidth,
      NULL },

    { "format-col-fit",
      NULL,
      "Auto-fit column width to content",
      0,
      &SheetEditor::CMD_FormatColFit,
      NULL },

    //--- format-cell-align- (cell alignment) -------------------------------------
    { "format-cell-align-left",
      NULL,
      "Left-align cell contents",
      0,
      &SheetEditor::CMD_FormatCellAlignLeft,
      NULL },

    { "format-cell-align-center",
      NULL,
      "Center cell contents",
      0,
      &SheetEditor::CMD_FormatCellAlignCenter,
      NULL },

    { "format-cell-align-right",
      NULL,
      "Right-align cell contents",
      0,
      &SheetEditor::CMD_FormatCellAlignRight,
      NULL },

    //--- format-cell-number- (number formatting) ---------------------------------
    { "format-cell-number-currency",
      NULL,
      "Toggle currency format ($)",
      0,
      &SheetEditor::CMD_FormatCellNumberCurrency,
      NULL },

    { "format-cell-number-decimal",
      "<n>",
      "Set decimal places (0-10)",
      CMD_FLAG_NEEDS_ARG,
      &SheetEditor::CMD_FormatCellNumberDecimal,
      NULL },

    { "format-cell-number-percent",
      NULL,
      "Toggle percent format (%)",
      0,
      &SheetEditor::CMD_FormatCellNumberPercent,
      NULL },

    { "format-cell-number-thousands",
      NULL,
      "Toggle thousands separators (,)",
      0,
      &SheetEditor::CMD_FormatCellNumberThousands,
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

    //--- end -----------------------------------------------------------------
    { NULL, NULL, NULL, 0, NULL, NULL }
};
