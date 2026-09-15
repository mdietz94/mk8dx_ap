// Runtime symbol resolution for a stripped game binary.
//
// MK8DX ships no symbols, so every game function we hook or call is located
// at boot by scanning the main module's .text for a byte pattern (IDA-style
// "AA BB ?? CC", wildcards allowed) and applying a signed offset. Entries
// without a pattern fall back to an absolute main-relative offset that is
// only trusted when the main build id matches the 3.0.5 pin.
//
// The table itself lives in SymbolTable.inc (generated from
// syms/mk8dx_symbols.toml). resolveAll() runs once from hkMain; everything
// after that is a table lookup. Pure C++ (no allocation, no exceptions) so
// the scanner can also be built and tested on the host (tests/test_pattern.cpp).

#pragma once

#include <cstddef>
#include <cstdint>

namespace mk8ap::sym {

struct Entry {
    const char* name;      // "gear::GetRaceInfo"
    const char* mangled;   // Itanium name (documentation / future sail use)
    const char* pattern;   // "" = no pattern
    std::int64_t offset;   // added to the first pattern match
    std::int64_t offset_305;  // main-relative fallback on 3.0.5, 0 = none
};

enum class How : std::uint8_t { Unresolved = 0, Pattern = 1, Offset305 = 2 };

// ---- pure scanner (host-testable) -----------------------------------------

inline constexpr std::size_t kMaxPatternBytes = 128;

struct CompiledPattern {
    std::uint8_t bytes[kMaxPatternBytes] = {};
    std::uint8_t mask[kMaxPatternBytes] = {};  // 1 = must match, 0 = wildcard
    std::size_t len = 0;
};

// Parse "AA BB ?? CC". Returns false on a malformed token or overflow.
bool compilePattern(const char* text, CompiledPattern& out);

// Scan [hay, hay+len) for the pattern. Returns the offset of the first
// match, or -1. `second` (optional) receives the offset of a second match
// (ambiguity diagnostic) or -1.
std::int64_t scan(const std::uint8_t* hay, std::size_t len,
                  const CompiledPattern& pat, std::int64_t* second = nullptr);

// ---- module-level API (Switch only) ---------------------------------------

// Scan main .text for every table entry. Idempotent; logs a summary.
void resolveAll();

// Absolute address of a resolved symbol, or 0.
std::uintptr_t addr(const char* name);
// Main-relative offset (for installAtMainOffset), or 0 when unresolved.
std::uintptr_t mainOffset(const char* name);
How howResolved(const char* name);

int resolvedCount();
int totalCount();
// "FE941ED5BA14BE5D505698DA1BBF4FE7"-style uppercase hex of main's build id.
const char* buildIdHex();
bool isBuild305();

// Convenience: fetch a resolved address as a function pointer, or nullptr.
template <typename Fn>
Fn fn(const char* name) {
    return reinterpret_cast<Fn>(addr(name));
}

}  // namespace mk8ap::sym
