// Host test: byte-pattern compiler, relaxed matcher, scanner, entry resolution.
//   g++ -std=c++20 -DMK8AP_HOST_TEST switch-mod/tests/test_pattern.cpp
//       switch-mod/src/sym/PatternResolver.cpp -Iswitch-mod/src

#include <cstdio>
#include <cstring>
#include <vector>

#include "sym/PatternResolver.hpp"

using namespace mk8ap::sym;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static void put32(std::vector<std::uint8_t>& v, std::size_t off, std::uint32_t w) {
    v[off] = w & 0xFF; v[off + 1] = (w >> 8) & 0xFF; v[off + 2] = (w >> 16) & 0xFF; v[off + 3] = (w >> 24) & 0xFF;
}

int main() {
    CompiledPattern p;
    CHECK(compilePattern("8C 01 0B 9B ?? 45 40 B9", p));
    CHECK(p.len == 8);
    CHECK(p.mask[4] == 0x00 && p.mask[0] == 0xFF && p.bytes[0] == 0x8C && p.bytes[7] == 0xB9);
    CHECK(compilePattern("aa ? bb", p) && p.len == 3 && p.mask[1] == 0);
    CHECK(!compilePattern("ZZ", p));
    CHECK(!compilePattern("", p));
    CHECK(!compilePattern("A", p));

    const std::uint8_t hay[] = {0x00, 0x8C, 0x01, 0x0B, 0x9B, 0x77, 0x45, 0x40, 0xB9, 0x00,
                                0x8C, 0x01, 0x0B, 0x9B, 0x11, 0x45, 0x40, 0xB9};
    CHECK(compilePattern("8C 01 0B 9B ?? 45 40 B9", p));
    int n = 0;
    CHECK(scan(hay, sizeof(hay), p, 1, &n) == 1);
    CHECK(n == 2);
    CHECK(compilePattern("11 45 40 B9", p));
    CHECK(scan(hay, sizeof(hay), p) == 14);
    CHECK(scan(hay, sizeof(hay), p, 4) == -1);  // not instruction-aligned
    CHECK(compilePattern("11 45 40 B9 FF", p));
    CHECK(scan(hay, sizeof(hay), p) == -1);
    CHECK(compilePattern("?? ??", p));
    CHECK(scan(hay, sizeof(hay), p) == 0);
    CHECK(compilePattern("00", p));
    CHECK(scan(nullptr, 0, p) == -1);

    // ---- relaxed form ------------------------------------------------------
    // ui::SetNextCourse fragment authored on 3.0.5: "09 45 97 B9" is
    // LDRSW X9,[X8,#0x1744]. Ro's stream shows that member moved in 4.0.0,
    // so the same instruction there carries a different imm12.
    CHECK(compilePattern("08 19 40 F9 09 45 97 B9 0A 39 54 B9 29 05 00 91", p));
    CompiledPattern r;
    CHECK(relaxPattern(p, r));
    CHECK(r.len == 16);
    // load/store unsigned imm12 keeps 0xFFC003FF -> bytes: FF 03 C0 FF
    CHECK(r.mask[4] == 0xFF && r.mask[5] == 0x03 && r.mask[6] == 0xC0 && r.mask[7] == 0xFF);
    // ADD imm12 (29 05 00 91) relaxed the same way
    CHECK(r.mask[12] == 0xFF && r.mask[13] == 0x03 && r.mask[14] == 0xC0 && r.mask[15] == 0xFF);
    CHECK(compilePattern("AA BB CC", p));
    CHECK(!relaxPattern(p, r));  // not a whole number of instructions

    // Synthetic 4.0.0 text: same code as the 3.0.5 pattern, but the member
    // offset moved (0x1744 -> 0x1844) and a BL distance changed.
    std::vector<std::uint8_t> text(0x400, 0);
    for (std::size_t i = 0; i < text.size(); i += 4) put32(text, i, 0xD503201F);  // NOP
    const std::size_t at = 0x120;
    put32(text, at + 0,  0xF9401908);  // LDR X8,[X8,#0x30]
    put32(text, at + 4,  0xB9984509);  // LDRSW X9,[X8,#0x1844]   (3.0.5: B9974509)
    put32(text, at + 8,  0x97FFF123);  // BL elsewhere            (3.0.5: 94000010)
    put32(text, at + 12, 0x91000529);  // ADD X9,X9,#1
    Entry e{"test::fn", "", "08 19 40 F9 09 45 97 B9 10 00 00 94 29 05 00 91", -0x20, 0};
    How how = How::Unresolved;
    std::int64_t rel = resolveEntry(e, text.data(), text.size(), how);
    CHECK(rel == static_cast<std::int64_t>(at) - 0x20);
    CHECK(how == How::PatternRelaxed);

    // Exact match is used when the bytes are unchanged.
    put32(text, at + 4, 0xB9974509);
    put32(text, at + 8, 0x94000010);
    rel = resolveEntry(e, text.data(), text.size(), how);
    CHECK(rel == static_cast<std::int64_t>(at) - 0x20 && how == How::Pattern);

    // An ambiguous exact match is not trusted.
    const std::size_t at2 = 0x300;
    put32(text, at2 + 0,  0xF9401908);
    put32(text, at2 + 4,  0xB9974509);
    put32(text, at2 + 8,  0x94000010);
    put32(text, at2 + 12, 0x91000529);
    rel = resolveEntry(e, text.data(), text.size(), how);
    CHECK(rel == -1 && how == How::Unresolved);

    // Relaxed must be unique too.
    put32(text, at + 4, 0xB9984509);
    put32(text, at2 + 4, 0xB9994509);
    put32(text, at2 + 8, 0x94000444);
    rel = resolveEntry(e, text.data(), text.size(), how);
    CHECK(rel == -1 && how == How::Unresolved);
    put32(text, at2, 0xD503201F);  // remove the second site again

    // A published address is used as-is, and the pattern result is reported
    // separately for cross-checking.
    Entry a{"test::addr", "", "", 0, 0x200};
    CHECK(resolveEntry(a, text.data(), text.size(), how) == 0x200 && how == How::Address);
    Entry both{"test::both", "", "08 19 40 F9 09 45 97 B9 10 00 00 94 29 05 00 91", -0x20, 0x2A0};
    std::int64_t prel = -1;
    rel = resolveEntry(both, text.data(), text.size(), how, &prel);
    CHECK(rel == 0x2A0 && how == How::Address && prel == static_cast<std::int64_t>(at) - 0x20);
    // Out-of-range addresses are rejected.
    Entry far{"test::far", "", "", 0, 0x100000};
    CHECK(resolveEntry(far, text.data(), text.size(), how) == -1 && how == How::Unresolved);

    // Build id pin
    CHECK(isSupportedBuildId("158C0C7399AABD7BF83795C2EF3484C7"));
    CHECK(!isSupportedBuildId("2C336A9BCF79C3040CE506CDD391B578"));  // 4.0.0
    CHECK(!isSupportedBuildId("FE941ED5BA14BE5D505698DA1BBF4FE7"));  // 3.0.5
    CHECK(!isSupportedBuildId(""));
    CHECK(!isSupportedBuildId(nullptr));

    // Table sanity: every pattern compiles and relaxes; every entry has a locator.
    static const Entry rows[] = {
#include "sym/SymbolTable.inc"
    };
    int rows_n = 0, with_addr = 0;
    for (const auto& row : rows) {
        ++rows_n;
        if (row.address) ++with_addr;
        if (row.pattern[0]) {
            CompiledPattern cp, cr;
            if (!compilePattern(row.pattern, cp)) { std::printf("bad pattern: %s\n", row.name); ++g_fail; }
            else if (!relaxPattern(cp, cr)) { std::printf("unaligned pattern: %s\n", row.name); ++g_fail; }
        } else if (!row.address) {
            std::printf("no locator: %s\n", row.name); ++g_fail;
        }
    }
    CHECK(rows_n >= 15);
    CHECK(with_addr >= 5);

    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("test_pattern OK (%d table rows, %d with published 4.0.1 addresses)\n", rows_n, with_addr);
    return 0;
}
