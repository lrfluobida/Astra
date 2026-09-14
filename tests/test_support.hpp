#pragma once

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace astra::test {

using TestFunction = std::function<void()>;

struct TestCase {
    std::string name;
    TestFunction function;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

class Registrar {
public:
    Registrar(std::string name, TestFunction function) {
        registry().push_back({std::move(name), std::move(function)});
    }
};

inline void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

inline int run(const std::string& filter) {
    int failures = 0;
    int selected = 0;
    for (const auto& test : registry()) {
        if (!filter.empty() && test.name.find(filter) == std::string::npos) {
            continue;
        }
        ++selected;
        try {
            test.function();
            std::cout << "PASS " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
        }
    }
    if (selected == 0) {
        std::cerr << "No tests matched filter: " << filter << '\n';
        return 2;
    }
    return failures == 0 ? 0 : 1;
}

}  // namespace astra::test

#define ASTRA_TEST(name)                                                     \
    static void name();                                                      \
    static const ::astra::test::Registrar name##_registrar(#name, &name);   \
    static void name()
