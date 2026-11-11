// Semantic analysis: scopes, symbol tables, name resolution and static type checking.
#pragma once
#include <map>
#include <memory>
#include "nova/ast.hpp"

namespace nova {

struct Symbol {
    std::string name;
    std::string uniqueName;   // unique within the function; used by IR / bytecode
    Type type = Type::Error;
    enum Kind { Variable, Parameter } kind = Variable;
    SourceLoc declLoc;
    int scopeId = 0;
    std::string function;
    std::vector<SourceLoc> reads, writes;
};

struct Scope {
    int id = 0;
    int parent = -1;
    std::string kind;         // global | function | block
    std::string owner;        // enclosing function name
    SourceLoc loc;
    std::vector<Symbol*> symbols;
    std::vector<int> children;
};

struct FuncInfo {
    std::string name;
    std::vector<std::string> paramNames;
    std::vector<Type> params;
    Type ret = Type::Void;
    SourceLoc loc;
    bool builtin = false;
    int callCount = 0;
};

struct SemanticResult {
    std::vector<std::unique_ptr<Scope>> scopes;
    std::vector<std::unique_ptr<Symbol>> symbols;
    std::vector<std::unique_ptr<FuncInfo>> functions;
    DiagnosticList diagnostics;
    const FuncInfo* findFunction(const std::string& n) const {
        for (auto& f : functions) if (f->name == n) return f.get();
        return nullptr;
    }
    Json toJson() const;
};

// Annotates the AST in place (expression types, resolved symbols, callee info).
std::unique_ptr<SemanticResult> analyze(Program& program);

} // namespace nova
