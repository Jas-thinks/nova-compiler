// NOVA-SQL: a SQL compiler + in-memory engine. Pipeline: lexer -> recursive-descent parser -> binder/type checker ->
// logical plan -> rule-based optimizer -> expression bytecode (SQL-VM) -> executor. Semantics follow SQLite where SQL
// leaves room (NULL ordering, integer division, LIKE case-insensitivity), which is what the differential tests check.
#pragma once
#include <string>
#include "nova/json.hpp"

namespace nova {

// Runs a ';'-separated script against a fresh in-memory database and returns the complete compilation + execution
// report (diagnostics, AST, plans before/after optimization with a rule history, expression bytecode, results, stats).
Json runSqlScript(const std::string& script);

} // namespace nova
