//-------------------------------------------------------------------------------------------------
//
//  ClaudeHandler.cpp
//  ss (spreadsheet)
//
//  Copyright 2022-2025 Todd Vernon. All rights reserved.
//  Licensed under the Apache License, Version 2.0
//  See LICENSE file for details.
//
//  ClaudeHandler implementation - Anthropic API client via curl subprocess.
//
//-------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cx/json/json_factory.h>
#include <cx/json/json_object.h>
#include <cx/json/json_member.h>
#include <cx/json/json_string.h>
#include <cx/json/json_number.h>
#include <cx/json/json_boolean.h>
#include <cx/json/json_array.h>
#include <cx/sheetModel/sheetInputParser.h>

#include "ClaudeHandler.h"
#include "SheetEditor.h"


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::ClaudeHandler
//-------------------------------------------------------------------------------------------------
ClaudeHandler::ClaudeHandler(SheetEditor *editor)
: _editor(editor)
, _needsRedraw(0)
, _lastProcessedLine(0)
, _inToolUse(0)
, _displayLineCount(0)
{
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::~ClaudeHandler
//-------------------------------------------------------------------------------------------------
ClaudeHandler::~ClaudeHandler(void)
{
    // Free display lines
    for (int i = 0; i < (int)_displayLines.entries(); i++) {
        CxString *line = _displayLines.at(i);
        if (line != NULL) {
            delete line;
        }
    }
    _displayLines.clear();

    // Free message history
    for (int i = 0; i < (int)_messageHistory.entries(); i++) {
        CxString *msg = _messageHistory.at(i);
        if (msg != NULL) {
            delete msg;
        }
    }
    _messageHistory.clear();
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::setApiKey
//-------------------------------------------------------------------------------------------------
void
ClaudeHandler::setApiKey(CxString key)
{
    _apiKey = key;
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::hasApiKey
//-------------------------------------------------------------------------------------------------
int
ClaudeHandler::hasApiKey(void)
{
    return _apiKey.length() > 0 ? 1 : 0;
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::isRunning
//-------------------------------------------------------------------------------------------------
int
ClaudeHandler::isRunning(void)
{
    return _buildOutput.isRunning();
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::needsRedraw
//-------------------------------------------------------------------------------------------------
int
ClaudeHandler::needsRedraw(void)
{
    return _needsRedraw;
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::clearNeedsRedraw
//-------------------------------------------------------------------------------------------------
void
ClaudeHandler::clearNeedsRedraw(void)
{
    _needsRedraw = 0;
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::getDisplayLines
//-------------------------------------------------------------------------------------------------
CxSList<CxString*>*
ClaudeHandler::getDisplayLines(void)
{
    return &_displayLines;
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::getDisplayLineCount
//-------------------------------------------------------------------------------------------------
int
ClaudeHandler::getDisplayLineCount(void)
{
    return _displayLineCount;
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::clearHistory
//-------------------------------------------------------------------------------------------------
void
ClaudeHandler::clearHistory(void)
{
    for (int i = 0; i < (int)_messageHistory.entries(); i++) {
        CxString *msg = _messageHistory.at(i);
        if (msg != NULL) {
            delete msg;
        }
    }
    _messageHistory.clear();
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::clearDisplayLines
//-------------------------------------------------------------------------------------------------
void
ClaudeHandler::clearDisplayLines(void)
{
    for (int i = 0; i < (int)_displayLines.entries(); i++) {
        CxString *line = _displayLines.at(i);
        if (line != NULL) {
            delete line;
        }
    }
    _displayLines.clear();
    _displayLineCount = 0;
    _needsRedraw = 1;
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::logWrite
//
// Append text to /tmp/ss_claude.log for debugging the API dialog.
//-------------------------------------------------------------------------------------------------
void
ClaudeHandler::logWrite(CxString text)
{
    FILE *f = fopen("/tmp/ss_claude.log", "a");
    if (f != NULL) {
        fwrite(text.data(), 1, text.length(), f);
        fwrite("\n", 1, 1, f);
        fclose(f);
    }
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::logWriteJSON
//
// Pretty-print JSON string to the log file with indentation.
//-------------------------------------------------------------------------------------------------
void
ClaudeHandler::logWriteJSON(CxString json)
{
    FILE *f = fopen("/tmp/ss_claude.log", "a");
    if (f == NULL) return;

    int indent = 0;
    int inString = 0;
    int escaped = 0;

    for (int i = 0; i < json.length(); i++) {
        char c = json.data()[i];

        if (escaped) {
            fputc(c, f);
            escaped = 0;
            continue;
        }

        if (c == '\\' && inString) {
            fputc(c, f);
            escaped = 1;
            continue;
        }

        if (c == '"') {
            inString = !inString;
            fputc(c, f);
            continue;
        }

        if (inString) {
            fputc(c, f);
            continue;
        }

        if (c == '{' || c == '[') {
            fputc(c, f);
            indent += 2;
            fputc('\n', f);
            for (int s = 0; s < indent; s++) fputc(' ', f);
        }
        else if (c == '}' || c == ']') {
            indent -= 2;
            if (indent < 0) indent = 0;
            fputc('\n', f);
            for (int s = 0; s < indent; s++) fputc(' ', f);
            fputc(c, f);
        }
        else if (c == ',') {
            fputc(c, f);
            fputc('\n', f);
            for (int s = 0; s < indent; s++) fputc(' ', f);
        }
        else if (c == ':') {
            fputc(c, f);
            fputc(' ', f);
        }
        else if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            fputc(c, f);
        }
    }

    fputc('\n', f);
    fclose(f);
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::escapeJSON
//
// Escape a string for inclusion in a JSON string value.
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::escapeJSON(CxString text)
{
    CxString escaped = "";
    for (unsigned long i = 0; i < text.length(); i++) {
        char c = text.data()[i];
        switch (c) {
            case '"':  escaped.append("\\\""); break;
            case '\\': escaped.append("\\\\"); break;
            case '\n': escaped.append("\\n"); break;
            case '\r': escaped.append("\\r"); break;
            case '\t': escaped.append("\\t"); break;
            case '\b': escaped.append("\\b"); break;
            case '\f': escaped.append("\\f"); break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    sprintf(buf, "\\u%04x", (unsigned char)c);
                    escaped.append(buf);
                } else {
                    escaped.append(c);
                }
                break;
        }
    }
    return escaped;
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::addDisplayLine
//-------------------------------------------------------------------------------------------------
void
ClaudeHandler::addDisplayLine(CxString line)
{
    CxString *linePtr = new CxString(line);
    _displayLines.append(linePtr);
    _displayLineCount = (int)_displayLines.entries();
    _needsRedraw = 1;
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::addWrappedLines
//
// Word-wrap text to terminal width and add each line to display buffer.
//-------------------------------------------------------------------------------------------------
void
ClaudeHandler::addWrappedLines(CxString text)
{
    int maxWidth = _editor->screen->cols() - 1;
    if (maxWidth < 20) maxWidth = 20;

    // Split by newlines first
    int start = 0;
    for (int i = 0; i <= (int)text.length(); i++) {
        if (i == (int)text.length() || text.data()[i] == '\n') {
            CxString segment;
            if (i > start) {
                segment = text.subString(start, i - start);
            }
            start = i + 1;

            // Wrap this segment if too long
            if ((int)segment.length() <= maxWidth) {
                addDisplayLine(segment);
            } else {
                int pos = 0;
                while (pos < (int)segment.length()) {
                    int remaining = (int)segment.length() - pos;
                    int lineLen = (remaining <= maxWidth) ? remaining : maxWidth;

                    // Try to break at a space
                    if (lineLen < remaining) {
                        int breakAt = -1;
                        for (int j = pos + lineLen - 1; j >= pos + (lineLen / 2); j--) {
                            if (segment.data()[j] == ' ') {
                                breakAt = j;
                                break;
                            }
                        }
                        if (breakAt > 0) {
                            lineLen = breakAt - pos + 1;
                        }
                    }

                    addDisplayLine(segment.subString(pos, lineLen));
                    pos += lineLen;
                }
            }
        }
    }
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::buildSystemPrompt
//
// Build system prompt that describes the spreadsheet context and tool usage.
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::buildSystemPrompt(void)
{
    CxString prompt = "You are an AI assistant embedded in a terminal spreadsheet application "
        "called ss. You can read and modify the spreadsheet using tools. "
        "Keep responses concise - the display area is small (6 lines visible). "
        "When the user asks you to do something, do it immediately. "
        "Read cells if needed to understand context, then make the changes in the same turn. "
        "Do not describe what you see and ask what to do - just complete the task. "
        "When done, briefly confirm what you did. "
        "Cell addresses use spreadsheet notation: columns are letters (A-ZZ), rows are 1-based numbers. "
        "For single cell writes, use the formula parameter WITHOUT the leading = sign "
        "(e.g., formula: \"A1+B1\" not \"=A1+B1\"). "
        "For range writes (values array), prefix formulas with = "
        "(e.g., \"=A1+B1\" in the values array). "
        "Prefer write_range over multiple write_cell calls when writing several cells.";
    return prompt;
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::buildToolDefinitions
//
// Build the JSON tools array for the API request.
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::buildToolDefinitions(void)
{
    CxString tools = "[";

    // read_cells - read one cell or a range
    tools.append("{\"name\":\"read_cells\",\"description\":\"Read cells. One cell: set cell. Range: set start and end.\",");
    tools.append("\"input_schema\":{\"type\":\"object\",\"properties\":{");
    tools.append("\"cell\":{\"type\":\"string\",\"description\":\"Single cell like A1\"},");
    tools.append("\"start\":{\"type\":\"string\",\"description\":\"Range start like A1\"},");
    tools.append("\"end\":{\"type\":\"string\",\"description\":\"Range end like C10\"}");
    tools.append("}}}");

    // write_cells - write one cell or a range
    tools.append(",{\"name\":\"write_cells\",\"description\":\"Write cells. Single: set cell + value or formula (no leading =). Range: set start + values (array of rows, prefix formulas with = e.g. '=A1+B1').\",");
    tools.append("\"input_schema\":{\"type\":\"object\",\"properties\":{");
    tools.append("\"cell\":{\"type\":\"string\",\"description\":\"Single cell like A1\"},");
    tools.append("\"value\":{\"description\":\"Text or number\"},");
    tools.append("\"formula\":{\"type\":\"string\",\"description\":\"Formula without = (e.g. A1+B1)\"},");
    tools.append("\"start\":{\"type\":\"string\",\"description\":\"Range start for bulk write\"},");
    tools.append("\"values\":{\"type\":\"array\",\"description\":\"Array of rows\",\"items\":{\"type\":\"array\",\"items\":{}}}");
    tools.append("}}}");

    // get_sheet_info
    tools.append(",{\"name\":\"get_sheet_info\",\"description\":\"Get sheet dimensions, cursor, filename\",");
    tools.append("\"input_schema\":{\"type\":\"object\",\"properties\":{}}}");

    // modify - format, column width, goto
    tools.append(",{\"name\":\"modify\",\"description\":\"Modify cell format, column width, or move cursor. Actions: format (set cell attr like currency/percent/align/bold/decimalPlaces), width (set column width), goto (move cursor).\",");
    tools.append("\"input_schema\":{\"type\":\"object\",\"properties\":{");
    tools.append("\"action\":{\"type\":\"string\",\"description\":\"format, width, or goto\"},");
    tools.append("\"cell\":{\"type\":\"string\",\"description\":\"Cell for format/goto\"},");
    tools.append("\"attr\":{\"type\":\"string\",\"description\":\"Format attribute name\"},");
    tools.append("\"value\":{\"type\":\"string\",\"description\":\"Attribute value, width value, etc.\"},");
    tools.append("\"column\":{\"type\":\"string\",\"description\":\"Column letter for width\"},");
    tools.append("\"width\":{\"type\":\"integer\",\"description\":\"Column width in chars\"}");
    tools.append("},\"required\":[\"action\"]}}");

    // rows - insert, delete, hide, show
    tools.append(",{\"name\":\"rows\",\"description\":\"Row operations: insert, delete, hide, show.\",");
    tools.append("\"input_schema\":{\"type\":\"object\",\"properties\":{");
    tools.append("\"action\":{\"type\":\"string\",\"description\":\"insert, delete, hide, or show\"},");
    tools.append("\"row\":{\"type\":\"integer\",\"description\":\"Row number (1-based)\"},");
    tools.append("\"end_row\":{\"type\":\"integer\",\"description\":\"End row for hide/show range\"}");
    tools.append("},\"required\":[\"action\",\"row\"]}}");

    // columns - insert, delete, hide, show
    tools.append(",{\"name\":\"columns\",\"description\":\"Column operations: insert, delete, hide, show.\",");
    tools.append("\"input_schema\":{\"type\":\"object\",\"properties\":{");
    tools.append("\"action\":{\"type\":\"string\",\"description\":\"insert, delete, hide, or show\"},");
    tools.append("\"column\":{\"type\":\"string\",\"description\":\"Column letter like A, B, AA\"},");
    tools.append("\"end_column\":{\"type\":\"string\",\"description\":\"End column for hide/show range\"}");
    tools.append("},\"required\":[\"action\",\"column\"]}}");

    // show_all (with cache_control on last tool for prompt caching)
    tools.append(",{\"name\":\"show_all\",\"description\":\"Unhide all rows and columns.\",");
    tools.append("\"input_schema\":{\"type\":\"object\",\"properties\":{}},");
    tools.append("\"cache_control\":{\"type\":\"ephemeral\"}}");

    tools.append("]");
    return tools;
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::trimHistory
//
// Remove old messages from the front of history to keep token count manageable.
// Called before each new user message (not during tool continuations, so the
// current tool chain is never broken).
//
// Strategy: if history exceeds 20 messages, remove the oldest entries until
// we're at 20. Then ensure the first remaining message has role "user" and is
// not a tool_result — if it's not, keep removing until we find one.
//-------------------------------------------------------------------------------------------------
void
ClaudeHandler::trimHistory(void)
{
    int maxMessages = 20;
    int count = (int)_messageHistory.entries();

    if (count <= maxMessages) return;

    // Remove oldest entries to get under the limit
    int removeCount = count - maxMessages;

    // After removing, ensure the first remaining message is a real user message.
    // Scan forward from the cut point to find one.
    while (removeCount < count) {
        CxString *msg = _messageHistory.at(removeCount);
        if (msg != NULL && msg->length() > 15 &&
            msg->subString(0, 15) == "{\"role\":\"user\"," &&
            msg->index("tool_result") < 0) {
            break;  // found a real user message
        }
        removeCount++;
    }

    // Safety: don't remove everything
    if (removeCount >= count) return;

    // Physically remove entries from the front
    for (int i = 0; i < removeCount; i++) {
        CxString *msg = _messageHistory.at(0);
        if (msg != NULL) delete msg;
        _messageHistory.removeAt(0);
    }
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::buildMessagesJSON
//
// Build the messages array from conversation history.
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::buildMessagesJSON(void)
{
    CxString messages = "[";
    for (int i = 0; i < (int)_messageHistory.entries(); i++) {
        if (i > 0) messages.append(",");
        messages.append(*_messageHistory.at(i));
    }
    messages.append("]");
    return messages;
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::buildRequestJSON
//
// Build the complete API request JSON.
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::buildRequestJSON(CxString userMessage)
{
    CxString request = "{";

    // Model
    request.append("\"model\":\"claude-sonnet-4-20250514\",");

    // Max tokens
    request.append("\"max_tokens\":1024,");

    // Stream
    request.append("\"stream\":true,");

    // System prompt (as content block array with cache_control)
    request.append("\"system\":[{\"type\":\"text\",\"text\":\"");
    request.append(escapeJSON(buildSystemPrompt()));
    request.append("\",\"cache_control\":{\"type\":\"ephemeral\"}}],");

    // Tools
    request.append("\"tools\":");
    request.append(buildToolDefinitions());
    request.append(",");

    // Add user message to history
    CxString userMsg = "{\"role\":\"user\",\"content\":\"";
    userMsg.append(escapeJSON(userMessage));
    userMsg.append("\"}");
    _messageHistory.append(new CxString(userMsg));

    // Messages
    request.append("\"messages\":");
    request.append(buildMessagesJSON());

    request.append("}");
    return request;
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::sendMessage
//
// Build request JSON and start curl subprocess.
//-------------------------------------------------------------------------------------------------
void
ClaudeHandler::sendMessage(CxString userMessage)
{
    if (!hasApiKey()) {
        addDisplayLine("Error: No API key configured.");
        addDisplayLine("Set apiKey in .ssrc claude section or ANTHROPIC_API_KEY env var.");
        return;
    }

    // Trim old history before starting new exchange
    trimHistory();

    // Show user message in display
    addDisplayLine(CxString("> ") + userMessage);

    // Reset streaming state
    _currentResponseText = "";
    _sseBuffer = "";
    _inToolUse = 0;
    _toolUseId = "";
    _toolUseName = "";
    _toolInputAccum = "";
    _assistantContentJSON = "";

    // Build request
    CxString requestJSON = buildRequestJSON(userMessage);

    // Write request to temp file to avoid shell escaping issues
    CxString tmpPath = "/tmp/ss_claude_request.json";
    FILE *tmpFile = fopen(tmpPath.data(), "w");
    if (tmpFile == NULL) {
        addDisplayLine("Error: Could not create temp file for request.");
        return;
    }
    fwrite(requestJSON.data(), 1, requestJSON.length(), tmpFile);
    fclose(tmpFile);

    // Build curl command
    CxString cmd = "curl -s --no-buffer -N -X POST https://api.anthropic.com/v1/messages";
    cmd.append(" -H \"content-type: application/json\"");
    cmd.append(" -H \"x-api-key: ");
    cmd.append(_apiKey);
    cmd.append("\"");
    cmd.append(" -H \"anthropic-version: 2023-06-01\"");
    cmd.append(" -d @");
    cmd.append(tmpPath);
    cmd.append(" 2>&1");

    // Log outgoing request
    logWrite("=== USER MESSAGE ===");
    logWrite(CxString("User: ") + userMessage);
    logWrite("--- Request JSON ---");
    logWriteJSON(requestJSON);
    logWrite("--- End Request ---");

    // Start curl subprocess
    _buildOutput.clear();
    _lastProcessedLine = 0;
    _buildOutput.start(cmd);
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::poll
//
// Read available output from curl subprocess and process SSE events.
// Returns 1 if display needs updating.
//-------------------------------------------------------------------------------------------------
int
ClaudeHandler::poll(void)
{
    if (!_buildOutput.isRunning() && !_buildOutput.isComplete()) {
        return 0;
    }

    int hadUpdate = 0;

    if (_buildOutput.poll()) {
        hadUpdate = 1;

        // Process only new lines (not previously processed ones)
        int lineCount = _buildOutput.lineCount();
        for (int i = _lastProcessedLine; i < lineCount; i++) {
            BuildOutputLine *line = _buildOutput.lineAt(i);
            if (line == NULL) continue;

            CxString text = line->text;
            text.stripTrailing("\n\r");

            // Add to SSE buffer for partial line handling
            _sseBuffer.append(text);
            _sseBuffer.append("\n");
        }
        _lastProcessedLine = lineCount;
    }

    // Process complete SSE lines from buffer
    while (1) {
        int newlinePos = _sseBuffer.index("\n");
        if (newlinePos < 0) break;

        CxString line;
        if (newlinePos > 0) {
            line = _sseBuffer.subString(0, newlinePos);
        }
        _sseBuffer = _sseBuffer.subString(newlinePos + 1,
                                           _sseBuffer.length() - newlinePos - 1);

        if (line.length() > 0) {
            processSSELine(line);
        }
    }

    // Check if build completed (curl exited)
    if (_buildOutput.isComplete() || (_buildOutput.getState() == BUILD_ERROR)) {
        // Check for non-SSE error output (e.g., curl errors)
        if (_currentResponseText.length() == 0 && _buildOutput.lineCount() > 0) {
            // Might be a raw error message from curl or API
            BuildOutputLine *firstLine = _buildOutput.lineAt(0);
            if (firstLine != NULL) {
                CxString firstText = firstLine->text;
                // Check if it looks like a JSON error (not SSE)
                if (firstText.length() > 0 && firstText.data()[0] == '{') {
                    CxJSONBase *json = CxJSONFactory::parse(firstText);
                    if (json != NULL) {
                        CxJSONObject *obj = (CxJSONObject *)json;
                        CxJSONMember *errMember = obj->find("error");
                        if (errMember != NULL) {
                            CxJSONObject *errObj = (CxJSONObject *)errMember->object();
                            CxJSONMember *msgMember = errObj->find("message");
                            if (msgMember != NULL) {
                                CxString errMsg = ((CxJSONString *)msgMember->object())->get();
                                addDisplayLine(CxString("API error: ") + errMsg);
                            }
                        }
                        delete json;
                    }
                }
            }
        }
        _buildOutput.clear();
        hadUpdate = 1;
    }

    return hadUpdate;
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::processSSELine
//
// Process a single SSE line. SSE format: "data: {...}" or "event: ..."
//-------------------------------------------------------------------------------------------------
void
ClaudeHandler::processSSELine(CxString line)
{
    // Log raw SSE data
    if (line.length() > 0) {
        logWrite(CxString("SSE: ") + line);
    }

    // SSE lines starting with "data: " contain JSON
    if (line.length() > 6 && line.subString(0, 6) == "data: ") {
        CxString jsonStr = line.subString(6, line.length() - 6);
        processSSEEvent(jsonStr);
    }
    // Ignore "event:" lines and empty lines
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::processSSEEvent
//
// Process a parsed SSE JSON event from the streaming API.
//-------------------------------------------------------------------------------------------------
void
ClaudeHandler::processSSEEvent(CxString jsonStr)
{
    CxJSONBase *json = CxJSONFactory::parse(jsonStr);
    if (json == NULL) return;

    CxJSONObject *obj = (CxJSONObject *)json;
    CxJSONMember *typeMember = obj->find("type");
    if (typeMember == NULL) {
        delete json;
        return;
    }

    CxString eventType = ((CxJSONString *)typeMember->object())->get();

    //---------------------------------------------------------------------------------------------
    // content_block_start - beginning of a text or tool_use block
    //---------------------------------------------------------------------------------------------
    if (eventType == "content_block_start") {
        CxJSONMember *cbMember = obj->find("content_block");
        if (cbMember != NULL) {
            CxJSONObject *cb = (CxJSONObject *)cbMember->object();
            CxJSONMember *cbTypeMember = cb->find("type");
            if (cbTypeMember != NULL) {
                CxString cbType = ((CxJSONString *)cbTypeMember->object())->get();
                if (cbType == "tool_use") {
                    _inToolUse = 1;
                    _toolInputAccum = "";

                    CxJSONMember *idMember = cb->find("id");
                    if (idMember != NULL) {
                        _toolUseId = ((CxJSONString *)idMember->object())->get();
                    }
                    CxJSONMember *nameMember = cb->find("name");
                    if (nameMember != NULL) {
                        _toolUseName = ((CxJSONString *)nameMember->object())->get();
                    }

                    addDisplayLine(CxString("[") + _toolUseName + "...]");
                }
            }
        }
    }

    //---------------------------------------------------------------------------------------------
    // content_block_delta - streaming content chunk
    //---------------------------------------------------------------------------------------------
    else if (eventType == "content_block_delta") {
        CxJSONMember *deltaMember = obj->find("delta");
        if (deltaMember != NULL) {
            CxJSONObject *delta = (CxJSONObject *)deltaMember->object();
            CxJSONMember *deltaTypeMember = delta->find("type");
            if (deltaTypeMember != NULL) {
                CxString deltaType = ((CxJSONString *)deltaTypeMember->object())->get();

                if (deltaType == "text_delta") {
                    CxJSONMember *textMember = delta->find("text");
                    if (textMember != NULL) {
                        CxString text = ((CxJSONString *)textMember->object())->get();
                        _currentResponseText.append(text);
                        _needsRedraw = 1;
                    }
                }
                else if (deltaType == "input_json_delta") {
                    CxJSONMember *partialMember = delta->find("partial_json");
                    if (partialMember != NULL) {
                        CxString partial = ((CxJSONString *)partialMember->object())->get();
                        _toolInputAccum.append(partial);
                    }
                }
            }
        }
    }

    //---------------------------------------------------------------------------------------------
    // content_block_stop - end of a content block
    //---------------------------------------------------------------------------------------------
    else if (eventType == "content_block_stop") {
        if (_inToolUse) {
            // Build assistant content block for history
            CxString toolBlock = "{\"type\":\"tool_use\",\"id\":\"";
            toolBlock.append(escapeJSON(_toolUseId));
            toolBlock.append("\",\"name\":\"");
            toolBlock.append(escapeJSON(_toolUseName));
            toolBlock.append("\",\"input\":");
            if (_toolInputAccum.length() > 0) {
                toolBlock.append(_toolInputAccum);
            } else {
                toolBlock.append("{}");
            }
            toolBlock.append("}");

            // Accumulate content blocks
            if (_assistantContentJSON.length() > 0) {
                _assistantContentJSON.append(",");
            }
            _assistantContentJSON.append(toolBlock);

            // Execute the tool
            CxString toolResult = executeTool(_toolUseName, _toolInputAccum);

            // Add assistant message to history (with all content blocks so far)
            CxString assistantMsg = "{\"role\":\"assistant\",\"content\":[";
            if (_currentResponseText.length() > 0) {
                assistantMsg.append("{\"type\":\"text\",\"text\":\"");
                assistantMsg.append(escapeJSON(_currentResponseText));
                assistantMsg.append("\"},");
            }
            assistantMsg.append(_assistantContentJSON);
            assistantMsg.append("]}");
            _messageHistory.append(new CxString(assistantMsg));

            // Add tool result to history (full result - truncation happens at send time)
            CxString toolResultMsg = "{\"role\":\"user\",\"content\":[{\"type\":\"tool_result\",\"tool_use_id\":\"";
            toolResultMsg.append(escapeJSON(_toolUseId));
            toolResultMsg.append("\",\"content\":\"");
            toolResultMsg.append(escapeJSON(toolResult));
            toolResultMsg.append("\"}]}");
            _messageHistory.append(new CxString(toolResultMsg));

            _inToolUse = 0;

            // Send continuation request
            sendToolResult();
        }
        else {
            // Text block ended - show in display and accumulate for history
            if (_currentResponseText.length() > 0) {
                addWrappedLines(_currentResponseText);
                _needsRedraw = 1;

                CxString textBlock = "{\"type\":\"text\",\"text\":\"";
                textBlock.append(escapeJSON(_currentResponseText));
                textBlock.append("\"}");
                if (_assistantContentJSON.length() > 0) {
                    _assistantContentJSON.append(",");
                }
                _assistantContentJSON.append(textBlock);
                _currentResponseText = "";
            }
        }
    }

    //---------------------------------------------------------------------------------------------
    // message_stop - end of message
    //---------------------------------------------------------------------------------------------
    else if (eventType == "message_stop") {
        // Add final text to display
        if (_currentResponseText.length() > 0) {
            addWrappedLines(_currentResponseText);
        }

        // Add assistant message to history (if not already added by tool use)
        if (_assistantContentJSON.length() > 0 && !_inToolUse) {
            CxString assistantMsg = "{\"role\":\"assistant\",\"content\":[";
            assistantMsg.append(_assistantContentJSON);
            assistantMsg.append("]}");
            _messageHistory.append(new CxString(assistantMsg));
        }

        _currentResponseText = "";
        _assistantContentJSON = "";
        _needsRedraw = 1;
    }

    //---------------------------------------------------------------------------------------------
    // message_delta - check for stop_reason
    //---------------------------------------------------------------------------------------------
    else if (eventType == "message_delta") {
        CxJSONMember *deltaMember = obj->find("delta");
        if (deltaMember != NULL) {
            CxJSONObject *delta = (CxJSONObject *)deltaMember->object();
            CxJSONMember *stopMember = delta->find("stop_reason");
            if (stopMember != NULL) {
                CxString stopReason = ((CxJSONString *)stopMember->object())->get();
                if (stopReason == "end_turn") {
                    // Normal end - text will be finalized in message_stop
                }
            }
        }
    }

    //---------------------------------------------------------------------------------------------
    // error - API error in stream
    //---------------------------------------------------------------------------------------------
    else if (eventType == "error") {
        CxJSONMember *errMember = obj->find("error");
        if (errMember != NULL) {
            CxJSONObject *errObj = (CxJSONObject *)errMember->object();
            CxJSONMember *msgMember = errObj->find("message");
            if (msgMember != NULL) {
                CxString errMsg = ((CxJSONString *)msgMember->object())->get();
                addDisplayLine(CxString("Error: ") + errMsg);
            }
        }
    }

    delete json;
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::sendToolResult
//
// Send a continuation request after tool execution.
//-------------------------------------------------------------------------------------------------
void
ClaudeHandler::sendToolResult(void)
{
    // Reset streaming state for new response
    _currentResponseText = "";
    _sseBuffer = "";
    _inToolUse = 0;
    _assistantContentJSON = "";

    // Build request with full history (which now includes tool result)
    CxString request = "{";
    request.append("\"model\":\"claude-sonnet-4-20250514\",");
    request.append("\"max_tokens\":1024,");
    request.append("\"stream\":true,");
    request.append("\"system\":[{\"type\":\"text\",\"text\":\"");
    request.append(escapeJSON(buildSystemPrompt()));
    request.append("\",\"cache_control\":{\"type\":\"ephemeral\"}}],");
    request.append("\"tools\":");
    request.append(buildToolDefinitions());
    request.append(",");
    request.append("\"messages\":");
    request.append(buildMessagesJSON());
    request.append("}");

    // Log continuation request
    logWrite("=== TOOL CONTINUATION ===");
    logWrite("--- Request JSON ---");
    logWriteJSON(request);
    logWrite("--- End Request ---");

    // Write to temp file
    CxString tmpPath = "/tmp/ss_claude_request.json";
    FILE *tmpFile = fopen(tmpPath.data(), "w");
    if (tmpFile == NULL) {
        addDisplayLine("Error: Could not create temp file.");
        return;
    }
    fwrite(request.data(), 1, request.length(), tmpFile);
    fclose(tmpFile);

    // Build curl command
    CxString cmd = "curl -s --no-buffer -N -X POST https://api.anthropic.com/v1/messages";
    cmd.append(" -H \"content-type: application/json\"");
    cmd.append(" -H \"x-api-key: ");
    cmd.append(_apiKey);
    cmd.append("\"");
    cmd.append(" -H \"anthropic-version: 2023-06-01\"");
    cmd.append(" -d @");
    cmd.append(tmpPath);
    cmd.append(" 2>&1");

    _buildOutput.clear();
    _lastProcessedLine = 0;
    _buildOutput.start(cmd);
}


//-------------------------------------------------------------------------------------------------
// ClaudeHandler::executeTool
//
// Execute a tool by name and return the result as a string.
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::executeTool(CxString toolName, CxString toolInput)
{
    logWrite("=== TOOL CALL ===");
    logWrite(CxString("Tool: ") + toolName);
    logWrite("Input:");
    logWriteJSON(toolInput);

    CxString result;

    if (toolName == "read_cells")       result = toolReadCells(toolInput);
    else if (toolName == "write_cells") result = toolWriteCells(toolInput);
    else if (toolName == "get_sheet_info") result = toolGetSheetInfo(toolInput);
    else if (toolName == "modify")      result = toolModify(toolInput);
    else if (toolName == "rows")        result = toolRows(toolInput);
    else if (toolName == "columns")     result = toolColumns(toolInput);
    else if (toolName == "show_all")    result = toolShowAll(toolInput);
    else                                result = CxString("Unknown tool: ") + toolName;

    logWrite("Result:");
    logWriteJSON(result);
    logWrite("--- End Tool ---");

    return result;
}


//-------------------------------------------------------------------------------------------------
// Consolidated tool: read_cells
//
// Routes to toolReadCell (if "cell" param) or toolReadRange (if "start"/"end" params).
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::toolReadCells(CxString input)
{
    CxJSONBase *json = CxJSONFactory::parse(input);
    if (json == NULL) return "Error: invalid input JSON";

    CxJSONObject *obj = (CxJSONObject *)json;
    if (obj->find("start") != NULL) {
        delete json;
        return toolReadRange(input);
    }
    delete json;
    return toolReadCell(input);
}


//-------------------------------------------------------------------------------------------------
// Consolidated tool: write_cells
//
// Routes to toolWriteCell (if "cell" param) or toolWriteRange (if "start"/"values" params).
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::toolWriteCells(CxString input)
{
    CxJSONBase *json = CxJSONFactory::parse(input);
    if (json == NULL) return "Error: invalid input JSON";

    CxJSONObject *obj = (CxJSONObject *)json;
    if (obj->find("values") != NULL) {
        delete json;
        return toolWriteRange(input);
    }
    delete json;
    return toolWriteCell(input);
}


//-------------------------------------------------------------------------------------------------
// Consolidated tool: modify
//
// Routes based on "action": format → toolSetFormat, width → toolSetColumnWidth, goto → toolGotoCell
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::toolModify(CxString input)
{
    CxJSONBase *json = CxJSONFactory::parse(input);
    if (json == NULL) return "Error: invalid input JSON";

    CxJSONObject *obj = (CxJSONObject *)json;
    CxJSONMember *actionMember = obj->find("action");
    if (actionMember == NULL) { delete json; return "Error: missing action"; }

    CxString action = ((CxJSONString *)actionMember->object())->get();
    delete json;

    if (action == "format") return toolSetFormat(input);
    if (action == "width")  return toolSetColumnWidth(input);
    if (action == "goto")   return toolGotoCell(input);

    return CxString("Error: unknown action: ") + action;
}


//-------------------------------------------------------------------------------------------------
// Consolidated tool: rows
//
// Routes based on "action": insert, delete, hide, show
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::toolRows(CxString input)
{
    CxJSONBase *json = CxJSONFactory::parse(input);
    if (json == NULL) return "Error: invalid input JSON";

    CxJSONObject *obj = (CxJSONObject *)json;
    CxJSONMember *actionMember = obj->find("action");
    if (actionMember == NULL) { delete json; return "Error: missing action"; }

    CxString action = ((CxJSONString *)actionMember->object())->get();
    delete json;

    if (action == "insert") return toolInsertRow(input);
    if (action == "delete") return toolDeleteRow(input);
    if (action == "hide") {
        // Map row/end_row to start_row/end_row expected by toolHideRows
        CxJSONBase *json2 = CxJSONFactory::parse(input);
        CxJSONObject *obj2 = (CxJSONObject *)json2;
        CxJSONMember *rowMember = obj2->find("row");
        CxJSONMember *endMember = obj2->find("end_row");
        int startRow = (int)((CxJSONNumber *)rowMember->object())->get();
        int endRow = endMember != NULL ? (int)((CxJSONNumber *)endMember->object())->get() : startRow;
        delete json2;

        // Build input for toolHideRows
        char buf[128];
        sprintf(buf, "{\"start_row\":%d,\"end_row\":%d}", startRow, endRow);
        return toolHideRows(CxString(buf));
    }
    if (action == "show") {
        CxJSONBase *json2 = CxJSONFactory::parse(input);
        CxJSONObject *obj2 = (CxJSONObject *)json2;
        CxJSONMember *rowMember = obj2->find("row");
        CxJSONMember *endMember = obj2->find("end_row");
        int startRow = (int)((CxJSONNumber *)rowMember->object())->get();
        int endRow = endMember != NULL ? (int)((CxJSONNumber *)endMember->object())->get() : startRow;
        delete json2;

        char buf[128];
        sprintf(buf, "{\"start_row\":%d,\"end_row\":%d}", startRow, endRow);
        return toolShowRows(CxString(buf));
    }

    return CxString("Error: unknown action: ") + action;
}


//-------------------------------------------------------------------------------------------------
// Consolidated tool: columns
//
// Routes based on "action": insert, delete, hide, show
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::toolColumns(CxString input)
{
    CxJSONBase *json = CxJSONFactory::parse(input);
    if (json == NULL) return "Error: invalid input JSON";

    CxJSONObject *obj = (CxJSONObject *)json;
    CxJSONMember *actionMember = obj->find("action");
    if (actionMember == NULL) { delete json; return "Error: missing action"; }

    CxString action = ((CxJSONString *)actionMember->object())->get();
    delete json;

    if (action == "insert") return toolInsertColumn(input);
    if (action == "delete") return toolDeleteColumn(input);
    if (action == "hide") {
        CxJSONBase *json2 = CxJSONFactory::parse(input);
        CxJSONObject *obj2 = (CxJSONObject *)json2;
        CxJSONMember *colMember = obj2->find("column");
        CxJSONMember *endMember = obj2->find("end_column");
        CxString startCol = ((CxJSONString *)colMember->object())->get();
        CxString endCol = endMember != NULL ? ((CxJSONString *)endMember->object())->get() : startCol;
        delete json2;

        CxString hideInput = CxString("{\"start_column\":\"") + startCol +
                              "\",\"end_column\":\"" + endCol + "\"}";
        return toolHideColumns(hideInput);
    }
    if (action == "show") {
        CxJSONBase *json2 = CxJSONFactory::parse(input);
        CxJSONObject *obj2 = (CxJSONObject *)json2;
        CxJSONMember *colMember = obj2->find("column");
        CxJSONMember *endMember = obj2->find("end_column");
        CxString startCol = ((CxJSONString *)colMember->object())->get();
        CxString endCol = endMember != NULL ? ((CxJSONString *)endMember->object())->get() : startCol;
        delete json2;

        CxString showInput = CxString("{\"start_column\":\"") + startCol +
                              "\",\"end_column\":\"" + endCol + "\"}";
        return toolShowColumns(showInput);
    }

    return CxString("Error: unknown action: ") + action;
}


//-------------------------------------------------------------------------------------------------
// Tool: read_cell
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::toolReadCell(CxString input)
{
    CxJSONBase *json = CxJSONFactory::parse(input);
    if (json == NULL) return "Error: invalid input JSON";

    CxJSONObject *obj = (CxJSONObject *)json;
    CxJSONMember *cellMember = obj->find("cell");
    if (cellMember == NULL) { delete json; return "Error: missing cell parameter"; }

    CxString cellAddr = ((CxJSONString *)cellMember->object())->get();
    CxSheetCellCoordinate coord(cellAddr);

    CxSheetCell *cell = _editor->sheetModel->getCellPtr(coord);

    CxString result = "{\"cell\":\"";
    result.append(escapeJSON(cellAddr));
    result.append("\"");

    if (cell == NULL || cell->cellType == CxSheetCell::EMPTY) {
        result.append(",\"type\":\"empty\"");
    } else if (cell->cellType == CxSheetCell::TEXT) {
        result.append(",\"type\":\"text\",\"value\":\"");
        result.append(escapeJSON(cell->text));
        result.append("\"");
    } else if (cell->cellType == CxSheetCell::DOUBLE) {
        result.append(",\"type\":\"number\",\"value\":");
        char numBuf[64];
        sprintf(numBuf, "%.10g", (double)cell->evaluatedValue);
        result.append(numBuf);
    } else if (cell->cellType == CxSheetCell::FORMULA) {
        result.append(",\"type\":\"formula\",\"formula\":\"");
        result.append(escapeJSON(cell->text));
        result.append("\",\"value\":");
        char numBuf[64];
        sprintf(numBuf, "%.10g", (double)cell->evaluatedValue);
        result.append(numBuf);
    }

    // Include format attributes
    if (cell != NULL) {
        if (cell->hasAppAttribute("currency")) {
            result.append(",\"currency\":");
            result.append(cell->getAppAttributeBool("currency") ? "true" : "false");
        }
        if (cell->hasAppAttribute("percent")) {
            result.append(",\"percent\":");
            result.append(cell->getAppAttributeBool("percent") ? "true" : "false");
        }
        if (cell->hasAppAttribute("align")) {
            result.append(",\"align\":\"");
            result.append(cell->getAppAttributeString("align"));
            result.append("\"");
        }
    }

    result.append("}");
    delete json;
    return result;
}


//-------------------------------------------------------------------------------------------------
// Tool: write_cell
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::toolWriteCell(CxString input)
{
    CxJSONBase *json = CxJSONFactory::parse(input);
    if (json == NULL) return "Error: invalid input JSON";

    CxJSONObject *obj = (CxJSONObject *)json;
    CxJSONMember *cellMember = obj->find("cell");
    if (cellMember == NULL) { delete json; return "Error: missing cell parameter"; }

    CxString cellAddr = ((CxJSONString *)cellMember->object())->get();
    CxSheetCellCoordinate coord(cellAddr);

    // Preserve existing cell formatting
    CxSheetCell *oldCellPtr = _editor->sheetModel->getCellPtr(coord);
    int oldHasCurrency = 0, oldHasPercent = 0, oldHasThousands = 0;
    int oldHasDecimalPlaces = 0, oldDecimalPlaces = 2, oldHasAlign = 0;
    CxString oldAlign;
    int oldHasFgColor = 0, oldHasBgColor = 0;
    CxString oldFgColor, oldBgColor;
    if (oldCellPtr) {
        oldHasCurrency = oldCellPtr->hasAppAttribute("currency") ? 1 : 0;
        oldHasPercent = oldCellPtr->hasAppAttribute("percent") ? 1 : 0;
        oldHasThousands = oldCellPtr->hasAppAttribute("thousands") ? 1 : 0;
        oldHasDecimalPlaces = oldCellPtr->hasAppAttribute("decimalPlaces") ? 1 : 0;
        oldDecimalPlaces = oldCellPtr->getAppAttributeInt("decimalPlaces", 2);
        oldHasAlign = oldCellPtr->hasAppAttribute("align") ? 1 : 0;
        if (oldHasAlign) oldAlign = oldCellPtr->getAppAttributeString("align");
        oldHasFgColor = oldCellPtr->hasAppAttribute("fgColor") ? 1 : 0;
        if (oldHasFgColor) oldFgColor = oldCellPtr->getAppAttributeString("fgColor");
        oldHasBgColor = oldCellPtr->hasAppAttribute("bgColor") ? 1 : 0;
        if (oldHasBgColor) oldBgColor = oldCellPtr->getAppAttributeString("bgColor");
    }

    CxSheetCell cell;
    CxSheetInputParseResult parseResult;
    parseResult.success = 0;
    parseResult.hasCurrency = 0;
    parseResult.hasPercent = 0;
    parseResult.hasThousands = 0;
    parseResult.decimalDigits = -1;

    // Check for formula
    CxJSONMember *formulaMember = obj->find("formula");
    if (formulaMember != NULL) {
        CxString formulaText = ((CxJSONString *)formulaMember->object())->get();
        cell.setFormula(formulaText);
    }
    else {
        // Check for value
        CxJSONMember *valueMember = obj->find("value");
        if (valueMember != NULL) {
            CxJSONBase *valueObj = valueMember->object();
            if (valueObj->type() == CxJSONBase::NUMBER) {
                double val = ((CxJSONNumber *)valueObj)->get();
                cell.setDouble(CxDouble(val));
            } else if (valueObj->type() == CxJSONBase::STRING) {
                CxString text = ((CxJSONString *)valueObj)->get();
                parseResult = CxSheetInputParser::parseAndClassify(text);
                if (parseResult.success) {
                    if (parseResult.cellType == 2) {
                        cell.setDouble(CxDouble(parseResult.doubleValue));
                    } else if (parseResult.cellType == 1) {
                        cell.setText(parseResult.textValue);
                    }
                } else {
                    cell.setText(text);
                }
            }
        }
    }

    _editor->sheetModel->setCell(coord, cell);

    // Apply parse-derived and preserved formatting
    CxSheetCell *cellPtr = _editor->sheetModel->getCellPtr(coord);
    if (cellPtr) {
        if (parseResult.success) {
            CxSheetInputParser::applyParsingAttributes(cellPtr, parseResult);
        }
        if (!parseResult.hasCurrency && oldHasCurrency)
            cellPtr->setAppAttribute("currency", true);
        if (!parseResult.hasPercent && oldHasPercent)
            cellPtr->setAppAttribute("percent", true);
        if (!parseResult.hasThousands && oldHasThousands)
            cellPtr->setAppAttribute("thousands", true);
        if (oldHasDecimalPlaces && parseResult.decimalDigits < 0)
            cellPtr->setAppAttribute("decimalPlaces", oldDecimalPlaces);
        if (oldHasAlign)
            cellPtr->setAppAttribute("align", oldAlign.data());
        if (oldHasFgColor)
            cellPtr->setAppAttribute("fgColor", oldFgColor.data());
        if (oldHasBgColor)
            cellPtr->setAppAttribute("bgColor", oldBgColor.data());
    }

    // Get affected cells and update display
    CxSList<CxSheetCellCoordinate> affected = _editor->sheetModel->getLastAffectedCells();
    _editor->sheetView->updateCells(affected);
    _editor->sheetView->updateVisibleTextmapCells();
    _editor->sheetView->updateStatusLine();

    delete json;
    return CxString("Cell ") + cellAddr + " updated.";
}


//-------------------------------------------------------------------------------------------------
// Tool: read_range
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::toolReadRange(CxString input)
{
    CxJSONBase *json = CxJSONFactory::parse(input);
    if (json == NULL) return "Error: invalid input JSON";

    CxJSONObject *obj = (CxJSONObject *)json;
    CxJSONMember *startMember = obj->find("start");
    CxJSONMember *endMember = obj->find("end");
    if (startMember == NULL || endMember == NULL) {
        delete json;
        return "Error: missing start or end parameter";
    }

    CxString startAddr = ((CxJSONString *)startMember->object())->get();
    CxString endAddr = ((CxJSONString *)endMember->object())->get();
    CxSheetCellCoordinate startCoord(startAddr);
    CxSheetCellCoordinate endCoord(endAddr);

    int minRow = (int)startCoord.getRow();
    int maxRow = (int)endCoord.getRow();
    int minCol = (int)startCoord.getCol();
    int maxCol = (int)endCoord.getCol();
    if (minRow > maxRow) { int t = minRow; minRow = maxRow; maxRow = t; }
    if (minCol > maxCol) { int t = minCol; minCol = maxCol; maxCol = t; }

    // Build column header: ["A","B","C",...]
    CxString result = "{\"columns\":[";
    for (int c = minCol; c <= maxCol; c++) {
        if (c > minCol) result.append(",");
        CxSheetCellCoordinate colCoord(0, c);
        result.append("\"");
        result.append(colCoord.colToLetters(c));
        result.append("\"");
    }
    result.append("],\"rows\":{");

    // Each row keyed by row number: "16":[...], "17":[...]
    int firstRow = 1;
    for (int r = minRow; r <= maxRow; r++) {
        if (!firstRow) result.append(",");
        firstRow = 0;

        char rowBuf[16];
        sprintf(rowBuf, "\"%d\":[", r + 1);  // 1-based row display
        result.append(rowBuf);

        for (int c = minCol; c <= maxCol; c++) {
            if (c > minCol) result.append(",");
            CxSheetCell *cell = _editor->sheetModel->getCellPtr(CxSheetCellCoordinate(r, c));
            if (cell == NULL || cell->cellType == CxSheetCell::EMPTY) {
                result.append("null");
            } else if (cell->cellType == CxSheetCell::TEXT) {
                result.append("\"");
                result.append(escapeJSON(cell->text));
                result.append("\"");
            } else if (cell->cellType == CxSheetCell::DOUBLE) {
                char numBuf[64];
                sprintf(numBuf, "%.10g", (double)cell->evaluatedValue);
                result.append(numBuf);
            } else if (cell->cellType == CxSheetCell::FORMULA) {
                char numBuf[64];
                sprintf(numBuf, "%.10g", (double)cell->evaluatedValue);
                result.append(numBuf);
            }
        }
        result.append("]");
    }
    result.append("}}");

    delete json;
    return result;
}


//-------------------------------------------------------------------------------------------------
// Tool: write_range
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::toolWriteRange(CxString input)
{
    CxJSONBase *json = CxJSONFactory::parse(input);
    if (json == NULL) return "Error: invalid input JSON";

    CxJSONObject *obj = (CxJSONObject *)json;
    CxJSONMember *startMember = obj->find("start");
    CxJSONMember *valuesMember = obj->find("values");
    if (startMember == NULL || valuesMember == NULL) {
        delete json;
        return "Error: missing start or values parameter";
    }

    CxString startAddr = ((CxJSONString *)startMember->object())->get();
    CxSheetCellCoordinate startCoord(startAddr);
    int startRow = (int)startCoord.getRow();
    int startCol = (int)startCoord.getCol();

    CxJSONArray *rows = (CxJSONArray *)valuesMember->object();
    int cellCount = 0;
    CxSList<CxSheetCellCoordinate> allAffected;

    for (int r = 0; r < (int)rows->entries(); r++) {
        CxJSONArray *row = (CxJSONArray *)rows->at(r);
        for (int c = 0; c < (int)row->entries(); c++) {
            CxJSONBase *val = row->at(c);
            CxSheetCellCoordinate coord(startRow + r, startCol + c);

            // Preserve existing cell formatting
            CxSheetCell *oldCellPtr = _editor->sheetModel->getCellPtr(coord);
            int oldHasCurrency = 0, oldHasPercent = 0, oldHasThousands = 0;
            int oldHasDecimalPlaces = 0, oldDecimalPlaces = 2;
            if (oldCellPtr) {
                oldHasCurrency = oldCellPtr->hasAppAttribute("currency") ? 1 : 0;
                oldHasPercent = oldCellPtr->hasAppAttribute("percent") ? 1 : 0;
                oldHasThousands = oldCellPtr->hasAppAttribute("thousands") ? 1 : 0;
                oldHasDecimalPlaces = oldCellPtr->hasAppAttribute("decimalPlaces") ? 1 : 0;
                oldDecimalPlaces = oldCellPtr->getAppAttributeInt("decimalPlaces", 2);
            }

            CxSheetCell cell;
            CxSheetInputParseResult parseResult;
            parseResult.success = 0;
            parseResult.hasCurrency = 0;
            parseResult.hasPercent = 0;
            parseResult.hasThousands = 0;
            parseResult.decimalDigits = -1;

            if (val->type() == CxJSONBase::NUMBER) {
                cell.setDouble(CxDouble(((CxJSONNumber *)val)->get()));
            } else if (val->type() == CxJSONBase::STRING) {
                CxString text = ((CxJSONString *)val)->get();
                if (text.length() > 0 && text.data()[0] == '=') {
                    cell.setFormula(text.subString(1, text.length() - 1));
                } else {
                    parseResult = CxSheetInputParser::parseAndClassify(text);
                    if (parseResult.success) {
                        if (parseResult.cellType == 2) {
                            cell.setDouble(CxDouble(parseResult.doubleValue));
                        } else if (parseResult.cellType == 1) {
                            cell.setText(parseResult.textValue);
                        }
                    } else {
                        cell.setText(text);
                    }
                }
            }
            // NULL type leaves cell empty

            _editor->sheetModel->setCell(coord, cell);

            // Apply parse-derived and preserved formatting
            CxSheetCell *cellPtr = _editor->sheetModel->getCellPtr(coord);
            if (cellPtr) {
                if (parseResult.success) {
                    CxSheetInputParser::applyParsingAttributes(cellPtr, parseResult);
                }
                if (!parseResult.hasCurrency && oldHasCurrency)
                    cellPtr->setAppAttribute("currency", true);
                if (!parseResult.hasPercent && oldHasPercent)
                    cellPtr->setAppAttribute("percent", true);
                if (!parseResult.hasThousands && oldHasThousands)
                    cellPtr->setAppAttribute("thousands", true);
                if (oldHasDecimalPlaces && parseResult.decimalDigits < 0)
                    cellPtr->setAppAttribute("decimalPlaces", oldDecimalPlaces);
            }

            // Collect affected cells from each setCell
            CxSList<CxSheetCellCoordinate> affected = _editor->sheetModel->getLastAffectedCells();
            for (int a = 0; a < (int)affected.entries(); a++) {
                allAffected.append(affected.at(a));
            }
            cellCount++;
        }
    }

    // Update display for all affected cells
    _editor->sheetView->updateCells(allAffected);
    _editor->sheetView->updateVisibleTextmapCells();
    _editor->sheetView->updateStatusLine();

    char buf[64];
    sprintf(buf, "%d cells written.", cellCount);
    delete json;
    return CxString(buf);
}


//-------------------------------------------------------------------------------------------------
// Tool: get_sheet_info
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::toolGetSheetInfo(CxString input)
{
    (void)input;

    CxSheetCellCoordinate extents = _editor->sheetModel->getSheetExtents();
    CxSheetCellCoordinate cursor = _editor->sheetModel->getCurrentPosition();

    CxString result = "{\"rows\":";
    char buf[32];
    sprintf(buf, "%lu", extents.getRow() + 1);
    result.append(buf);
    result.append(",\"columns\":");
    sprintf(buf, "%lu", extents.getCol() + 1);
    result.append(buf);
    result.append(",\"cursor\":\"");
    result.append(cursor.toAddress());
    result.append("\"");
    if (_editor->_filePath.length() > 0) {
        result.append(",\"filename\":\"");
        result.append(escapeJSON(_editor->_filePath));
        result.append("\"");
    }
    result.append("}");
    return result;
}


//-------------------------------------------------------------------------------------------------
// Tool: set_format
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::toolSetFormat(CxString input)
{
    CxJSONBase *json = CxJSONFactory::parse(input);
    if (json == NULL) return "Error: invalid input JSON";

    CxJSONObject *obj = (CxJSONObject *)json;
    CxJSONMember *cellMember = obj->find("cell");
    CxJSONMember *attrMember = obj->find("attr");
    CxJSONMember *valueMember = obj->find("value");
    if (cellMember == NULL || attrMember == NULL || valueMember == NULL) {
        delete json;
        return "Error: missing cell, attr, or value parameter";
    }

    CxString cellAddr = ((CxJSONString *)cellMember->object())->get();
    CxString attr = ((CxJSONString *)attrMember->object())->get();
    CxString value = ((CxJSONString *)valueMember->object())->get();

    // Parse cell address - support range syntax like "Q16:Q33"
    int minRow, maxRow, minCol, maxCol;
    int colonPos = cellAddr.index(":");
    if (colonPos >= 0) {
        CxSheetCellCoordinate startCoord(cellAddr.subString(0, colonPos));
        CxSheetCellCoordinate endCoord(cellAddr.subString(colonPos + 1, cellAddr.length() - colonPos - 1));
        minRow = (int)startCoord.getRow();
        maxRow = (int)endCoord.getRow();
        minCol = (int)startCoord.getCol();
        maxCol = (int)endCoord.getCol();
        if (minRow > maxRow) { int t = minRow; minRow = maxRow; maxRow = t; }
        if (minCol > maxCol) { int t = minCol; minCol = maxCol; maxCol = t; }
    } else {
        CxSheetCellCoordinate coord(cellAddr);
        minRow = maxRow = (int)coord.getRow();
        minCol = maxCol = (int)coord.getCol();
    }

    CxSList<CxSheetCellCoordinate> cells;

    for (int r = minRow; r <= maxRow; r++) {
        for (int c = minCol; c <= maxCol; c++) {
            CxSheetCellCoordinate coord(r, c);

            // Ensure cell exists
            CxSheetCell *cellPtr = _editor->sheetModel->getCellPtr(coord);
            if (cellPtr == NULL) {
                CxSheetCell cell;
                _editor->sheetModel->setCell(coord, cell);
                cellPtr = _editor->sheetModel->getCellPtr(coord);
            }

            if (cellPtr != NULL) {
                if (attr == "currency" || attr == "percent" || attr == "thousands" || attr == "bold") {
                    int boolVal = (value == "true" || value == "1") ? 1 : 0;
                    cellPtr->setAppAttribute(attr.data(), boolVal ? true : false);
                }
                else if (attr == "align") {
                    cellPtr->setAppAttribute("align", value.data());
                }
                else if (attr == "decimalPlaces") {
                    cellPtr->setAppAttribute("decimalPlaces", value.toInt());
                }
            }

            cells.append(coord);
        }
    }

    // Update display
    _editor->sheetView->updateCells(cells);

    delete json;
    return CxString("Format ") + attr + " set to " + value + " on " + cellAddr;
}


//-------------------------------------------------------------------------------------------------
// Tool: set_column_width
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::toolSetColumnWidth(CxString input)
{
    CxJSONBase *json = CxJSONFactory::parse(input);
    if (json == NULL) return "Error: invalid input JSON";

    CxJSONObject *obj = (CxJSONObject *)json;
    CxJSONMember *colMember = obj->find("column");
    CxJSONMember *widthMember = obj->find("width");
    if (colMember == NULL || widthMember == NULL) {
        delete json;
        return "Error: missing column or width parameter";
    }

    CxString colLetter = ((CxJSONString *)colMember->object())->get();
    int width = (int)((CxJSONNumber *)widthMember->object())->get();

    // Convert column letter to index
    CxSheetCellCoordinate tempCoord(colLetter + "1");
    int colIdx = (int)tempCoord.getCol();

    _editor->sheetView->setColumnWidth(colIdx, width);
    _editor->sheetView->updateScreen();

    delete json;

    char buf[64];
    sprintf(buf, "Column %s width set to %d", colLetter.data(), width);
    return CxString(buf);
}


//-------------------------------------------------------------------------------------------------
// Tool: insert_row
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::toolInsertRow(CxString input)
{
    CxJSONBase *json = CxJSONFactory::parse(input);
    if (json == NULL) return "Error: invalid input JSON";

    CxJSONObject *obj = (CxJSONObject *)json;
    CxJSONMember *rowMember = obj->find("row");
    if (rowMember == NULL) { delete json; return "Error: missing row parameter"; }

    int row = (int)((CxJSONNumber *)rowMember->object())->get();
    int dataRow = row - 1;  // convert to 0-based

    _editor->sheetModel->insertRow(dataRow);
    _editor->sheetView->shiftHiddenRows(dataRow, 1);
    _editor->sheetView->updateScreen();

    delete json;

    char buf[64];
    sprintf(buf, "Row %d inserted.", row);
    return CxString(buf);
}


//-------------------------------------------------------------------------------------------------
// Tool: delete_row
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::toolDeleteRow(CxString input)
{
    CxJSONBase *json = CxJSONFactory::parse(input);
    if (json == NULL) return "Error: invalid input JSON";

    CxJSONObject *obj = (CxJSONObject *)json;
    CxJSONMember *rowMember = obj->find("row");
    if (rowMember == NULL) { delete json; return "Error: missing row parameter"; }

    int row = (int)((CxJSONNumber *)rowMember->object())->get();
    int dataRow = row - 1;

    _editor->sheetModel->deleteRow(dataRow);
    _editor->sheetView->shiftHiddenRows(dataRow, -1);
    _editor->sheetView->updateScreen();

    delete json;

    char buf[64];
    sprintf(buf, "Row %d deleted.", row);
    return CxString(buf);
}


//-------------------------------------------------------------------------------------------------
// Tool: insert_column
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::toolInsertColumn(CxString input)
{
    CxJSONBase *json = CxJSONFactory::parse(input);
    if (json == NULL) return "Error: invalid input JSON";

    CxJSONObject *obj = (CxJSONObject *)json;
    CxJSONMember *colMember = obj->find("column");
    if (colMember == NULL) { delete json; return "Error: missing column parameter"; }

    CxString colLetter = ((CxJSONString *)colMember->object())->get();
    CxSheetCellCoordinate tempCoord(colLetter + "1");
    int colIdx = (int)tempCoord.getCol();

    _editor->sheetModel->insertColumn(colIdx);
    _editor->sheetView->shiftColumnWidths(colIdx, 1);
    _editor->sheetView->shiftColumnFormats(colIdx, 1);
    _editor->sheetView->updateScreen();

    delete json;
    return CxString("Column ") + colLetter + " inserted.";
}


//-------------------------------------------------------------------------------------------------
// Tool: delete_column
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::toolDeleteColumn(CxString input)
{
    CxJSONBase *json = CxJSONFactory::parse(input);
    if (json == NULL) return "Error: invalid input JSON";

    CxJSONObject *obj = (CxJSONObject *)json;
    CxJSONMember *colMember = obj->find("column");
    if (colMember == NULL) { delete json; return "Error: missing column parameter"; }

    CxString colLetter = ((CxJSONString *)colMember->object())->get();
    CxSheetCellCoordinate tempCoord(colLetter + "1");
    int colIdx = (int)tempCoord.getCol();

    _editor->sheetModel->deleteColumn(colIdx);
    _editor->sheetView->shiftColumnWidths(colIdx, -1);
    _editor->sheetView->shiftColumnFormats(colIdx, -1);
    _editor->sheetView->updateScreen();

    delete json;
    return CxString("Column ") + colLetter + " deleted.";
}


//-------------------------------------------------------------------------------------------------
// Tool: goto_cell
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::toolGotoCell(CxString input)
{
    CxJSONBase *json = CxJSONFactory::parse(input);
    if (json == NULL) return "Error: invalid input JSON";

    CxJSONObject *obj = (CxJSONObject *)json;
    CxJSONMember *cellMember = obj->find("cell");
    if (cellMember == NULL) { delete json; return "Error: missing cell parameter"; }

    CxString cellAddr = ((CxJSONString *)cellMember->object())->get();
    CxSheetCellCoordinate coord(cellAddr);

    CxSheetCellCoordinate oldPos = _editor->sheetModel->getCurrentPosition();
    _editor->sheetModel->jumpToCell(coord);
    CxSheetCellCoordinate newPos = _editor->sheetModel->getCurrentPosition();
    _editor->sheetView->updateCursorMove(oldPos, newPos);
    _editor->sheetView->updateStatusLine();

    delete json;
    return CxString("Cursor moved to ") + cellAddr;
}


//-------------------------------------------------------------------------------------------------
// Tool: hide_rows
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::toolHideRows(CxString input)
{
    CxJSONBase *json = CxJSONFactory::parse(input);
    if (json == NULL) return "Error: invalid input JSON";

    CxJSONObject *obj = (CxJSONObject *)json;
    CxJSONMember *startMember = obj->find("start_row");
    CxJSONMember *endMember = obj->find("end_row");
    if (startMember == NULL || endMember == NULL) {
        delete json;
        return "Error: missing start_row or end_row parameter";
    }

    int startRow = (int)((CxJSONNumber *)startMember->object())->get();
    int endRow = (int)((CxJSONNumber *)endMember->object())->get();

    // Convert from 1-based to 0-based
    startRow--;
    endRow--;

    int count = 0;
    for (int row = startRow; row <= endRow; row++) {
        _editor->sheetView->setRowHidden(row, 1);
        count++;
    }

    // Move cursor if it's now on a hidden row
    CxSheetCellCoordinate pos = _editor->sheetModel->getCurrentPosition();
    if (_editor->sheetView->isRowHidden(pos.getRow())) {
        int newRow = _editor->sheetView->nextVisibleRow(pos.getRow(), 1);
        _editor->sheetModel->jumpToCell(CxSheetCellCoordinate(newRow, pos.getCol()));
    }

    _editor->sheetView->updateScreen();

    delete json;
    char msg[64];
    sprintf(msg, "%d row%s hidden.", count, count == 1 ? "" : "s");
    return CxString(msg);
}


//-------------------------------------------------------------------------------------------------
// Tool: hide_columns
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::toolHideColumns(CxString input)
{
    CxJSONBase *json = CxJSONFactory::parse(input);
    if (json == NULL) return "Error: invalid input JSON";

    CxJSONObject *obj = (CxJSONObject *)json;
    CxJSONMember *startMember = obj->find("start_column");
    CxJSONMember *endMember = obj->find("end_column");
    if (startMember == NULL || endMember == NULL) {
        delete json;
        return "Error: missing start_column or end_column parameter";
    }

    CxString startCol = ((CxJSONString *)startMember->object())->get();
    CxString endCol = ((CxJSONString *)endMember->object())->get();

    CxSheetCellCoordinate startCoord(startCol + "1");
    CxSheetCellCoordinate endCoord(endCol + "1");
    int startIdx = (int)startCoord.getCol();
    int endIdx = (int)endCoord.getCol();

    int count = 0;
    for (int col = startIdx; col <= endIdx; col++) {
        _editor->sheetView->setColumnHidden(col, 1);
        count++;
    }

    // Move cursor if it's now on a hidden column
    CxSheetCellCoordinate pos = _editor->sheetModel->getCurrentPosition();
    if (_editor->sheetView->isColumnHidden(pos.getCol())) {
        int newCol = _editor->sheetView->nextVisibleCol(pos.getCol(), 1);
        _editor->sheetModel->jumpToCell(CxSheetCellCoordinate(pos.getRow(), newCol));
    }

    _editor->sheetView->updateScreen();

    delete json;
    char msg[64];
    sprintf(msg, "%d column%s hidden.", count, count == 1 ? "" : "s");
    return CxString(msg);
}


//-------------------------------------------------------------------------------------------------
// Tool: show_rows
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::toolShowRows(CxString input)
{
    CxJSONBase *json = CxJSONFactory::parse(input);
    if (json == NULL) return "Error: invalid input JSON";

    CxJSONObject *obj = (CxJSONObject *)json;
    CxJSONMember *startMember = obj->find("start_row");
    CxJSONMember *endMember = obj->find("end_row");
    if (startMember == NULL || endMember == NULL) {
        delete json;
        return "Error: missing start_row or end_row parameter";
    }

    int startRow = (int)((CxJSONNumber *)startMember->object())->get();
    int endRow = (int)((CxJSONNumber *)endMember->object())->get();

    // Convert from 1-based to 0-based
    startRow--;
    endRow--;

    int count = 0;
    for (int row = startRow; row <= endRow; row++) {
        if (_editor->sheetView->isRowHidden(row)) {
            _editor->sheetView->setRowHidden(row, 0);
            count++;
        }
    }

    _editor->sheetView->updateScreen();

    delete json;
    char msg[64];
    sprintf(msg, "%d row%s shown.", count, count == 1 ? "" : "s");
    return CxString(msg);
}


//-------------------------------------------------------------------------------------------------
// Tool: show_columns
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::toolShowColumns(CxString input)
{
    CxJSONBase *json = CxJSONFactory::parse(input);
    if (json == NULL) return "Error: invalid input JSON";

    CxJSONObject *obj = (CxJSONObject *)json;
    CxJSONMember *startMember = obj->find("start_column");
    CxJSONMember *endMember = obj->find("end_column");
    if (startMember == NULL || endMember == NULL) {
        delete json;
        return "Error: missing start_column or end_column parameter";
    }

    CxString startCol = ((CxJSONString *)startMember->object())->get();
    CxString endCol = ((CxJSONString *)endMember->object())->get();

    CxSheetCellCoordinate startCoord(startCol + "1");
    CxSheetCellCoordinate endCoord(endCol + "1");
    int startIdx = (int)startCoord.getCol();
    int endIdx = (int)endCoord.getCol();

    int count = 0;
    for (int col = startIdx; col <= endIdx; col++) {
        if (_editor->sheetView->isColumnHidden(col)) {
            _editor->sheetView->setColumnHidden(col, 0);
            count++;
        }
    }

    _editor->sheetView->updateScreen();

    delete json;
    char msg[64];
    sprintf(msg, "%d column%s shown.", count, count == 1 ? "" : "s");
    return CxString(msg);
}


//-------------------------------------------------------------------------------------------------
// Tool: show_all
//-------------------------------------------------------------------------------------------------
CxString
ClaudeHandler::toolShowAll(CxString input)
{
    (void)input;

    _editor->sheetView->showAllRows();
    _editor->sheetView->showAllColumns();
    _editor->sheetView->updateScreen();

    return "All rows and columns are now visible.";
}
