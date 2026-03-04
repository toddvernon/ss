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

    //--- format- -------------------------------------------------------------
    { "format-width",
      "<+n|-n>",
      "Adjust column width (+n wider, -n narrower)",
      CMD_FLAG_NEEDS_ARG,
      &SheetEditor::CMD_FormatWidth,
      NULL },

    { "format-width-auto",
      NULL,
      "Auto-fit column width to content",
      0,
      &SheetEditor::CMD_FormatWidthAuto,
      NULL },

    //--- insert- -------------------------------------------------------------
    { "insert-symbol",
      "<type>",
      "Insert box drawing symbol",
      CMD_FLAG_NEEDS_ARG | CMD_FLAG_SYMBOL_ARG,
      &SheetEditor::CMD_InsertSymbol,
      NULL },

    //--- end -----------------------------------------------------------------
    { NULL, NULL, NULL, 0, NULL, NULL }
};
