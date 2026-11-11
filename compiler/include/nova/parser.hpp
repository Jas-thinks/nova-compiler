// Table-driven canonical LR(1) parser driver. Produces a parse tree and a full step trace.
#pragma once
#include <memory>
#include "nova/diagnostics.hpp"
#include "nova/grammar.hpp"
#include "nova/lexer.hpp"

namespace nova {

struct ParseNode {
    std::string symbol;
    int prod = -1;                 // production used to build this node (-1 for tokens)
    bool isToken = false;
    std::string lexeme;
    SourceLoc loc;
    bool hasLoc = false;
    std::vector<std::unique_ptr<ParseNode>> children;
    Json toJson() const;
};

struct ParseStep {
    int step = 0;
    std::vector<std::string> stack;   // alternating: state, symbol, state, symbol, ..., state
    std::string lookahead;
    std::vector<std::string> input;   // upcoming lexemes (including lookahead)
    std::string action;               // "shift 5", "reduce 12", "accept", "error"
    int prod = -1;
    std::string detail;               // human-readable description
    Json toJson() const;
};

struct ParseResult {
    bool ok = false;
    std::unique_ptr<ParseNode> tree;
    std::vector<ParseStep> trace;
    bool traceTruncated = false;
    int totalSteps = 0, shifts = 0, reduces = 0, maxStackDepth = 0;
    int errorsReported = 0, recoveries = 0;
    DiagnosticList diagnostics;
};

ParseResult parseTokens(const std::vector<Token>& tokens, const LR1Table& table);
// Shared, lazily-built LR(1) table for the NOVA grammar.
const LR1Table& novaTable();
std::string terminalDisplay(const std::string& terminal);

} // namespace nova
