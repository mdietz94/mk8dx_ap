// See PatternResolver.hpp.

#include "PatternResolver.hpp"

#include <cstring>

#ifndef MK8AP_HOST_TEST
#  include <hk/ro/RoUtil.h>
#  include <hk/types.h>
#  include "../util/Log.hpp"
#endif

namespace mk8ap::sym {

namespace {

const Entry kTable[] = {
#include "SymbolTable.inc"
};
constexpr int kCount = static_cast<int>(sizeof(kTable) / sizeof(kTable[0]));

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Bits of an AArch64 instruction that must still match in the relaxed form.
std::uint32_t relaxedKeepMask(std::uint32_t w) {
    if ((w & 0x7C000000u) == 0x14000000u) return 0xFC000000u;  // B / BL imm26
    if ((w & 0xFF000010u) == 0x54000000u) return 0xFF00001Fu;  // B.cond imm19
    if ((w & 0x7E000000u) == 0x34000000u) return 0xFF00001Fu;  // CBZ / CBNZ imm19
    if ((w & 0x7E000000u) == 0x36000000u) return 0xFFF8001Fu;  // TBZ / TBNZ imm14
    if ((w & 0x1F000000u) == 0x10000000u) return 0x9F00001Fu;  // ADR / ADRP imm21
    if ((w & 0x3B000000u) == 0x18000000u) return 0xFF00001Fu;  // LDR literal imm19
    if ((w & 0x3B000000u) == 0x39000000u) return 0xFFC003FFu;  // load/store unsigned imm12
    if ((w & 0x3B200C00u) == 0x38000000u) return 0xFFE00FFFu;  // load/store unscaled imm9
    if ((w & 0x1F000000u) == 0x11000000u) return 0xFFC003FFu;  // ADD / SUB imm12
    if ((w & 0x1F800000u) == 0x12800000u) return 0xFFE0001Fu;  // MOVN/MOVZ/MOVK imm16
    return 0xFFFFFFFFu;
}

}  // namespace

bool compilePattern(const char* text, CompiledPattern& out) {
    out.len = 0;
    if (!text) return false;
    const char* p = text;
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        if (out.len >= kMaxPatternBytes) return false;
        if (*p == '?') {
            ++p;
            if (*p == '?') ++p;
            out.bytes[out.len] = 0;
            out.mask[out.len] = 0x00;
        } else {
            const int hi = hexNibble(p[0]);
            const int lo = p[1] ? hexNibble(p[1]) : -1;
            if (hi < 0 || lo < 0) return false;
            p += 2;
            out.bytes[out.len] = static_cast<std::uint8_t>((hi << 4) | lo);
            out.mask[out.len] = 0xFF;
        }
        ++out.len;
        if (*p && *p != ' ' && *p != '\t') return false;
    }
    return out.len > 0;
}

bool relaxPattern(const CompiledPattern& in, CompiledPattern& out) {
    if (in.len == 0 || (in.len % 4) != 0) return false;
    out = in;
    for (std::size_t i = 0; i + 4 <= in.len; i += 4) {
        // Only classify instructions whose four bytes are all fixed; a
        // hand-wildcarded instruction is left as authored.
        if (in.mask[i] != 0xFF || in.mask[i + 1] != 0xFF ||
            in.mask[i + 2] != 0xFF || in.mask[i + 3] != 0xFF) {
            continue;
        }
        const std::uint32_t w = static_cast<std::uint32_t>(in.bytes[i]) |
                                (static_cast<std::uint32_t>(in.bytes[i + 1]) << 8) |
                                (static_cast<std::uint32_t>(in.bytes[i + 2]) << 16) |
                                (static_cast<std::uint32_t>(in.bytes[i + 3]) << 24);
        const std::uint32_t keep = relaxedKeepMask(w);
        for (int b = 0; b < 4; ++b) {
            const std::uint8_t m = static_cast<std::uint8_t>((keep >> (8 * b)) & 0xFF);
            out.mask[i + b] = m;
            out.bytes[i + b] = static_cast<std::uint8_t>(in.bytes[i + b] & m);
        }
    }
    return true;
}

