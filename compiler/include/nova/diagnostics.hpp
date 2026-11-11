// Source locations, diagnostics and spelling-suggestion helpers.
#pragma once
#include <string>
#include <vector>
#include "nova/json.hpp"

namespace nova {

struct SourceLoc {
    int line = 1, col = 1;        // 1-based start
    int endLine = 1, endCol = 1;  // exclusive end column
    Json toJson() const {
        Json j = Json::object();
        j.set("line", line).set("col", col).set("endLine", endLine).set("endCol", endCol);
        return j;
    }
};

enum class Severity { Error, Warning, Info };

struct Diagnostic {
    Severity severity = Severity::Error;
    std::string phase;       // lexical | syntax | semantic | runtime
    std::string category;    // e.g. "Undefined variable"
    std::string code;        // e.g. "E-SEM-001"
    std::string message;
    std::string explanation;
    std::string suggestion;
    SourceLoc loc;
    Json toJson() const;
};

class DiagnosticList {
public:
    void add(Diagnostic d) { items_.push_back(std::move(d)); }
    const std::vector<Diagnostic>& items() const { return items_; }
    bool hasErrors() const { for (auto& d : items_) if (d.severity == Severity::Error) return true; return false; }
    size_t errorCount() const { size_t n = 0; for (auto& d : items_) if (d.severity == Severity::Error) ++n; return n; }
    Json toJson() const { Json a = Json::array(); for (auto& d : items_) a.push(d.toJson()); return a; }
private:
    std::vector<Diagnostic> items_;
};

// Levenshtein edit distance (dynamic programming, O(|a||b|)).
int editDistance(const std::string& a, const std::string& b);
// Best candidate within a length-scaled threshold; empty string when none is close enough.
std::string closestMatch(const std::string& word, const std::vector<std::string>& candidates);

} // namespace nova
