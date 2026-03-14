//-------------------------------------------------------------------------------------------------
//
//  ClaudeHandler.h
//  ss (spreadsheet)
//
//  Copyright 2022-2025 Todd Vernon. All rights reserved.
//  Licensed under the Apache License, Version 2.0
//  See LICENSE file for details.
//
//  ClaudeHandler - Anthropic API client using curl subprocess for streaming.
//  Manages conversation history, SSE parsing, and tool execution.
//
//-------------------------------------------------------------------------------------------------

#include <stdio.h>

#include <cx/base/string.h>
#include <cx/base/slist.h>
#include <cx/buildoutput/buildoutput.h>

#ifndef _ClaudeHandler_h_
#define _ClaudeHandler_h_

// Forward declarations
class SheetEditor;


//-------------------------------------------------------------------------------------------------
//
// ClaudeHandler
//
// Handles communication with the Anthropic Claude API via curl subprocess.
// Uses BuildOutput for non-blocking I/O with idle callback polling.
// Manages conversation history (session-persistent) and tool execution.
//
//-------------------------------------------------------------------------------------------------

class ClaudeHandler {

  public:

    ClaudeHandler(SheetEditor *editor);
    ~ClaudeHandler(void);

    void setApiKey(CxString key);
    // set the API key (from .ssrc or env var)

    int hasApiKey(void);
    // returns 1 if API key is configured

    void sendMessage(CxString userMessage);
    // build request JSON and start curl subprocess

    int poll(void);
    // read available SSE data from curl subprocess
    // returns 1 if display needs updating, 0 otherwise

    int isRunning(void);
    // returns 1 if curl subprocess is running

    int needsRedraw(void);
    // returns 1 if display has changed since last redraw

    void clearNeedsRedraw(void);
    // clear the redraw flag

    CxSList<CxString*>* getDisplayLines(void);
    // get the display buffer for ClaudeView

    int getDisplayLineCount(void);
    // get number of display lines

    void clearHistory(void);
    // clear conversation history (but not display)

    void clearDisplayLines(void);
    // clear display buffer

  private:

    // History management
    void trimHistory(void);
    // remove old messages from front of history to stay under token limits

    // Logging
    void logWrite(CxString text);
    // append text to /tmp/ss_claude.log

    void logWriteJSON(CxString json);
    // pretty-print JSON and append to log

    // JSON building helpers
    CxString escapeJSON(CxString text);
    // escape a string for inclusion in JSON

    CxString buildSystemPrompt(void);
    // build the system prompt describing the spreadsheet context

    CxString buildToolDefinitions(void);
    // build the tools array JSON

    CxString buildMessagesJSON(void);
    // build the messages array from conversation history

    CxString buildRequestJSON(CxString userMessage);
    // build the complete API request JSON

    // SSE parsing
    void processSSELine(CxString line);
    // process a single SSE data line

    void processSSEEvent(CxString jsonStr);
    // process a parsed SSE JSON event

    // Tool execution
    CxString executeTool(CxString toolName, CxString toolInput);
    // execute a tool and return the result as JSON string

    void sendToolResult(void);
    // send a continuation request with tool result

    // Consolidated tool dispatchers
    CxString toolReadCells(CxString input);
    CxString toolWriteCells(CxString input);
    CxString toolModify(CxString input);
    CxString toolRows(CxString input);
    CxString toolColumns(CxString input);

    // Tool handlers
    CxString toolReadCell(CxString input);
    CxString toolWriteCell(CxString input);
    CxString toolReadRange(CxString input);
    CxString toolWriteRange(CxString input);
    CxString toolGetSheetInfo(CxString input);
    CxString toolSetFormat(CxString input);
    CxString toolSetColumnWidth(CxString input);
    CxString toolInsertRow(CxString input);
    CxString toolDeleteRow(CxString input);
    CxString toolInsertColumn(CxString input);
    CxString toolDeleteColumn(CxString input);
    CxString toolGotoCell(CxString input);
    CxString toolHideRows(CxString input);
    CxString toolHideColumns(CxString input);
    CxString toolShowRows(CxString input);
    CxString toolShowColumns(CxString input);
    CxString toolShowAll(CxString input);

    // Display helpers
    void addDisplayLine(CxString line);
    // add a line to the display buffer (with word wrapping)

    void addWrappedLines(CxString text);
    // word-wrap text to terminal width and add to display

    SheetEditor *_editor;
    BuildOutput _buildOutput;       // async curl subprocess
    CxString _apiKey;               // from .ssrc or ANTHROPIC_API_KEY env var

    // Conversation history - stored as raw JSON message objects
    CxSList<CxString*> _messageHistory;

    // Streaming state
    CxString _currentResponseText;  // accumulated streaming text
    CxString _sseBuffer;            // partial SSE line buffer
    int _needsRedraw;
    int _lastProcessedLine;         // index of last BuildOutput line processed

    // Tool use state machine
    int _inToolUse;
    CxString _toolUseId;
    CxString _toolUseName;
    CxString _toolInputAccum;       // accumulated input_json_delta chunks

    // Full assistant response content blocks (for history)
    CxString _assistantContentJSON;  // accumulated content blocks JSON array

    // Display buffer - formatted lines for ClaudeView
    CxSList<CxString*> _displayLines;
    int _displayLineCount;
};


#endif
