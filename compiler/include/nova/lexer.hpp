// Lexical analysis: hand-written scanner (equivalent to a DFA) producing positioned tokens.
#pragma once
#include <string>
#include <vector>
#include "nova/diagnostics.hpp"

namespace nova {

enum class TokCat { Keyword, Identifier, IntLiteral, FloatLiteral, Operator, Punctuation, Eof };

struct Token {
    TokCat cat = TokCat::Eof;
    std::string lexeme;
    std::string terminal;  // grammar terminal name: "ID", "INT_LIT", "FLOAT_LIT", keyword/operator text, or "$"
    SourceLoc loc;
    int offset = 0;        // byte offset in source
    Json toJson() const;
};

const char* tokCatName(TokCat c);
bool isKeyword(const std::string& s);
const std::vector<std::string>& keywordList();

struct LexResult {
    std::vector<Token> tokens;  // always terminated by an EOF token
    DiagnosticList diagnostics;
};

LexResult lex(const std::string& source);

} // namespace nova
