// Host test: byte-pattern compiler + scanner.
//   g++ -std=c++20 -DMK8AP_HOST_TEST switch-mod/tests/test_pattern.cpp \
//       switch-mod/src/sym/PatternResolver.cpp -Iswitch-mod/src

#include <cstdio>
#include <cstring>

#include "sym/PatternResolver.hpp"

using namespace mk8ap::sym;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

int main() {
    CompiledPattern p;
    CHECK(compilePattern("8C 01 0B 9B ?? 45 40 B9", p));
    CHECK(p.len == 8);
    CHECK(p.mask[4] == 0 && p.mask[0] == 1 && p.bytes[0] == 0x8C && p.bytes[7] == 0xB9);
    CHECK(compilePattern("aa ? bb", p) && p.len == 3 && p.mask[1] == 0);
    CHECK(!compilePattern("ZZ", p));
    CHECK(!compilePattern("", p));
    CHECK(!compilePattern("A", p));

    const std::uint8_t hay[] = {0x00, 0x8C, 0x01, 0x0B, 0x9B, 0x77, 0x45, 0x40, 0xB9, 0x00,
                                0x8C, 0x01, 0x0B, 0x9B, 0x11, 0x45, 0x40, 0xB9};
    CHECK(compilePattern("8C 01 0B 9B ?? 45 40 B9", p));
    std::int64_t second = -1;
    CHECK(scan(hay, sizeof(hay), p, &second) == 1);
    CHECK(second == 10);
    CHECK(compilePattern("11 45 40 B9", p));
    CHECK(scan(hay, sizeof(hay), p) == 14);
    CHECK(compilePattern("11 45 40 B9 FF", p));
    CHECK(scan(hay, sizeof(hay), p) == -1);
    CHECK(compilePattern("?? ??", p));
    CHECK(scan(hay, sizeof(hay), p) == 0);
    CHECK(scan(hay, 2, p) == 0);
    CHECK(compilePattern("00", p));
    CHECK(scan(nullptr, 0, p) == -1);

    // Table sanity: every entry compiles (or is an explicit placeholder).
    struct Row { const char* name; const char* mangled; const char* pattern; std::int64_t off; std::int64_t off305; };
    static const Row rows[] = {
#include "sym/SymbolTable.inc"
    };
    int n = 0;
    for (const auto& r : rows) {
        ++n;
        if (r.pattern[0]) {
            CompiledPattern cp;
            if (!compilePattern(r.pattern, cp)) { std::printf("bad pattern: %s\n", r.name); ++g_fail; }
        } else if (!r.off305) {
            std::printf("note: placeholder %s\n", r.name);
        }
    }
    CHECK(n >= 15);

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("test_pattern OK (%d table rows)\n", n);
    return 0;
}
