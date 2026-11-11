#include "nova/parser.hpp"
#include <algorithm>

namespace nova {

const LR1Table& novaTable() {
    static const LR1Table t = LR1Table::build(Grammar::nova());
    return t;
}

std::string terminalDisplay(const std::string& t) {
    if (t == "ID") return "identifier";
    if (t == "INT_LIT") return "integer literal";
    if (t == "FLOAT_LIT") return "float literal";
    if (t == "STR_LIT") return "string literal";
    if (t == "$") return "end of file";
    return "'" + t + "'";
}

Json ParseNode::toJson() const {
    Json j = Json::object();
    j.set("symbol", symbol).set("isToken", isToken);
    if (isToken) j.set("lexeme", lexeme);
    else j.set("prod", prod);
    if (hasLoc) j.set("loc", loc.toJson());
    Json ch = Json::array();
    for (auto& c : children) ch.push(c->toJson());
    j.set("children", ch);
    return j;
}

Json ParseStep::toJson() const {
    Json j = Json::object();
    Json st = Json::array(), in = Json::array();
    for (auto& s : stack) st.push(s);
    for (auto& s : input) in.push(s);
    j.set("step", step).set("stack", st).set("lookahead", lookahead).set("input", in)
     .set("action", action).set("prod", prod).set("detail", detail);
    return j;
}

namespace {
constexpr int kTraceCap = 4000;
}

ParseResult parseTokens(const std::vector<Token>& tokens, const LR1Table& T) {
    const Grammar& g = T.grammar;
    ParseResult res;
    struct Entry { int state; std::unique_ptr<ParseNode> node; };
    std::vector<Entry> stack;
    stack.push_back({0, nullptr});
    size_t pos = 0, lastErrorPos = static_cast<size_t>(-1);
    int shiftsSinceError = 0;

    auto stackStrings = [&]() {
        std::vector<std::string> v;
        for (auto& e : stack) {
            v.push_back(std::to_string(e.state));
            if (e.node) v.push_back(e.node->symbol);
        }
        return v;
    };

    while (true) {
        const Token& tok = tokens[pos];
        int sym = g.symbol(tok.terminal);
        int state = stack.back().state;
        LRAction act = sym >= 0 ? T.table[state][sym] : LRAction{};
        res.maxStackDepth = std::max<int>(res.maxStackDepth, static_cast<int>(stack.size()));

        ParseStep step;
        bool record = res.totalSteps < kTraceCap;
        if (record) {
            step.step = res.totalSteps + 1;
            step.stack = stackStrings();
            step.lookahead = tok.terminal;
            for (size_t k = pos; k < tokens.size() && k < pos + 8; ++k)
                step.input.push_back(tokens[k].cat == TokCat::Eof ? "$" : tokens[k].lexeme);
        } else res.traceTruncated = true;
        ++res.totalSteps;

        if (act.kind == LRAction::Shift) {
            if (record) { step.action = "shift " + std::to_string(act.target);
                step.detail = "Lookahead '" + (tok.lexeme.empty() ? tok.terminal : tok.lexeme) + "' is valid in state " + std::to_string(state) + ": push it and go to state " + std::to_string(act.target) + "."; }
            auto n = std::make_unique<ParseNode>();
            n->symbol = tok.terminal == "ID" || tok.terminal == "INT_LIT" || tok.terminal == "FLOAT_LIT" ? tok.terminal : tok.terminal;
            n->isToken = true; n->lexeme = tok.lexeme; n->loc = tok.loc; n->hasLoc = true;
            stack.push_back({act.target, std::move(n)});
            ++pos; ++res.shifts; ++shiftsSinceError;
        } else if (act.kind == LRAction::Reduce) {
            const Production& p = g.prods[act.target];
            if (record) { step.action = "reduce " + std::to_string(act.target); step.prod = act.target;
                step.detail = "State " + std::to_string(state) + " + lookahead '" + tok.terminal + "' → reduce by " + g.prodText(act.target) + ": pop " + std::to_string(p.rhs.size()) + " symbol(s), then take GOTO."; }
            auto n = std::make_unique<ParseNode>();
            n->symbol = g.names[p.lhs]; n->prod = act.target;
            size_t k = p.rhs.size();
            std::vector<std::unique_ptr<ParseNode>> kids;
            for (size_t i = 0; i < k; ++i) { kids.push_back(std::move(stack.back().node)); stack.pop_back(); }
            std::reverse(kids.begin(), kids.end());
            for (auto& c : kids) {
                if (!c->hasLoc) continue;
                if (!n->hasLoc) { n->loc = c->loc; n->hasLoc = true; }
                else { n->loc.endLine = c->loc.endLine; n->loc.endCol = c->loc.endCol; }
            }
            n->children = std::move(kids);
            LRAction gt = T.table[stack.back().state][p.lhs];
            stack.push_back({gt.target, std::move(n)});
            ++res.reduces;
        } else if (act.kind == LRAction::Accept) {
            if (record) { step.action = "accept"; step.detail = "Lookahead is $ and the start production is complete: input accepted."; res.trace.push_back(std::move(step)); }
            res.tree = std::move(stack.back().node);
            res.ok = res.errorsReported == 0;       // accepted after recovery: the tree is partial, the input was still invalid
            return res;
        } else {
            if (record) { step.action = "error"; step.detail = "ACTION[" + std::to_string(state) + ", " + tok.terminal + "] is empty."; res.trace.push_back(std::move(step)); }
            // Build a diagnostic.
            std::vector<std::string> exp;
            for (int t : T.expectedTerminals(state)) exp.push_back(g.names[t]);
            auto has = [&](const char* s) { return std::find(exp.begin(), exp.end(), s) != exp.end(); };
            Diagnostic d; d.phase = "syntax";
            std::string found = terminalDisplay(tok.cat == TokCat::Eof ? "$" : (tok.cat == TokCat::Identifier || tok.cat == TokCat::IntLiteral || tok.cat == TokCat::FloatLiteral ? tok.lexeme : tok.terminal));
            if (tok.cat == TokCat::Identifier || tok.cat == TokCat::IntLiteral || tok.cat == TokCat::FloatLiteral) found = "'" + tok.lexeme + "'";
            std::string expList;
            size_t shown = 0;
            for (auto& e : exp) { if (shown++ >= 8) { expList += ", …"; break; } expList += (expList.empty() ? "" : ", ") + terminalDisplay(e); }
            SourceLoc prevEnd;
            bool havePrev = pos > 0;
            if (havePrev) { const Token& pv = tokens[pos - 1]; prevEnd = {pv.loc.endLine, pv.loc.endCol, pv.loc.endLine, pv.loc.endCol + 1}; }
            d.loc = tok.loc;
            if (tok.cat == TokCat::Eof && (has("}") || has(")"))) {
                d.category = "Unexpected end of file"; d.code = "E-SYN-004";
                d.message = "Unexpected end of file; expected " + expList;
                d.explanation = "The parser reached the end of the input while a construct was still open.";
                d.suggestion = has("}") ? "Add a closing '}' to finish the block or function." : "Add the missing ')'.";
                if (havePrev) d.loc = prevEnd;
            } else if (has(";") && havePrev) {
                d.category = "Missing semicolon"; d.code = "E-SYN-001";
                d.message = "Expected ';' but found " + found;
                d.explanation = "Every statement in NOVA ends with ';'. The parser finished an expression and the next token cannot continue it.";
                d.suggestion = "Insert ';' at the end of the previous statement.";
                d.loc = prevEnd;
            } else if (has(")") && havePrev && !(has("INT_LIT") && has("ID"))) {
                d.category = "Missing parenthesis"; d.code = "E-SYN-002";
                d.message = "Expected ')' but found " + found;
                d.explanation = "A '(' was opened earlier and has not been closed.";
                d.suggestion = "Insert ')' to close the parenthesis.";
                d.loc = prevEnd;
            } else if (has("}") && !has("INT_LIT")) {
                d.category = "Missing brace"; d.code = "E-SYN-003";
                d.message = "Expected '}' or a statement but found " + found;
                d.explanation = "A block was opened with '{' and the parser found a token that cannot start a statement.";
                d.suggestion = "Check for a missing statement terminator or add '}' to close the block.";
            } else if (has("INT_LIT") && has("ID") && has("(")) {
                d.category = "Invalid expression"; d.code = "E-SYN-005";
                d.message = "Expected an expression but found " + found;
                d.explanation = "An operand (number, variable, call, or parenthesised expression) is required here.";
                d.suggestion = "Provide a value or variable at this position.";
            } else {
                d.category = "Syntax error"; d.code = "E-SYN-006";
                d.message = "Unexpected " + found + "; expected " + expList;
                d.explanation = "The token does not fit the grammar at this point (LR(1) state " + std::to_string(state) + ").";
                d.suggestion = exp.size() == 1 ? "Insert " + terminalDisplay(exp[0]) + "." : "Replace it with one of: " + expList + ".";
            }
            // ---- panic-mode recovery (yacc style): `Stmt -> error ;` and `Stmt -> error` productions ----
            bool suppress = res.errorsReported > 0 && shiftsSinceError < 3;     // avoid cascades of follow-on errors
            if (!suppress) { res.diagnostics.add(std::move(d)); ++res.errorsReported; }
            int errSym = g.symbol("error");
            if (errSym < 0 || tok.cat == TokCat::Eof) return res;
            if (pos == lastErrorPos) { ++pos; if (pos >= tokens.size()) return res; }   // no progress since last recovery: force-skip one token
            lastErrorPos = pos;
            size_t popped = 0;
            while (stack.size() > 1 && T.table[stack.back().state][errSym].kind != LRAction::Shift) { stack.pop_back(); ++popped; }
            if (T.table[stack.back().state][errSym].kind != LRAction::Shift) return res;      // no enclosing statement list: unrecoverable
            auto en = std::make_unique<ParseNode>();
            en->symbol = "error"; en->isToken = true; en->lexeme = "";
            stack.push_back({T.table[stack.back().state][errSym].target, std::move(en)});
            size_t skipped = 0;
            while (pos < tokens.size() && tokens[pos].cat != TokCat::Eof) {
                int ts = g.symbol(tokens[pos].terminal);
                if (ts >= 0 && T.table[stack.back().state][ts].kind != LRAction::None) break;
                ++pos; ++skipped;
            }
            if (tokens[pos].cat == TokCat::Eof && T.table[stack.back().state][g.symbol("$")].kind == LRAction::None) return res;
            shiftsSinceError = 0;
            ++res.recoveries;
            if (res.trace.size() < static_cast<size_t>(kTraceCap)) {
                ParseStep rs; rs.step = res.totalSteps; rs.action = "recover"; rs.stack = stackStrings(); rs.lookahead = tokens[pos].terminal;
                for (size_t k = pos; k < tokens.size() && k < pos + 8; ++k) rs.input.push_back(tokens[k].cat == TokCat::Eof ? "$" : tokens[k].lexeme);
                rs.detail = "Panic-mode recovery: popped " + std::to_string(popped) + " stack entries to a state that accepts `error`, shifted `error`, skipped " + std::to_string(skipped) + " token(s) to a synchronizing token.";
                res.trace.push_back(std::move(rs));
            }
            continue;
        }
        if (record) res.trace.push_back(std::move(step));
    }
}

} // namespace nova
