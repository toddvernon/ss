//-------------------------------------------------------------------------------------------------
//
//  CommandTable.h
//  ss (spreadsheet)
//
//  Copyright 2022-2025 Todd Vernon. All rights reserved.
//  Licensed under the Apache License, Version 2.0
//  See LICENSE file for details.
//
//  Static command table definitions for ESC commands.
//
//-------------------------------------------------------------------------------------------------

#ifndef _CommandTable_h_
#define _CommandTable_h_

#include <cx/base/string.h>

//-------------------------------------------------------------------------------------------------
// Command flags
//-------------------------------------------------------------------------------------------------
#define CMD_FLAG_NEEDS_ARG      0x01    // command requires an argument
#define CMD_FLAG_OPTIONAL_ARG   0x02    // argument is optional
#define CMD_FLAG_SYMBOL_ARG     0x04    // argument is a UTF symbol (uses child completer)

//-------------------------------------------------------------------------------------------------
// Command handler function type
//-------------------------------------------------------------------------------------------------
class SheetEditor;
typedef void (SheetEditor::*CommandHandler)(CxString arg);

//-------------------------------------------------------------------------------------------------
// CommandEntry - defines a single command
//-------------------------------------------------------------------------------------------------
struct CommandEntry
{
    const char *name;           // "file-load", "file-save", "file-quit"
    const char *argHint;        // hint for argument: "<filename>", or NULL
    const char *description;    // "Load spreadsheet from file"
    int flags;                  // CMD_FLAG_*
    CommandHandler handler;     // pointer to member function
    const char *symbolFilter;   // symbol filter prefix or NULL
};

//-------------------------------------------------------------------------------------------------
// Free-standing command table (NULL-terminated)
//-------------------------------------------------------------------------------------------------
extern CommandEntry commandTable[];

#endif
