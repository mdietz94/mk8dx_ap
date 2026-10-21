// Runtime symbol resolution for MK8DX 4.0.1 (a stripped binary).
//
// The mod supports exactly one game build: 4.0.1, main build id
// 158C0C7399AABD7B... On any other build nothing is resolved and no hook
// is installed.
//
// Per symbol:
//   * ADDRESS: a published main-relative 4.0.1 address (from the commented
//     ASM of RoGamer97's 4.0.1 patches). Used as-is.
//   * PATTERN: for functions nobody has published a 4.0.1 address for. The
//     byte patterns were authored against 3.0.5, so two passes are tried:
//       1. exact bytes, at instruction alignment;
//       2. RELAXED: everything that legitimately moved between 3.0.5 and
//          4.0.x wildcarded (branch / ADRP distances, struct-offset
//          immediates), accepted only when it matches exactly once.
//     This is the manual porting method Ro demonstrates in "Porting MK8D
//     patches to v4.0.0": search for the surrounding code, skip the branch
//     links, expect member offsets to have shifted.
//
// The table lives in SymbolTable.inc (generated from syms/mk8dx_symbols.toml).
// The scanner is pure C++ (no allocation, no exceptions) and host-tested
// (tests/test_pattern.cpp).

#pragma once

#include <cstddef>
#include <cstdint>

namespace mk8ap::sym {

struct Entry {
    const char* name;      // "gear::GetRaceInfo"
    const char* mangled;   // Itanium name (documentation)
    const char* pattern;   // "" = address only
    std::int64_t offset;   // added to the pattern match
    std::int64_t address;  // published main-relative address on 4.0.1, 0 = none
};

enum class How : std::uint8_t {
    Unresolved = 0,
    Address = 1,         // published 4.0.1 address
    Pattern = 2,         // exact pattern
    PatternRelaxed = 3,  // relaxed pattern, unique match
};

// First 8 bytes of the 4.0.1 main build id, uppercase hex.
inline constexpr const char* kBuildId = "158C0C7399AABD7B";

// ---- pure scanner (host-testable) -----------------------------------------

inline constexpr std::size_t kMaxPatternBytes = 128;

struct CompiledPattern {
    std::uint8_t bytes[kMaxPatternBytes] = {};
    // Per-byte bit mask: 0xFF = must match, 0x00 = wildcard, anything else =
    // only the set bits are compared (used by the relaxed form).
    std::uint8_t mask[kMaxPatternBytes] = {};
    std::size_t len = 0;
};

// Parse "AA BB ?? CC". Returns false on a malformed token or overflow.
bool compilePattern(const char* text, CompiledPattern& out);

// Build the relaxed form of an instruction-aligned pattern (len % 4 == 0).
// Wildcards, per AArch64 instruction: B/BL imm26, B.cond/CBZ/CBNZ imm19,
// TBZ/TBNZ imm14, ADR/ADRP imm21, LDR-literal imm19, load/store unsigned
// imm12 and unscaled imm9, ADD/SUB imm12, MOVZ/MOVN/MOVK imm16. Opcode and
// register fields stay fixed. Returns false if the pattern is not aligned.
bool relaxPattern(const CompiledPattern& in, CompiledPattern& out);

// Scan [hay, hay+len) at `align`-byte steps. Returns the offset of the first
// match or -1. `count` (optional) receives the number of matches found,
// capped at 2 (enough to tell unique from ambiguous).
std::int64_t scan(const std::uint8_t* hay, std::size_t len,
                  const CompiledPattern& pat, std::size_t align = 1,
                  int* count = nullptr);

// True when `hex` (uppercase build id) is the supported 4.0.1 build.
bool isSupportedBuildId(const char* hex);

// Resolve one table entry against a 4.0.1 text image. Pure: used by
// resolveAll and by the host test. Returns the main-relative offset or -1.
// `pattern_rel` (optional) receives what the pattern alone resolved to
// (-1 if it missed), for cross-checking against a published address.
std::int64_t resolveEntry(const Entry& e, const std::uint8_t* text,
                          std::size_t text_len, How& how,
                          std::int64_t* pattern_rel = nullptr);

// ---- module-level API (Switch only) ---------------------------------------

// Identify the build and, if it is 4.0.1, resolve every table entry.
// Idempotent; logs a summary.
void resolveAll();

// True when the running game is MK8DX 4.0.1.
bool isSupportedBuild();

// Absolute address of a resolved symbol, or 0.
std::uintptr_t addr(const char* name);
// Main-relative offset (for installAtMainOffset), or 0 when unresolved.
std::uintptr_t mainOffset(const char* name);
How howResolved(const char* name);

int resolvedCount();
int totalCount();
// Uppercase hex of main's build id (first 16 bytes).
const char* buildIdHex();
// Comma-separated names of the symbols that did not resolve ("" if none),
// truncated to fit. Sent in HELLO so the PC can say which features are off.
void unresolvedNames(char* out, std::size_t cap);

// Convenience: fetch a resolved address as a function pointer, or nullptr.
template <typename Fn>
Fn fn(const char* name) {
    return reinterpret_cast<Fn>(addr(name));
}

}  // namespace mk8ap::sym
