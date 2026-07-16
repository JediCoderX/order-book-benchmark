#pragma once

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace testing {

using TestFunc = void (*)();

struct TestCase {
    const char* name;
    TestFunc func;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* name, TestFunc func) { registry().push_back({name, func}); }
};

inline int runAll() {
    int failures = 0;
    for (const auto& test : registry()) {
        try {
            test.func();
            std::cout << "[PASS] " << test.name << "\n";
        } catch (const std::exception& e) {
            std::cout << "[FAIL] " << test.name << ": " << e.what() << "\n";
            ++failures;
        }
    }
    std::cout << (registry().size() - failures) << "/" << registry().size() << " passed\n";
    return failures == 0 ? 0 : 1;
}

} // namespace testing

#define TEST(name)                                                              \
    static void test_##name();                                                  \
    static ::testing::Registrar registrar_##name(#name, test_##name);           \
    static void test_##name()

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                          \
            std::ostringstream oss;                                             \
            oss << "CHECK failed: " #cond " (" << __FILE__ << ":" << __LINE__ << ")"; \
            throw std::runtime_error(oss.str());                                \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                          \
    do {                                                                         \
        auto va = (a);                                                          \
        auto vb = (b);                                                          \
        if (!(va == vb)) {                                                      \
            std::ostringstream oss;                                             \
            oss << "CHECK_EQ failed: " #a " != " #b " (" << va << " vs " << vb   \
                << ") at " << __FILE__ << ":" << __LINE__;                      \
            throw std::runtime_error(oss.str());                                \
        }                                                                        \
    } while (0)