std::int64_t scan(const std::uint8_t* hay, std::size_t len,
                  const CompiledPattern& pat, std::size_t align, int* count) {
    if (count) *count = 0;
    if (!hay || pat.len == 0 || len < pat.len) return -1;
    if (align == 0) align = 1;
    // Anchor on the first fully fixed byte to keep the inner loop cheap.
    std::size_t anchor = 0;
    while (anchor < pat.len && pat.mask[anchor] != 0xFF) ++anchor;
    const bool has_anchor = anchor < pat.len;
    const std::uint8_t first = has_anchor ? pat.bytes[anchor] : 0;
    std::int64_t found = -1;
    const std::size_t last_start = len - pat.len;
    for (std::size_t i = 0; i <= last_start; i += align) {
        if (has_anchor && hay[i + anchor] != first) continue;
        bool ok = true;
        for (std::size_t j = 0; j < pat.len; ++j) {
            if (((hay[i + j] ^ pat.bytes[j]) & pat.mask[j]) != 0) { ok = false; break; }
        }
        if (!ok) continue;
        if (found < 0) {
            found = static_cast<std::int64_t>(i);
            if (count) *count = 1;
            else return found;
        } else {
            *count = 2;
            return found;
        }
    }
    return found;
}

bool isSupportedBuildId(const char* hex) {
    return hex && std::strncmp(hex, kBuildId, 16) == 0;
}

std::int64_t resolveEntry(const Entry& e, const std::uint8_t* text,
                          std::size_t text_len, How& how,
                          std::int64_t* pattern_rel) {
    how = How::Unresolved;
    std::int64_t rel = -1;

    CompiledPattern pat;
    if (e.pattern && e.pattern[0] && compilePattern(e.pattern, pat)) {
        const std::size_t align = (pat.len % 4 == 0) ? 4 : 1;
        int n = 0;
        std::int64_t hit = scan(text, text_len, pat, align, &n);
        if (hit >= 0 && n == 1) {
            how = How::Pattern;
        } else {
            // No exact hit (or an ambiguous one): try the relaxed form, which
            // must be unique to be trusted.
            hit = -1;
            CompiledPattern relaxed;
            if (n == 0 && relaxPattern(pat, relaxed)) {
                hit = scan(text, text_len, relaxed, 4, &n);
                if (hit >= 0 && n == 1) how = How::PatternRelaxed;
                else hit = -1;
            }
        }
        if (hit >= 0) {
            rel = hit + e.offset;
            if (rel < 0 || static_cast<std::size_t>(rel) >= text_len) {
                rel = -1;
                how = How::Unresolved;
            }
        }
    }
    if (pattern_rel) *pattern_rel = rel;

    if (e.address > 0 && static_cast<std::size_t>(e.address) < text_len) {
        how = How::Address;
        rel = e.address;
    }
    return rel;
}

#ifndef MK8AP_HOST_TEST

namespace {

struct Slot {
    std::uintptr_t addr = 0;
    How how = How::Unresolved;
};
Slot g_slots[kCount];
bool g_resolved = false;
bool g_supported = false;
int g_resolved_count = 0;
char g_build_hex[33] = "";
std::uintptr_t g_text_start = 0;

int indexOf(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < kCount; ++i) {
        if (std::strcmp(kTable[i].name, name) == 0) return i;
    }
    return -1;
}

const char* howName(How h) {
    switch (h) {
        case How::Address: return "published address";
        case How::Pattern: return "pattern";
        case How::PatternRelaxed: return "RELAXED pattern";
        case How::Unresolved: break;
    }
    return "unresolved";
}

}  // namespace

