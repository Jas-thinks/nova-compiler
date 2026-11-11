#include "nova/grammar.hpp"
#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <unordered_set>

namespace nova {

std::vector<uint32_t> LR1Table::closure(const std::vector<uint32_t>& kernel) const {
    const Grammar& g = grammar;
    std::unordered_set<uint32_t> seen(kernel.begin(), kernel.end());
    std::vector<uint32_t> work(kernel.begin(), kernel.end());
    for (size_t w = 0; w < work.size(); ++w) {
        uint32_t it = work[w];
        const Production& p = g.prods[itemProd(it)];
        size_t dot = itemDot(it);
        if (dot >= p.rhs.size()) continue;
        int b = p.rhs[dot];
        if (g.terminal[b]) continue;
        std::set<int> la = g.firstOfSeq(p.rhs, dot + 1, itemLa(it));
        for (size_t pi = 0; pi < g.prods.size(); ++pi) {
            if (g.prods[pi].lhs != b) continue;
            for (int t : la) {
                uint32_t ni = packItem(static_cast<int>(pi), 0, t);
                if (seen.insert(ni).second) work.push_back(ni);
            }
        }
    }
    std::sort(work.begin(), work.end());
    return work;
}

LR1Table LR1Table::build(const Grammar& g) {
    auto t0 = std::chrono::steady_clock::now();
    LR1Table t;
    t.grammar = g;
    std::map<std::vector<uint32_t>, int> index;
    std::vector<std::map<int, int>> trans;
    std::vector<uint32_t> start = {packItem(0, 0, g.eof)};
    t.kernels.push_back(start); index[start] = 0;
    for (size_t s = 0; s < t.kernels.size(); ++s) {
        std::vector<uint32_t> cl = t.closure(t.kernels[s]);
        std::map<int, std::vector<uint32_t>> next;
        for (uint32_t it : cl) {
            const Production& p = g.prods[itemProd(it)];
            size_t dot = itemDot(it);
            if (dot < p.rhs.size()) next[p.rhs[dot]].push_back(packItem(itemProd(it), static_cast<int>(dot) + 1, itemLa(it)));
        }
        trans.emplace_back();
        for (auto& [sym, kern] : next) {
            std::sort(kern.begin(), kern.end());
            auto it = index.find(kern);
            int id;
            if (it == index.end()) { id = static_cast<int>(t.kernels.size()); index[kern] = id; t.kernels.push_back(kern); }
            else id = it->second;
            trans[s][sym] = id;
        }
    }
    size_t S = t.kernels.size(), N = g.names.size();
    t.table.assign(S, std::vector<LRAction>(N));
    auto setAction = [&](size_t st, int sym, LRAction a) {
        LRAction& cur = t.table[st][sym];
        if (cur.kind == LRAction::None) { cur = a; return; }
        if (cur.kind == a.kind && cur.target == a.target) return;
        auto desc = [&](const LRAction& x) {
            return x.kind == LRAction::Shift ? "shift " + std::to_string(x.target)
                 : x.kind == LRAction::Reduce ? "reduce " + g.prodText(x.target) : std::string("accept");
        };
        t.conflicts.push_back("state " + std::to_string(st) + " on '" + g.names[sym] + "': " + desc(cur) + " / " + desc(a));
    };
    for (size_t s = 0; s < S; ++s) {
        for (auto& [sym, to] : trans[s]) {
            if (g.terminal[sym]) setAction(s, sym, {LRAction::Shift, to});
            else t.table[s][sym] = {LRAction::Goto, to};
        }
        for (uint32_t it : t.closure(t.kernels[s])) {
            const Production& p = g.prods[itemProd(it)];
            if (static_cast<size_t>(itemDot(it)) < p.rhs.size()) continue;
            if (itemProd(it) == 0) setAction(s, itemLa(it), {LRAction::Accept, 0});
            else setAction(s, itemLa(it), {LRAction::Reduce, itemProd(it)});
        }
    }
    t.buildMillis = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    return t;
}

std::vector<int> LR1Table::expectedTerminals(int state) const {
    std::vector<int> out;
    for (size_t s = 0; s < grammar.names.size(); ++s)
        if (grammar.terminal[s] && grammar.names[s] != "error" && table[state][s].kind != LRAction::None) out.push_back(static_cast<int>(s));
    return out;
}

std::vector<ItemRow> LR1Table::itemRows(const std::vector<uint32_t>& items) const {
    std::vector<ItemRow> rows;
    std::map<std::pair<int, int>, size_t> idx;
    for (uint32_t it : items) {
        auto key = std::make_pair(itemProd(it), itemDot(it));
        auto f = idx.find(key);
        if (f == idx.end()) { idx[key] = rows.size(); rows.push_back({key.first, key.second, {itemLa(it)}}); }
        else rows[f->second].lookaheads.push_back(itemLa(it));
    }
    return rows;
}

std::string LR1Table::itemText(const ItemRow& r) const {
    std::string s = "[" + grammar.prodText(r.prod, r.dot) + ", ";
    for (size_t i = 0; i < r.lookaheads.size(); ++i) { if (i) s += "/"; s += grammar.names[r.lookaheads[i]]; }
    return s + "]";
}

Json LR1Table::summaryJson() const {
    Json j = Json::object();
    j.set("algorithm", "Canonical LR(1)").set("states", static_cast<int>(stateCount()))
     .set("buildMillis", buildMillis).set("grammar", grammar.toJson());
    Json c = Json::array();
    for (auto& s : conflicts) c.push(s);
    j.set("conflicts", c);
    int shifts = 0, reduces = 0, gotos = 0;
    for (auto& row : table) for (auto& a : row) {
        if (a.kind == LRAction::Shift) ++shifts; else if (a.kind == LRAction::Reduce) ++reduces; else if (a.kind == LRAction::Goto) ++gotos;
    }
    j.set("shiftEntries", shifts).set("reduceEntries", reduces).set("gotoEntries", gotos);
    return j;
}

Json LR1Table::tablesJson() const {
    Json j = summaryJson();
    Json states = Json::array();
    for (size_t s = 0; s < stateCount(); ++s) {
        Json st = Json::object();
        st.set("id", static_cast<int>(s));
        Json kern = Json::array();
        for (auto& r : itemRows(kernels[s])) kern.push(itemText(r));
        st.set("kernel", kern);
        Json act = Json::object(), gt = Json::object();
        for (size_t sym = 0; sym < grammar.names.size(); ++sym) {
            const LRAction& a = table[s][sym];
            if (a.kind == LRAction::None) continue;
            std::string txt = a.kind == LRAction::Shift ? "s" + std::to_string(a.target)
                            : a.kind == LRAction::Reduce ? "r" + std::to_string(a.target)
                            : a.kind == LRAction::Accept ? "acc" : std::to_string(a.target);
            (a.kind == LRAction::Goto ? gt : act).set(grammar.names[sym], txt);
        }
        st.set("action", act).set("goto", gt);
        states.push(st);
    }
    j.set("stateList", states);
    return j;
}

Json LR1Table::stateJson(int state) const {
    Json j = Json::object();
    j.set("id", state);
    Json kern = Json::array(), clos = Json::array();
    for (auto& r : itemRows(kernels[state])) kern.push(itemText(r));
    for (auto& r : itemRows(closure(kernels[state]))) clos.push(itemText(r));
    j.set("kernel", kern).set("closure", clos);
    return j;
}

} // namespace nova
