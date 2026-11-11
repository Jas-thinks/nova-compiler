// Context-free grammar representation, FIRST/FOLLOW sets and the canonical LR(1) construction.
#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>
#include "nova/json.hpp"

namespace nova {

struct Production {
    int lhs = 0;
    std::vector<int> rhs;  // empty for epsilon
};

class Grammar {
public:
    // The NOVA grammar (see docs/GRAMMAR.md). Built from the textual definition returned by novaGrammarText().
    static Grammar nova();
    static Grammar fromText(const std::string& text);

    std::vector<std::string> names;       // symbol id -> name
    std::vector<bool> terminal;           // symbol id -> is terminal
    std::map<std::string, int> ids;
    std::vector<Production> prods;        // prods[0] is the augmented production S' -> Start
    int eof = 0;                          // '$'
    int augStart = 0;                     // S'
    std::vector<bool> nullable;
    std::vector<std::set<int>> first, follow;

    int symbol(const std::string& n) const { auto it = ids.find(n); return it == ids.end() ? -1 : it->second; }
    std::string prodText(int p, int dot = -1) const;
    // FIRST(seq[from..]) followed by lookahead la
    std::set<int> firstOfSeq(const std::vector<int>& seq, size_t from, int la) const;
    Json toJson() const;
private:
    int intern(const std::string& n, bool isTerm);
    void computeSets();
};

const char* novaGrammarText();

struct LRAction {
    enum Kind : uint8_t { None, Shift, Reduce, Accept, Goto } kind = None;
    int target = 0;  // state for Shift/Goto, production for Reduce
};

struct ItemRow {  // an LR(0) core together with all its lookaheads
    int prod, dot;
    std::vector<int> lookaheads;
};

class LR1Table {
public:
    static LR1Table build(const Grammar& g);

    Grammar grammar;
    std::vector<std::vector<uint32_t>> kernels;   // per-state kernel LR(1) items (packed)
    std::vector<std::vector<LRAction>> table;     // [state][symbol]: ACTION for terminals, GOTO for nonterminals
    std::vector<std::string> conflicts;           // empty when the grammar is LR(1)
    int buildMillis = 0;

    size_t stateCount() const { return kernels.size(); }
    std::vector<uint32_t> closure(const std::vector<uint32_t>& kernel) const;
    std::vector<ItemRow> itemRows(const std::vector<uint32_t>& items) const;  // groups lookaheads
    std::vector<int> expectedTerminals(int state) const;
    std::string itemText(const ItemRow& r) const;
    Json summaryJson() const;                      // grammar, FIRST/FOLLOW, counts, conflicts
    Json tablesJson() const;                       // full ACTION/GOTO tables + kernels
    Json stateJson(int state) const;               // kernel + full closure for one state
};

inline uint32_t packItem(int prod, int dot, int la) { return (uint32_t(prod) << 16) | (uint32_t(dot) << 8) | uint32_t(la); }
inline int itemProd(uint32_t i) { return int(i >> 16); }
inline int itemDot(uint32_t i) { return int((i >> 8) & 0xff); }
inline int itemLa(uint32_t i) { return int(i & 0xff); }

} // namespace nova
