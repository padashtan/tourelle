#pragma once
// Minimal test harness: no third-party dependency. Register cases with TEST(),
// check with EXPECT()/EXPECT_EQ(); main() lives in TestMain.cpp.

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace tourelle::test {

struct Case {
    const char* name;
    std::function<void()> body;
};

inline std::vector<Case>& Registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& Failures() {
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> body) { Registry().push_back({name, std::move(body)}); }
};

inline void Fail(const char* file, int line, const std::string& message) {
    ++Failures();
    std::printf("  FAILED %s:%d: %s\n", file, line, message.c_str());
}

}  // namespace tourelle::test

#define TOURELLE_CONCAT2(a, b) a##b
#define TOURELLE_CONCAT(a, b) TOURELLE_CONCAT2(a, b)
#define TEST(name)                                                                                         \
    static void TOURELLE_CONCAT(test_, name)();                                                            \
    static ::tourelle::test::Registrar TOURELLE_CONCAT(registrar_, name)(#name, TOURELLE_CONCAT(test_, name)); \
    static void TOURELLE_CONCAT(test_, name)()

#define EXPECT(condition)                                                                     \
    do {                                                                                      \
        if (!(condition)) ::tourelle::test::Fail(__FILE__, __LINE__, "expected " #condition); \
    } while (0)

#define EXPECT_EQ(actual, expected)                                                                         \
    do {                                                                                                    \
        const auto& a_ = (actual);                                                                          \
        const auto& e_ = (expected);                                                                        \
        if (!(a_ == e_)) {                                                                                  \
            ::tourelle::test::Fail(__FILE__, __LINE__,                                                      \
                                   std::string(#actual " == " #expected " (got ") + std::to_string(a_) +    \
                                       ", expected " + std::to_string(e_) + ")");                           \
        }                                                                                                   \
    } while (0)
