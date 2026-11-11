#include "nova/lexer.hpp"
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <unordered_set>

namespace nova {

static const std::vector<std::string> kKeywords = {"fn", "let", "if", "else", "while", "return", "print",
                                                   "int", "float", "bool", "true", "false", "string", "new"};

const std::vector<std::string>& keywordList() { return kKeywords; }
bool isKeyword(const std::string& s) {
    for (auto& k : kKeywords) if (k == s) return true;
    return false;
}

const char* tokCatName(TokCat c) {
    switch (c) {
        case TokCat::Keyword: return "KEYWORD";
        case TokCat::Identifier: return "IDENTIFIER";
        case TokCat::IntLiteral: return "INT_LITERAL";
        case TokCat::FloatLiteral: return "FLOAT_LITERAL";
        case TokCat::StringLiteral: return "STRING_LITERAL";
        case TokCat::Operator: return "OPERATOR";
        case TokCat::Punctuation: return "PUNCTUATION";
        case TokCat::Eof: return "EOF";
    }
    return "?";
}

Json Token::toJson() const {
    Json j = Json::object();
    j.set("category", tokCatName(cat)).set("lexeme", lexeme).set("terminal", terminal)
     .set("loc", loc.toJson()).set("offset", offset);
    return j;
}

namespace {

class Scanner {
public:
    explicit Scanner(const std::string& s) : src_(s) {}

    LexResult run() {
        while (true) {
            skipTrivia();
            if (pos_ >= src_.size()) break;
            scanToken();
        }
        Token eof;
        eof.cat = TokCat::Eof; eof.terminal = "$"; eof.lexeme = "";
        eof.loc = {line_, col_, line_, col_};
        eof.offset = static_cast<int>(pos_);
        res_.tokens.push_back(eof);
        return std::move(res_);
    }

private:
    const std::string& src_;
    size_t pos_ = 0;
    int line_ = 1, col_ = 1;
    LexResult res_;

    char peek(size_t k = 0) const { return pos_ + k < src_.size() ? src_[pos_ + k] : '\0'; }
    char advance() {
        char c = src_[pos_++];
        if (c == '\n') { ++line_; col_ = 1; } else { ++col_; }
        return c;
    }

    void error(const std::string& cat, const std::string& code, const std::string& msg, const std::string& expl,
               const std::string& sugg, SourceLoc loc) {
        Diagnostic d;
        d.phase = "lexical"; d.category = cat; d.code = code; d.message = msg;
        d.explanation = expl; d.suggestion = sugg; d.loc = loc;
        res_.diagnostics.add(std::move(d));
    }

    void skipTrivia() {
        while (pos_ < src_.size()) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(); continue; }
            if (c == '/' && peek(1) == '/') { while (pos_ < src_.size() && peek() != '\n') advance(); continue; }
            if (c == '/' && peek(1) == '*') {
                int sl = line_, sc = col_;
                advance(); advance();
                bool closed = false;
                while (pos_ < src_.size()) {
                    if (peek() == '*' && peek(1) == '/') { advance(); advance(); closed = true; break; }
                    advance();
                }
                if (!closed)
                    error("Unterminated comment", "E-LEX-003", "Block comment starting here is never closed",
                          "A comment opened with '/*' must be closed with '*/' before the end of the file.",
                          "Add '*/' to close the comment.", {sl, sc, sl, sc + 2});
                continue;
            }
            break;
        }
    }

    void emit(TokCat cat, const std::string& lex, const std::string& terminal, int sl, int sc, int off) {
        Token t;
        t.cat = cat; t.lexeme = lex; t.terminal = terminal;
        t.loc = {sl, sc, line_, col_}; t.offset = off;
        res_.tokens.push_back(std::move(t));
    }

