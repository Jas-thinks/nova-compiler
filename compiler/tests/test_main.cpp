#include <cstring>
#include <iostream>
#include "test_framework.hpp"
#include "nova/json.hpp"

int main(int argc, char** argv) {
    bool json = false;
    std::string filter;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--json")) json = true;
        else filter = argv[i];
    }
    int passed = 0, failed = 0;
    nova::Json results = nova::Json::array();
    for (auto& t : registry()) {
        if (!filter.empty() && t.suite.find(filter) == std::string::npos) continue;
        std::string msg;
        bool ok = true;
        try { t.fn(); } catch (const std::exception& e) { ok = false; msg = e.what(); }
        ok ? ++passed : ++failed;
        nova::Json r = nova::Json::object();
        r.set("suite", t.suite).set("name", t.name).set("passed", ok).set("message", msg);
        results.push(r);
        if (!json) std::cout << (ok ? "[ PASS ] " : "[ FAIL ] ") << t.suite << "." << t.name << (ok ? "" : "\n         " + msg) << "\n";
    }
    if (json) {
        nova::Json out = nova::Json::object();
        out.set("passed", passed).set("failed", failed).set("total", passed + failed).set("tests", results);
        std::cout << out.dump() << "\n";
    } else {
        std::cout << "\n" << passed << " passed, " << failed << " failed\n";
    }
    return failed ? 1 : 0;
}
