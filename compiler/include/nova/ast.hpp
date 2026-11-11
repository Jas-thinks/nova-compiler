// Typed abstract syntax tree with source locations.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "nova/diagnostics.hpp"
#include "nova/parser.hpp"

namespace nova {

enum class Type { Int, Float, Bool, Void, Error, Str, ArrInt, ArrFloat, ArrBool };
const char* typeName(Type t);
inline bool isArrayType(Type t) { return t == Type::ArrInt || t == Type::ArrFloat || t == Type::ArrBool; }
inline bool isRefType(Type t) { return t == Type::Str || isArrayType(t); }      // heap-allocated, passed by reference
inline Type elemType(Type t) { return t == Type::ArrInt ? Type::Int : t == Type::ArrFloat ? Type::Float : t == Type::ArrBool ? Type::Bool : Type::Error; }
inline Type arrayOf(Type t) { return t == Type::Int ? Type::ArrInt : t == Type::Float ? Type::ArrFloat : t == Type::Bool ? Type::ArrBool : Type::Error; }

enum class NodeKind { Program, Function, Param, Block, VarDecl, Assign, If, While, Return, Print, ExprStmt,
                      IntLit, FloatLit, BoolLit, VarRef, Binary, Unary, Call,
                      StrLit, ArrayLit, NewArray, Index, IndexAssign };
const char* nodeKindName(NodeKind k);

struct Symbol;
struct FuncInfo;

struct Node {
    NodeKind kind;
    SourceLoc loc;
    explicit Node(NodeKind k) : kind(k) {}
    virtual ~Node() = default;
    virtual std::vector<Node*> children() { return {}; }
    virtual std::string label() const { return nodeKindName(kind); }  // short description for visualisation
    virtual Type exprType() const { return Type::Void; }
    Json toJson(int& nextId) const;
};
using NodeP = std::unique_ptr<Node>;

struct Expr : Node {
    Type type = Type::Error;
    using Node::Node;
    Type exprType() const override { return type; }
};
using ExprP = std::unique_ptr<Expr>;

struct Stmt : Node { using Node::Node; };
using StmtP = std::unique_ptr<Stmt>;

struct IntLit : Expr { int64_t value = 0; IntLit() : Expr(NodeKind::IntLit) { type = Type::Int; }
    std::string label() const override { return "IntLit " + std::to_string(value); } };
struct FloatLit : Expr { double value = 0; std::string text; FloatLit() : Expr(NodeKind::FloatLit) { type = Type::Float; }
    std::string label() const override { return "FloatLit " + text; } };
struct BoolLit : Expr { bool value = false; BoolLit() : Expr(NodeKind::BoolLit) { type = Type::Bool; }
    std::string label() const override { return std::string("BoolLit ") + (value ? "true" : "false"); } };
struct StrLit : Expr { std::string value; StrLit() : Expr(NodeKind::StrLit) { type = Type::Str; }
    std::string label() const override { return "StrLit \"" + value + "\""; } };
struct ArrayLit : Expr { std::vector<ExprP> elems; ArrayLit() : Expr(NodeKind::ArrayLit) {}
    std::vector<Node*> children() override { std::vector<Node*> v; for (auto& a : elems) v.push_back(a.get()); return v; } };
struct NewArray : Expr { Type elem = Type::Int; ExprP size; NewArray() : Expr(NodeKind::NewArray) {}
    std::vector<Node*> children() override { return {size.get()}; }
    std::string label() const override { return std::string("NewArray ") + typeName(elem) + "[]"; } };
struct Index : Expr { ExprP base, index; Index() : Expr(NodeKind::Index) {}
    std::vector<Node*> children() override { return {base.get(), index.get()}; } };
struct VarRef : Expr { std::string name; Symbol* sym = nullptr; VarRef() : Expr(NodeKind::VarRef) {}
    std::string label() const override { return "VarRef " + name; } };
struct Binary : Expr { std::string op; ExprP lhs, rhs; Binary() : Expr(NodeKind::Binary) {}
    std::vector<Node*> children() override { return {lhs.get(), rhs.get()}; }
    std::string label() const override { return "Binary " + op; } };
struct Unary : Expr { std::string op; ExprP operand; Unary() : Expr(NodeKind::Unary) {}
    std::vector<Node*> children() override { return {operand.get()}; }
    std::string label() const override { return "Unary " + op; } };
struct Call : Expr { std::string name; SourceLoc nameLoc; std::vector<ExprP> args; const FuncInfo* callee = nullptr; bool builtin = false;
    Call() : Expr(NodeKind::Call) {}
    std::vector<Node*> children() override { std::vector<Node*> v; for (auto& a : args) v.push_back(a.get()); return v; }
    std::string label() const override { return "Call " + name; } };

struct Block : Stmt { std::vector<StmtP> stmts; int scopeId = -1; Block() : Stmt(NodeKind::Block) {}
    std::vector<Node*> children() override { std::vector<Node*> v; for (auto& s : stmts) v.push_back(s.get()); return v; } };
struct VarDecl : Stmt { std::string name; SourceLoc nameLoc; Type declType = Type::Error; ExprP init; Symbol* sym = nullptr;
    VarDecl() : Stmt(NodeKind::VarDecl) {}
    std::vector<Node*> children() override { return {init.get()}; }
    std::string label() const override { return "VarDecl " + name + ": " + typeName(declType); } };
struct Assign : Stmt { std::string name; SourceLoc nameLoc; ExprP value; Symbol* sym = nullptr; Assign() : Stmt(NodeKind::Assign) {}
    std::vector<Node*> children() override { return {value.get()}; }
    std::string label() const override { return "Assign " + name; } };
struct IndexAssign : Stmt { std::string name; SourceLoc nameLoc; ExprP index, value; Symbol* sym = nullptr; IndexAssign() : Stmt(NodeKind::IndexAssign) {}
    std::vector<Node*> children() override { return {index.get(), value.get()}; }
    std::string label() const override { return "IndexAssign " + name + "[]"; } };
struct If : Stmt { ExprP cond; std::unique_ptr<Block> thenB; StmtP elseB; If() : Stmt(NodeKind::If) {}
    std::vector<Node*> children() override { std::vector<Node*> v{cond.get(), thenB.get()}; if (elseB) v.push_back(elseB.get()); return v; } };
struct While : Stmt { ExprP cond; std::unique_ptr<Block> body; While() : Stmt(NodeKind::While) {}
    std::vector<Node*> children() override { return {cond.get(), body.get()}; } };
struct Return : Stmt { ExprP value; Return() : Stmt(NodeKind::Return) {}
    std::vector<Node*> children() override { return value ? std::vector<Node*>{value.get()} : std::vector<Node*>{}; } };
struct Print : Stmt { ExprP value; Print() : Stmt(NodeKind::Print) {}
    std::vector<Node*> children() override { return {value.get()}; } };
struct ExprStmt : Stmt { ExprP expr; ExprStmt() : Stmt(NodeKind::ExprStmt) {}
    std::vector<Node*> children() override { return {expr.get()}; } };

struct Param : Node { std::string name; Type type = Type::Error; Symbol* sym = nullptr; Param() : Node(NodeKind::Param) {}
    std::string label() const override { return "Param " + name + ": " + typeName(type); } };
struct FuncDecl : Node { std::string name; SourceLoc nameLoc; std::vector<std::unique_ptr<Param>> params; Type ret = Type::Void;
    std::unique_ptr<Block> body; int scopeId = -1; FuncDecl() : Node(NodeKind::Function) {}
    std::vector<Node*> children() override { std::vector<Node*> v; for (auto& p : params) v.push_back(p.get()); v.push_back(body.get()); return v; }
    std::string label() const override { return "Function " + name + " -> " + typeName(ret); } };
struct Program : Node { std::vector<std::unique_ptr<FuncDecl>> functions; Program() : Node(NodeKind::Program) {}
    std::vector<Node*> children() override { std::vector<Node*> v; for (auto& f : functions) v.push_back(f.get()); return v; } };

// Convert a parse tree into a typed AST (the grammar is unambiguous, so this is purely structural).
std::unique_ptr<Program> buildAst(const ParseNode& root);

// Structural validation of an AST (optionally after semantic analysis): non-null children, sane source ranges,
// children contained in their parent's range, and - when typed - that every expression carries a concrete type.
// Returns human-readable problems; empty means valid.
std::vector<std::string> validateAst(Node* root, bool requireTypes);
// Canonical, stable textual form (S-expression) for golden tests: independent of source locations.
std::string astToSexpr(Node* root);

// Pre-order traversal helper.
template <class F> void walk(Node* n, F&& f) { f(n); for (Node* c : n->children()) if (c) walk(c, f); }

} // namespace nova
