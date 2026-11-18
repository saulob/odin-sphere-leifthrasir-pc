// The counters and helpers the cheat test files share.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace cheat_test {

using Bytes = std::vector<uint8_t>;

inline int g_checks = 0;
inline int g_failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        ++cheat_test::g_checks;                                                                \
        if (!(condition)) {                                                                    \
            ++cheat_test::g_failures;                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                  \
        }                                                                                      \
    } while (0)

inline Bytes Hex(const char* text) {
    Bytes out;
    for (const char* p = text; *p;) {
        if (*p == ' ') {
            ++p;
            continue;
        }
        out.push_back(static_cast<uint8_t>(std::strtoul(std::string(p, 2).c_str(), nullptr, 16)));
        p += 2;
    }
    return out;
}

// The session, menu, hotkey, input and persistence tests (cheats_menu_tests.cpp).
void RunMenuTests();

// The Experience Multiplier (experience_tests.cpp).
void RunExperienceTests();

// The cheats in the Classic engine and the build x mode x cheat matrix (cheats_classic_tests.cpp).
void RunClassicTests();

} // namespace cheat_test
