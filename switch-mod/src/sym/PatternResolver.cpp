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
            out.mask[out.len] = 0;
        } else {
            const int hi = hexNibble(p[0]);
            const int lo = p[1] ? hexNibble(p[1]) : -1;
            if (hi < 0 || lo < 0) return false;
            p += 2;
            out.bytes[out.len] = static_cast<std::uint8_t>((hi << 4) | lo);
            out.mask[out.len] = 1;
        }
        ++out.len;
        if (*p && *p != ' ' && *p != '\t') return false;
    }
    return out.len > 0;
}

std::int64_t scan(const std::uint8_t* hay, std::size_t len,
                  const CompiledPattern& pat, std::int64_t* second) {
    if (second) *second = -1;
    if (!hay || pat.len == 0 || len < pat.len) return -1;
    // Anchor on the first fixed byte to keep the inner loop cheap.
    std::size_t anchor = 0;
    while (anchor < pat.len && pat.mask[anchor] == 0) ++anchor;
    if (anchor == pat.len) return 0;  // all wildcards: matches at 0
    const std::uint8_t first = pat.bytes[anchor];
    std::int64_t found = -1;
    const std::size_t last_start = len - pat.len;
    for (std::size_t i = 0; i <= last_start; ++i) {
        if (hay[i + anchor] != first) continue;
        bool ok = true;
        for (std::size_t j = 0; j < pat.len; ++j) {
            if (pat.mask[j] && hay[i + j] != pat.bytes[j]) { ok = false; break; }
        }
        if (!ok) continue;
        if (found < 0) {
            found = static_cast<std::int64_t>(i);
            if (!second) return found;
        } else {
            *second = static_cast<std::int64_t>(i);
            return found;
        }
    }
    return found;
}

#ifndef MK8AP_HOST_TEST

namespace {

struct Slot {
    std::uintptr_t addr = 0;
    How how = How::Unresolved;
};
Slot g_slots[kCount];
bool g_resolved = false;
int g_resolved_count = 0;
char g_build_hex[33] = "";
bool g_is_305 = false;
std::uintptr_t g_text_start = 0;

constexpr const char* kBuild305 = "FE941ED5BA14BE5D";  // first 8 bytes

int indexOf(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < kCount; ++i) {
        if (std::strcmp(kTable[i].name, name) == 0) return i;
    }
    return -1;
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
        g_is_305 = std::strncmp(g_build_hex, kBuild305, 16) == 0;
    }
    MK8AP_LOG_INFO("[sym] main text=%p size=0x%zx build=%s (3.0.5 pin: %s)",
                   reinterpret_cast<void*>(text.start()),
                   static_cast<std::size_t>(text.size()),
                   g_build_hex[0] ? g_build_hex : "(unknown)",
                   g_is_305 ? "MATCH" : "no");

    const auto* hay = reinterpret_cast<const std::uint8_t*>(text.start());
    const std::size_t len = text.size();

    for (int i = 0; i < kCount; ++i) {
        const Entry& e = kTable[i];
        Slot& s = g_slots[i];
        CompiledPattern pat;
        if (e.pattern[0] && compilePattern(e.pattern, pat)) {
            std::int64_t second = -1;
            const std::int64_t hit = scan(hay, len, pat, &second);
            if (hit >= 0) {
                const std::int64_t rel = hit + e.offset;
                s.addr = text.start() + static_cast<std::uintptr_t>(rel);
                s.how = How::Pattern;
                if (second >= 0) {
                    MK8AP_LOG_WARN("[sym] %s: pattern ambiguous (2nd hit +0x%llx); using first",
                                   e.name, static_cast<unsigned long long>(second));
                }
                if (g_is_305 && e.offset_305 && rel != e.offset_305) {
                    MK8AP_LOG_WARN("[sym] %s: pattern -> +0x%llx but 3.0.5 table says +0x%llx",
                                   e.name, static_cast<unsigned long long>(rel),
                                   static_cast<unsigned long long>(e.offset_305));
                }
            }
        }
        if (s.how == How::Unresolved && g_is_305 && e.offset_305) {
            s.addr = text.start() + static_cast<std::uintptr_t>(e.offset_305);
            s.how = How::Offset305;
        }
        if (s.how != How::Unresolved) {
            ++g_resolved_count;
            MK8AP_LOG_INFO("[sym] %-40s +0x%08llx (%s)", e.name,
                           static_cast<unsigned long long>(s.addr - text.start()),
                           s.how == How::Pattern ? "pattern" : "3.0.5 offset");
        } else {
            MK8AP_LOG_WARN("[sym] %-40s UNRESOLVED", e.name);
        }
    }
    MK8AP_LOG_INFO("[sym] resolved %d/%d", g_resolved_count, kCount);
}

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
bool isBuild305() { return g_is_305; }

#endif  // !MK8AP_HOST_TEST

}  // namespace mk8ap::sym
