#pragma once
#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace doctest {
struct Approx {
    double value;
    explicit Approx(double v): value(v) {}
    template <class T> friend bool operator==(T lhs, const Approx& rhs) {
        const double a = static_cast<double>(lhs), b = rhs.value;
        const double tol = 1e-5 * (1.0 + std::max(std::fabs(a), std::fabs(b)));
        return std::fabs(a-b) <= tol;
    }
    template <class T> friend bool operator==(const Approx& lhs, T rhs) { return rhs == lhs; }
    template <class T> friend bool operator!=(T lhs, const Approx& rhs) { return !(lhs == rhs); }
    template <class T> friend bool operator!=(const Approx& lhs, T rhs) { return !(lhs == rhs); }
};
namespace detail {
struct Test { const char* name; void(*fn)(); };
inline std::vector<Test>& tests() { static std::vector<Test> v; return v; }
struct Registrar { Registrar(const char* n, void(*f)()) { tests().push_back({n,f}); } };
struct RequireFailure : std::exception {};
inline int failures = 0;
inline void fail(const char* kind, const char* expr, const char* file, int line, const std::string& msg={}, bool fatal=false) {
    ++failures;
    std::cerr << file << ':' << line << ": " << kind << " failed: " << expr;
    if(!msg.empty()) std::cerr << " | " << msg;
    std::cerr << '\n';
    if(fatal) throw RequireFailure{};
}
inline int run() {
    int failed_tests=0;
    for (auto& t: tests()) {
        int before=failures;
        try { t.fn(); } catch(const RequireFailure&) {} catch(const std::exception& e) { ++failures; std::cerr << "EXCEPTION in " << t.name << ": " << e.what() << '\n'; } catch(...) { ++failures; std::cerr << "UNKNOWN EXCEPTION in " << t.name << '\n'; }
        if(failures!=before) { ++failed_tests; std::cerr << "[FAILED] " << t.name << '\n'; }
    }
    std::cerr << "tests=" << tests().size() << " failed_tests=" << failed_tests << " assertions_failed=" << failures << '\n';
    return failures ? 1 : 0;
}
}
}

#define DT_CAT_I(a,b) a##b
#define DT_CAT(a,b) DT_CAT_I(a,b)
#define TEST_CASE(name) DT_TEST_CASE_IMPL(name, __COUNTER__)
#define DT_TEST_CASE_IMPL(name, n) \
    static void DT_CAT(dt_test_, n)(); \
    static ::doctest::detail::Registrar DT_CAT(dt_reg_, n)(name, &DT_CAT(dt_test_, n)); \
    static void DT_CAT(dt_test_, n)()
#define CHECK(expr) do { if(!(expr)) ::doctest::detail::fail("CHECK", #expr, __FILE__, __LINE__); } while(0)
#define CHECK_FALSE(expr) CHECK(!(expr))
#define REQUIRE(expr) do { if(!(expr)) ::doctest::detail::fail("REQUIRE", #expr, __FILE__, __LINE__, {}, true); } while(0)
#define REQUIRE_MESSAGE(expr, msg) do { if(!(expr)) { std::ostringstream dt_oss; dt_oss << msg; ::doctest::detail::fail("REQUIRE", #expr, __FILE__, __LINE__, dt_oss.str(), true); } } while(0)

#ifdef DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
int main() { return ::doctest::detail::run(); }
#endif