    void scanToken() {
        int sl = line_, sc = col_, off = static_cast<int>(pos_);
        char c = peek();
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::string s;
            while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') s += advance();
            if (isKeyword(s)) emit(TokCat::Keyword, s, s, sl, sc, off);
            else emit(TokCat::Identifier, s, "ID", sl, sc, off);
            return;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) { scanNumber(sl, sc, off); return; }
        if (c == '"') { scanString(sl, sc, off); return; }
        // two-character operators
        char n = peek(1);
        auto two = [&](const char* op) { return c == op[0] && n == op[1]; };
        static const char* twoOps[] = {"==", "!=", "<=", ">=", "&&", "||", "->"};
        for (auto op : twoOps) {
            if (two(op)) {
                advance(); advance();
                emit(TokCat::Operator, op, op, sl, sc, off);
                return;
            }
        }
        static const std::string ops = "+-*/%<>=!";
        static const std::string puncts = "(){}[],;:";
        if (ops.find(c) != std::string::npos) { advance(); emit(TokCat::Operator, std::string(1, c), std::string(1, c), sl, sc, off); return; }
        if (puncts.find(c) != std::string::npos) { advance(); emit(TokCat::Punctuation, std::string(1, c), std::string(1, c), sl, sc, off); return; }
        // errors
        advance();
        if (c == '&' || c == '|') {
            std::string two2(2, c);
            error("Invalid operator", "E-LEX-002", std::string("Unexpected '") + c + "'",
                  std::string("NOVA has the logical operator '") + two2 + "' but no single-character '" + c + "' operator.",
                  std::string("Did you mean '") + two2 + "'?", {sl, sc, line_, col_});
        } else {
            std::string shown = (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7e)
                                    ? "byte 0x" + hex(static_cast<unsigned char>(c)) : std::string(1, c);
            error("Invalid character", "E-LEX-001", "Unexpected character '" + shown + "'",
                  "This character is not part of the NOVA alphabet, so the scanner cannot form a token from it.",
                  "Remove the character or replace it with a valid operator or punctuation.", {sl, sc, line_, col_});
        }
    }

    void scanString(int sl, int sc, int off) {
        advance();   // opening quote
        std::string s;
        bool closed = false;
        while (pos_ < src_.size() && peek() != '\n') {
            char c = advance();
            if (c == '"') { closed = true; break; }
            if (c == '\\') {
                char e = pos_ < src_.size() ? advance() : '\0';
                if (e == 'n') s += '\n'; else if (e == 't') s += '\t'; else if (e == '\\') s += '\\'; else if (e == '"') s += '"';
                else error("Invalid escape", "E-LEX-008", std::string("Unknown escape sequence '\\") + e + "'",
                           "Supported escapes in string literals are \\n, \\t, \\\\ and \\\".", "Use one of the supported escapes.", {line_, col_ - 2, line_, col_});
            } else s += c;
        }
        if (!closed) { error("Unterminated string", "E-LEX-007", "String literal is never closed",
                            "A string literal must end with a closing '\"' on the same line.", "Add the closing '\"'.", {sl, sc, line_, col_}); return; }
        emit(TokCat::StringLiteral, s, "STR_LIT", sl, sc, off);
    }

    static std::string hex(unsigned v) { const char* d = "0123456789abcdef"; return std::string() + d[v >> 4] + d[v & 15]; }

    void scanNumber(int sl, int sc, int off) {
        std::string s;
        while (std::isdigit(static_cast<unsigned char>(peek()))) s += advance();
        bool isFloat = false;
        if (peek() == '.') {
            if (std::isdigit(static_cast<unsigned char>(peek(1)))) {
                isFloat = true; s += advance();
                while (std::isdigit(static_cast<unsigned char>(peek()))) s += advance();
            } else {
                s += advance();
                error("Malformed number", "E-LEX-004", "Number '" + s + "' has no digits after the decimal point",
                      "A float literal needs at least one digit on both sides of '.'.",
                      "Write '" + s + "0' instead.", {sl, sc, line_, col_});
                return;
            }
        }
        if (std::isalpha(static_cast<unsigned char>(peek())) || peek() == '_') {
            std::string bad = s;
            while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') bad += advance();
            error("Malformed number", "E-LEX-005", "Invalid literal '" + bad + "'",
                  "Identifiers cannot start with a digit, and numbers cannot contain letters.",
                  "Separate the number from the name, or rename the identifier to start with a letter.", {sl, sc, line_, col_});
            return;
        }
        if (!isFloat) {
            errno = 0;
            char* end = nullptr;
            std::strtoll(s.c_str(), &end, 10);
            if (errno == ERANGE) {
                error("Integer literal too large", "E-LEX-006", "Integer literal '" + s + "' does not fit in 64 bits",
                      "NOVA 'int' is a signed 64-bit integer (max 9223372036854775807).",
                      "Use a smaller value or a float literal such as '" + s + ".0'.", {sl, sc, line_, col_});
                return;
            }
        }
        emit(isFloat ? TokCat::FloatLiteral : TokCat::IntLiteral, s, isFloat ? "FLOAT_LIT" : "INT_LIT", sl, sc, off);
    }
};

} // namespace

LexResult lex(const std::string& source) { return Scanner(source).run(); }

} // namespace nova
