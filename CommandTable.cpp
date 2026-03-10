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

    { "edit-fill-down",
      NULL,
      "Fill selection down from first row",
      0,
      &SheetEditor::CMD_FillDown,
      NULL },

    { "edit-fill-right",
      NULL,
      "Fill selection right from first column",
      0,
      &SheetEditor::CMD_FillRight,
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

    //--- modify-col-align- (column alignment defaults) ---------------------------
    { "modify-col-align-left",
      NULL,
      "Set column default alignment to left",
      0,
      &SheetEditor::CMD_FormatColAlignLeft,
      NULL },

    { "modify-col-align-center",
      NULL,
      "Set column default alignment to center",
      0,
      &SheetEditor::CMD_FormatColAlignCenter,
      NULL },

    { "modify-col-align-right",
      NULL,
      "Set column default alignment to right",
      0,
      &SheetEditor::CMD_FormatColAlignRight,
      NULL },

    //--- modify-col-number- (column number format defaults) ----------------------
    { "modify-col-number-currency",
      NULL,
      "Toggle column default currency format ($)",
      0,
      &SheetEditor::CMD_FormatColNumberCurrency,
      NULL },

    { "modify-col-number-decimal",
      "<n>",
      "Set column default decimal places (0-10)",
      CMD_FLAG_NEEDS_ARG,
      &SheetEditor::CMD_FormatColNumberDecimal,
      NULL },

    { "modify-col-number-percent",
      NULL,
      "Toggle column default percent format (%)",
      0,
      &SheetEditor::CMD_FormatColNumberPercent,
      NULL },

    { "modify-col-number-thousands",
      NULL,
      "Toggle column default thousands separators (,)",
      0,
      &SheetEditor::CMD_FormatColNumberThousands,
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

    //--- modify-cell-color- (cell color) ----------------------------------------
    { "modify-cell-color-foreground",
      NULL,
      "Set cell text color",
      CMD_FLAG_COLOR_PICKER_FG,
      &SheetEditor::CMD_FormatCellColorForeground,
      NULL },

    { "modify-cell-color-background",
      NULL,
      "Set cell background color",
      CMD_FLAG_COLOR_PICKER_BG,
      &SheetEditor::CMD_FormatCellColorBackground,
      NULL },

    //--- modify-col-color- (column color defaults) -----------------------------
    { "modify-col-color-foreground",
      NULL,
      "Set column default text color",
      CMD_FLAG_COLOR_PICKER_FG | CMD_FLAG_COLUMN_DEFAULT,
      &SheetEditor::CMD_FormatColColorForeground,
      NULL },

    { "modify-col-color-background",
      NULL,
      "Set column default background color",
      CMD_FLAG_COLOR_PICKER_BG | CMD_FLAG_COLUMN_DEFAULT,
      &SheetEditor::CMD_FormatColColorBackground,
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