void resolveAll() {
    if (g_resolved) return;
    g_resolved = true;

    auto* main = hk::ro::getMainModule();
    if (!main) {
        MK8AP_LOG_ERROR("[sym] getMainModule() returned null; nothing resolved");
        return;
    }
    const auto text = main->text();
    g_text_start = text.start();

    const u8* bid = main->getBuildId();
    if (bid) {
        static constexpr char kHex[] = "0123456789ABCDEF";
        for (int i = 0; i < 16; ++i) {
            g_build_hex[i * 2]     = kHex[bid[i] >> 4];
            g_build_hex[i * 2 + 1] = kHex[bid[i] & 0xF];
        }
        g_build_hex[32] = '\0';
    }
    g_supported = isSupportedBuildId(g_build_hex);
    MK8AP_LOG_INFO("[sym] main text=%p size=0x%zx build=%s",
                   reinterpret_cast<void*>(text.start()),
                   static_cast<std::size_t>(text.size()),
                   g_build_hex[0] ? g_build_hex : "(unknown)");
    if (!g_supported) {
        MK8AP_LOG_ERROR("[sym] this is not MK8DX 4.0.1 (expected build id %s...). "
                        "The mod only supports 4.0.1; no hooks will be installed.",
                        kBuildId);
        return;
    }

    const auto* hay = reinterpret_cast<const std::uint8_t*>(text.start());
    const std::size_t len = text.size();

    for (int i = 0; i < kCount; ++i) {
        const Entry& e = kTable[i];
        Slot& s = g_slots[i];
        std::int64_t pattern_rel = -1;
        const std::int64_t rel = resolveEntry(e, hay, len, s.how, &pattern_rel);
        if (rel < 0) {
            MK8AP_LOG_WARN("[sym] %-42s UNRESOLVED", e.name);
            continue;
        }
        s.addr = text.start() + static_cast<std::uintptr_t>(rel);
        ++g_resolved_count;
        MK8AP_LOG_INFO("[sym] %-42s +0x%08llx (%s)", e.name,
                       static_cast<unsigned long long>(rel), howName(s.how));
        if (s.how == How::Address && e.pattern[0] && pattern_rel != rel) {
            // Informational: tells us how well the 3.0.5-era patterns carry
            // over, i.e. how much to trust the pattern-only entries.
            if (pattern_rel < 0) {
                MK8AP_LOG_INFO("[sym]   (its pattern did not match 4.0.1)");
            } else {
                MK8AP_LOG_WARN("[sym]   its pattern resolved to +0x%llx instead; "
                               "published address used",
                               static_cast<unsigned long long>(pattern_rel));
            }
        }
    }
    MK8AP_LOG_INFO("[sym] resolved %d/%d", g_resolved_count, kCount);
}

bool isSupportedBuild() { return g_supported; }

std::uintptr_t addr(const char* name) {
    const int i = indexOf(name);
    if (i < 0) {
        MK8AP_LOG_WARN("[sym] unknown symbol name '%s'", name ? name : "(null)");
        return 0;
    }
    return g_slots[i].addr;
}

std::uintptr_t mainOffset(const char* name) {
    const std::uintptr_t a = addr(name);
    return a ? a - g_text_start : 0;
}

How howResolved(const char* name) {
    const int i = indexOf(name);
    return i < 0 ? How::Unresolved : g_slots[i].how;
}

int resolvedCount() { return g_resolved_count; }
int totalCount() { return kCount; }
const char* buildIdHex() { return g_build_hex; }

void unresolvedNames(char* out, std::size_t cap) {
    if (!out || cap == 0) return;
    std::size_t n = 0;
    out[0] = '\0';
    for (int i = 0; i < kCount; ++i) {
        if (g_slots[i].how != How::Unresolved) continue;
        const std::size_t len = std::strlen(kTable[i].name);
        if (n + len + 2 >= cap) break;
        if (n) out[n++] = ',';
        std::memcpy(out + n, kTable[i].name, len);
        n += len;
        out[n] = '\0';
    }
}

#endif  // !MK8AP_HOST_TEST

}  // namespace mk8ap::sym
