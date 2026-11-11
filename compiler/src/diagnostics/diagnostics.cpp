#include "nova/diagnostics.hpp"
#include <algorithm>

namespace nova {

Json Diagnostic::toJson() const {
    Json j = Json::object();
    const char* sev = severity == Severity::Error ? "error" : severity == Severity::Warning ? "warning" : "info";
    j.set("severity", sev).set("phase", phase).set("category", category).set("code", code)
     .set("message", message).set("explanation", explanation).set("suggestion", suggestion)
     .set("loc", loc.toJson());
    return j;
}

int editDistance(const std::string& a, const std::string& b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<int>(j);
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = static_cast<int>(i);
        for (size_t j = 1; j <= b.size(); ++j) {
            int sub = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, sub});
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

std::string closestMatch(const std::string& word, const std::vector<std::string>& candidates) {
    int limit = word.size() <= 3 ? 1 : (word.size() <= 7 ? 2 : 3);
    int best = limit + 1;
    std::string out;
    for (auto& c : candidates) {
        if (c == word) continue;
        int d = editDistance(word, c);
        if (d < best) { best = d; out = c; }
    }
    return best <= limit ? out : std::string();
}

} // namespace nova
