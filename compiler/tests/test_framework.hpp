// Minimal self-registering unit-test framework (no external dependencies).
#pragma once
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct TestFailure : std::runtime_error { using std::runtime_error::runtime_error; };
struct TestCase { std::string suite, name; std::function<void()> fn; };
inline std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
struct Registrar { Registrar(const char* s, const char* n, std::function<void()> f) { registry().push_back({s, n, std::move(f)}); } };

#define NOVA_CAT2(a, b) a##b
#define NOVA_CAT(a, b) NOVA_CAT2(a, b)
#define TEST(suite, name) \
    static void NOVA_CAT(test_##suite##_, name)(); \
    static Registrar NOVA_CAT(reg_##suite##_, name)(#suite, #name, NOVA_CAT(test_##suite##_, name)); \
    static void NOVA_CAT(test_##suite##_, name)()

#define CHECK(cond) do { if (!(cond)) { std::ostringstream _os; _os << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond; throw TestFailure(_os.str()); } } while (0)
#define CHECK_EQ(a, b) do { auto _a = (a); auto _b = (b); if (!(_a == _b)) { std::ostringstream _os; _os << __FILE__ << ":" << __LINE__ << ": CHECK_EQ failed: " #a " == " #b " (" << _a << " vs " << _b << ")"; throw TestFailure(_os.str()); } } while (0)
